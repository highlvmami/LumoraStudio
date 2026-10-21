#include "FrameRenderer.h"

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

#include "ai/Redaction.h"
#include "ai/Vision.h"
#include "color/ColorProcessor.h"
#include "ffmpeg/FFmpeg.h"
#include "media/VideoDecoder.h"
#include "render/TitleRenderer.h"

namespace lumora {

namespace {

constexpr size_t kMaxSlots = 8;

double smoothstep(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

int swsColorspace(const AVFrame* f) {
    switch (f->colorspace) {
    case AVCOL_SPC_BT709: return SWS_CS_ITU709;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
    case AVCOL_SPC_SMPTE240M: return SWS_CS_SMPTE240M;
    case AVCOL_SPC_FCC: return SWS_CS_FCC;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: return SWS_CS_ITU601;
    default: return f->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601; // unspecified: HD is almost always 709
    }
}

bool hasAlpha(int format) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(format));
    return d && (d->flags & AV_PIX_FMT_FLAG_ALPHA);
}

} // namespace

struct FrameRenderer::Slot {
    std::unique_ptr<VideoDecoder> decoder;
    bool failed = false;
    av::SwsPtr sws;
    int swsKey[6] = {0, 0, -1, 0, 0, -1};
    QImage image;
    Ticks imagePts = kInvalidTicks;
    QSize imageSize;
    ColorGrade imageGrade;
    Redaction imageRedaction;
    BackgroundRemoval imageBackground;
    std::unique_ptr<ColorProcessor> color; // for the clip's current grade
    // Subject matte of the last frame (background removal is the slow part).
    QImage matte;
    Ticks mattePts = kInvalidTicks;
    QString matteModel;
};

// Opens decoders for clips that are about to come into view and decodes up
// to their first frame, one at a time on its own thread. The renderer takes
// a finished decoder over when it reaches the clip.
struct FrameRenderer::Prefetcher {
    struct Job {
        QString path;
        Ticks sourceTime = 0;
        ColorGrade grade;
        std::unique_ptr<VideoDecoder> decoder;
        std::unique_ptr<ColorProcessor> color;
        bool started = false;
        bool done = false;
    };

    std::mutex mutex;
    std::condition_variable cv;
    std::map<QUuid, std::shared_ptr<Job>> jobs; // per clip
    std::deque<std::shared_ptr<Job>> queue;     // not started yet
    bool quit = false;
    std::thread thread;

    ~Prefetcher() {
        {
            std::lock_guard lock(mutex);
            quit = true;
            queue.clear();
        }
        cv.notify_all();
        if (thread.joinable())
            thread.join();
    }

    void request(const QUuid& clip, const QString& path, Ticks sourceTime, const ColorGrade& grade) {
        {
            std::lock_guard lock(mutex);
            if (jobs.count(clip))
                return;
            auto job = std::make_shared<Job>();
            job->path = path;
            job->sourceTime = sourceTime;
            job->grade = grade;
            jobs[clip] = job;
            queue.push_back(job);
            if (!thread.joinable())
                thread = std::thread([this] { run(); });
        }
        cv.notify_all();
    }

    bool has(const QUuid& clip) {
        std::lock_guard lock(mutex);
        return jobs.count(clip) > 0;
    }

    // Forgets the jobs of clips that are no longer coming up.
    void retain(const std::vector<QUuid>& wanted) {
        std::lock_guard lock(mutex);
        for (auto it = jobs.begin(); it != jobs.end();) {
            if (std::find(wanted.begin(), wanted.end(), it->first) != wanted.end()) {
                ++it;
                continue;
            }
            queue.erase(std::remove(queue.begin(), queue.end(), it->second), queue.end());
            it = jobs.erase(it);
        }
    }

    // Hands over the job of `clip`. Waits when it is being prepared right
    // now (it is ahead of the caller); one that has not started is dropped
    // and the caller opens the decoder itself.
    std::shared_ptr<Job> take(const QUuid& clip) {
        std::unique_lock lock(mutex);
        auto it = jobs.find(clip);
        if (it == jobs.end())
            return {};
        auto job = it->second;
        jobs.erase(it);
        if (!job->started) {
            queue.erase(std::remove(queue.begin(), queue.end(), job), queue.end());
            return {};
        }
        cv.wait(lock, [&] { return job->done; });
        return job;
    }

    void run() {
        while (true) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return quit || !queue.empty(); });
                if (quit)
                    return;
                job = queue.front();
                queue.pop_front();
                job->started = true;
            }
            auto decoder = std::make_unique<VideoDecoder>();
            if (decoder->open(job->path))
                decoder->frameAt(job->sourceTime);
            else
                decoder.reset();
            std::unique_ptr<ColorProcessor> color;
            if (!job->grade.isIdentity())
                color = std::make_unique<ColorProcessor>(job->grade);
            {
                std::lock_guard lock(mutex);
                job->decoder = std::move(decoder);
                job->color = std::move(color);
                job->done = true;
            }
            cv.notify_all();
        }
    }
};

FrameRenderer::FrameRenderer(const MediaPool* pool) : m_pool(pool) {}
FrameRenderer::~FrameRenderer() = default;

void FrameRenderer::setTimeline(const TimelineState& state) { m_state = state; }

void FrameRenderer::setPrecision(Precision p) {
    if (p == m_precision)
        return;
    m_precision = p;
    for (auto& [id, slot] : m_slots) {
        slot->image = {};
        slot->sws.reset();
    }
}

void FrameRenderer::clearCache() {
    m_slots.clear();
    m_lru.clear();
    m_prefetcher.reset();
}

QSize FrameRenderer::defaultCanvasSize(const TimelineState& state, const MediaPool& pool) {
    if (state.resolution.isValid() && !state.resolution.isEmpty())
        return state.resolution;
    for (int ti : state.trackIndices(TrackType::Video))
        for (const auto& c : state.tracks[ti].clips) {
            if (!c.hasMedia())
                continue;
            auto m = pool.find(c.mediaId);
            if (!m || !m->info.video)
                continue;
            const auto& v = *m->info.video;
            int w = v.width;
            if (v.sampleAspectRatio.num > 0 && v.sampleAspectRatio.den > 0)
                w = static_cast<int>(av_rescale(w, v.sampleAspectRatio.num, v.sampleAspectRatio.den));
            QSize s(w, v.height);
            if (v.rotation == 90 || v.rotation == 270)
                s.transpose();
            if (!s.isEmpty())
                return QSize(s.width() & ~1, s.height() & ~1);
        }
    return QSize(1920, 1080);
}

Ticks FrameRenderer::mediaDuration(const QUuid& id) const {
    auto m = m_pool ? m_pool->find(id) : nullptr;
    return m ? m->info.duration : 0;
}

double FrameRenderer::fadeFactor(const Clip& c, Ticks t) const {
    double f = 1.0;
    const Ticks local = t - c.timelineStart;
    if (c.fadeIn > 0 && local < c.fadeIn)
        f *= std::clamp(static_cast<double>(local) / c.fadeIn, 0.0, 1.0);
    const Ticks left = c.timelineEnd() - t;
    if (c.fadeOut > 0 && left < c.fadeOut)
        f *= std::clamp(static_cast<double>(left) / c.fadeOut, 0.0, 1.0);
    return f;
}

FrameRenderer::Slot* FrameRenderer::slotFor(const Clip& clip) {
    std::lock_guard lock(m_slotMutex); // transitions look up two slots at once
    // A decoder prepared ahead of time for this clip.
    auto media = m_pool ? m_pool->find(clip.mediaId) : nullptr;
    std::shared_ptr<Prefetcher::Job> job;
    if (m_prefetcher && m_prefetcher->has(clip.id))
        job = m_prefetcher->take(clip.id);
    if (job && (!job->decoder || !media || job->decoder->path() != media->path))
        job.reset(); // failed, or the clip now shows another file
    auto adopt = [&](Slot* slot) {
        if (!job)
            return;
        slot->decoder = std::move(job->decoder);
        slot->failed = false;
        slot->image = {};
        slot->imagePts = kInvalidTicks;
        if (job->color)
            slot->color = std::move(job->color);
        ++m_prefetchedUsed;
    };

    auto it = m_slots.find(clip.id);
    if (it != m_slots.end()) {
        m_lru.remove(clip.id);
        m_lru.push_back(clip.id);
        adopt(it->second.get());
        return it->second.get();
    }
    auto slot = std::make_unique<Slot>();
    if (!media || !media->info.video) {
        slot->failed = true;
    } else if (job) {
        adopt(slot.get());
    } else {
        slot->decoder = std::make_unique<VideoDecoder>();
        if (!slot->decoder->open(media->path))
            slot->failed = true;
    }
    while (m_slots.size() >= kMaxSlots && !m_lru.empty()) {
        m_slots.erase(m_lru.front());
        m_lru.pop_front();
    }
    m_lru.push_back(clip.id);
    return (m_slots[clip.id] = std::move(slot)).get();
}

void FrameRenderer::prefetch(Ticks t, Ticks horizon) {
    std::vector<QUuid> wanted;
    auto dur = [this](const QUuid& id) { return mediaDuration(id); };
    for (int ti : m_state.trackIndices(TrackType::Video)) {
        const Track& track = m_state.tracks[ti];
        if (track.hidden)
            continue;
        for (int k = 0; k < static_cast<int>(track.clips.size()); ++k) {
            const Clip& c = track.clips[k];
            if (!c.hasMedia())
                continue;
            // The incoming clip of a storyline transition shows up half the
            // transition before its cut.
            Ticks first = c.timelineStart;
            if (ti == 0)
                first -= m_state.transitionDuration(k, dur) / 2;
            if (first <= t || first > t + horizon)
                continue;
            auto media = m_pool ? m_pool->find(c.mediaId) : nullptr;
            if (!media || !media->info.video)
                continue;
            const Ticks source = std::max<Ticks>(0, c.mediaTime(first)) + m_sampleOffset;
            if (auto it = m_slots.find(c.id); it != m_slots.end() &&
                (it->second->failed || it->second->decoder->reachesForward(source)))
                continue; // nothing to gain
            if (!m_prefetcher)
                m_prefetcher = std::make_unique<Prefetcher>();
            m_prefetcher->request(c.id, media->path, source, c.color);
            wanted.push_back(c.id);
        }
    }
    if (m_prefetcher)
        m_prefetcher->retain(wanted);
}

QImage FrameRenderer::clipImage(const Clip& clip, Ticks sourceTime, QSize* fitSize, int* rotation) {
    Slot* slot = slotFor(clip);
    if (slot->failed)
        return {};
    Ticks pts = 0;
    const AVFrame* f = slot->decoder->frameAt(sourceTime + m_sampleOffset, &pts);
    if (!f || f->width <= 0 || f->height <= 0)
        return {};
    const int rot = slot->decoder->info().rotation;
    if (rotation)
        *rotation = rot;
    // Display size of the picture (square pixels, rotated).
    double dw = f->width, dh = f->height;
    if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0)
        dw = dw * f->sample_aspect_ratio.num / f->sample_aspect_ratio.den;
    const bool sideways = rot == 90 || rot == 270;
    QSizeF display = sideways ? QSizeF(dh, dw) : QSizeF(dw, dh);
    const QSizeF fit = display.scaled(QSizeF(m_output), Qt::KeepAspectRatio);
    if (fitSize)
        *fitSize = fit.toSize();
    // Pixel size of the converted image (before rotation), capped so a big
    // zoom does not allocate huge images; the painter upscales the rest.
    const double zoom = clip.stabilization.isActive() ? std::clamp(clip.stabilization.zoom, 1.0, 2.0) : 1.0;
    const double scale = std::clamp(clip.transform.scale * zoom, 0.01, 2.0);
    QSizeF target = fit * scale;
    if (sideways)
        target.transpose();
    const int tw = std::max(2, static_cast<int>(std::lround(target.width())));
    const int th = std::max(2, static_cast<int>(std::lround(target.height())));
    if (slot->imagePts == pts && slot->imageSize == QSize(tw, th) && slot->imageGrade == clip.color &&
        slot->imageRedaction == clip.redaction && slot->imageBackground == clip.background && !slot->image.isNull())
        return slot->image;

    const bool final = m_precision == Precision::Final;
    const bool alpha = hasAlpha(f->format);
    // Native-endian formats that match QImage's memory layout.
    const AVPixelFormat dstFmt = final ? AV_PIX_FMT_RGBA64 : (alpha ? AV_PIX_FMT_RGB32 : AV_PIX_FMT_0RGB32);
    const int key[6] = {f->width, f->height, f->format, tw, th, f->colorspace * 4 + f->color_range};
    if (!slot->sws || !std::equal(std::begin(key), std::end(key), std::begin(slot->swsKey))) {
        const int flags = final ? (SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT) : (SWS_BICUBIC | SWS_FULL_CHR_H_INT);
        slot->sws.reset(sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), tw, th, dstFmt, flags,
                                       nullptr, nullptr, nullptr));
        if (!slot->sws)
            return {};
        sws_setColorspaceDetails(slot->sws.get(), sws_getCoefficients(swsColorspace(f)),
                                 f->color_range == AVCOL_RANGE_JPEG ? 1 : 0, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0,
                                 1 << 16, 1 << 16);
        std::copy(std::begin(key), std::end(key), std::begin(slot->swsKey));
    }
    QImage img(tw, th, final ? QImage::Format_RGBA64 : (alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32));
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int stride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
    sws_scale(slot->sws.get(), f->data, f->linesize, 0, f->height, dst, stride);
    if (final && !alpha) {
        // Opaque sources: make sure alpha is exactly 1.0 whatever sws wrote.
        for (int y = 0; y < th; ++y) {
            auto* px = reinterpret_cast<quint16*>(img.scanLine(y));
            for (int x = 0; x < tw; ++x)
                px[x * 4 + 3] = 0xffff;
        }
    }
    // The subject is found on the picture as shot, before grading, so colour
    // changes do not move the cut-out edge.
    QImage matte;
    if (clip.background.enabled) {
        // The detailed model takes seconds per frame; the preview uses the
        // fast one when it is there.
        QString model = clip.background.model;
        if (!final && model != QLatin1String("fast") && ai::visionModelReady(ai::backgroundModelId(QStringLiteral("fast"))))
            model = QStringLiteral("fast");
        if (slot->mattePts != pts || slot->matteModel != model || slot->matte.isNull()) {
            slot->matte = ai::subjectMatte(img, model);
            slot->mattePts = pts;
            slot->matteModel = model;
        }
        matte = slot->matte;
    }
    if (!clip.color.isIdentity()) {
        if (!slot->color || !(slot->color->grade() == clip.color))
            slot->color = std::make_unique<ColorProcessor>(clip.color);
        slot->color->apply(img);
    }
    if (clip.redaction.isActive())
        ai::hideRegions(img, clip.redaction.regionsAt(pts), clip.redaction.style, clip.redaction.strength);
    if (!matte.isNull())
        ai::applyMatte(img, matte, clip.background.softness);
    slot->image = img;
    slot->imageGrade = clip.color;
    slot->imageRedaction = clip.redaction;
    slot->imageBackground = clip.background;
    slot->imagePts = pts;
    slot->imageSize = QSize(tw, th);
    return img;
}

QImage FrameRenderer::newCanvas(const QColor& fill) const {
    QImage img(m_output, m_precision == Precision::Final ? QImage::Format_RGBA64_Premultiplied
                                                         : QImage::Format_ARGB32_Premultiplied);
    img.fill(fill);
    return img;
}

void FrameRenderer::drawLayer(QPainter& p, const Clip& clip, Ticks t, Ticks sourceTime, double opacity) {
    const ClipTransform& tr = clip.transform;
    const double alpha = opacity * std::clamp(tr.opacity, 0.0, 1.0);
    if (alpha <= 0.0)
        return;
    const QSizeF frame(m_output);
    const QPointF follow = clip.motion.offsetAt(t - clip.timelineStart);
    const QPointF centre(frame.width() / 2 + (tr.x + follow.x()) * frame.width(),
                         frame.height() / 2 + (tr.y + follow.y()) * frame.height());
    if (clip.kind == ClipKind::Title) {
        p.save();
        p.translate(centre);
        p.rotate(tr.rotation);
        p.scale(tr.scale, tr.scale);
        p.translate(-frame.width() / 2, -frame.height() / 2);
        drawTitle(p, m_output, clip.title, t - clip.timelineStart, clip.duration(), alpha);
        p.restore();
        return;
    }
    QSize fit;
    int rotation = 0;
    const QImage img = clipImage(clip, sourceTime, &fit, &rotation);
    if (img.isNull())
        return;
    p.save();
    p.setOpacity(alpha);
    p.translate(centre);
    const double angle = tr.rotation + rotation;
    p.rotate(angle);
    const double scale = std::clamp(tr.scale, 0.0, 100.0);
    QSizeF size = QSizeF(fit) * scale;
    if (rotation == 90 || rotation == 270)
        size.transpose();
    bool stabilized = false;
    if (clip.stabilization.isActive()) {
        // In the decoded picture's own axes: move the frame onto the smoothed
        // camera path and zoom in so its edges stay outside the frame.
        const auto c = clip.stabilization.correctionAt(sourceTime);
        p.translate(c.x * size.width(), c.y * size.height());
        p.rotate(c.angle);
        const double z = std::clamp(clip.stabilization.zoom, 1.0, 2.0);
        p.scale(z, z);
        stabilized = true;
    }
    const bool exact = !stabilized && std::fmod(angle, 360.0) == 0.0 && QSize(qRound(size.width()), qRound(size.height())) == img.size();
    p.setRenderHint(QPainter::SmoothPixmapTransform, !exact);
    p.drawImage(QRectF(QPointF(-size.width() / 2, -size.height() / 2), size), img);
    p.restore();
}

QImage FrameRenderer::renderStorylineClip(int index, Ticks t) {
    QImage canvas = newCanvas(Qt::black);
    const Clip& c = m_state.tracks[0].clips[index];
    QPainter p(&canvas);
    drawLayer(p, c, t, c.mediaTime(t), fadeFactor(c, t));
    return canvas;
}

QImage FrameRenderer::renderStoryline(Ticks t) {
    if (m_state.tracks.empty() || m_state.tracks[0].hidden)
        return newCanvas(Qt::black);
    const auto& v = m_state.tracks[0].clips;
    int i = -1;
    for (int k = 0; k < static_cast<int>(v.size()); ++k)
        if (v[k].contains(t)) {
            i = k;
            break;
        }
    if (i < 0)
        return newCanvas(Qt::black);
    auto dur = [this](const QUuid& id) { return mediaDuration(id); };

    int a = -1, b = -1;
    double progress = 0;
    const Ticks dIn = m_state.transitionDuration(i, dur);
    if (dIn > 0 && t < v[i].timelineStart + dIn / 2) {
        a = i - 1;
        b = i;
        progress = static_cast<double>(t - (v[i].timelineStart - dIn / 2)) / dIn;
    } else if (i + 1 < static_cast<int>(v.size())) {
        const Ticks dOut = m_state.transitionDuration(i + 1, dur);
        if (dOut > 0 && t >= v[i].timelineEnd() - dOut / 2) {
            a = i;
            b = i + 1;
            progress = static_cast<double>(t - (v[i].timelineEnd() - dOut / 2)) / dOut;
        }
    }
    if (a < 0)
        return renderStorylineClip(i, t);

    // Both pictures of a transition are decoded at the same time (each clip
    // has its own decoder), otherwise transitions of big videos play at half
    // speed. Slots are made beforehand so the two threads only look them up.
    for (int k : {a, b})
        if (v[k].hasMedia())
            slotFor(v[k]);
    auto incoming = std::async(std::launch::async, [&] { return renderStorylineClip(b, t); });
    const QImage la = renderStorylineClip(a, t);
    const QImage lb = incoming.get();
    const double p = std::clamp(progress, 0.0, 1.0);
    const double e = smoothstep(p);
    const double w = m_output.width(), h = m_output.height();
    QImage out = newCanvas(Qt::black);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    switch (v[b].transitionIn.type) {
    case TransitionType::None:
    case TransitionType::CrossDissolve:
        painter.drawImage(0, 0, la);
        painter.setOpacity(p);
        painter.drawImage(0, 0, lb);
        break;
    case TransitionType::DipToBlack:
    case TransitionType::DipToWhite: {
        const QColor c = v[b].transitionIn.type == TransitionType::DipToBlack ? Qt::black : Qt::white;
        painter.fillRect(out.rect(), c);
        if (p < 0.5) {
            painter.setOpacity(1.0 - 2 * p);
            painter.drawImage(0, 0, la);
        } else {
            painter.setOpacity(2 * p - 1.0);
            painter.drawImage(0, 0, lb);
        }
        break;
    }
    case TransitionType::WipeLeft:
    case TransitionType::WipeRight:
    case TransitionType::WipeUp:
    case TransitionType::WipeDown: {
        painter.drawImage(0, 0, la);
        QRectF r;
        switch (v[b].transitionIn.type) {
        case TransitionType::WipeLeft: r = QRectF(w * (1 - e), 0, w * e, h); break;
        case TransitionType::WipeRight: r = QRectF(0, 0, w * e, h); break;
        case TransitionType::WipeUp: r = QRectF(0, h * (1 - e), w, h * e); break;
        default: r = QRectF(0, 0, w, h * e); break;
        }
        painter.drawImage(r, lb, r);
        break;
    }
    case TransitionType::SlideLeft:
        painter.drawImage(QPointF(-e * w, 0), la);
        painter.drawImage(QPointF((1 - e) * w, 0), lb);
        break;
    case TransitionType::SlideRight:
        painter.drawImage(QPointF(e * w, 0), la);
        painter.drawImage(QPointF(-(1 - e) * w, 0), lb);
        break;
    case TransitionType::Zoom: {
        auto drawScaled = [&](const QImage& img, double s, double opacity) {
            painter.save();
            painter.setOpacity(opacity);
            painter.translate(w / 2, h / 2);
            painter.scale(s, s);
            painter.drawImage(QPointF(-w / 2, -h / 2), img);
            painter.restore();
        };
        drawScaled(lb, 0.8 + 0.2 * e, 1.0);
        drawScaled(la, 1.0 + 0.5 * e, 1.0 - e);
        break;
    }
    case TransitionType::Blur: {
        const double radius = std::sin(M_PI * p) * 0.04 * w;
        painter.drawImage(0, 0, blurred(la, radius));
        painter.setOpacity(e);
        painter.drawImage(0, 0, blurred(lb, radius));
        break;
    }
    }
    return out;
}

QImage FrameRenderer::render(Ticks t) {
    m_lastTime = t;
    QImage canvas = renderStoryline(t);
    const auto video = m_state.trackIndices(TrackType::Video);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    for (int ti : video) {
        if (ti == 0)
            continue;
        const Track& track = m_state.tracks[ti];
        if (track.hidden)
            continue;
        for (const auto& c : track.clips)
            if (c.contains(t))
                drawLayer(p, c, t, c.mediaTime(t), fadeFactor(c, t));
    }
    for (const auto& [track, clip] : subtitlesAt(t))
        drawSubtitle(p, canvas.size(), track->subtitleStyle, clip->text);
    return canvas;
}

std::vector<std::pair<const Track*, const Clip*>> FrameRenderer::subtitlesAt(Ticks t) const {
    std::vector<std::pair<const Track*, const Clip*>> out;
    if (!m_subtitles)
        return out;
    for (int ti : m_state.trackIndices(TrackType::Subtitle)) {
        const Track& track = m_state.tracks[ti];
        if (track.hidden)
            continue;
        for (const auto& c : track.clips)
            if (c.contains(t) && !c.text.trimmed().isEmpty())
                out.emplace_back(&track, &c);
    }
    return out;
}

const AVFrame* FrameRenderer::passthroughFrame(Ticks t) {
    if (m_state.tracks.empty() || m_state.tracks[0].hidden || !subtitlesAt(t).empty())
        return nullptr;
    for (int ti : m_state.trackIndices(TrackType::Video)) {
        if (ti == 0 || m_state.tracks[ti].hidden)
            continue;
        for (const auto& c : m_state.tracks[ti].clips)
            if (c.contains(t))
                return nullptr;
    }
    const auto& v = m_state.tracks[0].clips;
    auto dur = [this](const QUuid& id) { return mediaDuration(id); };
    for (int k = 0; k < static_cast<int>(v.size()); ++k) {
        const Clip& c = v[k];
        if (!c.contains(t))
            continue;
        if (!c.hasMedia() || !c.transform.isIdentity() || !c.color.isIdentity() || fadeFactor(c, t) != 1.0 ||
            c.stabilization.isActive() || !c.motion.isEmpty() || c.redaction.isActive() || c.background.enabled)
            return nullptr;
        const Ticks dIn = m_state.transitionDuration(k, dur);
        if (dIn > 0 && t < c.timelineStart + dIn / 2)
            return nullptr;
        if (k + 1 < static_cast<int>(v.size())) {
            const Ticks dOut = m_state.transitionDuration(k + 1, dur);
            if (dOut > 0 && t >= c.timelineEnd() - dOut / 2)
                return nullptr;
        }
        Slot* slot = slotFor(c);
        if (slot->failed || slot->decoder->info().rotation != 0)
            return nullptr;
        const AVFrame* f = slot->decoder->frameAt(c.mediaTime(t) + m_sampleOffset);
        if (!f || f->width != m_output.width() || f->height != m_output.height())
            return nullptr;
        if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.num != f->sample_aspect_ratio.den)
            return nullptr;
        return f;
    }
    return nullptr;
}

QImage blurred(const QImage& image, double radius) {
    if (radius < 1.0 || image.isNull())
        return image;
    const double factor = std::clamp(radius / 2.0, 1.0, 64.0);
    const QSize small(std::max(1, static_cast<int>(image.width() / factor)),
                      std::max(1, static_cast<int>(image.height() / factor)));
    QImage tmp = image.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    tmp = tmp.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return tmp;
}

} // namespace lumora

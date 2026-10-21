#include "Redaction.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "ai/Vision.h"
#include "media/VideoDecoder.h"

namespace lumora::ai {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::ai)
};

// Frames are analysed at most this often; faster footage skips frames and
// the boxes in between are interpolated.
constexpr double kAnalysisRate = 30.0;

double overlap(const QRectF& a, const QRectF& b) {
    const QRectF i = a.intersected(b);
    if (i.isEmpty())
        return 0.0;
    const double inter = i.width() * i.height();
    return inter / (a.width() * a.height() + b.width() * b.height() - inter);
}

// Detections are tight around the face (brow to chin) or the plate; the
// hidden region also covers the hair, ears and the plate's frame.
QRectF grow(const QRectF& r, RedactionKind kind) {
    const double gx = kind == RedactionKind::Face ? 0.18 : 0.12;
    const double gTop = kind == RedactionKind::Face ? 0.3 : 0.2;
    const double gBottom = kind == RedactionKind::Face ? 0.12 : 0.2;
    return r.adjusted(-gx * r.width(), -gTop * r.height(), gx * r.width(), gBottom * r.height());
}

// Detection rectangle (pixels of the upright picture) -> fraction of the
// decoded picture, which is stored the other way up when the file has a
// rotation flag.
QRectF toDecoded(const QRectF& r, QSizeF shown, int rotation) {
    const QRectF n(r.x() / shown.width(), r.y() / shown.height(), r.width() / shown.width(), r.height() / shown.height());
    auto map = [rotation](QPointF p) {
        switch (rotation) {
        case 90: return QPointF(p.y(), 1.0 - p.x());
        case 180: return QPointF(1.0 - p.x(), 1.0 - p.y());
        case 270: return QPointF(1.0 - p.y(), p.x());
        default: return p;
        }
    };
    return QRectF(map(n.topLeft()), map(n.bottomRight())).normalized();
}

struct FrameDetections {
    std::vector<QRectF> faces, plates;
    std::vector<float> faceScores, plateScores;
};

FrameDetections detectAll(const QImage& upright, const RedactionOptions& o, int rotation, QString* error) {
    FrameDetections d;
    const QSizeF size(upright.size());
    if (o.faces)
        for (const auto& det : detectFaces(upright, o.faceThreshold, error)) {
            d.faces.push_back(toDecoded(grow(det.rect, RedactionKind::Face), size, rotation).intersected(QRectF(0, 0, 1, 1)));
            d.faceScores.push_back(det.score);
        }
    if (o.plates)
        for (const auto& det : detectPlates(upright, o.plateThreshold, error)) {
            d.plates.push_back(toDecoded(grow(det.rect, RedactionKind::Plate), size, rotation).intersected(QRectF(0, 0, 1, 1)));
            d.plateScores.push_back(det.score);
        }
    return d;
}

QString checkModels(const RedactionOptions& o) {
    if (!visionAvailable())
        return Tr::tr("This build of LumoraStudio has no AI tools.");
    if (o.faces && !visionModelReady(QStringLiteral("face")))
        return Tr::tr("The face detection model has not been downloaded yet.");
    if (o.plates && !visionModelReady(QStringLiteral("plate")))
        return Tr::tr("The number plate detection model has not been downloaded yet.");
    return {};
}

} // namespace

DetectionTracker::DetectionTracker(RedactionKind kind, Ticks maxGap) : m_kind(kind), m_maxGap(maxGap) {}

void DetectionTracker::add(Ticks t, const std::vector<QRectF>& boxes, const std::vector<float>& scores) {
    // Tracks not seen for too long are finished.
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (t - it->track.end() > m_maxGap) {
            close(*it);
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    // Greedy matching, best overlap first. Fast movement can leave little
    // overlap, so nearby boxes of a similar size match too.
    struct Pair {
        double score;
        size_t track, box;
    };
    std::vector<Pair> pairs;
    for (size_t a = 0; a < m_active.size(); ++a) {
        const QRectF& last = m_active[a].track.boxes.back().rect;
        for (size_t b = 0; b < boxes.size(); ++b) {
            double s = overlap(last, boxes[b]);
            if (s < 0.1) {
                const QPointF d = last.center() - boxes[b].center();
                const double reach = std::max(last.width(), last.height());
                const double sizeRatio = std::sqrt((boxes[b].width() * boxes[b].height()) / std::max(1e-9, last.width() * last.height()));
                if (std::hypot(d.x(), d.y()) < reach && sizeRatio > 0.6 && sizeRatio < 1.6)
                    s = 0.05;
            }
            if (s > 0.0)
                pairs.push_back({s, a, b});
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) { return x.score > y.score; });
    std::vector<bool> trackUsed(m_active.size()), boxUsed(boxes.size());
    for (const auto& p : pairs) {
        if (trackUsed[p.track] || boxUsed[p.box])
            continue;
        trackUsed[p.track] = boxUsed[p.box] = true;
        m_active[p.track].track.boxes.push_back({t, boxes[p.box]});
        m_active[p.track].bestScore = std::max(m_active[p.track].bestScore, p.box < scores.size() ? scores[p.box] : 1.0f);
    }
    for (size_t b = 0; b < boxes.size(); ++b) {
        if (boxUsed[b])
            continue;
        Active a;
        a.track.kind = m_kind;
        a.track.boxes.push_back({t, boxes[b]});
        a.bestScore = b < scores.size() ? scores[b] : 1.0f;
        m_active.push_back(std::move(a));
    }
}

void DetectionTracker::close(Active& a) {
    auto& boxes = a.track.boxes;
    // A face seen in one frame only is kept when the detector was sure.
    if (boxes.size() < 2 && a.bestScore < 0.8f)
        return;
    // Light smoothing against detector jitter (three frame average).
    if (boxes.size() > 2) {
        std::vector<RedactionBox> s = boxes;
        for (size_t i = 1; i + 1 < boxes.size(); ++i) {
            const QRectF &p = boxes[i - 1].rect, &c = boxes[i].rect, &n = boxes[i + 1].rect;
            s[i].rect = QRectF((p.x() + c.x() + n.x()) / 3, (p.y() + c.y() + n.y()) / 3, (p.width() + c.width() + n.width()) / 3,
                               (p.height() + c.height() + n.height()) / 3);
        }
        boxes = std::move(s);
    }
    // Boxes that the linear interpolation of their neighbours reproduces are
    // dropped (projects stay small); kept boxes are grown by the tolerance.
    constexpr double kTolerance = 0.003;
    if (boxes.size() > 2) {
        std::vector<RedactionBox> kept{boxes.front()};
        for (size_t i = 1; i + 1 < boxes.size(); ++i) {
            const RedactionBox& a0 = kept.back();
            const RedactionBox& b0 = boxes[i + 1];
            const double f = static_cast<double>(boxes[i].time - a0.time) / std::max<Ticks>(1, b0.time - a0.time);
            auto lerp = [f](double x, double y) { return x + (y - x) * f; };
            const QRectF& c = boxes[i].rect;
            const double err = std::max({std::abs(lerp(a0.rect.left(), b0.rect.left()) - c.left()),
                                         std::abs(lerp(a0.rect.top(), b0.rect.top()) - c.top()),
                                         std::abs(lerp(a0.rect.right(), b0.rect.right()) - c.right()),
                                         std::abs(lerp(a0.rect.bottom(), b0.rect.bottom()) - c.bottom())});
            if (err > kTolerance)
                kept.push_back(boxes[i]);
        }
        kept.push_back(boxes.back());
        for (auto& b : kept)
            b.rect.adjust(-kTolerance, -kTolerance, kTolerance, kTolerance);
        boxes = std::move(kept);
    }
    m_done.push_back(std::move(a.track));
}

std::vector<RedactionTrack> DetectionTracker::finish() {
    for (auto& a : m_active)
        close(a);
    m_active.clear();
    std::vector<RedactionTrack> out = std::move(m_done);
    m_done.clear();
    std::sort(out.begin(), out.end(), [](const RedactionTrack& a, const RedactionTrack& b) { return a.start() < b.start(); });
    return out;
}

RedactionResult findRedactions(const QString& path, Ticks from, Ticks to, const RedactionOptions& options, const Progress& progress) {
    RedactionResult r;
    r.error = checkModels(options);
    if (!r.error.isEmpty())
        return r;
    VideoDecoder dec;
    QString err;
    if (!dec.open(path, &err) || to <= from || !dec.seek(from)) {
        r.error = err.isEmpty() ? Tr::tr("The video cannot be read.") : err;
        return r;
    }
    const int rotation = dec.info().rotation;
    // Gaps where a face is briefly missed (turned away, motion blur) are
    // bridged; the box is interpolated there.
    const Ticks maxGap = kTicksPerSecond / 2;
    DetectionTracker faces(RedactionKind::Face, maxGap), plates(RedactionKind::Plate, maxGap);
    FrameConverter conv;
    DecodedFrame f;
    Ticks last = kInvalidTicks;
    const Ticks step = static_cast<Ticks>(kTicksPerSecond / kAnalysisRate * 0.9);
    QString detectError;
    while (dec.next(f)) {
        if (f.pts + std::max<Ticks>(1, f.duration) <= from)
            continue;
        if (f.pts >= to)
            break;
        if (last != kInvalidTicks && f.pts - last < step)
            continue;
        last = f.pts;
        QImage img = conv.convert(f.frame.get(), QSize(1280, 1280));
        if (img.isNull())
            continue;
        if (rotation != 0)
            img = img.transformed(QTransform().rotate(rotation));
        const FrameDetections d = detectAll(img, options, rotation, &detectError);
        if (!detectError.isEmpty()) {
            r.error = detectError;
            return r;
        }
        faces.add(f.pts, d.faces, d.faceScores);
        plates.add(f.pts, d.plates, d.plateScores);
        if (progress && !progress(static_cast<double>(f.pts - from) / (to - from))) {
            r.cancelled = true;
            return r;
        }
    }
    r.tracks = faces.finish();
    auto p = plates.finish();
    r.tracks.insert(r.tracks.end(), std::make_move_iterator(p.begin()), std::make_move_iterator(p.end()));
    return r;
}

RedactionResult findRedactions(const QImage& image, const RedactionOptions& options) {
    RedactionResult r;
    r.error = checkModels(options);
    if (!r.error.isEmpty() || image.isNull())
        return r;
    QString err;
    const FrameDetections d = detectAll(image, options, 0, &err);
    if (!err.isEmpty()) {
        r.error = err;
        return r;
    }
    auto single = [&](const std::vector<QRectF>& boxes, RedactionKind kind) {
        for (const auto& b : boxes) {
            RedactionTrack t;
            t.kind = kind;
            t.boxes.push_back({0, b});
            r.tracks.push_back(std::move(t));
        }
    };
    single(d.faces, RedactionKind::Face);
    single(d.plates, RedactionKind::Plate);
    return r;
}

void hideRegions(QImage& image, const std::vector<Redaction::Region>& regions, RedactionStyle style, double strength) {
    if (image.isNull() || regions.empty())
        return;
    strength = std::clamp(strength, 0.0, 1.0);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    for (const auto& reg : regions) {
        const QRect px = QRectF(reg.rect.x() * image.width(), reg.rect.y() * image.height(), reg.rect.width() * image.width(),
                                reg.rect.height() * image.height())
                             .toAlignedRect()
                             .intersected(image.rect());
        if (px.width() < 2 || px.height() < 2)
            continue;
        QPainterPath shape;
        if (reg.ellipse)
            shape.addEllipse(QRectF(px));
        else
            shape.addRect(QRectF(px));
        p.save();
        p.setClipPath(shape);
        if (style == RedactionStyle::Fill) {
            p.fillRect(px, Qt::black);
        } else {
            // A few cells across the region: nothing recognisable is left at
            // full strength, while weaker settings keep a rough shape.
            const double cells = style == RedactionStyle::Pixelate ? 16.0 - 11.0 * strength : 10.0 - 7.0 * strength;
            const double unit = std::max(px.width(), px.height()) / cells;
            const int cw = std::max(1, static_cast<int>(std::lround(px.width() / unit)));
            const int ch = std::max(1, static_cast<int>(std::lround(px.height() / unit)));
            const QImage small = image.copy(px).scaled(cw, ch, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (style == RedactionStyle::Pixelate) {
                p.drawImage(px, small.scaled(px.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
            } else {
                // Two smooth steps up give a soft blur without blocks.
                const QImage mid = small.scaled(std::max(cw, px.width() / 8), std::max(ch, px.height() / 8), Qt::IgnoreAspectRatio,
                                                Qt::SmoothTransformation);
                p.drawImage(px, mid.scaled(px.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
        }
        p.restore();
    }
}

} // namespace lumora::ai

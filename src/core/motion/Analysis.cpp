#include "Analysis.h"

#include <optional>

#include "media/VideoDecoder.h"
#include "motion/Motion.h"
#include "render/FrameRenderer.h"

namespace lumora::motion {

namespace {
constexpr int kAnalysisWidth = 480;
constexpr int kTrackingWidth = 640;
} // namespace

double pictureAspect(const MediaPool& pool, const Clip& clip) {
    auto m = pool.find(clip.mediaId);
    if (!m || !m->info.video || m->info.video->width <= 0 || m->info.video->height <= 0)
        return 16.0 / 9.0;
    const auto& v = *m->info.video;
    double w = v.width;
    if (v.sampleAspectRatio.num > 0 && v.sampleAspectRatio.den > 0)
        w = w * v.sampleAspectRatio.num / v.sampleAspectRatio.den;
    const double a = w / v.height;
    return (v.rotation == 90 || v.rotation == 270) ? 1.0 / a : a;
}

std::vector<CameraSample> analyzeCameraPath(const QString& path, Ticks from, Ticks to, const Progress& progress) {
    std::vector<CameraSample> out;
    VideoDecoder dec;
    if (!dec.open(path) || to <= from || !dec.seek(from))
        return out;
    GrayConverter conv;
    Pyramid prev;
    CameraSample acc;
    DecodedFrame f;
    while (dec.next(f)) {
        if (f.pts + std::max<Ticks>(1, f.duration) <= from)
            continue;
        if (f.pts >= to)
            break;
        const GrayImage g = conv.convert(f.frame.get(), kAnalysisWidth);
        if (g.isNull())
            continue;
        Pyramid cur = Pyramid::build(g, 3);
        if (!prev.isNull()) {
            // Frames the analysis cannot read (a cut, a flash) count as no motion.
            if (auto s = estimateMotion(prev, cur)) {
                const QPointF c(g.width / 2.0, g.height / 2.0);
                const QPointF d = s->map(c) - c;
                acc.x += d.x() / g.width;
                acc.y += d.y() / g.height;
                acc.angle += s->angleDegrees();
            }
        }
        acc.time = f.pts;
        out.push_back(acc);
        prev = std::move(cur);
        if (progress && !progress(static_cast<double>(f.pts - from) / (to - from)))
            return {};
    }
    return out;
}

MotionPath trackRegion(const TimelineState& timeline, const MediaPool& pool, Ticks from, Ticks to, const QRectF& region,
                       AVRational rate, const Progress& progress) {
    MotionPath out;
    if (to <= from || region.isEmpty() || timeline.tracks.empty())
        return out;
    // Only the storyline: what the follower clip sits on.
    TimelineState state = timeline;
    for (size_t i = 1; i < state.tracks.size(); ++i)
        state.tracks[i].hidden = true;
    FrameRenderer r(&pool);
    r.setTimeline(state);
    const QSize canvas = FrameRenderer::defaultCanvasSize(state, pool);
    r.setCanvasSize(canvas);
    const int w = std::min(kTrackingWidth, canvas.width());
    const QSize size(w, std::max(2, canvas.height() * w / std::max(1, canvas.width())));
    r.setOutputSize(size);
    r.setPrecision(FrameRenderer::Precision::Preview);

    const Ticks step = frameDuration(rate.num > 0 ? rate : AVRational{25, 1});
    const QRectF pixels(region.x() * size.width(), region.y() * size.height(), region.width() * size.width(),
                        region.height() * size.height());
    std::optional<RegionTracker> tracker;
    QPointF start;
    for (Ticks t = from; t < to; t += step) {
        const GrayImage g = grayFromImage(r.render(t));
        if (!tracker) {
            tracker.emplace(g, pixels);
            start = pixels.center();
            out.points.push_back({0, 0.0, 0.0});
        } else {
            const QPointF c = tracker->update(g).center();
            out.points.push_back({t - from, (c.x() - start.x()) / size.width(), (c.y() - start.y()) / size.height()});
        }
        if (progress && !progress(static_cast<double>(t - from) / (to - from)))
            return {};
    }
    return out;
}

} // namespace lumora::motion

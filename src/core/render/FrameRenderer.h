#pragma once

#include <QImage>
#include <QSize>
#include <QUuid>
#include <list>
#include <map>
#include <memory>

#include "media/MediaPool.h"
#include "timeline/Timeline.h"

struct AVFrame;

namespace lumora {

class VideoDecoder;

// Composites the timeline at a given time: the primary storyline with its
// transitions, then every other video track on top (V2, V3, ...), each clip
// with its transform, opacity, fades, and titles.
//
// The same code renders the preview (8-bit, preview size, fast scaling) and
// the export (16-bit per channel, output size, Lanczos scaling), so what you
// see is what you get. Not thread safe: each playback / export worker owns
// its own renderer.
class FrameRenderer {
public:
    enum class Precision {
        Preview, // QImage::Format_ARGB32_Premultiplied
        Final,   // QImage::Format_RGBA64_Premultiplied
    };

    explicit FrameRenderer(const MediaPool* pool);
    ~FrameRenderer();
    FrameRenderer(const FrameRenderer&) = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;

    void setTimeline(const TimelineState& state);
    const TimelineState& timeline() const { return m_state; }
    // Frame size of the timeline (the composition), e.g. 1920x1080.
    void setCanvasSize(QSize size) { m_canvas = size; }
    QSize canvasSize() const { return m_canvas; }
    // Size of the rendered image; the canvas is scaled to it.
    void setOutputSize(QSize size) { m_output = size; }
    void setPrecision(Precision p);

    QImage render(Ticks t);

    // When the frame at `t` is exactly one storyline media frame shown at its
    // native size (no transform, fade, transition, layer or title), returns
    // that decoded frame so an exporter can convert it directly without
    // going through RGB. Otherwise nullptr. Valid until the next call.
    const AVFrame* passthroughFrame(Ticks t);

    // Drops cached decoders (e.g. after the media changed).
    void clearCache();

    // Default canvas size of a timeline: its resolution, else the first
    // video clip's display size, else 1920x1080.
    static QSize defaultCanvasSize(const TimelineState& state, const MediaPool& pool);

private:
    struct Slot;
    struct Layer {
        const Clip* clip = nullptr;
        Ticks sourceTime = 0;
        double opacity = 1.0;
    };

    Slot* slotFor(const Clip& clip);
    QImage clipImage(const Clip& clip, Ticks sourceTime, QSize* fitSize, int* rotation);
    void drawLayer(QPainter& p, const Clip& clip, Ticks t, Ticks sourceTime, double opacity);
    QImage renderStoryline(Ticks t);
    QImage renderStorylineClip(int index, Ticks t);
    QImage newCanvas(const QColor& fill) const;
    Ticks mediaDuration(const QUuid& id) const;
    double fadeFactor(const Clip& c, Ticks t) const;

    const MediaPool* m_pool;
    TimelineState m_state;
    QSize m_canvas{1920, 1080};
    QSize m_output{1920, 1080};
    Precision m_precision = Precision::Preview;
    std::map<QUuid, std::unique_ptr<Slot>> m_slots; // per clip
    std::list<QUuid> m_lru;
    Ticks m_lastTime = 0;
};

// Blurs an image by `radius` pixels (cheap approximation, used by the blur
// transition).
QImage blurred(const QImage& image, double radius);

} // namespace lumora

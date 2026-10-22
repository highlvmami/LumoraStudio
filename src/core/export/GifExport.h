#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QString>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <functional>

#include "TimeBase.h"
#include "timeline/Timeline.h"

namespace lumora {

class MediaPool;

// Animated GIF of a part of the timeline. GIF holds at most 256 colours per
// picture, so one palette is measured over the whole clip (no flicker) and
// the frames are dithered against it.
struct GifSettings {
    Ticks from = 0;
    Ticks to = 0;
    int width = 480;      // height follows the timeline's shape
    double fps = 15.0;    // GIF timing is in 1/100 s, so 10, 12.5, 15, 20, 25 or 50 are exact
    int colors = 256;     // 2 .. 256
    bool dither = true;   // Floyd-Steinberg error diffusion
    int loops = 0;        // 0 = forever, otherwise how many times it plays
};

namespace gif {

// A palette of at most `colors` entries (0xffRRGGBB) for the pictures, by
// median cut over their colours.
QVector<QRgb> makePalette(const QVector<QImage>& samples, int colors);

// `image` as palette indices (one byte per pixel, rows `width` bytes apart).
std::vector<std::uint8_t> quantize(const QImage& image, const QVector<QRgb>& palette, bool dither);

// Renders the range of `timeline` and writes it to `path`. Runs synchronously;
// call it from a worker thread.
bool exportTimeline(const TimelineState& timeline, const MediaPool& pool, const GifSettings& settings, const QString& path,
                    const std::function<void(double)>& progress = {}, const std::atomic_bool* cancel = nullptr,
                    QString* error = nullptr);

// Writes ready pictures (all the same size) as a GIF, each shown for
// `delays[i]` hundredths of a second.
bool writeFrames(const QVector<QImage>& frames, const QVector<int>& delays, const GifSettings& settings, const QString& path,
                 QString* error = nullptr);

} // namespace gif

} // namespace lumora

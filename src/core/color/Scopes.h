#pragma once

#include <QImage>
#include <QPointF>
#include <QSize>
#include <array>

namespace lumora::scopes {

enum class Kind { Histogram, Waveform, Parade, Vectorscope };

// Per channel counts of a picture (R, G, B and Rec.709 luma), 256 bins each.
struct Histogram {
    std::array<std::array<quint32, 256>, 4> bins{};
    quint64 pixels = 0;
};

Histogram histogram(const QImage& image);

// Renders the trace of a scope into a transparent image of `size`; the
// caller draws the background and graticule. Big pictures are sampled down
// first, so this is cheap enough to run on every preview frame.
//
//  Histogram:   x = value 0..255, y = relative count (R, G, B and luma)
//  Waveform:    x = picture column, y = luma 0 (bottom) .. 1 (top)
//  Parade:      the waveform of R, G and B side by side
//  Vectorscope: Cb/Cr plane (Rec.709), centre = neutral, square image
QImage render(const QImage& image, Kind kind, QSize size);

// Position of a colour on the vectorscope, in -0.5 .. 0.5 (Cb right, Cr up),
// for drawing the colour targets and skin tone line.
QPointF vectorscopePosition(double r, double g, double b);

} // namespace lumora::scopes

#pragma once

#include <QImage>
#include <cstdint>
#include <vector>

#include "timeline/ClipEffects.h"

namespace lumora::fx {

// A picture as straight (not premultiplied) RGBA floats in 0..1, row by row.
// The picture effects work on it so they behave the same in the 8-bit
// preview and the 16-bit export.
struct FloatImage {
    int width = 0;
    int height = 0;
    std::vector<float> px; // 4 floats per pixel

    bool isNull() const { return width <= 0 || height <= 0; }
    float* row(int y) { return px.data() + static_cast<size_t>(y) * width * 4; }
    const float* row(int y) const { return px.data() + static_cast<size_t>(y) * width * 4; }
};

FloatImage toFloat(const QImage& image);
// Back to `format` (any 8-bit or 16-bit RGB(A) format).
QImage fromFloat(const FloatImage& image, QImage::Format format);

// Applies the clip's picture effects in place (denoise, sharpen, blur, green
// screen, vignette, grain). `seed` varies the film grain from frame to frame.
// Images without alpha come back with alpha when the green screen is on.
void applyPictureEffects(QImage& image, const PictureEffects& effects, std::uint32_t seed = 0);

// Individual steps on float pictures.
void chromaKey(FloatImage& img, const ChromaKey& key);
void denoise(FloatImage& img, double amount);
void sharpen(FloatImage& img, double amount);
void blur(FloatImage& img, double radius);
void vignette(FloatImage& img, double amount);
void grain(FloatImage& img, double amount, std::uint32_t seed);

// A picture between two neighbouring video frames, `w` of the way from `a`
// to `b` (0 .. 1). Both must have the same size and format; the result has
// that format too.
QImage blendFrames(const QImage& a, const QImage& b, double w);
// Like blendFrames, but things move to where they are at that moment (dense
// optical flow measured between the frames), which makes slow motion smooth.
QImage interpolateFrames(const QImage& a, const QImage& b, double w);

// Dense optical flow from `a` to `b` on grey pictures of the same size (one
// float per pixel): for each pixel of `a` how far it moved, in pixels.
void opticalFlow(const std::vector<float>& a, const std::vector<float>& b, int width, int height, std::vector<float>& flowX,
                 std::vector<float>& flowY);

} // namespace lumora::fx

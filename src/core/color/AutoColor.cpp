#include "AutoColor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace lumora {

namespace {

double toLinear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double toDisplay(double v) { return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055; }

} // namespace

ColorGrade autoCorrect(const QImage& picture, const ColorGrade& base) {
    ColorGrade g = base;
    if (picture.isNull())
        return g;
    // A small copy is plenty for statistics.
    const QImage img = picture.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    std::array<int, 256> hist{};
    double sumR = 0, sumG = 0, sumB = 0, sumSat = 0;
    int count = 0, neutral = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double r = qRed(line[x]) / 255.0, gr = qGreen(line[x]) / 255.0, b = qBlue(line[x]) / 255.0;
            const double lum = 0.2126 * r + 0.7152 * gr + 0.0722 * b;
            ++hist[std::clamp(static_cast<int>(lum * 255 + 0.5), 0, 255)];
            const double mx = std::max({r, gr, b}), mn = std::min({r, gr, b});
            const double sat = mx > 0 ? (mx - mn) / mx : 0;
            sumSat += sat;
            ++count;
            // Grey world on the mid tones that are not strongly coloured.
            if (lum > 0.08 && lum < 0.92 && sat < 0.5) {
                sumR += toLinear(r);
                sumG += toLinear(gr);
                sumB += toLinear(b);
                ++neutral;
            }
        }
    }
    if (count == 0)
        return g;
    auto percentile = [&](double q) {
        const int target = static_cast<int>(q * count);
        int acc = 0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[i];
            if (acc > target)
                return i / 255.0;
        }
        return 1.0;
    };
    const double p1 = percentile(0.01), p50 = percentile(0.5), p99 = percentile(0.99);

    // Exposure: bring the middle of the picture to a natural brightness.
    const double ev = std::clamp(std::log2(toLinear(0.46) / std::max(1e-4, toLinear(std::max(0.02, p50)))) * 0.7, -2.0, 2.0);
    g.exposure = std::round(ev * 100) / 100;
    const double gain = std::exp2(ev);
    const double lo = toDisplay(std::min(1.0, toLinear(p1) * gain));
    const double hi = toDisplay(std::min(1.0, toLinear(p99) * gain));
    // Stretch the ends towards black and white.
    g.whites = std::round(std::clamp((0.96 - hi) * 160, -30.0, 40.0));
    g.blacks = std::round(std::clamp(-(lo - 0.03) * 160, -40.0, 20.0));
    if (hi > 0.97)
        g.highlights = std::round(std::clamp(-(hi - 0.97) * 600, -40.0, 0.0));
    const double spread = hi - lo;
    g.contrast = std::round(std::clamp((0.75 - spread) * 50, 0.0, 25.0));

    // White balance: make the average mid tone grey.
    if (neutral > count / 20 && sumR > 0 && sumG > 0 && sumB > 0) {
        const double t = std::log2(sumB / sumR) / 0.9;
        const double m = -std::log2(std::sqrt(sumR * sumB) / sumG) / 0.4;
        g.temperature = std::round(std::clamp(t * 100 * 0.7, -60.0, 60.0));
        g.tint = std::round(std::clamp(m * 100 * 0.7, -60.0, 60.0));
    }
    // A little more colour for dull pictures.
    const double meanSat = sumSat / count;
    g.vibrance = std::round(std::clamp((0.3 - meanSat) * 80, 0.0, 25.0));
    g.saturation = 0;
    g.shadows = std::round(std::clamp((0.25 - lo) * 40, 0.0, 15.0));
    return g;
}

} // namespace lumora

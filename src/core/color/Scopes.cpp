#include "Scopes.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lumora::scopes {

namespace {

constexpr int kMaxSampleWidth = 640;

QImage sampled(const QImage& image) {
    QImage img = image;
    if (img.width() > kMaxSampleWidth)
        img = img.scaledToWidth(kMaxSampleWidth, Qt::FastTransformation);
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    return img;
}

inline int luma709(int r, int g, int b) { return (54 * r + 183 * g + 19 * b + 128) >> 8; }

// Maps a hit count to a trace brightness; logarithmic so sparse values stay
// visible next to large flat areas.
QImage traceImage(const std::vector<quint32>& counts, int w, int h, const QColor& tint, double reference) {
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const double norm = 1.0 / std::log1p(std::max(1.0, reference));
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const quint32 n = counts[static_cast<size_t>(y) * w + x];
            if (!n)
                continue;
            const double a = std::clamp(0.25 + 0.75 * std::log1p(n) * norm, 0.0, 1.0);
            line[x] = qPremultiply(qRgba(tint.red(), tint.green(), tint.blue(), static_cast<int>(a * 255)));
        }
    }
    return out;
}

void accumulateWaveform(const QImage& img, int channel, std::vector<quint32>& counts, int w, int h) {
    for (int y = 0; y < img.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            int v;
            switch (channel) {
            case 0: v = qRed(p); break;
            case 1: v = qGreen(p); break;
            case 2: v = qBlue(p); break;
            default: v = luma709(qRed(p), qGreen(p), qBlue(p)); break;
            }
            const int col = x * w / img.width();
            const int row = (h - 1) - v * (h - 1) / 255;
            ++counts[static_cast<size_t>(row) * w + col];
        }
    }
}

} // namespace

Histogram histogram(const QImage& image) {
    Histogram h;
    if (image.isNull())
        return h;
    const QImage img = sampled(image);
    for (int y = 0; y < img.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            ++h.bins[0][r];
            ++h.bins[1][g];
            ++h.bins[2][b];
            ++h.bins[3][luma709(r, g, b)];
        }
    }
    h.pixels = static_cast<quint64>(img.width()) * img.height();
    return h;
}

QPointF vectorscopePosition(double r, double g, double b) {
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double cb = (b - y) / 1.8556;
    const double cr = (r - y) / 1.5748;
    return {cb, cr};
}

QImage render(const QImage& image, Kind kind, QSize size) {
    if (size.isEmpty())
        return {};
    if (kind == Kind::Vectorscope) {
        const int s = std::min(size.width(), size.height());
        size = QSize(s, s);
    }
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (image.isNull())
        return out;
    const int w = size.width(), h = size.height();

    if (kind == Kind::Histogram) {
        const Histogram hist = histogram(image);
        // Normalise on the largest inner bin so a clipped spike at 0 or 255
        // does not flatten everything else.
        quint32 peak = 1;
        for (int c = 0; c < 4; ++c)
            for (int i = 1; i < 255; ++i)
                peak = std::max(peak, hist.bins[c][i]);
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor colors[4] = {QColor(235, 70, 70), QColor(70, 220, 90), QColor(70, 130, 255), QColor(230, 230, 230)};
        for (int c : {3, 0, 1, 2}) {
            QPainterPath path;
            path.moveTo(0, h);
            for (int i = 0; i < 256; ++i) {
                const double v = std::min(1.0, hist.bins[c][i] / static_cast<double>(peak));
                path.lineTo(i * (w - 1) / 255.0, h - v * (h - 1));
            }
            path.lineTo(w - 1, h);
            path.closeSubpath();
            QColor fill = colors[c];
            fill.setAlpha(c == 3 ? 60 : 90);
            p.setCompositionMode(c == 3 ? QPainter::CompositionMode_SourceOver : QPainter::CompositionMode_Plus);
            p.fillPath(path, fill);
            QColor line = colors[c];
            line.setAlpha(200);
            p.setPen(QPen(line, 1.0));
            p.drawPath(path);
        }
        return out;
    }

    const QImage img = sampled(image);
    if (kind == Kind::Waveform) {
        std::vector<quint32> counts(static_cast<size_t>(w) * h, 0);
        accumulateWaveform(img, 3, counts, w, h);
        const double reference = static_cast<double>(img.height()) * img.width() / w;
        return traceImage(counts, w, h, QColor(170, 245, 180), reference);
    }

    if (kind == Kind::Parade) {
        const int third = w / 3;
        QPainter p(&out);
        const QColor tints[3] = {QColor(255, 110, 110), QColor(110, 240, 130), QColor(120, 160, 255)};
        for (int c = 0; c < 3; ++c) {
            std::vector<quint32> counts(static_cast<size_t>(third) * h, 0);
            accumulateWaveform(img, c, counts, third, h);
            const double reference = static_cast<double>(img.height()) * img.width() / third;
            p.drawImage(c * third + (w - 3 * third) / 2, 0, traceImage(counts, third, h, tints[c], reference));
        }
        return out;
    }

    // Vectorscope.
    std::vector<quint32> counts(static_cast<size_t>(w) * h, 0);
    const double half = (w - 1) / 2.0;
    for (int y = 0; y < img.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QPointF v = vectorscopePosition(qRed(line[x]) / 255.0, qGreen(line[x]) / 255.0, qBlue(line[x]) / 255.0);
            const int px = static_cast<int>(std::lround(half + v.x() * 2 * half));
            const int py = static_cast<int>(std::lround(half - v.y() * 2 * half));
            if (px >= 0 && px < w && py >= 0 && py < h)
                ++counts[static_cast<size_t>(py) * w + px];
        }
    }
    return traceImage(counts, w, h, QColor(200, 240, 210), static_cast<double>(img.width()) * img.height() / 50.0);
}

} // namespace lumora::scopes

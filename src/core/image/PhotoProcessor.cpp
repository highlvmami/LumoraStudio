#include "PhotoProcessor.h"

#include <QColorSpace>
#include <QPainter>
#include <QImageReader>
#include <QImageWriter>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <map>
#include <mutex>
#include <cmath>
#include <numeric>
#include <vector>

#include "RawDecoder.h"

namespace lumora {

namespace {

inline float blendChannel(BlendMode m, float b, float s) {
    switch (m) {
    case BlendMode::Normal: return s;
    case BlendMode::Multiply: return b * s;
    case BlendMode::Screen: return 1 - (1 - b) * (1 - s);
    case BlendMode::Overlay: return b < 0.5f ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
    case BlendMode::SoftLight: {
        // W3C compositing definition.
        if (s <= 0.5f)
            return b - (1 - 2 * s) * b * (1 - b);
        const float d = b <= 0.25f ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
        return b + (2 * s - 1) * (d - b);
    }
    case BlendMode::Darken: return std::min(b, s);
    case BlendMode::Lighten: return std::max(b, s);
    case BlendMode::Add: return std::min(1.0f, b + s);
    case BlendMode::Difference: return std::abs(b - s);
    }
    return s;
}

// Decoded layer pictures, shared by preview and export.
std::mutex g_layerMutex;
std::map<QString, QImage> g_layerImages;

QImage layerImage(const QString& path) {
    {
        std::lock_guard lock(g_layerMutex);
        auto it = g_layerImages.find(path);
        if (it != g_layerImages.end())
            return it->second;
    }
    QImage img = PhotoProcessor::load(path);
    if (!img.isNull())
        img = img.convertToFormat(QImage::Format_RGBA64_Premultiplied);
    std::lock_guard lock(g_layerMutex);
    if (g_layerImages.size() >= 8)
        g_layerImages.clear();
    g_layerImages[path] = img;
    return img;
}

// Composites one layer onto `base` (RGBA64 / RGBX64, straight alpha) in place.
void compositeLayer(QImage& base, const PhotoLayer& layer) {
    const int w = base.width(), h = base.height();
    QImage source; // straight colour to blend with, same size as base
    QImage coverage; // Image layers: premultiplied layer pixels
    if (layer.kind == LayerKind::Adjustment) {
        source = ColorProcessor(layer.grade).applied(base);
    } else {
        const QImage pic = layerImage(layer.imagePath);
        if (pic.isNull())
            return;
        coverage = QImage(base.size(), QImage::Format_RGBA64_Premultiplied);
        coverage.fill(Qt::transparent);
        QPainter p(&coverage);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSizeF size = QSizeF(pic.size()).scaled(QSizeF(base.size()), Qt::KeepAspectRatio) *
                            std::clamp(layer.scale, 0.01, 100.0);
        const QPointF centre(w / 2.0 + layer.x * w, h / 2.0 + layer.y * h);
        p.drawImage(QRectF(centre - QPointF(size.width() / 2, size.height() / 2), size), pic);
    }
    const bool masked = layer.mask.shape != MaskShape::None || layer.mask.invert;
    const std::vector<float> mask = masked ? layer.mask.render(w, h) : std::vector<float>{};
    const float opacity = static_cast<float>(std::clamp(layer.opacity, 0.0, 1.0));
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);
    QtConcurrent::blockingMap(rows, [&](int y) {
        auto* b = reinterpret_cast<QRgba64*>(base.scanLine(y));
        const QRgba64* s = source.isNull() ? nullptr : reinterpret_cast<const QRgba64*>(source.constScanLine(y));
        const QRgba64* c = coverage.isNull() ? nullptr : reinterpret_cast<const QRgba64*>(coverage.constScanLine(y));
        const float* m = masked ? mask.data() + static_cast<size_t>(y) * w : nullptr;
        for (int x = 0; x < w; ++x) {
            float a = opacity;
            float top[3];
            if (c) {
                const QRgba64 px = c[x];
                if (px.alpha() == 0)
                    continue;
                const float la = px.alpha() / 65535.0f;
                a *= la;
                top[0] = px.red() / 65535.0f / la;
                top[1] = px.green() / 65535.0f / la;
                top[2] = px.blue() / 65535.0f / la;
            } else {
                top[0] = s[x].red() / 65535.0f;
                top[1] = s[x].green() / 65535.0f;
                top[2] = s[x].blue() / 65535.0f;
            }
            if (m)
                a *= m[x];
            if (a <= 0.0f)
                continue;
            const float bottom[3] = {b[x].red() / 65535.0f, b[x].green() / 65535.0f, b[x].blue() / 65535.0f};
            quint16 out[3];
            for (int k = 0; k < 3; ++k) {
                const float blended = blendChannel(layer.blend, bottom[k], std::clamp(top[k], 0.0f, 1.0f));
                const float r = bottom[k] + (blended - bottom[k]) * a;
                out[k] = static_cast<quint16>(std::lround(std::clamp(r, 0.0f, 1.0f) * 65535.0f));
            }
            b[x] = QRgba64::fromRgba64(out[0], out[1], out[2], b[x].alpha());
        }
    });
}

} // namespace

QImage PhotoProcessor::load(const QString& path, QImage* original, QString* error) {
    QImage img;
    if (raw::isRawFile(path)) {
        img = raw::decode(path, error);
        if (img.isNull())
            return {};
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        reader.setAllocationLimit(2048); // MB; large photos are welcome
        img = reader.read();
        if (img.isNull()) {
            if (error)
                *error = reader.errorString();
            return {};
        }
    }
    if (original)
        *original = img;
    QImage working = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_RGBA64 : QImage::Format_RGBX64);
    working.setColorSpace(img.colorSpace());
    return working;
}

QImage PhotoProcessor::apply(const QImage& src, const PhotoAdjustments& adj, bool applyCrop) {
    if (src.isNull())
        return {};
    QImage input = src;
    if (input.format() != QImage::Format_RGBA64 && input.format() != QImage::Format_RGBX64)
        input = input.convertToFormat(QImage::Format_RGBA64);
    const QRect rect = applyCrop ? adj.cropRect(input.size()) : input.rect();
    if (!adj.hasToneChanges()) {
        QImage out = input.copy(rect);
        out.setColorSpace(src.colorSpace());
        return out;
    }
    // Layer masks and placement are relative to the whole picture, so with
    // layers the crop comes last; without, only the kept part is processed.
    const bool layers = adj.hasLayerChanges();
    QImage out = layers ? input.copy() : input.copy(rect);
    ColorProcessor(adj.color).apply(out);
    if (layers) {
        for (const auto& l : adj.layers)
            if (!l.isNoOp())
                compositeLayer(out, l);
        if (rect != out.rect())
            out = out.copy(rect);
    }
    out.setColorSpace(src.colorSpace());
    return out;
}

QImage PhotoProcessor::makeProxy(const QImage& img, int maxSide) {
    if (img.width() <= maxSide && img.height() <= maxSide)
        return img;
    QImage p = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    p.setColorSpace(img.colorSpace());
    return p;
}

int PhotoExporter::sourceBitDepth(const QImage& img) {
    switch (img.format()) {
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_Grayscale16:
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return 16;
    default:
        return 8;
    }
}

QString PhotoExporter::extension(PhotoFormat f) {
    switch (f) {
    case PhotoFormat::Png: return QStringLiteral("png");
    case PhotoFormat::Tiff: return QStringLiteral("tif");
    case PhotoFormat::Jpeg: return QStringLiteral("jpg");
    case PhotoFormat::WebP: return QStringLiteral("webp");
    }
    return QStringLiteral("png");
}

bool PhotoExporter::formatAvailable(PhotoFormat f) {
    const auto formats = QImageWriter::supportedImageFormats();
    switch (f) {
    case PhotoFormat::Png: return formats.contains("png");
    case PhotoFormat::Tiff: return formats.contains("tiff") || formats.contains("tif");
    case PhotoFormat::Jpeg: return formats.contains("jpeg") || formats.contains("jpg");
    case PhotoFormat::WebP: return formats.contains("webp");
    }
    return false;
}

bool PhotoExporter::save(const QString& path, const QImage& original, const QImage& working,
                         const PhotoAdjustments& adj, const PhotoExportSettings& s, QString* error) {
    const int srcDepth = sourceBitDepth(original);
    int depth = s.bitDepth;
    if (depth == 0)
        depth = (adj.hasToneChanges() || srcDepth > 8) ? 16 : 8;
    if (s.format == PhotoFormat::Jpeg || s.format == PhotoFormat::WebP)
        depth = 8; // these formats are 8-bit only

    QImage out;
    if (!adj.hasToneChanges() && depth == srcDepth) {
        // Bit-exact path: crop the decoded source pixels without conversion.
        out = original.copy(adj.cropRect(original.size()));
    } else {
        out = PhotoProcessor::apply(working, adj, true);
        if (depth == 8)
            out = out.convertToFormat(out.hasAlphaChannel() ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    }
    if (s.format == PhotoFormat::Jpeg && out.hasAlphaChannel())
        out = out.convertToFormat(depth == 8 ? QImage::Format_RGB888 : QImage::Format_RGBX64);
    out.setColorSpace(original.colorSpace());

    QImageWriter writer(path);
    switch (s.format) {
    case PhotoFormat::Png:
        writer.setFormat("png");
        writer.setCompression(9); // still lossless, just smaller
        break;
    case PhotoFormat::Tiff:
        writer.setFormat("tiff");
        writer.setCompression(1); // LZW, lossless
        break;
    case PhotoFormat::Jpeg:
        writer.setFormat("jpeg");
        writer.setQuality(s.quality);
        writer.setOptimizedWrite(true);
        break;
    case PhotoFormat::WebP:
        writer.setFormat("webp");
        writer.setQuality(s.quality);
        break;
    }
    if (!writer.write(out)) {
        if (error)
            *error = writer.errorString();
        return false;
    }
    return true;
}

} // namespace lumora

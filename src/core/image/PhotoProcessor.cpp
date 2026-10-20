#include "PhotoProcessor.h"

#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace lumora {

namespace {

inline float srgbToLinear(float v) {
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float v) {
    if (v <= 0.0f)
        return 0.0f;
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// 16-bit decode table: exact for every possible input value.
const std::vector<float>& decodeTable() {
    static const std::vector<float> table = [] {
        std::vector<float> t(65536);
        for (int i = 0; i < 65536; ++i)
            t[i] = srgbToLinear(i / 65535.0f);
        return t;
    }();
    return table;
}

struct ToneParams {
    float wb[3] = {1, 1, 1};
    float exposureGain = 1;
    float contrastPower = 1;
    bool contrast = false;
};

ToneParams makeParams(const PhotoAdjustments& a) {
    ToneParams p;
    // White balance as per-channel gains in linear light, normalised so that
    // overall luminance is unchanged (exposure stays separate).
    const float t = static_cast<float>(a.temperature) / 100.0f;
    const float m = static_cast<float>(a.tint) / 100.0f;
    float r = std::exp2(0.45f * t + 0.10f * m);
    float g = std::exp2(-0.30f * m);
    float b = std::exp2(-0.45f * t + 0.10f * m);
    const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    p.wb[0] = r / lum;
    p.wb[1] = g / lum;
    p.wb[2] = b / lum;
    p.exposureGain = std::exp2(static_cast<float>(a.exposure));
    p.contrast = a.contrast != 0.0;
    p.contrastPower = std::exp2(static_cast<float>(a.contrast) / 100.0f * 0.75f);
    return p;
}

constexpr float kMiddleGrey = 0.18f;

void processRow(const QRgba64* in, QRgba64* out, int width, const ToneParams& p, const std::vector<float>& lut) {
    for (int x = 0; x < width; ++x) {
        const QRgba64 px = in[x];
        float rgb[3] = {lut[px.red()], lut[px.green()], lut[px.blue()]};
        for (int c = 0; c < 3; ++c)
            rgb[c] *= p.wb[c] * p.exposureGain;
        if (p.contrast) {
            // Contrast on luminance in log space around middle grey; scaling
            // RGB by the luminance ratio keeps hue and saturation stable.
            const float lum = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
            if (lum > 1e-6f) {
                const float newLum = kMiddleGrey * std::pow(lum / kMiddleGrey, p.contrastPower);
                const float k = newLum / lum;
                for (float& v : rgb)
                    v *= k;
            }
        }
        auto enc = [](float v) {
            const float e = linearToSrgb(v);
            return static_cast<quint16>(std::lround(std::clamp(e, 0.0f, 1.0f) * 65535.0f));
        };
        out[x] = QRgba64::fromRgba64(enc(rgb[0]), enc(rgb[1]), enc(rgb[2]), px.alpha());
    }
}

} // namespace

QImage PhotoProcessor::load(const QString& path, QImage* original, QString* error) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(2048); // MB; large photos are welcome
    QImage img = reader.read();
    if (img.isNull()) {
        if (error)
            *error = reader.errorString();
        return {};
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
    QImage out(rect.size(), input.format());
    out.setColorSpace(src.colorSpace());
    const ToneParams params = makeParams(adj);
    const auto& lut = decodeTable();
    std::vector<int> rows(rect.height());
    std::iota(rows.begin(), rows.end(), 0);
    QtConcurrent::blockingMap(rows, [&](int y) {
        const auto* in = reinterpret_cast<const QRgba64*>(input.constScanLine(rect.top() + y)) + rect.left();
        auto* o = reinterpret_cast<QRgba64*>(out.scanLine(y));
        processRow(in, o, rect.width(), params, lut);
    });
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

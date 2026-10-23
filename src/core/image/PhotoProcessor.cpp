#include "PhotoProcessor.h"

#include <QColorSpace>
#include <QPainter>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImageWriter>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <list>
#include <map>
#include <mutex>
#include <cmath>
#include <numeric>
#include <vector>

#include "ai/Enhance.h"
#include "ai/Redaction.h"
#include "media/MediaInfo.h"
#include "media/VideoDecoder.h"
#include "ai/Vision.h"
#include "HeifDecoder.h"
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

QImage layerImage(const QString& path, Ticks frameTime) {
    const QString key = path + QLatin1Char('@') + QString::number(frameTime);
    {
        std::lock_guard lock(g_layerMutex);
        auto it = g_layerImages.find(key);
        if (it != g_layerImages.end())
            return it->second;
    }
    // Pictures load through the photo loader (RAW, orientation); anything
    // else is a video and shows one frame.
    QImage img = isImageFile(path) ? PhotoProcessor::load(path) : decodeVideoFrame(path, frameTime);
    if (!img.isNull())
        img = img.convertToFormat(QImage::Format_RGBA64_Premultiplied);
    std::lock_guard lock(g_layerMutex);
    if (g_layerImages.size() >= 8)
        g_layerImages.clear();
    g_layerImages[key] = img;
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
        const QImage pic = layerImage(layer.imagePath, layer.frameTime);
        if (pic.isNull())
            return;
        coverage = QImage(base.size(), QImage::Format_RGBA64_Premultiplied);
        coverage.fill(Qt::transparent);
        QPainter p(&coverage);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSizeF size = QSizeF(pic.size()).scaled(QSizeF(base.size()), Qt::KeepAspectRatio) *
                            std::clamp(layer.scale, 0.01, 100.0);
        p.translate(w / 2.0 + layer.x * w, h / 2.0 + layer.y * h);
        p.rotate(layer.rotation);
        p.drawImage(QRectF(QPointF(-size.width() / 2, -size.height() / 2), size), pic);
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

QSize PhotoProcessor::layerPictureSize(const PhotoLayer& layer) {
    if (layer.kind != LayerKind::Image || layer.imagePath.isEmpty())
        return {};
    return layerImage(layer.imagePath, layer.frameTime).size();
}

QImage PhotoProcessor::load(const QString& path, QImage* original, QString* error) {
    QImage img;
    if (raw::isRawFile(path)) {
        img = raw::decode(path, error);
        if (img.isNull())
            return {};
    } else if (heif::isHeifFile(path)) {
        img = heif::decode(path, error);
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

namespace {

QImage cachedMatte(const QImage& image, const QString& model) {
    static std::mutex mutex;
    static qint64 key = 0;
    static QString keyModel;
    static QImage matte;
    {
        std::lock_guard lock(mutex);
        if (key == image.cacheKey() && keyModel == model && matte.size() == image.size())
            return matte;
    }
    QImage m = ai::subjectMatte(image, model);
    std::lock_guard lock(mutex);
    key = image.cacheKey();
    keyModel = model;
    matte = m;
    return m;
}

// The last few repaired images by what they were made from.
class RetouchCache {
public:
    QImage find(const QString& key) {
        std::lock_guard lock(m_mutex);
        for (auto it = m_items.begin(); it != m_items.end(); ++it)
            if (it->first == key) {
                auto item = *it;
                m_items.erase(it);
                m_items.push_front(item);
                return item.second;
            }
        return {};
    }
    void insert(const QString& key, const QImage& image) {
        std::lock_guard lock(m_mutex);
        m_items.push_front({key, image});
        while (m_items.size() > 6)
            m_items.pop_back();
    }

private:
    std::mutex m_mutex;
    std::list<std::pair<QString, QImage>> m_items;
};

RetouchCache& retouchCache() {
    static RetouchCache cache;
    return cache;
}

QString strokesKey(const std::vector<BrushStroke>& strokes) {
    QJsonArray arr;
    for (const auto& s : strokes)
        arr.append(s.toJson());
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace

QImage PhotoProcessor::retouched(const QImage& image, const PhotoRetouch& retouch, const std::function<bool(double)>& progress,
                                 QString* error) {
    if (image.isNull())
        return {};
    QImage out = image;
    const bool erase = !retouch.erase.empty(), faces = retouch.faces > 0.0;
    // Each step is kept on its own, so moving the face slider does not
    // fill in the removed objects again.
    if (erase) {
        const QString key = QStringLiteral("erase %1 %2").arg(image.cacheKey()).arg(strokesKey(retouch.erase));
        QImage done = retouchCache().find(key);
        if (done.isNull()) {
            LayerMask m;
            m.shape = MaskShape::Brush;
            m.strokes = retouch.erase;
            const std::vector<float> v = m.render(image.width(), image.height());
            QImage mask(image.size(), QImage::Format_Grayscale8);
            for (int y = 0; y < mask.height(); ++y) {
                uchar* line = mask.scanLine(y);
                for (int x = 0; x < mask.width(); ++x)
                    line[x] = static_cast<uchar>(std::lround(std::clamp(v[static_cast<size_t>(y) * mask.width() + x], 0.0f, 1.0f) * 255.0f));
            }
            done = ai::inpaint(image, mask, [&](double p) { return !progress || progress(faces ? p / 2 : p); }, error);
            if (done.isNull())
                return {};
            // Same format as the photo (a photo without transparency gets
            // none), and the same image each time for the caches after.
            if (!image.hasAlphaChannel())
                done = done.convertToFormat(QImage::Format_RGBX64);
            done.setColorSpace(image.colorSpace());
            retouchCache().insert(key, done);
        }
        out = done;
    }
    if (faces) {
        const QString key = QStringLiteral("faces %1 %2").arg(out.cacheKey()).arg(retouch.faces);
        QImage done = retouchCache().find(key);
        if (done.isNull()) {
            done = ai::restoreFaces(out, retouch.faces, [&](double p) { return !progress || progress(erase ? 0.5 + p / 2 : p); }, error);
            if (done.isNull())
                return {};
            // Same format as the photo (a photo without transparency gets
            // none), and the same image each time for the caches after.
            if (!image.hasAlphaChannel())
                done = done.convertToFormat(QImage::Format_RGBX64);
            done.setColorSpace(image.colorSpace());
            retouchCache().insert(key, done);
        }
        out = done;
    }
    if (progress)
        progress(1.0);
    return out;
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
    // Layer masks and placement, hidden regions and the subject are relative
    // to the whole picture, so with any of them the crop comes last; without,
    // only the kept part is processed.
    const bool layers = adj.hasLayerChanges();
    const bool whole = layers || adj.hasAiChanges();
    if (adj.retouch.isActive()) {
        // A missing model leaves the photo as it is.
        const QImage repaired = retouched(input, adj.retouch);
        if (!repaired.isNull())
            input = repaired;
    }
    // The subject is found on the photo as shot, so tone changes do not move
    // the cut-out edge (and the matte is reused while sliders move).
    const QImage matte = adj.background.enabled ? cachedMatte(input, adj.background.model) : QImage();
    QImage out = whole ? input.copy() : input.copy(rect);
    ColorProcessor(adj.color).apply(out);
    if (whole) {
        for (const auto& l : adj.layers)
            if (!l.isNoOp())
                compositeLayer(out, l);
        if (adj.redaction.isActive())
            ai::hideRegions(out, adj.redaction.regionsAt(0), adj.redaction.style, adj.redaction.strength);
        if (!matte.isNull())
            ai::applyMatte(out, matte, adj.background.softness);
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
                         const PhotoAdjustments& adj, const PhotoExportSettings& s, QString* error,
                         const std::function<bool(double)>& progress) {
    const int srcDepth = sourceBitDepth(original);
    int depth = s.bitDepth;
    if (depth == 0)
        depth = (adj.hasToneChanges() || srcDepth > 8) ? 16 : 8;
    if (s.format == PhotoFormat::Jpeg || s.format == PhotoFormat::WebP)
        depth = 8; // these formats are 8-bit only

    const bool enlarge = s.enlarge > 1;
    if (enlarge && s.bitDepth == 0 && s.format != PhotoFormat::Jpeg && s.format != PhotoFormat::WebP)
        depth = 16; // the enlarged detail is new: keep all of it
    QImage out;
    if (!adj.hasToneChanges() && !enlarge && depth == srcDepth) {
        // Bit-exact path: crop the decoded source pixels without conversion.
        out = original.copy(adj.cropRect(original.size()));
    } else {
        // Repairs are slow at full size: they are made first, with progress.
        if (adj.retouch.isActive()) {
            QString why;
            const QImage repaired = PhotoProcessor::retouched(working, adj.retouch, [&](double p) {
                return !progress || progress(enlarge ? p * 0.3 : p);
            }, &why);
            if (repaired.isNull()) {
                if (error)
                    *error = why.isEmpty() ? tr("Cancelled.") : why;
                return false;
            }
        }
        out = PhotoProcessor::apply(working, adj, true);
        if (enlarge) {
            QString why;
            const bool alpha = out.hasAlphaChannel();
            out = ai::upscale(out, s.enlarge, [&](double p) {
                return !progress || progress(adj.retouch.isActive() ? 0.3 + p * 0.7 : p);
            }, &why);
            if (out.isNull()) {
                if (error)
                    *error = why.isEmpty() ? tr("Cancelled.") : why;
                return false;
            }
            if (!alpha)
                out = out.convertToFormat(QImage::Format_RGBX64);
        }
        if (depth == 8)
            out = out.convertToFormat(out.hasAlphaChannel() ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    }
    if (s.format == PhotoFormat::Jpeg && out.hasAlphaChannel()) {
        // JPEG has no transparency: a cut-out subject goes on white.
        QImage flat(out.size(), depth == 8 ? QImage::Format_RGB888 : QImage::Format_RGBX64);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, out);
        p.end();
        out = flat;
    }
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

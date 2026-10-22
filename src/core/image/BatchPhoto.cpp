#include "BatchPhoto.h"

#include <QColorSpace>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QUuid>
#include <cmath>

#include "color/AutoColor.h"

namespace lumora {

namespace {

PhotoFormat formatOf(const QString& path, PhotoFormat fallback) {
    const QString ext = QFileInfo(path).suffix().toLower();
    PhotoFormat f = fallback;
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
        f = PhotoFormat::Jpeg;
    else if (ext == QLatin1String("png"))
        f = PhotoFormat::Png;
    else if (ext == QLatin1String("tif") || ext == QLatin1String("tiff"))
        f = PhotoFormat::Tiff;
    else if (ext == QLatin1String("webp"))
        f = PhotoFormat::WebP;
    return PhotoExporter::formatAvailable(f) ? f : fallback;
}

QImage scaledTo(const QImage& img, QSize size) {
    if (img.size() == size)
        return img;
    QImage src = img;
    if (src.format() == QImage::Format_Indexed8 || src.format() == QImage::Format_Mono || src.format() == QImage::Format_MonoLSB)
        src = src.convertToFormat(src.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    QImage out = src.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    out.setColorSpace(img.colorSpace());
    return out;
}

} // namespace

BatchPhoto::BatchPhoto(const BatchPhotoSettings& settings) : m_s(settings) {
    if (m_s.watermark == BatchPhotoSettings::Watermark::Text && !m_s.watermarkText.trimmed().isEmpty()) {
        // Large enough for big photos; it is scaled down to its place.
        QFont font;
        font.setPixelSize(160);
        font.setBold(true);
        const QFontMetricsF fm(font);
        const QString text = m_s.watermarkText.trimmed();
        const QRectF box = fm.boundingRect(text);
        const int pad = 24;
        QImage pic(int(std::ceil(box.width())) + 2 * pad, int(std::ceil(fm.height())) + 2 * pad, QImage::Format_ARGB32_Premultiplied);
        pic.fill(Qt::transparent);
        QPainter p(&pic);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addText(pad - box.left(), pad + fm.ascent(), font, text);
        // A soft dark edge keeps light text readable on light photos.
        const QColor c = m_s.watermarkColor;
        const bool light = c.lightnessF() > 0.5;
        p.setPen(QPen(light ? QColor(0, 0, 0, 110) : QColor(255, 255, 255, 110), 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.fillPath(path, c);
        p.end();
        m_tempDir = QDir::temp().filePath(QStringLiteral("lumora-batch-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QDir().mkpath(m_tempDir);
        m_textPicture = QDir(m_tempDir).filePath(QStringLiteral("watermark.png"));
        pic.save(m_textPicture);
    }
}

BatchPhoto::~BatchPhoto() {
    if (!m_tempDir.isEmpty())
        QDir(m_tempDir).removeRecursively();
}

QSize BatchPhoto::targetSize(QSize size) const {
    if (size.isEmpty())
        return size;
    double f = 1.0;
    switch (m_s.resize) {
    case BatchPhotoSettings::Resize::None:
        return size;
    case BatchPhotoSettings::Resize::LongEdge:
        f = double(m_s.pixels) / std::max(size.width(), size.height());
        break;
    case BatchPhotoSettings::Resize::Width:
        f = double(m_s.pixels) / size.width();
        break;
    case BatchPhotoSettings::Resize::Height:
        f = double(m_s.pixels) / size.height();
        break;
    case BatchPhotoSettings::Resize::Percent:
        f = m_s.percent / 100.0;
        break;
    }
    if (f > 1.0 && !m_s.enlarge)
        f = 1.0;
    return QSize(std::max(1, int(std::lround(size.width() * f))), std::max(1, int(std::lround(size.height() * f))));
}

QString BatchPhoto::outputPath(const QString& source, int index, int count) const {
    const QFileInfo fi(source);
    const PhotoFormat format = m_s.keepFormat ? formatOf(source, m_s.format.format) : m_s.format.format;
    const int digits = std::max(2, int(QString::number(std::max(1, count)).size()));
    QString name = m_s.pattern.trimmed().isEmpty() ? QStringLiteral("{name}") : m_s.pattern.trimmed();
    name.replace(QLatin1String("{name}"), fi.completeBaseName());
    name.replace(QLatin1String("{n}"), QStringLiteral("%1").arg(index + 1, digits, 10, QLatin1Char('0')));
    name.replace(QLatin1String("{date}"), fi.lastModified().toString(QStringLiteral("yyyy-MM-dd")));
    for (QChar bad : {QLatin1Char('/'), QLatin1Char('\\'), QLatin1Char(':'), QLatin1Char('*'), QLatin1Char('?'), QLatin1Char('"'),
                      QLatin1Char('<'), QLatin1Char('>'), QLatin1Char('|')})
        name.replace(bad, QLatin1Char('_'));
    const QDir dir(m_s.folder.isEmpty() ? fi.absolutePath() : m_s.folder);
    const QString ext = PhotoExporter::extension(format);
    QString path = dir.filePath(name + QLatin1Char('.') + ext);
    // The original is never written over.
    for (int k = 1; QFileInfo(path) == fi; ++k)
        path = dir.filePath(QStringLiteral("%1-%2.%3").arg(name).arg(k).arg(ext));
    return path;
}

bool BatchPhoto::process(const QString& source, const QString& target, QString* error) const {
    QImage original;
    QImage working = PhotoProcessor::load(source, &original, error);
    if (working.isNull())
        return false;
    const QSize size = targetSize(working.size());
    working = scaledTo(working, size);
    original = scaledTo(original, size);

    PhotoAdjustments adj;
    if (m_s.autoColor)
        adj.color = autoCorrect(PhotoProcessor::makeProxy(working, 512));
    if (!m_s.look.isEmpty()) {
        adj.color.look = m_s.look;
        adj.color.lookAmount = m_s.lookAmount;
    }
    const QString mark = m_s.watermark == BatchPhotoSettings::Watermark::Text      ? m_textPicture
                         : m_s.watermark == BatchPhotoSettings::Watermark::Picture ? m_s.watermarkPicture
                                                                                     : QString();
    if (!mark.isEmpty()) {
        PhotoLayer layer;
        layer.id = QUuid::createUuid();
        layer.kind = LayerKind::Image;
        layer.imagePath = mark;
        layer.opacity = std::clamp(m_s.watermarkOpacity, 0.0, 1.0);
        const QSize pic = PhotoProcessor::layerPictureSize(layer);
        if (!pic.isEmpty()) {
            // Scale 1 fits the picture inside the photo; choose the scale that
            // makes it the wanted fraction of the photo width.
            const double A = double(size.width()) / size.height(), a = double(pic.width()) / pic.height();
            const double fitW = std::min(1.0, a / A), fitH = std::min(1.0, A / a);
            const double frac = std::clamp(m_s.watermarkSize, 0.02, 1.0);
            layer.scale = frac / fitW;
            const double w = fitW * layer.scale, h = fitH * layer.scale;
            const double margin = 0.03 * std::min(size.width(), size.height());
            const double mx = margin / size.width(), my = margin / size.height();
            const Qt::Alignment al = m_s.watermarkPosition;
            layer.x = al & Qt::AlignLeft ? -0.5 + mx + w / 2 : al & Qt::AlignRight ? 0.5 - mx - w / 2 : 0.0;
            layer.y = al & Qt::AlignTop ? -0.5 + my + h / 2 : al & Qt::AlignBottom ? 0.5 - my - h / 2 : 0.0;
            adj.layers.push_back(layer);
        }
    }
    PhotoExportSettings settings = m_s.format;
    if (m_s.keepFormat)
        settings.format = formatOf(source, m_s.format.format);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        if (error)
            *error = tr("Cannot create the folder %1.").arg(QFileInfo(target).absolutePath());
        return false;
    }
    return PhotoExporter::save(target, original, working, adj, settings, error);
}

} // namespace lumora

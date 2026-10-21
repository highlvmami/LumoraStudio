#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace lumora {

// HEIF / HEIC photos (the iPhone default) and AVIF, decoded through FFmpeg.
// Phones store a photo as a grid of small HEVC tiles; the tiles are put back
// together into the full picture, turned upright and kept at the precision
// the file holds (10-bit photos stay 16-bit), with the embedded colour
// profile attached.
namespace heif {

struct Info {
    QSize size;       // final size, already turned upright
    int bitDepth = 8; // bits per channel the file holds
    QString codec;    // "hevc", "av1"
};

QStringList fileExtensions();
bool isHeifFile(const QString& path);

bool probe(const QString& path, Info* info, QString* error = nullptr);
// Full picture: RGBX64 / RGBA64.
QImage decode(const QString& path, QString* error = nullptr);
QImage thumbnail(const QString& path, QSize maxSize);

} // namespace heif
} // namespace lumora

#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace lumora {

// Camera RAW photos (CR2/CR3, NEF, ARW, RAF, DNG, ...) through LibRaw. The
// sensor data is developed with the camera's white balance into 16-bit sRGB,
// so the photo tools start from the full precision the camera recorded.
// Without LibRaw at build time available() is false and nothing is listed.
namespace raw {

struct Info {
    QSize size;       // developed size, already turned upright
    QString camera;   // "Canon EOS R6"
    int bitDepth = 0; // bits the sensor recorded
};

bool available();
QString libraryVersion();
QStringList fileExtensions();
bool isRawFile(const QString& path);

bool probe(const QString& path, Info* info, QString* error = nullptr);
// Full development: RGBX64 in sRGB.
QImage decode(const QString& path, QString* error = nullptr);
// The JPEG preview stored in the file when there is one, otherwise a quick
// half-size development; scaled to fit maxSize.
QImage thumbnail(const QString& path, QSize maxSize);

} // namespace raw
} // namespace lumora

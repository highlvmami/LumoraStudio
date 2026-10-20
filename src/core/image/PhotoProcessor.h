#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QString>

#include "image/PhotoAdjustments.h"

namespace lumora {

// CPU implementation of the photo pipeline. Pixels are stored as 16 bits per
// channel; every operation is computed in 32-bit float in linear light:
//
//   decode transfer -> white balance -> exposure -> contrast (luminance, log
//   space around middle grey) -> encode transfer
//
// The interface is deliberately small (image in, settings in, image out) so a
// GPU implementation can replace it later without touching the UI.
class PhotoProcessor {
    Q_DECLARE_TR_FUNCTIONS(PhotoProcessor)

public:
    // Loads a photo with its EXIF orientation applied. `original` keeps the
    // file's own pixel format (for bit-exact lossless saving); the returned
    // working copy is RGBA64.
    static QImage load(const QString& path, QImage* original = nullptr, QString* error = nullptr);

    // Applies tone adjustments (and the crop when `applyCrop`) to an RGBA64
    // image. Returns an RGBA64 image.
    static QImage apply(const QImage& rgba64, const PhotoAdjustments& adj, bool applyCrop = true);

    // Downscaled copy for interactive preview (never used for export).
    static QImage makeProxy(const QImage& rgba64, int maxSide = 2048);
};

enum class PhotoFormat { Png, Tiff, Jpeg, WebP };

struct PhotoExportSettings {
    PhotoFormat format = PhotoFormat::Png;
    // 0 = automatic: keep the source depth, use 16 bit as soon as tones are
    // changed (so no banding is introduced). 8 or 16 forces a depth.
    int bitDepth = 0;
    int quality = 95; // JPEG / lossy WebP only
};

class PhotoExporter {
    Q_DECLARE_TR_FUNCTIONS(PhotoExporter)

public:
    // Renders the full-resolution result and writes it. When there are no
    // tone changes and the depth is unchanged, the source pixels are written
    // untouched (only cropped), so a lossless save is bit exact.
    static bool save(const QString& path, const QImage& original, const QImage& working,
                     const PhotoAdjustments& adj, const PhotoExportSettings& settings, QString* error = nullptr);

    static bool isLossless(const PhotoExportSettings& s) { return s.format == PhotoFormat::Png || s.format == PhotoFormat::Tiff; }
    static bool formatAvailable(PhotoFormat f);
    static QString extension(PhotoFormat f);
    static int sourceBitDepth(const QImage& img);
};

} // namespace lumora

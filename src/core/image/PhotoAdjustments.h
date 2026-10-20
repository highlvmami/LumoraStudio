#pragma once

#include <QJsonObject>
#include <QRectF>

namespace lumora {

// Non-destructive photo edit settings. The original file is never written;
// these values are stored in the project and applied on display / export.
struct PhotoAdjustments {
    double exposure = 0.0;    // stops (EV), -5 .. +5
    double contrast = 0.0;    // -100 .. +100
    double temperature = 0.0; // -100 (cooler) .. +100 (warmer)
    double tint = 0.0;        // -100 (green) .. +100 (magenta)
    // Crop rectangle in normalised coordinates of the (orientation corrected)
    // source image. A null rectangle means "no crop".
    QRectF crop;

    bool hasToneChanges() const { return exposure != 0.0 || contrast != 0.0 || temperature != 0.0 || tint != 0.0; }
    bool hasCrop() const { return !crop.isNull() && crop != QRectF(0, 0, 1, 1); }
    bool isIdentity() const { return !hasToneChanges() && !hasCrop(); }

    // Pixel rectangle of the crop for an image of the given size.
    QRect cropRect(QSize imageSize) const;

    QJsonObject toJson() const;
    static PhotoAdjustments fromJson(const QJsonObject& o);

    bool operator==(const PhotoAdjustments&) const = default;
};

} // namespace lumora

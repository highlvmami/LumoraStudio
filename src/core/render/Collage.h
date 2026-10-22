#pragma once

#include <QRectF>
#include <QString>
#include <vector>

#include "timeline/ClipEffects.h"

namespace lumora {

// Ready-made collage layouts: cells in fractions of the frame.
struct CollageLayout {
    QString id;
    QString name; // translated
    std::vector<QRectF> cells;
};

const std::vector<CollageLayout>& collageLayouts();
const CollageLayout* findCollageLayout(const QString& id);

// Transform that fills `cell` (fractions of the frame, minus `gap` on each
// side, as a fraction of the frame's short side) with a picture of
// `pictureAspect` (width / height as shown) on a frame of `frameAspect`.
// The picture is scaled to cover the cell and the overflow is cropped.
ClipTransform collageTransform(const QRectF& cell, double pictureAspect, double frameAspect, double gap = 0.0);

} // namespace lumora

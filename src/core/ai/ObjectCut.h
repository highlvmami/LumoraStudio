#pragma once

#include <QImage>
#include <QRectF>
#include <QString>
#include <functional>

#include "timeline/ClipEffects.h"
#include "timeline/Timeline.h"

namespace lumora::ai {

// Cutting out one chosen object (instead of "the subject"): the user draws a
// box around it, the box is followed through the video, and on each frame a
// segmentation model outlines the object inside the box.

// How much of each pixel belongs to the object inside `box` (fractions of
// the picture): Grayscale8 of the picture's size, 0 outside the box.
// `precise` uses EfficientSAM (about a second per picture); otherwise the
// fast subject model looks at the box only (for the preview).
QImage objectMatte(const QImage& image, const QRectF& box, bool precise, QString* error = nullptr);

struct ObjectTrackResult {
    RedactionTrack track; // boxes at media times, fractions of the upright picture
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled && !track.boxes.empty(); }
};

// Follows the object in `box` (fractions of the upright picture at the
// clip's first frame) through the clip's media.
ObjectTrackResult trackObject(const QString& path, const Clip& clip, const QRectF& box,
                              const std::function<bool(double)>& progress = {});

} // namespace lumora::ai

#pragma once

#include <QPointF>
#include <QSize>
#include <QString>
#include <functional>
#include <vector>

#include "TimeBase.h"
#include "timeline/ClipEffects.h"
#include "timeline/Timeline.h"

namespace lumora::ai {

// Reports progress (0..1); returning false cancels the work.
using Progress = std::function<bool(double)>;

// Automatic reframing: a landscape video becomes portrait (Reels, TikTok,
// Shorts) or square by filling the new frame and moving the picture so the
// person in it stays in view, like a camera operator would: the frame holds
// still while the face moves a little, follows smoothly when it walks away,
// and cuts straight to another person who takes over the picture.

// The output size for a format (width / height `aspect`) that keeps the
// detail of the upright `source` picture: 1080 wide, 2160 when the part cut
// out of the source is that wide.
QSize reframeSize(double aspect, QSize source);

// Scale (ClipTransform::scale, 1 = fit) that fills `frame` with a picture of
// `pictureAspect` (width / height) without bars.
double coverScale(double pictureAspect, QSize frame);

// Where the subject is in the picture at a moment: centre in fractions of
// the upright picture; `track` tells people apart.
struct SubjectSample {
    Ticks time = 0; // clip-local timeline time
    QPointF centre;
    int track = -1; // -1 = nobody in the picture
};

// Offsets of the filled picture (MotionPath, fractions of the frame) that
// keep the subject in view. `deadZone` is how far (fraction of the frame)
// the subject may wander before the frame follows.
MotionPath cameraPath(const std::vector<SubjectSample>& samples, double pictureAspect, QSize frame, double deadZone = 0.12);

struct ReframeResult {
    std::vector<SubjectSample> samples;
    int faces = 0; // people seen
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Finds the main person in a video clip about five times a second (the
// largest face, which changes only when another one is clearly bigger).
ReframeResult findSubject(const QString& path, const Clip& clip, const Progress& progress = {});

} // namespace lumora::ai

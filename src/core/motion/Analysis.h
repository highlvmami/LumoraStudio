#pragma once

#include <QRectF>
#include <QString>
#include <functional>
#include <vector>

#include "TimeBase.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora::motion {

// Reports progress (0..1); returning false cancels the work.
using Progress = std::function<bool(double)>;

// Width / height of a clip's picture as shown (pixel aspect and rotation
// applied); 16:9 when unknown.
double pictureAspect(const MediaPool& pool, const Clip& clip);

// Measures the camera path of [from, to) of a video file for stabilisation:
// one sample per decoded frame. Returns an empty path when the file cannot be
// read or the work was cancelled.
std::vector<CameraSample> analyzeCameraPath(const QString& path, Ticks from, Ticks to, const Progress& progress = {});

// Follows the object inside `region` (a fraction of the frame) on the
// primary storyline from timeline time `from` to `to`, as the picture is
// shown (with every transform and transition), frame by frame at `rate`.
// The result is the movement of the region's centre relative to `from`,
// as a fraction of the frame, at times relative to `from`.
MotionPath trackRegion(const TimelineState& timeline, const MediaPool& pool, Ticks from, Ticks to, const QRectF& region,
                       AVRational rate, const Progress& progress = {});

} // namespace lumora::motion

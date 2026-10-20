#pragma once

#include <QCoreApplication>

#include "export/ExportSettings.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora {

// Decides how a timeline will be written for the chosen mode and explains the
// consequences before anything is written: whether the streams can be copied
// untouched, where cuts snap to key frames, which encoder is used, and so on.
//
// This is the "quality gate" of the application: the planner never silently
// chooses a lossy path. If the requested mode cannot be honoured it reports
// blockers instead of degrading quality.
class ExportPlanner {
    Q_DECLARE_TR_FUNCTIONS(ExportPlanner)

public:
    static ExportPlan plan(const TimelineState& timeline, const MediaPool& pool, const ExportSettings& settings);

    // Default file extension / muxer for each mode, given the first source.
    static QString suggestedExtension(ExportMode mode, const QString& sourcePath);

    // Encoder names FFmpeg offers for `codec`, best first, filtered to those
    // available in the linked FFmpeg build.
    static QStringList encodersFor(int codecId);
};

// Snaps `t` back to the closest key frame at or before it. Returns `t` when no
// key frames are known.
Ticks previousKeyframe(const std::vector<Ticks>& keyframes, Ticks t, Ticks tolerance = 0);

} // namespace lumora

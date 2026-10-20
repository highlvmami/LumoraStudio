#pragma once

#include <QString>
#include <vector>

#include "TimeBase.h"

namespace lumora {

class MediaPool;
struct TimelineState;
struct MediaItem;

// Flattened, immutable view of what the preview plays: either the timeline
// (program) or one media file (source monitor). Built on the GUI thread and
// handed to the playback threads by value.
struct PlaybackSegment {
    QString path;
    Ticks sourceIn = 0;
    Ticks sourceOut = 0;
    Ticks start = 0; // position in the sequence
    bool video = true;
    bool audio = false;
    int rotation = 0;

    Ticks end() const { return start + (sourceOut - sourceIn); }
};

struct PlaybackSequence {
    std::vector<PlaybackSegment> segments;
    Ticks duration = 0;
    AVRational frameRate{25, 1};

    const PlaybackSegment* segmentAt(Ticks t) const;
    static PlaybackSequence fromTimeline(const TimelineState& state, const MediaPool& pool, AVRational rate);
    static PlaybackSequence fromMedia(const MediaItem& item);
};

} // namespace lumora

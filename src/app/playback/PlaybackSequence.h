#pragma once

#include <QSize>
#include <memory>

#include "TimeBase.h"
#include "timeline/Timeline.h"

namespace lumora {

class MediaPool;
struct MediaItem;

// What the preview plays: the timeline (program) or one media file shown as a
// one-clip timeline (source monitor). Built on the GUI thread and handed to
// the playback threads by value; it carries its own snapshot of the media it
// uses so the threads never touch the project's media pool.
struct PlaybackSequence {
    TimelineState timeline;
    std::shared_ptr<MediaPool> media;
    Ticks duration = 0;
    AVRational frameRate{25, 1};
    QSize canvas{1920, 1080}; // frame size of the composition

    bool isEmpty() const { return duration <= 0; }
    static PlaybackSequence fromTimeline(const TimelineState& state, const MediaPool& pool, AVRational rate);
    static PlaybackSequence fromMedia(const std::shared_ptr<MediaItem>& item);
};

} // namespace lumora

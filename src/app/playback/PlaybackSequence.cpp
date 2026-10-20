#include "PlaybackSequence.h"

#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora {

const PlaybackSegment* PlaybackSequence::segmentAt(Ticks t) const {
    for (const auto& s : segments)
        if (t >= s.start && t < s.end())
            return &s;
    return nullptr;
}

PlaybackSequence PlaybackSequence::fromTimeline(const TimelineState& state, const MediaPool& pool, AVRational rate) {
    PlaybackSequence seq;
    seq.frameRate = rate;
    if (state.tracks.empty())
        return seq;
    const Track& v = state.tracks[0];
    const Track* a = state.tracks.size() > 1 ? &state.tracks[1] : nullptr;
    for (const auto& c : v.clips) {
        auto media = pool.find(c.mediaId);
        if (!media)
            continue;
        PlaybackSegment s;
        s.path = media->path;
        s.sourceIn = c.sourceIn;
        s.sourceOut = c.sourceOut;
        s.start = c.timelineStart;
        s.video = !v.hidden && media->info.video.has_value();
        s.rotation = media->info.video ? media->info.video->rotation : 0;
        if (a && !a->muted && !c.linkId.isNull() && media->info.audio)
            for (const auto& ac : a->clips)
                if (ac.linkId == c.linkId)
                    s.audio = true;
        seq.segments.push_back(s);
        seq.duration = std::max(seq.duration, s.end());
    }
    return seq;
}

PlaybackSequence PlaybackSequence::fromMedia(const MediaItem& item) {
    PlaybackSequence seq;
    if (item.info.video)
        seq.frameRate = item.info.video->frameRate;
    PlaybackSegment s;
    s.path = item.path;
    s.sourceIn = 0;
    s.sourceOut = item.info.duration;
    s.video = item.info.video.has_value();
    s.audio = item.info.audio.has_value();
    s.rotation = item.info.video ? item.info.video->rotation : 0;
    seq.segments.push_back(s);
    seq.duration = s.sourceOut;
    return seq;
}

} // namespace lumora

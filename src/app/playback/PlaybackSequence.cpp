#include "PlaybackSequence.h"

#include "media/MediaPool.h"
#include "render/FrameRenderer.h"

namespace lumora {

PlaybackSequence PlaybackSequence::fromTimeline(const TimelineState& state, const MediaPool& pool, AVRational rate) {
    PlaybackSequence seq;
    seq.timeline = state;
    seq.frameRate = rate;
    seq.duration = state.duration();
    seq.media = std::make_shared<MediaPool>();
    for (const auto& t : state.tracks)
        for (const auto& c : t.clips)
            if (auto m = pool.find(c.mediaId); m && !seq.media->find(m->id))
                seq.media->add(m);
    seq.canvas = FrameRenderer::defaultCanvasSize(state, *seq.media);
    return seq;
}

PlaybackSequence PlaybackSequence::fromMedia(const std::shared_ptr<MediaItem>& item) {
    PlaybackSequence seq;
    if (!item)
        return seq;
    seq.media = std::make_shared<MediaPool>();
    seq.media->add(item);
    if (item->info.video && item->info.video->frameRate.num > 0)
        seq.frameRate = item->info.video->frameRate;
    Timeline tl;
    tl.insertMedia(item->id, 0, item->info.duration, item->info.video.has_value(), item->info.audio.has_value());
    seq.timeline = tl.state();
    seq.duration = seq.timeline.duration();
    seq.canvas = FrameRenderer::defaultCanvasSize(seq.timeline, *seq.media);
    return seq;
}

} // namespace lumora

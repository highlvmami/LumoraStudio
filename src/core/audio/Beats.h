#pragma once

#include <QString>
#include <QUuid>
#include <functional>
#include <vector>

#include "TimeBase.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora::beats {

// Reports progress (0..1); returning false cancels the work.
using Progress = std::function<bool(double)>;

// Beat tracking: an onset envelope (rises of loudness in 40 frequency
// bands), the tempo from its autocorrelation (with a preference for tempos
// near 120 BPM), and the beats as the best chain of strong onsets about one
// period apart (dynamic programming, D. Ellis 2007).
struct Beats {
    std::vector<double> times; // seconds from the start of the sound
    double bpm = 0.0;
};
Beats detect(const std::vector<float>& mono, int rate);

struct ClipBeats {
    std::vector<Ticks> times; // timeline time
    double bpm = 0.0;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};
// Beats of the music in an audio clip, where they are heard on the timeline.
ClipBeats clipBeats(const TimelineState& timeline, const MediaPool& pool, const QUuid& clipId, const Progress& progress = {});

// The clip most likely to be the music: the longest audio clip that is not
// the sound of a video clip. Null when there is none.
QUuid findMusicClip(const TimelineState& timeline);

// Moves the cuts of the storyline onto the beats. `every` = 0 moves each
// cut to the beat nearest to it; `every` = n makes each clip last exactly n
// beats. Clips only get as long as their media allows; clips after the last
// beat are left as they are. Returns the number of clips changed.
int cutToBeats(Timeline& timeline, const std::vector<Ticks>& beats, int every,
               const std::function<Ticks(const QUuid& mediaId)>& mediaDuration);

} // namespace lumora::beats

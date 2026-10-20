#pragma once

#include <QUuid>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora {

// Mixes every unmuted audio track of the timeline: clip gain, fades, and
// equal-power crossfades under storyline transitions (the audio of the
// outgoing clip keeps playing past the cut while the incoming one starts
// early, exactly like the picture). Output is interleaved 32-bit float at a
// fixed rate and channel count. Used for preview playback and for exports
// whose audio cannot simply be copied. Not thread safe.
class AudioMixer {
public:
    AudioMixer(const MediaPool* pool, int sampleRate, int channels);
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    void setTimeline(const TimelineState& state);
    int sampleRate() const { return m_rate; }
    int channels() const { return m_channels; }

    // Mixes `frames` sample frames starting at timeline sample `start`
    // (sample n is at time n / sampleRate) into `out`, which must hold
    // frames * channels floats.
    void mix(std::int64_t start, int frames, float* out);

    // True when nothing in the timeline produces sound.
    bool silent() const;

private:
    struct Source;
    struct Voice {
        const Clip* clip = nullptr;
        Ticks from = 0, to = 0;            // audible range on the timeline
        Ticks fadeInFrom = 0, fadeInTo = 0;   // transition crossfade in
        Ticks fadeOutFrom = 0, fadeOutTo = 0; // transition crossfade out
        double gain = 1.0;
    };

    void buildVoices();
    Source* sourceFor(const Clip& clip);
    double gainAt(const Voice& v, Ticks t) const;

    const MediaPool* m_pool;
    int m_rate;
    int m_channels;
    TimelineState m_state;
    std::vector<Voice> m_voices;
    std::map<QUuid, std::unique_ptr<Source>> m_sources;
    std::vector<float> m_scratch;
};

} // namespace lumora

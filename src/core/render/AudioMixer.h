#pragma once

#include <QUuid>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora {

// Mixes every unmuted audio track of the timeline: each clip's effects
// (low cut, noise reduction, equaliser, compressor), clip gain, fades,
// equal-power crossfades under storyline transitions (the audio of the
// outgoing clip keeps playing past the cut while the incoming one starts
// early, exactly like the picture) and ducking; then the master section
// (normalisation gain and limiter). Output is interleaved 32-bit float at a
// fixed rate and channel count. Used for preview playback, loudness
// measurement and exports whose audio cannot simply be copied. Effects with
// a delay (noise reduction, limiter) are compensated, so the result lines up
// with the picture sample for sample. Not thread safe.
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

    // Master section on/off (off for loudness measurement) and the
    // normalisation gain worked out by measuring (applied only when the
    // timeline's master settings ask for normalisation).
    void setMasterEnabled(bool on);
    void setNormalizationGainDb(double db);

private:
    struct Source;
    struct ClipFx;
    struct Master;
    struct Voice {
        const Clip* clip = nullptr;
        Ticks from = 0, to = 0;            // audible range on the timeline
        Ticks fadeInFrom = 0, fadeInTo = 0;   // transition crossfade in
        Ticks fadeOutFrom = 0, fadeOutTo = 0; // transition crossfade out
        double gain = 1.0;
    };

    void buildVoices();
    void rebuildMaster();
    Source* sourceFor(const Clip& clip);
    double gainAt(const Voice& v, Ticks t) const;
    // The mix before the master section; must be called for consecutive
    // ranges to keep effect state continuous (anything else resets it).
    void mixRaw(std::int64_t start, int frames, float* out);
    // `n` processed media samples of a clip starting at media sample `from`.
    void readVoice(const Clip& clip, Source* src, std::int64_t from, int n, float* dst);

    const MediaPool* m_pool;
    int m_rate;
    int m_channels;
    TimelineState m_state;
    std::vector<Voice> m_voices;
    std::map<QUuid, std::unique_ptr<Source>> m_sources;
    std::map<QUuid, std::unique_ptr<ClipFx>> m_fx;
    std::vector<float> m_scratch;
    std::vector<float> m_raw;

    // Ducking: follows the level of the clips that are not ducked.
    std::int64_t m_rawNext = INT64_MIN;
    double m_keyEnv = 0.0;
    double m_duck = 0.0; // 0 = full level .. 1 = fully ducked
    std::vector<float> m_duckCurve;

    // Master section.
    bool m_masterEnabled = true;
    double m_normGainDb = 0.0;
    std::unique_ptr<Master> m_master;
    std::int64_t m_masterNext = INT64_MIN;
};

} // namespace lumora

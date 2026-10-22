#include "AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include "audio/AudioDsp.h"
#include "media/AudioDecoder.h"

namespace lumora {

// Decoded audio of one clip, kept as a small sliding window in "media sample"
// coordinates (sample n of the media is at media time n / rate).
struct AudioMixer::Source {
    std::unique_ptr<AudioDecoder> decoder;
    bool failed = false;
    bool eof = false;
    std::deque<float> buffer; // interleaved
    std::int64_t bufferStart = 0; // media sample of buffer[0]
    int channels = 2;
    int rate = 48000;

    std::int64_t bufferEnd() const { return bufferStart + static_cast<std::int64_t>(buffer.size()) / channels; }

    void seek(std::int64_t sample) {
        buffer.clear();
        eof = false;
        bufferStart = sample;
        decoder->seek(av_rescale(sample, kTicksPerSecond, rate));
        primed = false;
    }

    // Copies media samples [from, from + n) into dst (zeros where there is
    // no audio).
    void read(std::int64_t from, int n, float* dst) {
        std::fill(dst, dst + static_cast<size_t>(n) * channels, 0.0f);
        if (failed)
            return;
        const std::int64_t to = from + n;
        if (!primed || from < bufferStart || from > bufferEnd() + rate / 4)
            seek(from);
        std::vector<float> chunk;
        while (!eof && bufferEnd() < to) {
            Ticks start = 0;
            if (!decoder->next(chunk, start)) {
                eof = true;
                break;
            }
            std::int64_t chunkStart = av_rescale(start, rate, kTicksPerSecond);
            std::int64_t count = static_cast<std::int64_t>(chunk.size()) / channels;
            std::int64_t skip = 0;
            if (!primed) {
                bufferStart = chunkStart;
                primed = true;
            } else if (chunkStart > bufferEnd()) {
                buffer.insert(buffer.end(), static_cast<size_t>(chunkStart - bufferEnd()) * channels, 0.0f);
            } else if (chunkStart < bufferEnd()) {
                skip = std::min(count, bufferEnd() - chunkStart);
            }
            buffer.insert(buffer.end(), chunk.begin() + skip * channels, chunk.end());
            // Never keep more than a little audio before the read position.
            const std::int64_t drop = std::min<std::int64_t>(from - bufferStart, bufferEnd() - bufferStart);
            if (drop > rate) {
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(drop * channels));
                bufferStart += drop;
            }
        }
        const std::int64_t a = std::max(from, bufferStart);
        const std::int64_t b = std::min(to, bufferEnd());
        for (std::int64_t s = a; s < b; ++s)
            for (int ch = 0; ch < channels; ++ch)
                dst[(s - from) * channels + ch] = buffer[static_cast<size_t>((s - bufferStart) * channels + ch)];
        // Drop what has been consumed (keep a little for small rewinds).
        const std::int64_t keepFrom = from - rate / 10;
        if (keepFrom > bufferStart) {
            const std::int64_t drop = std::min(keepFrom - bufferStart, bufferEnd() - bufferStart);
            buffer.erase(buffer.begin(), buffer.begin() + static_cast<long>(drop * channels));
            bufferStart += drop;
        }
    }

    bool primed = false;
};

// Effect state of one clip. Output samples lag the input by the chain's
// latency, so the input is read that far ahead.
struct AudioMixer::ClipFx {
    AudioEffects effects;
    std::unique_ptr<dsp::ClipChain> chain;
    std::int64_t inPos = 0;            // next media sample to feed
    std::int64_t outPos = INT64_MIN;   // media sample of pending[0]
    std::deque<float> pending;         // processed, interleaved
};

struct AudioMixer::Master {
    AudioMaster settings;
    double gainDb = 0.0;
    std::unique_ptr<dsp::Limiter> limiter;
    int latency() const { return limiter ? limiter->latency() : 0; }
};

AudioMixer::AudioMixer(const MediaPool* pool, int sampleRate, int channels)
    : m_pool(pool), m_rate(sampleRate), m_channels(channels) {}

AudioMixer::~AudioMixer() = default;

void AudioMixer::setMasterEnabled(bool on) {
    if (m_masterEnabled == on)
        return;
    m_masterEnabled = on;
    rebuildMaster();
}

void AudioMixer::setNormalizationGainDb(double db) {
    if (m_normGainDb == db)
        return;
    m_normGainDb = db;
    rebuildMaster();
}

void AudioMixer::rebuildMaster() {
    const AudioMaster& ms = m_state.audioMaster;
    if (!m_masterEnabled || ms.isIdentity()) {
        m_master.reset();
        m_masterNext = INT64_MIN;
        return;
    }
    const double gain = ms.normalize ? m_normGainDb : 0.0;
    if (m_master && m_master->settings == ms && m_master->gainDb == gain)
        return;
    const bool sameLimiter = m_master && m_master->limiter && m_master->settings.ceilingDb == ms.ceilingDb;
    if (!m_master)
        m_master = std::make_unique<Master>();
    m_master->settings = ms;
    m_master->gainDb = gain;
    if (ms.limiterActive()) {
        if (!sameLimiter) {
            m_master->limiter = std::make_unique<dsp::Limiter>(m_rate, m_channels, ms.ceilingDb);
            m_masterNext = INT64_MIN;
        }
    } else if (m_master->limiter) {
        m_master->limiter.reset();
        m_masterNext = INT64_MIN;
    }
}

void AudioMixer::setTimeline(const TimelineState& state) {
    m_state = state;
    buildVoices();
    // Forget decoders and effect state of clips that no longer exist.
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if (!m_state.findClip(it->first))
            it = m_sources.erase(it);
        else
            ++it;
    }
    for (auto it = m_fx.begin(); it != m_fx.end();) {
        const Clip* c = m_state.findClip(it->first);
        if (!c || c->audio.chainIsIdentity())
            it = m_fx.erase(it);
        else
            ++it;
    }
    rebuildMaster();
}

void AudioMixer::buildVoices() {
    m_voices.clear();
    auto dur = [this](const QUuid& id) {
        auto m = m_pool ? m_pool->find(id) : nullptr;
        return m ? m->info.duration : Ticks(0);
    };
    std::map<QUuid, int> storylineIndex; // link id -> V1 index
    if (!m_state.tracks.empty())
        for (int k = 0; k < static_cast<int>(m_state.tracks[0].clips.size()); ++k)
            if (!m_state.tracks[0].clips[k].linkId.isNull())
                storylineIndex[m_state.tracks[0].clips[k].linkId] = k;
    for (int ti : m_state.trackIndices(TrackType::Audio)) {
        const Track& t = m_state.tracks[ti];
        if (t.muted)
            continue;
        for (const auto& c : t.clips) {
            if (c.kind != ClipKind::Media)
                continue;
            Voice v;
            v.clip = &c;
            v.from = c.timelineStart;
            v.to = c.timelineEnd();
            v.gain = std::pow(10.0, c.gainDb / 20.0);
            auto it = storylineIndex.find(c.linkId);
            if (!c.linkId.isNull() && it != storylineIndex.end()) {
                const int k = it->second;
                const Ticks dIn = m_state.transitionDuration(k, dur);
                if (dIn > 0) {
                    v.from = c.timelineStart - dIn / 2;
                    v.fadeInFrom = v.from;
                    v.fadeInTo = c.timelineStart + dIn / 2;
                }
                const Ticks dOut = m_state.transitionDuration(k + 1, dur);
                if (dOut > 0) {
                    v.to = c.timelineEnd() + dOut / 2;
                    v.fadeOutFrom = c.timelineEnd() - dOut / 2;
                    v.fadeOutTo = v.to;
                }
            }
            m_voices.push_back(v);
        }
    }
}

bool AudioMixer::silent() const { return m_voices.empty(); }

double AudioMixer::gainAt(const Voice& v, Ticks t) const {
    const Clip& c = *v.clip;
    double g = v.gain;
    const Ticks local = t - c.timelineStart;
    if (c.fadeIn > 0 && local < c.fadeIn)
        g *= std::clamp(static_cast<double>(local) / c.fadeIn, 0.0, 1.0);
    const Ticks left = c.timelineEnd() - t;
    if (c.fadeOut > 0 && left < c.fadeOut)
        g *= std::clamp(static_cast<double>(left) / c.fadeOut, 0.0, 1.0);
    if (v.fadeInTo > v.fadeInFrom && t < v.fadeInTo) {
        const double p = std::clamp(static_cast<double>(t - v.fadeInFrom) / (v.fadeInTo - v.fadeInFrom), 0.0, 1.0);
        g *= std::sin(p * M_PI / 2);
    }
    if (v.fadeOutTo > v.fadeOutFrom && t >= v.fadeOutFrom) {
        const double p = std::clamp(static_cast<double>(t - v.fadeOutFrom) / (v.fadeOutTo - v.fadeOutFrom), 0.0, 1.0);
        g *= std::cos(p * M_PI / 2);
    }
    return g;
}

AudioMixer::Source* AudioMixer::sourceFor(const Clip& clip) {
    auto it = m_sources.find(clip.id);
    if (it != m_sources.end())
        return it->second.get();
    auto src = std::make_unique<Source>();
    src->channels = m_channels;
    src->rate = m_rate;
    auto media = m_pool ? m_pool->find(clip.mediaId) : nullptr;
    if (!media || !media->info.audio) {
        src->failed = true;
    } else {
        src->decoder = std::make_unique<AudioDecoder>();
        if (!src->decoder->open(media->path, m_rate, m_channels))
            src->failed = true;
    }
    return (m_sources[clip.id] = std::move(src)).get();
}

void AudioMixer::mix(std::int64_t start, int frames, float* out) {
    if (frames <= 0)
        return;
    if (!m_master) {
        mixRaw(start, frames, out);
        return;
    }
    const int look = m_master->latency();
    const size_t samples = static_cast<size_t>(frames) * m_channels;
    if (start != m_masterNext) {
        // The limiter's output lags its input by `look` samples: feed it the
        // first `look` samples of the request, so what comes out next is
        // exactly the requested range.
        if (m_master->limiter)
            m_master->limiter->reset();
        if (look > 0) {
            std::vector<float> prime(static_cast<size_t>(look) * m_channels);
            mixRaw(start, look, prime.data());
            const float g = static_cast<float>(dsp::dbToGain(m_master->gainDb));
            for (float& v : prime)
                v *= g;
            m_master->limiter->process(prime.data(), look);
        }
    }
    mixRaw(start + look, frames, out);
    const float g = static_cast<float>(dsp::dbToGain(m_master->gainDb));
    if (g != 1.0f)
        for (size_t i = 0; i < samples; ++i)
            out[i] *= g;
    if (m_master->limiter)
        m_master->limiter->process(out, frames);
    m_masterNext = start + frames;
}

void AudioMixer::readVoice(const Clip& c, Source* src, std::int64_t from, int n, float* dst) {
    auto readPadded = [&](std::int64_t pos, int count, float* buf) { readSource(c, src, pos, count, buf); };
    if (c.audio.chainIsIdentity()) {
        readPadded(from, n, dst);
        return;
    }
    auto& fx = m_fx[c.id];
    if (!fx || !(fx->effects == c.audio)) {
        fx = std::make_unique<ClipFx>();
        fx->effects = c.audio;
        fx->chain = std::make_unique<dsp::ClipChain>(c.audio, m_rate, m_channels);
    }
    if (fx->outPos != from) {
        // Start afresh a little earlier so the filters, the compressor and
        // the noise estimate have settled when the wanted audio comes out.
        fx->chain->reset();
        fx->pending.clear();
        const std::int64_t warmup = c.audio.denoise > 0 ? m_rate : m_rate / 5;
        fx->inPos = from - warmup;
        fx->outPos = fx->inPos - fx->chain->latency();
    }
    const auto pendingFrames = [&] { return static_cast<std::int64_t>(fx->pending.size()) / m_channels; };
    std::vector<float> chunk;
    while (fx->outPos + pendingFrames() < from + n) {
        const int k = static_cast<int>(std::min<std::int64_t>(4096, from + n - fx->outPos - pendingFrames() + 256));
        chunk.resize(static_cast<size_t>(k) * m_channels);
        readPadded(fx->inPos, k, chunk.data());
        fx->chain->process(chunk.data(), k);
        fx->pending.insert(fx->pending.end(), chunk.begin(), chunk.end());
        fx->inPos += k;
    }
    // Drop what lies before the request, hand out n samples.
    const std::int64_t skip = from - fx->outPos;
    if (skip > 0) {
        fx->pending.erase(fx->pending.begin(), fx->pending.begin() + static_cast<long>(skip * m_channels));
        fx->outPos = from;
    }
    std::copy(fx->pending.begin(), fx->pending.begin() + static_cast<long>(n) * m_channels, dst);
    fx->pending.erase(fx->pending.begin(), fx->pending.begin() + static_cast<long>(n) * m_channels);
    fx->outPos = from + n;
}

void AudioMixer::readSource(const Clip& c, Source* src, std::int64_t pos, int count, float* buf) {
    if (c.hasSpeedChange()) {
        const TimeRemap& r = c.remap;
        if (r.keepPitch && r.ramp.empty() && !r.reverse && r.speed >= 0.25 && r.speed <= 4.0)
            readStretched(c, src, pos, count, buf);
        else
            readVarispeed(c, src, pos, count, buf);
        return;
    }
    if (pos < 0) {
        const int pad = static_cast<int>(std::min<std::int64_t>(count, -pos));
        std::fill(buf, buf + static_cast<size_t>(pad) * m_channels, 0.0f);
        if (count > pad)
            src->read(0, count - pad, buf + static_cast<size_t>(pad) * m_channels);
    } else {
        src->read(pos, count, buf);
    }
}

// Speed changes that change the pitch too (backwards, ramps, or when asked):
// the media is resampled along the remapped time.
void AudioMixer::readVarispeed(const Clip& c, Source* src, std::int64_t pos, int count, float* buf) {
    const TimeRemap& r = c.remap;
    auto mediaPos = [&](std::int64_t v) {
        // Media sample position (fractional) of source sample v.
        const Ticks t = av_rescale(v, kTicksPerSecond, m_rate);
        return static_cast<double>(r.map(t)) * m_rate / kTicksPerSecond;
    };
    constexpr int step = 64;
    std::vector<double> positions(static_cast<size_t>(count) + 1);
    for (int i = 0; i <= count; i += step) {
        const int j = std::min(count, i + step);
        const double a = mediaPos(pos + i), b = mediaPos(pos + j);
        for (int k = i; k <= j; ++k)
            positions[static_cast<size_t>(k)] = a + (b - a) * (k - i) / std::max(1, j - i);
    }
    double lo = positions[0], hi = positions[0];
    for (double p : positions) {
        lo = std::min(lo, p);
        hi = std::max(hi, p);
    }
    const std::int64_t first = static_cast<std::int64_t>(std::floor(lo)) - 1;
    const int span = static_cast<int>(std::ceil(hi) - first) + 3;
    std::vector<float> media(static_cast<size_t>(span) * m_channels, 0.0f);
    const std::int64_t readFrom = std::max<std::int64_t>(0, first);
    if (first + span > 0)
        src->read(readFrom, static_cast<int>(first + span - readFrom), media.data() + (readFrom - first) * m_channels);
    for (int i = 0; i < count; ++i) {
        const double p = positions[static_cast<size_t>(i)] - first;
        const int k = std::clamp(static_cast<int>(std::floor(p)), 0, span - 2);
        const float f = static_cast<float>(std::clamp(p - k, 0.0, 1.0));
        for (int ch = 0; ch < m_channels; ++ch) {
            const float a = media[static_cast<size_t>(k) * m_channels + ch];
            const float b = media[static_cast<size_t>(k + 1) * m_channels + ch];
            buf[static_cast<size_t>(i) * m_channels + ch] = a + (b - a) * f;
        }
    }
}

// Constant speed that keeps the pitch: overlapping 40 ms grains of the media
// are laid out at the new speed (WSOLA: each grain is shifted by up to 10 ms
// to where it continues the previous one best, so voices do not warble).
// Every grain depends only on its own position, so any range can be read.
void AudioMixer::readStretched(const Clip& c, Source* src, std::int64_t pos, int count, float* buf) {
    const TimeRemap& r = c.remap;
    const int hop = std::max(64, m_rate / 50);      // output distance between grains
    const int search = std::max(16, m_rate / 100);  // alignment range
    const int win = 2 * hop;
    std::fill(buf, buf + static_cast<size_t>(count) * m_channels, 0.0f);
    auto centre = [&](std::int64_t k) {
        const Ticks t = av_rescale(k * hop, kTicksPerSecond, m_rate);
        return static_cast<std::int64_t>(std::llround(static_cast<double>(r.map(t)) * m_rate / kTicksPerSecond));
    };
    // Media samples [from, from + n) into a buffer (zeros outside the media).
    std::vector<float> grain, target;
    auto readMedia = [&](std::int64_t from, int n, std::vector<float>& out) {
        out.assign(static_cast<size_t>(n) * m_channels, 0.0f);
        const std::int64_t a = std::max<std::int64_t>(0, from);
        if (from + n > a)
            src->read(a, static_cast<int>(from + n - a), out.data() + (a - from) * m_channels);
    };
    const std::int64_t kFirst = static_cast<std::int64_t>(std::floor(static_cast<double>(pos - hop) / hop)) + 1;
    const std::int64_t kLast = static_cast<std::int64_t>(std::floor(static_cast<double>(pos + count + hop) / hop));
    std::vector<float> window(static_cast<size_t>(win));
    for (int j = 0; j < win; ++j)
        window[static_cast<size_t>(j)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * M_PI * j / win));
    for (std::int64_t k = kFirst; k <= kLast; ++k) {
        const std::int64_t ck = centre(k);
        // Natural continuation of the previous grain over the overlap.
        const std::int64_t prev = centre(k - 1);
        readMedia(prev, hop, target);
        readMedia(ck - hop - search, win + 2 * search, grain);
        int best = search;
        double bestScore = -1e300;
        if (k > 0 && ck != prev + hop) {
            for (int d = 0; d <= 2 * search; d += 2) {
                double score = 0;
                for (int j = 0; j < hop; j += 4)
                    for (int ch = 0; ch < m_channels; ++ch)
                        score += static_cast<double>(target[static_cast<size_t>(j) * m_channels + ch]) *
                                 grain[static_cast<size_t>(d + j) * m_channels + ch];
                if (score > bestScore) {
                    bestScore = score;
                    best = d;
                }
            }
        }
        // Overlap-add the grain centred at output sample k * hop.
        const std::int64_t outStart = k * hop - hop;
        for (int j = 0; j < win; ++j) {
            const std::int64_t o = outStart + j - pos;
            if (o < 0 || o >= count)
                continue;
            const float w = window[static_cast<size_t>(j)];
            for (int ch = 0; ch < m_channels; ++ch)
                buf[static_cast<size_t>(o) * m_channels + ch] += w * grain[static_cast<size_t>(best + j) * m_channels + ch];
        }
    }
}

void AudioMixer::mixRaw(std::int64_t start, int frames, float* out) {
    std::fill(out, out + static_cast<size_t>(frames) * m_channels, 0.0f);
    if (frames <= 0)
        return;
    if (start != m_rawNext) {
        m_keyEnv = 0.0;
        m_duck = 0.0;
    }
    m_rawNext = start + frames;
    bool anyDucked = false;
    for (const Voice& v : m_voices)
        anyDucked |= v.clip->audio.duckDb < 0.0;
    const Ticks t0 = av_rescale(start, kTicksPerSecond, m_rate);
    const Ticks t1 = av_rescale(start + frames, kTicksPerSecond, m_rate);
    m_scratch.resize(static_cast<size_t>(frames) * m_channels);
    // Two passes: the clips that are not ducked, then (after following their
    // level) the ducked ones.
    for (int pass = 0; pass < (anyDucked ? 2 : 1); ++pass) {
    if (pass == 1) {
        // Key level from what has been mixed so far; ducking engages 10 ms
        // after the key gets above -45 dBFS and lets go over half a second.
        m_duckCurve.resize(frames);
        const double keyAttack = std::exp(-1.0 / (0.01 * m_rate)), keyRelease = std::exp(-1.0 / (0.3 * m_rate));
        const double duckAttack = 1.0 - std::exp(-1.0 / (0.08 * m_rate));
        const double duckRelease = 1.0 - std::exp(-1.0 / (0.6 * m_rate));
        constexpr double threshold = 0.0056; // -45 dBFS
        for (int i = 0; i < frames; ++i) {
            double peak = 0.0;
            for (int ch = 0; ch < m_channels; ++ch)
                peak = std::max(peak, static_cast<double>(std::abs(out[static_cast<size_t>(i) * m_channels + ch])));
            const double kc = peak > m_keyEnv ? keyAttack : keyRelease;
            m_keyEnv = kc * m_keyEnv + (1 - kc) * peak;
            const double target = m_keyEnv > threshold ? 1.0 : 0.0;
            m_duck += (target - m_duck) * (target > m_duck ? duckAttack : duckRelease);
            m_duckCurve[i] = static_cast<float>(m_duck);
        }
    }
    for (const Voice& v : m_voices) {
        if ((v.clip->audio.duckDb < 0.0) != (pass == 1))
            continue;
        if (v.to <= t0 || v.from >= t1)
            continue;
        const Clip& c = *v.clip;
        // Sample range of the block where this voice sounds.
        const std::int64_t first = std::max<std::int64_t>(start, av_rescale(v.from, m_rate, kTicksPerSecond));
        const std::int64_t last = std::min<std::int64_t>(start + frames, av_rescale(v.to, m_rate, kTicksPerSecond));
        if (last <= first)
            continue;
        const int n = static_cast<int>(last - first);
        // Timeline sample -> media sample, in whole samples so long clips
        // never drift.
        const std::int64_t clipStart = av_rescale(c.timelineStart, m_rate, kTicksPerSecond);
        const std::int64_t clipIn = av_rescale(c.sourceIn, m_rate, kTicksPerSecond);
        const std::int64_t mediaFirst = clipIn + (first - clipStart);
        Source* src = sourceFor(c);
        if (src->failed)
            continue;
        float* buf = m_scratch.data();
        readVoice(c, src, mediaFirst, n, buf);
        const float duckGain = static_cast<float>(dsp::dbToGain(c.audio.duckDb));
        // Gain changes slowly: evaluate it every 32 samples and interpolate.
        constexpr int step = 32;
        for (int i = 0; i < n; i += step) {
            const int m = std::min(step, n - i);
            const double g0 = gainAt(v, av_rescale(first + i, kTicksPerSecond, m_rate));
            const double g1 = gainAt(v, av_rescale(first + i + m, kTicksPerSecond, m_rate));
            for (int j = 0; j < m; ++j) {
                float g = static_cast<float>(g0 + (g1 - g0) * j / m);
                if (pass == 1)
                    g *= 1.0f + (duckGain - 1.0f) * m_duckCurve[static_cast<size_t>(first - start + i + j)];
                const size_t o = static_cast<size_t>(first - start + i + j) * m_channels;
                const size_t s = static_cast<size_t>(i + j) * m_channels;
                for (int ch = 0; ch < m_channels; ++ch)
                    out[o + ch] += buf[s + ch] * g;
            }
        }
    }
    }
}

} // namespace lumora

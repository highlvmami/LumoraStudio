#include "AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <deque>

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

AudioMixer::AudioMixer(const MediaPool* pool, int sampleRate, int channels)
    : m_pool(pool), m_rate(sampleRate), m_channels(channels) {}

AudioMixer::~AudioMixer() = default;

void AudioMixer::setTimeline(const TimelineState& state) {
    m_state = state;
    buildVoices();
    // Forget decoders of clips that no longer exist.
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        if (!m_state.findClip(it->first))
            it = m_sources.erase(it);
        else
            ++it;
    }
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
    std::fill(out, out + static_cast<size_t>(frames) * m_channels, 0.0f);
    if (frames <= 0)
        return;
    const Ticks t0 = av_rescale(start, kTicksPerSecond, m_rate);
    const Ticks t1 = av_rescale(start + frames, kTicksPerSecond, m_rate);
    m_scratch.resize(static_cast<size_t>(frames) * m_channels);
    for (const Voice& v : m_voices) {
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
        if (mediaFirst < 0) {
            // Before the start of the media: silence for that part.
            const int pad = static_cast<int>(std::min<std::int64_t>(n, -mediaFirst));
            std::fill(buf, buf + static_cast<size_t>(pad) * m_channels, 0.0f);
            if (n > pad)
                src->read(0, n - pad, buf + static_cast<size_t>(pad) * m_channels);
        } else {
            src->read(mediaFirst, n, buf);
        }
        // Gain changes slowly: evaluate it every 32 samples and interpolate.
        constexpr int step = 32;
        for (int i = 0; i < n; i += step) {
            const int m = std::min(step, n - i);
            const double g0 = gainAt(v, av_rescale(first + i, kTicksPerSecond, m_rate));
            const double g1 = gainAt(v, av_rescale(first + i + m, kTicksPerSecond, m_rate));
            for (int j = 0; j < m; ++j) {
                const float g = static_cast<float>(g0 + (g1 - g0) * j / m);
                const size_t o = static_cast<size_t>(first - start + i + j) * m_channels;
                const size_t s = static_cast<size_t>(i + j) * m_channels;
                for (int ch = 0; ch < m_channels; ++ch)
                    out[o + ch] += buf[s + ch] * g;
            }
        }
    }
}

} // namespace lumora

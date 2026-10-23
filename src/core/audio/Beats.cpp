#include "Beats.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#include "audio/AudioDsp.h"
#include "render/AudioMixer.h"

namespace lumora::beats {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::beats)
};

constexpr int kRate = 22050;
constexpr int kWindow = 1024;
constexpr int kHop = 256;
constexpr int kBands = 40;

// Rise of loudness per hop, averaged over log-spaced bands; normalised to
// unit standard deviation. `peak` is the 95th percentile of the rises before
// normalising, in dB.
std::vector<double> onsetEnvelope(const std::vector<float>& mono, int rate, double* peak) {
    *peak = 0.0;
    const int frames = mono.size() < static_cast<size_t>(kWindow) ? 0 : static_cast<int>((mono.size() - kWindow) / kHop) + 1;
    std::vector<double> onset(std::max(0, frames), 0.0);
    if (frames < 2)
        return onset;
    std::vector<float> window(kWindow);
    for (int i = 0; i < kWindow; ++i)
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2 * M_PI * i / kWindow));
    // Band edges in bins, 30 Hz .. 8 kHz.
    std::vector<int> edge(kBands + 1);
    for (int b = 0; b <= kBands; ++b) {
        const double f = 30.0 * std::pow(8000.0 / 30.0, static_cast<double>(b) / kBands);
        edge[b] = std::clamp(static_cast<int>(std::lround(f * kWindow / rate)), 1, kWindow / 2);
    }
    std::vector<std::vector<float>> logBands(frames, std::vector<float>(kBands));
    std::vector<std::complex<float>> spec(kWindow);
    float top = -1000.0f;
    for (int t = 0; t < frames; ++t) {
        const float* x = mono.data() + static_cast<size_t>(t) * kHop;
        for (int i = 0; i < kWindow; ++i)
            spec[i] = {x[i] * window[i], 0.0f};
        dsp::fft(spec, false);
        for (int b = 0; b < kBands; ++b) {
            double p = 0.0;
            for (int k = edge[b]; k < std::max(edge[b] + 1, edge[b + 1]); ++k)
                p += std::norm(spec[k]);
            logBands[t][b] = static_cast<float>(10.0 * std::log10(p + 1e-10));
            top = std::max(top, logBands[t][b]);
        }
    }
    for (auto& f : logBands)
        for (float& v : f)
            v = std::max(v, top - 80.0f);
    for (int t = 1; t < frames; ++t) {
        double sum = 0.0;
        for (int b = 0; b < kBands; ++b)
            sum += std::max(0.0f, logBands[t][b] - logBands[t - 1][b]);
        onset[t] = sum / kBands;
    }
    std::vector<double> sorted = onset;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() * 95 / 100, sorted.end());
    *peak = sorted[sorted.size() * 95 / 100];
    const double mean = std::accumulate(onset.begin(), onset.end(), 0.0) / frames;
    double var = 0.0;
    for (double v : onset)
        var += (v - mean) * (v - mean);
    const double sd = std::sqrt(var / frames);
    if (sd > 1e-9)
        for (double& v : onset)
            v /= sd;
    return onset;
}

// Beat period in hops (fractional), from the autocorrelation of the onset
// envelope weighted towards 120 BPM. `strength` is the autocorrelation at
// that period relative to the envelope's variance: near 0 for sound without
// a rhythm.
double beatPeriod(const std::vector<double>& onset, double fps, double* strength) {
    const int n = static_cast<int>(onset.size());
    const double mean = std::accumulate(onset.begin(), onset.end(), 0.0) / std::max(1, n);
    const int minLag = std::max(1, static_cast<int>(std::floor(60.0 * fps / 220.0)));
    const int maxLag = std::min(n - 1, static_cast<int>(std::ceil(60.0 * fps / 40.0)));
    *strength = 0.0;
    if (maxLag <= minLag + 1)
        return 60.0 * fps / 120.0;
    double var = 0.0;
    for (double v : onset)
        var += (v - mean) * (v - mean);
    var /= n;
    std::vector<double> score(maxLag + 2, 0.0), acs(maxLag + 2, 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        if (lag < 1)
            continue;
        double ac = 0.0;
        for (int t = 0; t + lag < n; ++t)
            ac += (onset[t] - mean) * (onset[t + lag] - mean);
        ac /= (n - lag);
        acs[lag] = ac;
        const double bpm = 60.0 * fps / lag;
        const double w = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0), 2));
        score[lag] = ac * w;
    }
    int best = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (score[lag] > score[best])
            best = lag;
    *strength = var > 1e-12 ? acs[best] / var : 0.0;
    // Parabolic interpolation between the neighbouring lags.
    const double a = score[best - 1], b = score[best], c = score[best + 1];
    const double d = a - 2 * b + c;
    const double shift = std::abs(d) > 1e-12 ? std::clamp(0.5 * (a - c) / d, -0.5, 0.5) : 0.0;
    return best + shift;
}

} // namespace

Beats detect(const std::vector<float>& mono, int rate) {
    Beats out;
    double peak = 0.0;
    const std::vector<double> onset = onsetEnvelope(mono, rate, &peak);
    const int n = static_cast<int>(onset.size());
    const double fps = static_cast<double>(rate) / kHop;
    // Under two seconds, or no clear attacks (a held tone, a hum): no rhythm
    // to find.
    if (n < static_cast<int>(fps * 2) || peak < 0.3)
        return out;
    double strength = 0.0;
    const double period = beatPeriod(onset, fps, &strength);
    if (strength < 0.15) // steady sound, speech or silence: no beat to follow
        return out;
    out.bpm = 60.0 * fps / period;

    // Local score: the onsets smoothed over a short window.
    const int half = std::max(1, static_cast<int>(std::lround(period / 2)));
    std::vector<double> kernel(2 * half + 1);
    for (int i = -half; i <= half; ++i)
        kernel[i + half] = std::exp(-0.5 * std::pow(i * 32.0 / period, 2));
    std::vector<double> local(n, 0.0);
    for (int t = 0; t < n; ++t)
        for (int i = -half; i <= half; ++i)
            if (t + i >= 0 && t + i < n)
                local[t] += onset[t + i] * kernel[i + half];

    // Best chain of beats about one period apart.
    constexpr double tightness = 100.0;
    std::vector<double> cum(n);
    std::vector<int> back(n, -1);
    const int lo = static_cast<int>(std::lround(period / 2)), hi = static_cast<int>(std::lround(2 * period));
    for (int t = 0; t < n; ++t) {
        double best = -1e300;
        int arg = -1;
        for (int p = std::max(0, t - hi); p <= t - lo; ++p) {
            const double s = cum[p] - tightness * std::pow(std::log((t - p) / period), 2);
            if (s > best) {
                best = s;
                arg = p;
            }
        }
        cum[t] = local[t] + (arg >= 0 ? best : 0.0);
        back[t] = arg;
    }
    // Last beat: the last local maximum of the cumulative score that is
    // above half the median of all of them.
    std::vector<double> peaks;
    for (int t = 1; t + 1 < n; ++t)
        if (cum[t] > cum[t - 1] && cum[t] >= cum[t + 1])
            peaks.push_back(cum[t]);
    if (peaks.empty())
        return out;
    std::nth_element(peaks.begin(), peaks.begin() + peaks.size() / 2, peaks.end());
    const double threshold = 0.5 * peaks[peaks.size() / 2];
    int last = -1;
    for (int t = n - 2; t >= 1 && last < 0; --t)
        if (cum[t] > cum[t - 1] && cum[t] >= cum[t + 1] && cum[t] >= threshold)
            last = t;
    std::vector<int> chain;
    for (int t = last; t >= 0; t = back[t])
        chain.push_back(t);
    std::reverse(chain.begin(), chain.end());
    // Weak beats at the start and the end (fade in, fade out, silence) go.
    double sq = 0.0;
    for (int t : chain)
        sq += local[t] * local[t];
    const double weak = chain.empty() ? 0.0 : 0.5 * std::sqrt(sq / chain.size());
    while (!chain.empty() && local[chain.front()] < weak)
        chain.erase(chain.begin());
    while (!chain.empty() && local[chain.back()] < weak)
        chain.pop_back();
    // A hop's flux belongs to the hop between two windows, whose centres
    // are kWindow / 2 into them.
    for (int t : chain)
        out.times.push_back((static_cast<double>(t) * kHop + kWindow / 2.0 - kHop / 2.0) / rate);
    return out;
}

QUuid findMusicClip(const TimelineState& timeline) {
    QUuid best;
    Ticks longest = 0;
    for (const int i : timeline.trackIndices(TrackType::Audio))
        for (const Clip& c : timeline.tracks[i].clips)
            if (c.linkId.isNull() && c.hasMedia() && c.duration() > longest) {
                longest = c.duration();
                best = c.id;
            }
    return best;
}

ClipBeats clipBeats(const TimelineState& timeline, const MediaPool& pool, const QUuid& clipId, const Progress& progress) {
    ClipBeats r;
    int track = -1;
    const Clip* clip = timeline.findClip(clipId, &track);
    if (!clip) {
        r.error = Tr::tr("There is no music clip.");
        return r;
    }
    // Only this clip sounds.
    TimelineState state = timeline;
    for (int i = 0; i < static_cast<int>(state.tracks.size()); ++i) {
        auto& clips = state.tracks[i].clips;
        clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const Clip& c) { return c.id != clipId; }), clips.end());
        state.tracks[i].muted = false;
    }
    AudioMixer mixer(&pool, kRate, 1);
    mixer.setTimeline(state);
    mixer.setMasterEnabled(false);
    if (mixer.silent()) {
        r.error = Tr::tr("The clip has no sound.");
        return r;
    }
    const std::int64_t start = av_rescale(clip->timelineStart, kRate, kTicksPerSecond);
    const std::int64_t total = av_rescale(clip->duration(), kRate, kTicksPerSecond);
    std::vector<float> pcm(static_cast<size_t>(total));
    constexpr int chunk = kRate * 5;
    for (std::int64_t done = 0; done < total; done += chunk) {
        const int n = static_cast<int>(std::min<std::int64_t>(chunk, total - done));
        mixer.mix(start + done, n, pcm.data() + done);
        if (progress && !progress(0.8 * done / total)) {
            r.cancelled = true;
            return r;
        }
    }
    const Beats b = detect(pcm, kRate);
    if (b.times.size() < 2) {
        r.error = Tr::tr("No steady beat was found in the music.");
        return r;
    }
    r.bpm = b.bpm;
    for (double s : b.times)
        r.times.push_back(clip->timelineStart + ticksFromSeconds(s));
    if (progress)
        progress(1.0);
    return r;
}

int cutToBeats(Timeline& timeline, const std::vector<Ticks>& beatTimes, int every,
               const std::function<Ticks(const QUuid&)>& mediaDuration) {
    if (beatTimes.size() < 2 || timeline.tracks().empty())
        return 0;
    // Cuts fall on frames: each beat goes to the nearest frame start.
    const Ticks fd = std::max<Ticks>(1, timeline.frameDuration());
    std::vector<Ticks> beats;
    for (Ticks b : beatTimes)
        beats.push_back(static_cast<Ticks>(std::llround(static_cast<double>(b) / fd)) * fd);
    beats.erase(std::unique(beats.begin(), beats.end()), beats.end());
    std::vector<Ticks> gaps;
    for (size_t i = 1; i < beats.size(); ++i)
        gaps.push_back(beats[i] - beats[i - 1]);
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    const Ticks period = gaps[gaps.size() / 2];
    const Ticks minLen = std::max(fd, period / 2);

    int changed = 0;
    for (size_t i = 0;; ++i) {
        const auto& clips = timeline.tracks()[timeline.primaryVideoTrack()].clips;
        if (i >= clips.size())
            break;
        const Clip c = clips[i];
        const Ticks start = c.timelineStart, end = c.timelineEnd();
        Ticks maxLen = INT64_MAX / 4;
        Ticks media = 0;
        if (c.kind == ClipKind::Media && !c.loops()) {
            media = c.hasSpeedChange() ? c.remap.length : mediaDuration(c.mediaId);
            if (media > 0)
                maxLen = media - c.sourceIn;
        }
        const auto first = std::lower_bound(beats.begin(), beats.end(), start + minLen);
        if (first == beats.end())
            break; // past the music
        Ticks target = -1;
        if (every <= 0) {
            // The beat nearest to the current cut, if it is close.
            for (auto it = first; it != beats.end() && *it - start <= maxLen; ++it)
                if (target < 0 || std::llabs(*it - end) < std::llabs(target - end))
                    target = *it;
            if (target >= 0 && std::llabs(target - end) > period)
                target = -1;
        } else {
            // The clip starts on a beat (or before the first one): it lasts
            // until the `every`-th beat after its start.
            auto on = std::lower_bound(beats.begin(), beats.end(), start - fd / 2);
            const bool onBeat = on != beats.end() && std::llabs(*on - start) <= fd / 2;
            const size_t index = static_cast<size_t>(on - beats.begin()) + (onBeat ? every : every - 1);
            if (index >= beats.size())
                break;
            target = beats[index];
            if (target - start > maxLen) {
                // Not enough media: the last beat it still reaches.
                target = -1;
                for (auto it = first; it != beats.end() && *it - start <= maxLen; ++it)
                    target = *it;
            }
        }
        if (target < 0 || target - start == c.duration())
            continue;
        if (timeline.trimOut(c.id, c.sourceIn + (target - start), media))
            ++changed;
    }
    return changed;
}

} // namespace lumora::beats

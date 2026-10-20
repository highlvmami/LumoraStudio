#include "Loudness.h"

#include <algorithm>
#include <cmath>

#include "render/AudioMixer.h"
#include "timeline/Timeline.h"

namespace lumora {

LoudnessMeter::LoudnessMeter(int rate, int channels)
    : m_rate(rate), m_channels(std::max(1, channels)), m_step(std::max(1, rate / 10)) {
    // K-weighting for any sample rate (the BS.1770 48 kHz coefficients
    // re-derived through the bilinear transform).
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(M_PI * f0 / rate);
        const double Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        m_shelf = {(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(M_PI * f0 / rate);
        const double a0 = 1.0 + K / Q + K * K;
        m_hp = {1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0};
    }
    m_state.assign(static_cast<size_t>(m_channels) * 4, 0.0);
    // Channel weights: 1 for front channels, 1.41 for surrounds, 0 for LFE
    // (5.1 order L R C LFE Ls Rs).
    m_weights.assign(m_channels, 1.0);
    if (m_channels == 6) {
        m_weights[3] = 0.0;
        m_weights[4] = m_weights[5] = 1.41;
    }
}

void LoudnessMeter::add(const float* data, int frames) {
    for (int i = 0; i < frames; ++i) {
        double sum = 0.0;
        for (int ch = 0; ch < m_channels; ++ch) {
            const float x = data[static_cast<size_t>(i) * m_channels + ch];
            m_peak = std::max(m_peak, std::abs(x));
            double* z = &m_state[static_cast<size_t>(ch) * 4];
            // Direct form II transposed, two stages.
            const double y1 = m_shelf.b0 * x + z[0];
            z[0] = m_shelf.b1 * x - m_shelf.a1 * y1 + z[1];
            z[1] = m_shelf.b2 * x - m_shelf.a2 * y1;
            const double y2 = m_hp.b0 * y1 + z[2];
            z[2] = m_hp.b1 * y1 - m_hp.a1 * y2 + z[3];
            z[3] = m_hp.b2 * y1 - m_hp.a2 * y2;
            sum += m_weights[ch] * y2 * y2;
        }
        m_stepEnergy += sum;
        if (++m_stepFill == m_step) {
            m_steps.push_back(m_stepEnergy);
            m_stepEnergy = 0.0;
            m_stepFill = 0;
            if (m_steps.size() >= 4) {
                const size_t n = m_steps.size();
                const double e = m_steps[n - 1] + m_steps[n - 2] + m_steps[n - 3] + m_steps[n - 4];
                m_blocks.push_back(e / (4.0 * m_step));
            }
        }
    }
}

namespace {
double toLufs(double meanSquare) { return meanSquare > 0 ? -0.691 + 10.0 * std::log10(meanSquare) : loudness::kSilent; }
} // namespace

double LoudnessMeter::integrated() const {
    const double absGate = std::pow(10.0, (-70.0 + 0.691) / 10.0);
    double sum = 0.0;
    int n = 0;
    for (double b : m_blocks)
        if (b > absGate) {
            sum += b;
            ++n;
        }
    if (n == 0)
        return loudness::kSilent;
    const double relGate = sum / n * std::pow(10.0, -10.0 / 10.0);
    double sum2 = 0.0;
    int n2 = 0;
    for (double b : m_blocks)
        if (b > absGate && b > relGate) {
            sum2 += b;
            ++n2;
        }
    return n2 ? toLufs(sum2 / n2) : loudness::kSilent;
}

double LoudnessMeter::shortTerm() const {
    if (m_steps.empty())
        return loudness::kSilent;
    const size_t n = std::min<size_t>(30, m_steps.size());
    double e = 0.0;
    for (size_t i = m_steps.size() - n; i < m_steps.size(); ++i)
        e += m_steps[i];
    return toLufs(e / (static_cast<double>(n) * m_step));
}

double LoudnessMeter::samplePeakDb() const { return m_peak > 0 ? 20.0 * std::log10(m_peak) : loudness::kSilent; }

namespace loudness {

LoudnessResult measureTimeline(const TimelineState& state, const MediaPool* pool, Ticks from, Ticks to,
                               const std::function<void(double)>& progress, const std::atomic_bool* cancel) {
    LoudnessResult r;
    constexpr int rate = 48000, channels = 2;
    if (to <= from)
        return r;
    AudioMixer mixer(pool, rate, channels);
    mixer.setMasterEnabled(false);
    mixer.setTimeline(state);
    LoudnessMeter meter(rate, channels);
    const std::int64_t first = av_rescale(from, rate, kTicksPerSecond);
    const std::int64_t last = av_rescale(to, rate, kTicksPerSecond);
    constexpr int block = 4800;
    std::vector<float> buf(static_cast<size_t>(block) * channels);
    for (std::int64_t s = first; s < last; s += block) {
        if (cancel && cancel->load())
            return r;
        const int n = static_cast<int>(std::min<std::int64_t>(block, last - s));
        mixer.mix(s, n, buf.data());
        meter.add(buf.data(), n);
        if (progress && ((s - first) / block) % 50 == 0)
            progress(double(s - first) / double(last - first));
    }
    r.ok = true;
    r.integrated = meter.integrated();
    r.peakDb = meter.samplePeakDb();
    return r;
}

double normalizationGain(double measured, double target) {
    if (measured <= kSilent + 1)
        return 0.0;
    return std::clamp(target - measured, -24.0, 24.0);
}

} // namespace loudness

} // namespace lumora

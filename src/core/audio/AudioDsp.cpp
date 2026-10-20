#include "AudioDsp.h"

#include <algorithm>
#include <cmath>

namespace lumora::dsp {

double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
double gainToDb(double g) { return g > 1e-12 ? 20.0 * std::log10(g) : -240.0; }

// ------------------------------------------------------------------ Biquad

void Biquad::design(Type type, double rate, double freq, double q, double gainDb) {
    freq = std::clamp(freq, 10.0, rate * 0.45);
    const double w0 = 2 * M_PI * freq / rate;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2 * q);
    const double A = std::pow(10.0, gainDb / 40.0);
    double b0, b1, b2, a0, a1, a2;
    switch (type) {
    case Type::HighPass:
        b0 = (1 + cw) / 2, b1 = -(1 + cw), b2 = (1 + cw) / 2;
        a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
        break;
    case Type::Peak:
        b0 = 1 + alpha * A, b1 = -2 * cw, b2 = 1 - alpha * A;
        a0 = 1 + alpha / A, a1 = -2 * cw, a2 = 1 - alpha / A;
        break;
    case Type::LowShelf: {
        const double s = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cw + s);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - s;
        break;
    }
    case Type::HighShelf:
    default: {
        const double s = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cw + s);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - s;
        break;
    }
    }
    m_b0 = b0 / a0, m_b1 = b1 / a0, m_b2 = b2 / a0, m_a1 = a1 / a0, m_a2 = a2 / a0;
}

void Biquad::process(float* data, int frames, int channels) {
    if (m_z.size() != static_cast<size_t>(channels) * 2)
        setChannels(channels);
    for (int ch = 0; ch < channels; ++ch) {
        double z1 = m_z[ch * 2], z2 = m_z[ch * 2 + 1];
        for (int i = 0; i < frames; ++i) {
            float& s = data[static_cast<size_t>(i) * channels + ch];
            const double x = s;
            const double y = m_b0 * x + z1; // transposed direct form II
            z1 = m_b1 * x - m_a1 * y + z2;
            z2 = m_b2 * x - m_a2 * y;
            s = static_cast<float>(y);
        }
        // Flush denormals.
        m_z[ch * 2] = std::abs(z1) < 1e-25 ? 0.0 : z1;
        m_z[ch * 2 + 1] = std::abs(z2) < 1e-25 ? 0.0 : z2;
    }
}

// ------------------------------------------------------------- FFT helper

namespace {

void fft(std::vector<std::complex<float>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * M_PI / static_cast<double>(len) * (inverse ? 1 : -1);
        const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a)
            x /= static_cast<float>(n);
}

} // namespace

// -------------------------------------------------------- SpectralDenoiser

SpectralDenoiser::SpectralDenoiser(int rate, int channels, double strength)
    : m_n(rate > 50000 ? 2048 : 1024), m_hop(m_n / 4), m_channels(std::max(1, channels)),
      m_strength(std::clamp(strength, 0.0, 1.0)) {
    // sqrt-Hann on both sides: the product is a Hann window, which sums to 2
    // at 75 % overlap; the 0.5 is folded into the synthesis side.
    m_window.resize(m_n);
    for (int i = 0; i < m_n; ++i)
        m_window[i] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2 * M_PI * i / m_n)));
    // The floor estimate may rise by about 3 dB per second.
    const double framesPerSecond = double(rate) / m_hop;
    m_rise = static_cast<float>(std::pow(10.0, 0.3 / framesPerSecond));
    reset();
}

void SpectralDenoiser::reset() {
    m_in.assign(m_channels, std::vector<float>(m_n, 0.0f));
    m_out.assign(m_channels, std::vector<float>(m_n, 0.0f));
    m_noise.assign(m_n / 2 + 1, 0.0f);
    m_prevGain.assign(m_n / 2 + 1, 1.0f);
    m_spec.assign(m_n, {});
    m_chanSpec.assign(m_channels, std::vector<std::complex<float>>(m_n));
    m_fill = 0;
    m_frames = 0;
}

void SpectralDenoiser::process(float* data, int frames) {
    for (int i = 0; i < frames; ++i) {
        const int pos = m_n - m_hop + m_fill; // write position in the input window
        for (int ch = 0; ch < m_channels; ++ch) {
            float& s = data[static_cast<size_t>(i) * m_channels + ch];
            m_in[ch][pos] = s;
            s = m_out[ch][m_fill];
        }
        if (++m_fill == m_hop) {
            // Output for this hop has been handed out: shift it away.
            for (int ch = 0; ch < m_channels; ++ch) {
                auto& o = m_out[ch];
                std::move(o.begin() + m_hop, o.end(), o.begin());
                std::fill(o.end() - m_hop, o.end(), 0.0f);
            }
            runFrame();
            for (int ch = 0; ch < m_channels; ++ch) {
                auto& in = m_in[ch];
                std::move(in.begin() + m_hop, in.end(), in.begin());
            }
            m_fill = 0;
        }
    }
}

void SpectralDenoiser::runFrame() {
    const int bins = m_n / 2 + 1;
    std::vector<float> power(bins, 0.0f);
    for (int ch = 0; ch < m_channels; ++ch) {
        auto& spec = m_chanSpec[ch];
        for (int i = 0; i < m_n; ++i)
            spec[i] = {m_in[ch][i] * m_window[i], 0.0f};
        fft(spec, false);
        for (int k = 0; k < bins; ++k)
            power[k] += std::norm(spec[k]);
    }
    for (float& p : power)
        p /= static_cast<float>(m_channels);

    // Noise floor: quick to fall, slow to rise.
    for (int k = 0; k < bins; ++k) {
        float& nz = m_noise[k];
        if (m_frames == 0)
            nz = power[k];
        else if (power[k] < nz)
            nz = 0.7f * nz + 0.3f * power[k];
        else
            nz *= m_rise;
    }
    ++m_frames;

    // Spectral subtraction gain with a floor, smoothed over frequency and
    // (when closing) over time, which keeps "musical noise" away.
    const float over = static_cast<float>(1.5 + 2.5 * m_strength);
    const float floor = static_cast<float>(std::pow(10.0, -24.0 * m_strength / 20.0));
    std::vector<float> gain(bins);
    for (int k = 0; k < bins; ++k) {
        const float p = std::max(power[k], 1e-20f);
        gain[k] = std::max(floor, std::sqrt(std::max(0.0f, 1.0f - over * m_noise[k] / p)));
    }
    std::vector<float> smooth(bins);
    for (int k = 0; k < bins; ++k) {
        const float a = gain[std::max(0, k - 1)], b = gain[k], c = gain[std::min(bins - 1, k + 1)];
        float g = 0.25f * a + 0.5f * b + 0.25f * c;
        if (g < m_prevGain[k])
            g = std::max(g, 0.6f * m_prevGain[k] + 0.4f * g);
        smooth[k] = g;
        m_prevGain[k] = g;
    }

    for (int ch = 0; ch < m_channels; ++ch) {
        auto& spec = m_chanSpec[ch];
        for (int k = 0; k < bins; ++k) {
            spec[k] *= smooth[k];
            if (k > 0 && k < m_n / 2)
                spec[m_n - k] = std::conj(spec[k]);
        }
        fft(spec, true);
        auto& o = m_out[ch];
        for (int i = 0; i < m_n; ++i)
            o[i] += spec[i].real() * m_window[i] * 0.5f;
    }
}

// --------------------------------------------------------------- Compressor

Compressor::Compressor(int rate, double thresholdDb, double ratio, double makeupDb)
    : m_threshold(thresholdDb), m_ratio(std::max(1.0, ratio)), m_makeup(dbToGain(makeupDb)),
      m_attack(std::exp(-1.0 / (0.005 * rate))), m_release(std::exp(-1.0 / (0.15 * rate))) {}

void Compressor::process(float* data, int frames, int channels) {
    constexpr double knee = 6.0;
    for (int i = 0; i < frames; ++i) {
        float* s = data + static_cast<size_t>(i) * channels;
        double peak = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            peak = std::max(peak, static_cast<double>(std::abs(s[ch])));
        const double coef = peak > m_env ? m_attack : m_release;
        m_env = coef * m_env + (1 - coef) * peak;
        const double level = gainToDb(m_env);
        const double over = level - m_threshold;
        double reduction = 0.0; // dB, >= 0
        if (over >= knee / 2)
            reduction = over * (1 - 1 / m_ratio);
        else if (over > -knee / 2)
            reduction = (1 - 1 / m_ratio) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
        const float g = static_cast<float>(dbToGain(-reduction) * m_makeup);
        for (int ch = 0; ch < channels; ++ch)
            s[ch] *= g;
    }
}

// ------------------------------------------------------------------ Limiter

Limiter::Limiter(int rate, int channels, double ceilingDb)
    : m_look(std::max(1, rate / 200)), m_channels(std::max(1, channels)),
      m_ceiling(static_cast<float>(dbToGain(std::min(0.0, ceilingDb)))),
      m_release(1.0 - std::exp(-1.0 / (0.1 * rate))) {
    reset();
}

void Limiter::reset() {
    m_delay.assign(static_cast<size_t>(m_look) * m_channels, 0.0f);
    m_box.assign(m_look, 1.0f);
    m_boxSum = m_look;
    m_minQueue.clear();
    m_env = 1.0;
    m_pos = 0;
}

void Limiter::process(float* data, int frames) {
    for (int i = 0; i < frames; ++i) {
        float* s = data + static_cast<size_t>(i) * m_channels;
        float peak = 0.0f;
        for (int ch = 0; ch < m_channels; ++ch)
            peak = std::max(peak, std::abs(s[ch]));
        const float want = peak > m_ceiling ? m_ceiling / peak : 1.0f;
        // Minimum of the wanted gain over the next m_look samples (the
        // current input is m_look samples ahead of the output).
        while (!m_minQueue.empty() && m_minQueue.back().second >= want)
            m_minQueue.pop_back();
        m_minQueue.emplace_back(m_pos, want);
        while (m_minQueue.front().first < m_pos - m_look)
            m_minQueue.pop_front();
        const double target = m_minQueue.front().second;
        // Instant attack on the held minimum, smooth release.
        m_env = target < m_env ? target : m_env + (target - m_env) * m_release;
        // A moving average over the look-ahead turns the step into a ramp
        // that reaches the needed gain exactly when the peak comes out.
        const size_t slot = static_cast<size_t>(m_pos % m_look);
        m_boxSum += m_env - m_box[slot];
        m_box[slot] = static_cast<float>(m_env);
        const float g = static_cast<float>(std::min(1.0, m_boxSum / m_look));
        float* d = m_delay.data() + slot * m_channels;
        for (int ch = 0; ch < m_channels; ++ch) {
            const float out = d[ch] * g;
            d[ch] = s[ch];
            s[ch] = std::clamp(out, -m_ceiling, m_ceiling); // rounding guard
        }
        ++m_pos;
    }
}

// ---------------------------------------------------------------- ClipChain

ClipChain::ClipChain(const AudioEffects& fx, int rate, int channels) : m_channels(channels) {
    if (fx.lowCut > 0.0) {
        // Two cascaded Butterworth sections: 24 dB per octave.
        m_lowCut = true;
        m_hp1.design(Biquad::Type::HighPass, rate, fx.lowCut, 0.5412, 0.0);
        m_hp2.design(Biquad::Type::HighPass, rate, fx.lowCut, 1.3066, 0.0);
        m_hp1.setChannels(channels);
        m_hp2.setChannels(channels);
    }
    if (fx.denoise > 0.0)
        m_denoise = std::make_unique<SpectralDenoiser>(rate, channels, fx.denoise / 100.0);
    auto band = [&](Biquad::Type t, double f, double q, double g) {
        if (g == 0.0)
            return;
        Biquad b;
        b.design(t, rate, f, q, g);
        b.setChannels(channels);
        m_eq.push_back(b);
    };
    band(Biquad::Type::LowShelf, 100.0, 0.707, fx.eqLow);
    band(Biquad::Type::Peak, fx.eqMidFreq, 0.9, fx.eqMid);
    band(Biquad::Type::HighShelf, 8000.0, 0.707, fx.eqHigh);
    if (fx.compressor)
        m_comp = std::make_unique<Compressor>(rate, fx.compThreshold, fx.compRatio, fx.compMakeup);
}

ClipChain::~ClipChain() = default;

int ClipChain::latency() const { return m_denoise ? m_denoise->latency() : 0; }

void ClipChain::reset() {
    m_hp1.reset();
    m_hp2.reset();
    if (m_denoise)
        m_denoise->reset();
    for (auto& b : m_eq)
        b.reset();
    if (m_comp)
        m_comp->reset();
}

void ClipChain::process(float* data, int frames) {
    if (m_lowCut) {
        m_hp1.process(data, frames, m_channels);
        m_hp2.process(data, frames, m_channels);
    }
    if (m_denoise)
        m_denoise->process(data, frames);
    for (auto& b : m_eq)
        b.process(data, frames, m_channels);
    if (m_comp)
        m_comp->process(data, frames, m_channels);
}

} // namespace lumora::dsp

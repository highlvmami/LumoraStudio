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
    case Type::Notch:
        b0 = 1, b1 = -2 * cw, b2 = 1;
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

// ------------------------------------------------------- SpectralProcessor

SpectralProcessor::SpectralProcessor(int rate, int channels)
    : m_rate(rate), m_n(rate > 50000 ? 2048 : 1024), m_hop(m_n / 4), m_channels(std::max(1, channels)) {
    // sqrt-Hann on both sides: the product is a Hann window, which sums to 2
    // at 75 % overlap; the 0.5 is folded into the synthesis side.
    m_window.resize(m_n);
    for (int i = 0; i < m_n; ++i)
        m_window[i] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2 * M_PI * i / m_n)));
    m_in.assign(m_channels, std::vector<float>(m_n, 0.0f));
    m_out.assign(m_channels, std::vector<float>(m_n, 0.0f));
    m_chanSpec.assign(m_channels, std::vector<std::complex<float>>(m_n));
    m_power.assign(m_n / 2 + 1, 0.0f);
    m_gain.assign(m_n / 2 + 1, 1.0f);
}

void SpectralProcessor::reset() {
    for (auto& v : m_in)
        std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : m_out)
        std::fill(v.begin(), v.end(), 0.0f);
    m_fill = 0;
    resetState();
}

void SpectralProcessor::process(float* data, int frames) {
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

void SpectralProcessor::runFrame() {
    const int bins = m_n / 2 + 1;
    std::fill(m_power.begin(), m_power.end(), 0.0f);
    for (int ch = 0; ch < m_channels; ++ch) {
        auto& spec = m_chanSpec[ch];
        for (int i = 0; i < m_n; ++i)
            spec[i] = {m_in[ch][i] * m_window[i], 0.0f};
        fft(spec, false);
        for (int k = 0; k < bins; ++k)
            m_power[k] += std::norm(spec[k]);
    }
    for (float& p : m_power)
        p /= static_cast<float>(m_channels);
    shape(m_power, m_gain);
    for (int ch = 0; ch < m_channels; ++ch) {
        auto& spec = m_chanSpec[ch];
        for (int k = 0; k < bins; ++k) {
            spec[k] *= m_gain[k];
            if (k > 0 && k < m_n / 2)
                spec[m_n - k] = std::conj(spec[k]);
        }
        fft(spec, true);
        auto& o = m_out[ch];
        for (int i = 0; i < m_n; ++i)
            o[i] += spec[i].real() * m_window[i] * 0.5f;
    }
}

// -------------------------------------------------------- SpectralDenoiser

SpectralDenoiser::SpectralDenoiser(int rate, int channels, double strength)
    : SpectralProcessor(rate, channels), m_strength(std::clamp(strength, 0.0, 1.0)) {
    // Six sub-windows of a third of a second: the floor follows a change
    // of noise within about two seconds.
    m_subLength = std::max(1, static_cast<int>(rate / 3.0 / m_hop));
    resetState();
}

void SpectralDenoiser::resetState() {
    m_smooth.assign(m_n / 2 + 1, 0.0f);
    m_curMin.assign(m_n / 2 + 1, 0.0f);
    m_noise.assign(m_n / 2 + 1, 0.0f);
    m_windowMins.clear();
    m_subFill = 0;
    m_settle = 0;
    m_prevGain.assign(m_n / 2 + 1, 1.0f);
    m_frames = 0;
}

void SpectralDenoiser::shape(const std::vector<float>& power, std::vector<float>& smooth) {
    const int bins = m_n / 2 + 1;
    // Noise floor: minimum of the smoothed level over the recent
    // sub-windows, corrected for the bias of taking a minimum.
    constexpr float bias = 2.5f;
    // Digital silence (the start of a clip, a gap) says nothing about the
    // noise: such frames do not update the floor. Neither do frames whose
    // window still reaches back before the reset.
    float total = 0.0f;
    for (float p : power)
        total += p;
    ++m_frames;
    // The first frames after a reset are only partly filled.
    const bool informative = total >= 1e-14f * bins && m_frames > m_n / m_hop;
    // The smoothed level needs a few frames to settle before its minimum
    // means anything.
    const bool settled = informative && ++m_settle > 16;
    for (int k = 0; informative && k < bins; ++k) {
        m_smooth[k] = m_smooth[k] <= 0.0f ? power[k] : 0.9f * m_smooth[k] + 0.1f * power[k];
        if (settled)
            m_curMin[k] = m_subFill == 0 ? m_smooth[k] : std::min(m_curMin[k], m_smooth[k]);
    }
    if (settled && ++m_subFill == m_subLength) {
        m_windowMins.push_back(m_curMin);
        if (m_windowMins.size() > 6)
            m_windowMins.erase(m_windowMins.begin());
        m_subFill = 0;
    }
    // Until a minimum is known, nothing is treated as noise.
    const bool known = !m_windowMins.empty() || m_subFill > 0;
    for (int k = 0; informative && known && k < bins; ++k) {
        float mn = m_subFill > 0 ? m_curMin[k] : m_windowMins.back()[k];
        for (const auto& w : m_windowMins)
            mn = std::min(mn, w[k]);
        m_noise[k] = mn * bias;
    }

    // Spectral subtraction gain with a floor, smoothed over frequency and
    // (when closing) over time, which keeps "musical noise" away.
    const float over = static_cast<float>(1.0 + 2.0 * m_strength);
    const float floor = static_cast<float>(std::pow(10.0, -24.0 * m_strength / 20.0));
    std::vector<float> gain(bins);
    for (int k = 0; k < bins; ++k) {
        // Mostly the smoothed level (steady gains on noise) with some of the
        // frame's own level (quick on speech onsets).
        const float sm = informative ? m_smooth[k] : power[k];
        const float p = std::max(0.3f * power[k] + 0.7f * sm, 1e-20f);
        gain[k] = std::max(floor, std::sqrt(std::max(0.0f, 1.0f - over * m_noise[k] / p)));
    }
    for (int k = 0; k < bins; ++k) {
        const float a = gain[std::max(0, k - 1)], b = gain[k], c = gain[std::min(bins - 1, k + 1)];
        float g = 0.15f * a + 0.7f * b + 0.15f * c;
        if (g < m_prevGain[k])
            g = std::max(g, 0.6f * m_prevGain[k] + 0.4f * g);
        smooth[k] = g;
        m_prevGain[k] = g;
    }
}

// ------------------------------------------------------------ WindReducer

WindReducer::WindReducer(int rate, int channels, double strength)
    : SpectralProcessor(rate, channels), m_strength(std::clamp(strength, 0.0, 1.0)) {
    resetState();
}

void WindReducer::resetState() { m_prevGain.assign(m_n / 2 + 1, 1.0f); }

void WindReducer::shape(const std::vector<float>& power, std::vector<float>& gain) {
    const int bins = m_n / 2 + 1;
    // Energy of the wind band (60 .. 500 Hz) against the 500 Hz .. 4 kHz
    // band, where voices and instruments are strong and wind is weak.
    double low = 0.0, ref = 0.0;
    for (int k = 0; k < bins; ++k) {
        const double f = binHz(k);
        if (f >= 60.0 && f < 500.0)
            low += power[k];
        else if (f >= 500.0 && f < 4000.0)
            ref += power[k];
    }
    // Speech and music put up to about 4x (6 dB) more energy below 500 Hz
    // than above; more than that is wind, and the band is lowered to that
    // level (fully at strength 1, half as many dB at 0.5).
    const double allowed = 4.0 * ref + 1e-12;
    double bandDb = 0.0;
    if (low > allowed)
        bandDb = 10.0 * std::log10(allowed / low) * m_strength;
    const float floor = static_cast<float>(std::pow(10.0, -30.0 * m_strength / 20.0));
    // Gusts come fast: the gain drops at once and comes back over ~60 ms.
    const float release = static_cast<float>(std::exp(-static_cast<double>(m_hop) / (0.06 * m_rate)));
    for (int k = 0; k < bins; ++k) {
        const double f = binHz(k);
        float g = 1.0f;
        if (f < 60.0) {
            g = floor; // rumble only, nothing to keep
        } else if (f < 500.0 && bandDb < 0.0) {
            const double fade = f < 350.0 ? 1.0 : (500.0 - f) / 150.0; // towards 500 Hz
            g = std::max(floor, static_cast<float>(std::pow(10.0, bandDb * fade / 20.0)));
        }
        if (g > m_prevGain[k])
            g = m_prevGain[k] + (g - m_prevGain[k]) * (1.0f - release);
        m_prevGain[k] = g;
        gain[k] = g;
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
    if (fx.humHz > 0) {
        // Narrow notches (3 Hz wide) on the mains frequency and its
        // harmonics up to 1 kHz: the buzz goes, the voice around it stays.
        for (int k = 1; k * fx.humHz <= 1000 && k * fx.humHz < rate * 0.45; ++k) {
            Biquad b;
            const double f = static_cast<double>(k) * fx.humHz;
            b.design(Biquad::Type::Notch, rate, f, f / 3.0, 0.0);
            b.setChannels(channels);
            m_hum.push_back(b);
        }
    }
    if (fx.wind > 0.0)
        m_wind = std::make_unique<WindReducer>(rate, channels, fx.wind / 100.0);
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

int ClipChain::latency() const { return (m_wind ? m_wind->latency() : 0) + (m_denoise ? m_denoise->latency() : 0); }

void ClipChain::reset() {
    m_hp1.reset();
    m_hp2.reset();
    for (auto& b : m_hum)
        b.reset();
    if (m_wind)
        m_wind->reset();
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
    for (auto& b : m_hum)
        b.process(data, frames, m_channels);
    if (m_wind)
        m_wind->process(data, frames);
    if (m_denoise)
        m_denoise->process(data, frames);
    for (auto& b : m_eq)
        b.process(data, frames, m_channels);
    if (m_comp)
        m_comp->process(data, frames, m_channels);
}

} // namespace lumora::dsp

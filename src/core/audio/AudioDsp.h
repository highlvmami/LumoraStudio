#pragma once

#include <complex>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "audio/AudioEffects.h"

namespace lumora::dsp {

// Second order IIR filter (RBJ audio EQ cookbook), one state per channel.
class Biquad {
public:
    enum class Type { HighPass, LowShelf, HighShelf, Peak, Notch };
    void design(Type type, double rate, double freq, double q, double gainDb);
    void setChannels(int channels) { m_z.assign(static_cast<size_t>(channels) * 2, 0.0); }
    void reset() { std::fill(m_z.begin(), m_z.end(), 0.0); }
    // In place on interleaved samples.
    void process(float* data, int frames, int channels);

private:
    double m_b0 = 1, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
    std::vector<double> m_z;
};

// Short-time Fourier processing with 75 % overlap: every hop the last
// window of input is turned into a spectrum, shape() picks a gain for each
// frequency bin (one for all channels, which keeps the stereo picture) and
// the result is added back together. Output is delayed by latency().
class SpectralProcessor {
public:
    SpectralProcessor(int rate, int channels);
    virtual ~SpectralProcessor() = default;
    int latency() const { return m_n; }
    void reset();
    void process(float* data, int frames);

protected:
    // Gain per bin (n / 2 + 1 of them) for the power spectrum of this window,
    // averaged over the channels.
    virtual void shape(const std::vector<float>& power, std::vector<float>& gain) = 0;
    virtual void resetState() {}
    double binHz(int k) const { return static_cast<double>(k) * m_rate / m_n; }

    int m_rate, m_n, m_hop, m_channels;

private:
    void runFrame();

    std::vector<float> m_window;
    std::vector<std::vector<float>> m_in, m_out; // per channel, m_n each
    std::vector<std::vector<std::complex<float>>> m_chanSpec;
    std::vector<float> m_power, m_gain;
    int m_fill = 0; // new input samples since the last frame
};

// Short-time spectral noise reduction. The noise floor of every frequency
// bin is the minimum of its smoothed level over the last ~2 seconds
// ("minimum statistics": speech and music pause often enough that the
// minimum is the noise), and bins close to that floor are turned down,
// never by more than the strength allows.
class SpectralDenoiser : public SpectralProcessor {
public:
    SpectralDenoiser(int rate, int channels, double strength); // strength 0..1

protected:
    void shape(const std::vector<float>& power, std::vector<float>& gain) override;
    void resetState() override;

private:
    double m_strength;
    std::vector<float> m_smooth, m_curMin, m_noise, m_prevGain;
    std::vector<std::vector<float>> m_windowMins; // finished sub-windows
    int m_subLength = 1, m_subFill = 0, m_settle = 0;
    int m_frames = 0; // frames processed since reset
};

// Wind noise reduction. Wind on a microphone is a rumble far below the
// voice plus gusts that swamp the low frequencies. Below ~60 Hz everything
// is turned down; the 60 .. 500 Hz band is lowered only while it is much
// louder than speech or music would make it, judged from the 500 Hz .. 4 kHz
// band in the same moment, so a gust is pulled down and a clean voice is
// left alone.
class WindReducer : public SpectralProcessor {
public:
    WindReducer(int rate, int channels, double strength); // strength 0..1

protected:
    void shape(const std::vector<float>& power, std::vector<float>& gain) override;
    void resetState() override;

private:
    double m_strength;
    std::vector<float> m_prevGain;
};

// Feed-forward compressor with a soft knee; channels are linked.
class Compressor {
public:
    Compressor(int rate, double thresholdDb, double ratio, double makeupDb);
    void reset() { m_env = 0.0; }
    void process(float* data, int frames, int channels);

private:
    double m_threshold, m_ratio, m_makeup, m_attack, m_release, m_env = 0.0;
};

// Look-ahead peak limiter: the gain is lowered smoothly *before* a peak
// arrives, so no sample ever exceeds the ceiling. Output is delayed by
// latency().
class Limiter {
public:
    Limiter(int rate, int channels, double ceilingDb);
    int latency() const { return m_look; }
    void reset();
    void process(float* data, int frames);

private:
    int m_look, m_channels;
    float m_ceiling;
    double m_release;
    std::vector<float> m_delay; // interleaved ring, m_look frames
    std::deque<std::pair<std::int64_t, float>> m_minQueue; // sliding minimum of the wanted gain
    std::vector<float> m_box; // ring of smoothed gains for the moving average
    double m_boxSum = 0.0;
    double m_env = 1.0;
    std::int64_t m_pos = 0;
};

// The per-clip chain built from AudioEffects (without gain, fades and
// ducking, which the mixer applies). Output is delayed by latency().
class ClipChain {
public:
    ClipChain(const AudioEffects& fx, int rate, int channels);
    ~ClipChain();
    int latency() const;
    void reset();
    void process(float* data, int frames);

private:
    int m_channels;
    bool m_lowCut = false;
    Biquad m_hp1, m_hp2;
    std::vector<Biquad> m_hum;
    std::unique_ptr<WindReducer> m_wind;
    std::unique_ptr<SpectralDenoiser> m_denoise;
    std::vector<Biquad> m_eq;
    std::unique_ptr<Compressor> m_comp;
};

// In-place radix-2 FFT; the size must be a power of two. The inverse is
// scaled by 1/n.
void fft(std::vector<std::complex<float>>& a, bool inverse);

double dbToGain(double db);
double gainToDb(double g);

} // namespace lumora::dsp

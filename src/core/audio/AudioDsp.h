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
    enum class Type { HighPass, LowShelf, HighShelf, Peak };
    void design(Type type, double rate, double freq, double q, double gainDb);
    void setChannels(int channels) { m_z.assign(static_cast<size_t>(channels) * 2, 0.0); }
    void reset() { std::fill(m_z.begin(), m_z.end(), 0.0); }
    // In place on interleaved samples.
    void process(float* data, int frames, int channels);

private:
    double m_b0 = 1, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
    std::vector<double> m_z;
};

// Short-time spectral noise reduction. The noise floor of every frequency
// bin is followed continuously (it falls quickly and rises slowly, so
// speech and music pass over it), and bins close to the floor are turned
// down, never by more than the strength allows. All channels share one gain
// per bin, which keeps the stereo picture. Output is delayed by latency().
class SpectralDenoiser {
public:
    SpectralDenoiser(int rate, int channels, double strength); // strength 0..1
    int latency() const { return m_n; }
    void reset();
    void process(float* data, int frames);

private:
    void runFrame();

    int m_n, m_hop, m_channels;
    double m_strength;
    std::vector<float> m_window;
    std::vector<std::vector<float>> m_in, m_out; // per channel, m_n each
    std::vector<float> m_noise, m_prevGain;
    std::vector<std::complex<float>> m_spec;
    std::vector<std::vector<std::complex<float>>> m_chanSpec;
    int m_fill = 0;   // new input samples since the last frame
    int m_frames = 0; // frames processed since reset
    float m_rise;
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
    std::unique_ptr<SpectralDenoiser> m_denoise;
    std::vector<Biquad> m_eq;
    std::unique_ptr<Compressor> m_comp;
};

double dbToGain(double db);
double gainToDb(double g);

} // namespace lumora::dsp

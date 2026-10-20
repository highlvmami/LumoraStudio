#pragma once

#include <QString>
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "TimeBase.h"

namespace lumora {

class MediaPool;
struct TimelineState;

// Integrated loudness after ITU-R BS.1770-4 / EBU R128: K-weighting, 400 ms
// blocks with 75 % overlap, absolute gate at -70 LUFS and relative gate
// 10 LU below the ungated level. Also keeps the sample peak.
class LoudnessMeter {
public:
    LoudnessMeter(int rate, int channels);
    void add(const float* interleaved, int frames);
    // LUFS; -inf (as -1000) when there was nothing above the gate.
    double integrated() const;
    // Loudness of the most recent 3 s (EBU "short-term"), LUFS.
    double shortTerm() const;
    double samplePeakDb() const;

private:
    struct Filter {
        double b0, b1, b2, a1, a2;
    };
    int m_rate, m_channels;
    Filter m_shelf{}, m_hp{};
    std::vector<double> m_state; // 4 per channel
    std::vector<double> m_weights;
    int m_step;                 // samples per 100 ms
    double m_stepEnergy = 0.0;  // weighted energy of the running 100 ms
    int m_stepFill = 0;
    std::vector<double> m_steps; // energy of each finished 100 ms step
    std::vector<double> m_blocks; // mean square of each 400 ms block
    float m_peak = 0.0f;
};

struct LoudnessResult {
    bool ok = false;
    double integrated = -1000.0; // LUFS
    double peakDb = -1000.0;     // dBFS
};

namespace loudness {

constexpr double kSilent = -1000.0;

// Measures the mix of the timeline over [from, to) (clip effects, gains and
// ducking included, master settings excluded). `progress` gets 0..1;
// `cancel` stops early (the result is then not ok).
LoudnessResult measureTimeline(const TimelineState& state, const MediaPool* pool, Ticks from, Ticks to,
                               const std::function<void(double)>& progress = {},
                               const std::atomic_bool* cancel = nullptr);

// The gain in dB that brings `measured` to `target`, limited to +-24 dB.
double normalizationGain(double measuredLufs, double targetLufs);

} // namespace loudness

} // namespace lumora

#pragma once

#include <QJsonObject>

namespace lumora {

// Sound settings of one audio clip, applied in this order: low cut, noise
// reduction, three band equaliser, compressor; then the clip gain and fades.
// Everything is neutral at its default value.
struct AudioEffects {
    double lowCut = 0.0;       // Hz, high-pass filter; 0 = off
    double denoise = 0.0;      // 0 .. 100 % noise reduction
    double eqLow = 0.0;        // dB, shelf below ~100 Hz
    double eqMid = 0.0;        // dB, bell
    double eqMidFreq = 1000.0; // Hz
    double eqHigh = 0.0;       // dB, shelf above ~8 kHz
    bool compressor = false;
    double compThreshold = -20.0; // dBFS
    double compRatio = 3.0;       // 1 .. 20
    double compMakeup = 0.0;      // dB
    // Lowers this clip by this many dB (negative) while other clips sound,
    // e.g. music under a voice. 0 = off.
    double duckDb = 0.0;

    bool hasEq() const { return eqLow != 0.0 || eqMid != 0.0 || eqHigh != 0.0; }
    // True when the clip's own processing changes nothing (ducking aside).
    bool chainIsIdentity() const { return lowCut <= 0.0 && denoise <= 0.0 && !hasEq() && !compressor; }
    bool isIdentity() const { return chainIsIdentity() && duckDb == 0.0; }
    bool operator==(const AudioEffects&) const = default;

    // Only the fields that differ from neutral are written.
    QJsonObject toJson() const;
    static AudioEffects fromJson(const QJsonObject& o);
};

// Settings for the whole mix, applied after all tracks are summed.
struct AudioMaster {
    // Bring the whole programme to `targetLufs` integrated loudness (EBU
    // R128 / ITU-R BS.1770) when exporting. The limiter is then always on so
    // the extra gain cannot clip.
    bool normalize = false;
    double targetLufs = -14.0;
    bool limiter = false;
    double ceilingDb = -1.0; // dBFS

    bool isIdentity() const { return !normalize && !limiter; }
    bool limiterActive() const { return normalize || limiter; }
    bool operator==(const AudioMaster&) const = default;

    QJsonObject toJson() const;
    static AudioMaster fromJson(const QJsonObject& o);
};

} // namespace lumora

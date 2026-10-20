#pragma once

#include <cstdint>
#include <QString>

extern "C" {
#include <libavutil/rational.h>
#include <libavutil/mathematics.h>
}

namespace lumora {

// All timeline and clip times are stored as integer "ticks" in a single fixed
// time base. 1/705'600'000 s ("flicks") divides every common video frame
// duration (23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 120 fps) and every
// common audio sample duration (8..192 kHz) exactly, so edits never accumulate
// rounding errors no matter how often clips are split, trimmed or moved.
using Ticks = std::int64_t;

inline constexpr Ticks kTicksPerSecond = 705'600'000;
inline constexpr AVRational kTickTimeBase{1, 705'600'000};
inline constexpr Ticks kInvalidTicks = INT64_MIN;

inline Ticks ticksFromSeconds(double seconds) { return static_cast<Ticks>(seconds * kTicksPerSecond + (seconds >= 0 ? 0.5 : -0.5)); }
inline double ticksToSeconds(Ticks t) { return static_cast<double>(t) / kTicksPerSecond; }

// Conversion between a stream time base (e.g. 1/90000) and ticks.
inline Ticks ticksFromStream(std::int64_t ts, AVRational tb) {
    return av_rescale_q_rnd(ts, tb, kTickTimeBase, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}
inline std::int64_t ticksToStream(Ticks t, AVRational tb) {
    return av_rescale_q_rnd(t, kTickTimeBase, tb, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
}
inline std::int64_t ticksToStreamDown(Ticks t, AVRational tb) {
    return av_rescale_q_rnd(t, kTickTimeBase, tb, static_cast<AVRounding>(AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX));
}

// Duration of one frame at the given frame rate (rate = frames per second).
inline Ticks frameDuration(AVRational rate) {
    if (rate.num <= 0 || rate.den <= 0)
        return kTicksPerSecond / 25;
    return av_rescale(kTicksPerSecond, rate.den, rate.num);
}

// Snap a time to the start of the frame that contains it.
inline Ticks snapToFrame(Ticks t, AVRational rate) {
    const Ticks fd = frameDuration(rate);
    if (fd <= 0)
        return t;
    Ticks n = t / fd;
    if (t < 0 && t % fd != 0)
        --n;
    return n * fd;
}

// Formats as HH:MM:SS:FF (non drop-frame) for the given frame rate.
QString formatTimecode(Ticks t, AVRational rate);
// Formats as H:MM:SS.mmm.
QString formatSeconds(Ticks t);

} // namespace lumora

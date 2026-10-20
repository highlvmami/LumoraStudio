#include "Time.h"

#include <cmath>

namespace lumora {

QString formatTimecode(Ticks t, AVRational rate) {
    if (t < 0)
        t = 0;
    const double fps = (rate.num > 0 && rate.den > 0) ? av_q2d(rate) : 25.0;
    const int nominal = std::max(1, static_cast<int>(std::lround(fps)));
    const Ticks fd = frameDuration(rate);
    const std::int64_t frames = t / fd;
    const std::int64_t ff = frames % nominal;
    const std::int64_t totalSeconds = frames / nominal;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

QString formatSeconds(Ticks t) {
    if (t < 0)
        t = 0;
    const std::int64_t ms = av_rescale(t, 1000, kTicksPerSecond);
    const std::int64_t s = ms / 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(s / 3600)
        .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace lumora

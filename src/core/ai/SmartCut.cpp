#include "SmartCut.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace lumora::smartcut {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::smartcut)
};

constexpr int kFrame = speech::kRate / 100; // 10 ms
constexpr Ticks kFrameTicks = kTicksPerSecond / 100;

int frames(double seconds) { return static_cast<int>(std::lround(seconds * 100.0)); }

// Index of the quietest 10 ms frame in [a, b] (clamped to the levels).
int quietest(const std::vector<float>& levels, int a, int b) {
    a = std::clamp(a, 0, static_cast<int>(levels.size()) - 1);
    b = std::clamp(b, 0, static_cast<int>(levels.size()) - 1);
    int best = a;
    for (int i = a; i <= b; ++i)
        if (levels[i] < levels[best])
            best = i;
    return best;
}

} // namespace

Ticks Result::total() const {
    Ticks t = 0;
    for (const Cut& c : cuts)
        t += c.to - c.from;
    return t;
}

int Result::count(CutKind kind) const {
    return static_cast<int>(std::count_if(cuts.begin(), cuts.end(), [kind](const Cut& c) { return c.kind == kind; }));
}

std::vector<float> levels(const std::vector<float>& pcm) {
    std::vector<float> out(pcm.size() / kFrame);
    for (size_t f = 0; f < out.size(); ++f) {
        double sum = 0.0;
        for (size_t k = f * kFrame; k < (f + 1) * kFrame; ++k)
            sum += static_cast<double>(pcm[k]) * pcm[k];
        out[f] = static_cast<float>(20.0 * std::log10(std::sqrt(sum / kFrame) + 1e-9));
    }
    return out;
}

double autoThreshold(const std::vector<float>& levels) {
    std::vector<float> v;
    v.reserve(levels.size());
    for (float l : levels)
        if (l > -100.0f) // digital silence says nothing about the room
            v.push_back(l);
    if (v.size() < 10)
        return -45.0;
    std::sort(v.begin(), v.end());
    const double noise = v[v.size() / 10];
    const double speech = v[v.size() * 9 / 10];
    return std::clamp(noise + std::max(6.0, 0.3 * (speech - noise)), -60.0, -20.0);
}

std::vector<Cut> findSilences(const std::vector<float>& levels, const Options& options) {
    std::vector<Cut> out;
    const int n = static_cast<int>(levels.size());
    if (n == 0)
        return out;
    const double threshold = options.thresholdDb < 0.0 ? options.thresholdDb : autoThreshold(levels);
    std::vector<char> quiet(n);
    for (int i = 0; i < n; ++i)
        quiet[i] = levels[i] < threshold;
    // A click or a breath of up to 40 ms inside a pause does not end it.
    for (int i = 0; i < n;) {
        if (quiet[i]) {
            ++i;
            continue;
        }
        int j = i;
        while (j < n && !quiet[j])
            ++j;
        if (i > 0 && j < n && j - i <= 4)
            std::fill(quiet.begin() + i, quiet.begin() + j, 1);
        i = j;
    }
    const int minRun = std::max(1, frames(options.minSilence));
    const int keep = std::max(0, frames(options.keep));
    for (int i = 0; i < n;) {
        if (!quiet[i]) {
            ++i;
            continue;
        }
        int j = i;
        while (j < n && quiet[j])
            ++j;
        if (j - i >= minRun) {
            // At the very start and end nothing waits on the other side.
            const int a = i == 0 ? 0 : i + keep;
            const int b = j == n ? n : j - keep;
            if (b > a)
                out.push_back({a * kFrameTicks, b * kFrameTicks, CutKind::Silence, {}});
        }
        i = j;
    }
    return out;
}

bool isFiller(const QString& word) {
    static const QRegularExpression strip(QStringLiteral("[\\p{P}\\p{S}\\s]"));
    QString w = QLocale(QLocale::Turkish).toLower(word);
    w.remove(strip);
    if (w.isEmpty())
        return false;
    // Long vowels and hums, in Turkish and English spelling.
    static const QRegularExpression filler(QStringLiteral(
        "^(ı{2,}|ı+[hm]+|e{2,}|e+[hm]+|ee+h*m*|a{2,}h*|a+hh+|ö{2,}|ö+[hm]+|u+[hm]+|u{2,}|h+m+|m{2,}|"
        "hı+|hıh+ı*|er+m+|e+r{2,}|um+|uh+|hm+)$"));
    return filler.match(w).hasMatch();
}

std::vector<Cut> merge(std::vector<Cut> cuts, Ticks minLength) {
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.from < b.from; });
    std::vector<Cut> out;
    for (Cut& c : cuts) {
        if (!out.empty() && c.from <= out.back().to) {
            Cut& last = out.back();
            last.to = std::max(last.to, c.to);
            if (c.kind == CutKind::Filler) {
                last.kind = CutKind::Filler;
                last.text = last.text.isEmpty() ? c.text : last.text + QLatin1Char(' ') + c.text;
            }
            continue;
        }
        out.push_back(std::move(c));
    }
    out.erase(std::remove_if(out.begin(), out.end(), [minLength](const Cut& c) { return c.to - c.from < minLength; }),
              out.end());
    return out;
}

Result findCuts(const TimelineState& timeline, const MediaPool& pool, const Options& options, const Progress& progress) {
    Result r;
    // Only the storyline speaks: other audio tracks (music, effects) are
    // muted for listening.
    TimelineState state = timeline;
    for (int i : state.trackIndices(TrackType::Audio))
        if (i != 1)
            state.tracks[i].muted = true;
    Ticks end = 0;
    for (int i : {0, 1})
        if (i < static_cast<int>(state.tracks.size()))
            for (const Clip& c : state.tracks[i].clips)
                end = std::max(end, c.timelineEnd());
    if (end <= 0) {
        r.error = Tr::tr("The timeline is empty.");
        return r;
    }
    const double listenShare = options.fillers ? 0.15 : 1.0;
    const std::vector<float> pcm = speech::listen(state, pool, 0, end,
                                                  [&](double p) { return !progress || progress(listenShare * p); },
                                                  &r.cancelled);
    if (r.cancelled)
        return r;
    if (pcm.empty()) {
        r.error = Tr::tr("The clips on the main track have no sound.");
        return r;
    }
    const std::vector<float> lv = levels(pcm);
    std::vector<Cut> cuts;
    if (options.silences)
        cuts = findSilences(lv, options);
    if (options.fillers) {
        const auto words = speech::words(pcm, 0, options.speech, [&](double p) {
            return !progress || progress(listenShare + (1.0 - listenShare) * p);
        });
        if (!words.ok()) {
            r.cancelled = words.cancelled;
            r.error = words.error;
            return r;
        }
        const int n = static_cast<int>(lv.size());
        for (const auto& w : words.words) {
            if (!isFiller(w.text))
                continue;
            // Whisper's word times are rough: each edge moves to the quietest
            // moment close by, where the sound of the hesitation begins or ends.
            const int s = static_cast<int>(w.start / kFrameTicks);
            const int e = static_cast<int>(w.end / kFrameTicks);
            const int a = quietest(lv, s - 15, std::min(s + 10, e));
            const int b = quietest(lv, std::max(e - 10, a + 1), e + 15);
            if (b > a && a < n)
                cuts.push_back({a * kFrameTicks, std::min<Ticks>(b * kFrameTicks, end), CutKind::Filler, w.text.trimmed()});
        }
    }
    // Leftovers shorter than two frames are not worth a cut.
    r.cuts = merge(std::move(cuts), kTicksPerSecond / 15);
    if (progress)
        progress(1.0);
    return r;
}

} // namespace lumora::smartcut

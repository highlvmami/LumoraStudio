#pragma once

#include <QString>
#include <functional>
#include <vector>

#include "TimeBase.h"
#include "ai/SpeechToText.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora::smartcut {

// Reports progress (0..1); returning false cancels the work.
using Progress = std::function<bool(double)>;

// Finds the parts of a talk that can go: pauses and hesitations ("ııı",
// "eee", "um"). Only the primary storyline is listened to (its video's sound
// and the first audio track), so background music on other tracks does not
// hide the pauses.
struct Options {
    bool silences = true;
    double minSilence = 0.6; // seconds: shorter pauses are part of speaking
    double keep = 0.15;      // seconds of each pause kept before and after the speech
    // Quieter than this counts as silence (dBFS); 0 = worked out from the
    // recording (between its noise floor and its speech level).
    double thresholdDb = 0.0;
    bool fillers = false;    // needs speech recognition (Whisper)
    speech::Options speech;
};

enum class CutKind { Silence, Filler };

struct Cut {
    Ticks from = 0, to = 0; // timeline time
    CutKind kind = CutKind::Silence;
    QString text;           // the hesitation that was heard
};

struct Result {
    std::vector<Cut> cuts; // sorted, not overlapping
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
    Ticks total() const;
    int count(CutKind kind) const;
};

Result findCuts(const TimelineState& timeline, const MediaPool& pool, const Options& options, const Progress& progress = {});

// --- parts, public for the tests ---

// Level of every 10 ms of `pcm` (kRate mono) in dBFS.
std::vector<float> levels(const std::vector<float>& pcm);
// The silence threshold for a recording with these levels.
double autoThreshold(const std::vector<float>& levels);
// Pauses in the levels (times relative to the first level), with `keep` of
// each pause left at its edges. A pause at the very start or end is removed
// up to the speech.
std::vector<Cut> findSilences(const std::vector<float>& levels, const Options& options);
// True for a word that is only a hesitation: "ııı", "eee", "hmm", "um", "uh" ...
bool isFiller(const QString& word);
// Joins overlapping or touching cuts; drops cuts shorter than `minLength`.
std::vector<Cut> merge(std::vector<Cut> cuts, Ticks minLength);

} // namespace lumora::smartcut

#pragma once

#include <QCoreApplication>
#include <QString>
#include <QUrl>
#include <functional>
#include <vector>

#include "TimeBase.h"
#include "media/MediaPool.h"
#include "timeline/Subtitles.h"
#include "timeline/Timeline.h"

namespace lumora::speech {

// Reports progress (0..1); returning false cancels the work.
using Progress = std::function<bool(double)>;

// Local speech recognition with whisper.cpp. Nothing leaves the computer: the
// model file is downloaded once and recognition runs on the CPU.
bool available();

// The Whisper models offered, smallest first. All are multilingual
// (Turkish included); larger ones are slower and more accurate.
struct Model {
    QString id;       // e.g. "small"
    QString label;    // shown to the user
    int sizeMB = 0;   // download size
};
std::vector<Model> models();
QString defaultModel();
QUrl modelUrl(const QString& id);
// Where a downloaded model is kept (the folder is created when missing).
QString modelPath(const QString& id);

struct Options {
    QString modelPath;
    QString language = QStringLiteral("tr"); // ISO 639-1, or "auto"
    int maxLineLength = 42;                   // characters per cue
    int threads = 0;                          // 0 = all but one core
};

struct Result {
    std::vector<SubtitleCue> cues; // timeline times
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Transcribes what is heard on the timeline (the audio mix) between `from`
// and `to` into subtitle cues.
Result transcribe(const TimelineState& timeline, const MediaPool& pool, Ticks from, Ticks to, const Options& options,
                  const Progress& progress = {});

} // namespace lumora::speech

#pragma once

#include <QString>
#include <QStringList>
#include <QUuid>
#include <vector>

#include "TimeBase.h"

namespace lumora {

enum class ExportMode {
    // Lossless cut: packets are copied bit for bit, nothing is re-encoded.
    // Cut points snap back to the previous key frame.
    StreamCopy,
    // Re-encode the video with the source's codec, resolution, frame rate, bit
    // depth, colour description and bit rate. Audio is copied when possible.
    MatchSource,
    // Mathematically lossless intermediate: FFV1 video (+ copied audio or
    // FLAC) in Matroska. Large files, zero generation loss, frame exact.
    LosslessIntermediate,
};

enum class VideoStrategy { Copy, Encode };
enum class AudioStrategy { None, Copy, Encode };

struct ExportSettings {
    ExportMode mode = ExportMode::StreamCopy;
    QString outputPath;
    bool includeAudio = true;
    // Timeline range to export; kInvalidTicks = whole timeline.
    Ticks rangeStart = kInvalidTicks;
    Ticks rangeEnd = kInvalidTicks;
};

// One timeline clip as it will be written.
struct SegmentPlan {
    QUuid clipId;
    QUuid mediaId;
    QString path;
    Ticks requestedIn = 0;  // media time the user cut at
    Ticks requestedOut = 0;
    Ticks actualIn = 0;     // media time the export really starts at (key frame for stream copy)
    bool hasAudio = false;
    int keyframeShiftFrames = 0; // frames added at the start by snapping to a key frame
};

struct ExportPlan {
    ExportMode mode = ExportMode::StreamCopy;
    VideoStrategy video = VideoStrategy::Copy;
    AudioStrategy audio = AudioStrategy::None;
    QString videoEncoder;   // FFmpeg encoder name when video == Encode
    QString audioEncoder;
    QString containerName;  // FFmpeg muxer name
    std::vector<SegmentPlan> segments;
    QStringList blockers;   // reasons this plan cannot run
    QStringList notes;      // user-facing remarks (key frame snapping etc.)
    Ticks expectedDuration = 0;

    bool ok() const { return blockers.isEmpty() && !segments.empty(); }
};

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QStringList notes;
    Ticks writtenDuration = 0;
    qint64 fileSize = 0;
};

} // namespace lumora

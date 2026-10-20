#pragma once

#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <cstdint>
#include <vector>

#include "TimeBase.h"
#include "timeline/Timeline.h"

extern "C" {
#include <libavcodec/codec_id.h>
}

namespace lumora {

class MediaPool;

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
    // Everything chosen by the user (or a preset): codec, encoder (including
    // hardware encoders), rate control, resolution, frame rate, bit depth.
    Custom,
};

// How the encoder spends bits (Custom mode).
enum class RateControl {
    Quality,         // constant quality (CRF / CQ / ICQ): size follows content
    ConstantBitrate, // CBR
    VariableBitrate, // VBR / ABR with a peak limit
    TargetSize,      // bit rate derived from the wanted file size
};

// How a different output frame size is reached.
enum class ScaleMode {
    Fit,     // scale to fit and pad to the exact frame size (letter/pillar box)
    Inside,  // scale to fit inside the size, no bars; never upscales
};

struct VideoEncodeOptions {
    AVCodecID codec = AV_CODEC_ID_H264;
    QString encoder;             // FFmpeg encoder name; empty = best available
    RateControl rateControl = RateControl::Quality;
    int quality = -1;            // CRF / CQ value; -1 = encoder default (visually lossless)
    std::int64_t bitrate = 0;    // bits/s for CBR / VBR; 0 = taken from the source
    std::int64_t maxBitrate = 0; // VBR peak; 0 = 1.5 x bitrate
    bool twoPass = false;
    double targetSizeMB = 0;     // RateControl::TargetSize, in 10^6 bytes
    QSize resolution;            // invalid = same as source
    ScaleMode scaleMode = ScaleMode::Fit;
    AVRational frameRate{0, 1};  // 0 = same as source
    int bitDepth = 0;            // 0 = keep, 8, 10, 12
    int speed = 2;               // 0 fastest .. 4 slowest (mapped to the encoder's presets)
    QString profile;             // ProRes: proxy|lt|standard|hq|4444|4444xq, DNxHR: lb|sq|hq|hqx|444
};

struct AudioEncodeOptions {
    AVCodecID codec = AV_CODEC_ID_NONE; // NONE = copy when possible, otherwise AAC
    bool copyWhenPossible = true;
    std::int64_t bitrate = 0;    // 0 = source / codec default
    int sampleRate = 0;          // 0 = same as source
};

enum class VideoStrategy {
    Copy,   // packets copied (lossless cut)
    Encode, // each storyline clip decoded and re-encoded on its own
    Render, // the composited timeline (all tracks, titles, transitions) is encoded
};
enum class AudioStrategy { None, Copy, Encode };

struct ExportSettings {
    ExportMode mode = ExportMode::StreamCopy;
    QString outputPath;
    bool includeAudio = true;
    // Timeline range to export; kInvalidTicks = whole timeline.
    Ticks rangeStart = kInvalidTicks;
    Ticks rangeEnd = kInvalidTicks;
    // Custom mode only.
    VideoEncodeOptions video;
    AudioEncodeOptions audio;
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
    Ticks outputStart = 0;  // render path: where the segment starts in the output
};

struct ExportPlan {
    ExportMode mode = ExportMode::StreamCopy;
    VideoStrategy video = VideoStrategy::Copy;
    AudioStrategy audio = AudioStrategy::None;
    QString videoEncoder;   // FFmpeg encoder name when video == Encode
    QString audioEncoder;
    QString containerName;  // FFmpeg muxer name
    // Resolved output format when video == Encode.
    QSize outputSize;       // encoded frame size
    QSize pictureSize;      // size of the picture inside the frame (smaller when padded)
    AVRational outputRate{0, 1}; // constant output frame rate; {0,1} = follow the source timing
    std::int64_t videoBitrate = 0; // resolved bit rate for bit-rate based modes
    bool twoPass = false;
    bool hardwareEncoder = false;
    // Render path only.
    TimelineState timeline;
    const MediaPool* pool = nullptr;
    Ticks rangeStart = 0, rangeEnd = 0;  // timeline range written
    QPoint pictureOffset;                // where the picture sits inside the padded frame
    VideoEncodeOptions videoOptions;     // resolved custom options (Custom mode)
    int pixelFormat = -1;                // encoder input format; -1 = closest to the source
    int colorPrimaries = 2, colorTrc = 2, colorSpace = 2, colorRange = 0; // AVCOL_* tags of the output
    int audioSampleRate = 0;
    int audioChannels = 2;
    std::int64_t audioBitrate = 0;
    // Copy: one demuxer per segment. Render + audio copy: segments are the
    // audio pieces, placed at outputStart.
    std::vector<SegmentPlan> segments;
    QStringList blockers;   // reasons this plan cannot run
    QStringList notes;      // user-facing remarks (key frame snapping etc.)
    Ticks expectedDuration = 0;

    bool ok() const {
        return blockers.isEmpty() && (video == VideoStrategy::Render ? expectedDuration > 0 : !segments.empty());
    }
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

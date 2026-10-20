#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <vector>

#include "export/ExportSettings.h"

struct AVCodecContext;
struct AVDictionary;

namespace lumora {

// What the user can choose in the custom export settings, and how each
// FFmpeg encoder is driven so that "quality 18" or "8 Mbit/s, two pass" means
// the same thing whichever encoder (software or hardware) is used.
namespace encoders {

struct VideoCodecChoice {
    AVCodecID codec;
    QString label;       // "H.264", "HEVC (H.265)", ...
    bool intermediate;   // editing codec (ProRes, DNxHR, FFV1): no rate control
};

// Codecs offered for custom export, in display order.
std::vector<VideoCodecChoice> videoCodecs();

struct EncoderInfo {
    QString name;    // FFmpeg encoder name
    QString label;   // "x264 (software)", "NVIDIA NVENC", ...
    bool hardware = false;
};

// Encoders for `codec` that are present in the FFmpeg build and actually work
// on this computer (hardware encoders are tried once and the answer cached).
// Software encoders come first: they give the best quality per bit.
std::vector<EncoderInfo> available(AVCodecID codec);

bool isHardware(const QString& encoder);

// Opens the encoder once on a tiny frame to see whether it works here (a
// hardware encoder needs the right GPU and driver). Thread safe, cached.
bool works(const QString& encoder);

struct QualityScale {
    int min = 0, max = 51;
    int visuallyLossless = 18; // default
    int high = 23;             // good quality, smaller file
    bool lowerIsBetter = true;
    bool supported = true;     // false: the encoder has no constant quality mode
};
QualityScale qualityScale(const QString& encoder);

bool supportsTwoPass(const QString& encoder);
// True when two pass means encoding the video twice (software encoders);
// NVENC does its two passes internally in one run.
bool twoPassNeedsTwoRuns(const QString& encoder);
bool supportsConstantBitrate(const QString& encoder);
bool hasRateControl(AVCodecID codec); // false for ProRes, DNxHR, FFV1

// Profiles of intermediate codecs: {id, label}.
std::vector<std::pair<QString, QString>> profiles(AVCodecID codec);
QString defaultProfile(AVCodecID codec);

// Pixel format for `encoder` given the source format and the wanted bit
// depth (0 = keep the source's). Prefers 4:2:0 for delivery codecs and keeps
// chroma / alpha for intermediates. Returns AV_PIX_FMT_NONE (-1) when the
// encoder cannot produce that bit depth.
int pixelFormat(const QString& encoder, AVCodecID codec, int sourceFormat, int bitDepth, const QString& profile);

// Bit depths the encoder can write (8, 10, 12).
std::vector<int> bitDepths(const QString& encoder, AVCodecID codec, const QString& profile);

// Two pass state passed between the passes.
struct PassInfo {
    int pass = 0;          // 0 = single pass, 1 or 2
    QString statsPath;     // file based stats (x264, x265)
    QByteArray* stats = nullptr; // in-memory stats (libvpx, libaom, nvenc ignores)
};

// Sets the rate control, speed, profile and pixel format related options of
// `ctx` (already holding size, rate, time base and pix_fmt) before
// avcodec_open2. `bitrate` is the resolved bit rate for bit rate modes.
// Returns user-facing notes about anything that had to be adapted.
QStringList configure(AVCodecContext* ctx, AVDictionary** opts, const QString& encoder, const VideoEncodeOptions& o,
                      std::int64_t bitrate, const PassInfo& pass);

// Bit rate needed so that video + audio of `durationSec` fit in `sizeMB`
// (10^6 bytes), leaving room for the container. 0 when impossible.
std::int64_t bitrateForSize(double sizeMB, double durationSec, std::int64_t audioBitrate);

// Ready-made settings for common destinations.
struct Preset {
    QString id;
    QString label;
    QString extension; // container
    VideoEncodeOptions video;
    AudioEncodeOptions audio;
};
std::vector<Preset> presets();

} // namespace encoders
} // namespace lumora

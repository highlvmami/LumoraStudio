#pragma once

// Small FFmpeg helpers shared by the export sessions.

#include <vector>

#include "TimeBase.h"
#include "ffmpeg/FFmpeg.h"

namespace lumora::exportutil {

// Encoder capabilities, across FFmpeg 6.1 ... 8.x.
std::vector<AVPixelFormat> supportedPixelFormats(const AVCodecContext* ctx, const AVCodec* codec);
std::vector<AVSampleFormat> supportedSampleFormats(const AVCodecContext* ctx, const AVCodec* codec);
std::vector<int> supportedSampleRates(const AVCodecContext* ctx, const AVCodec* codec);

// The encoder format closest to `source` (bit depth, chroma, alpha).
AVPixelFormat choosePixelFormat(const AVCodecContext* ctx, const AVCodec* codec, AVPixelFormat source);
// Closest supported sample rate.
int chooseSampleRate(const AVCodecContext* ctx, const AVCodec* codec, int rate);

// Copies stream side data (HDR mastering, display matrix ...) not yet present.
void copySideData(const AVCodecParameters* from, AVCodecParameters* to);

// True when the codec tags of `in` are valid in the `out` muxer.
bool sameContainerFamily(const AVFormatContext* in, const AVOutputFormat* out);

// Presentation time of a packet relative to `origin`; kInvalidTicks if none.
Ticks packetTime(const AVPacket* pkt, AVRational tb, Ticks origin);

} // namespace lumora::exportutil

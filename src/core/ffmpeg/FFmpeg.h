#pragma once

// Thin RAII helpers around the FFmpeg C API. Everything in the core that talks
// to libav* goes through these so that ownership is explicit and nothing leaks
// on error paths.

#include <memory>
#include <string>

#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace lumora::av {

QString errorString(int errnum);

struct FormatInputDeleter {
    void operator()(AVFormatContext* ctx) const { avformat_close_input(&ctx); }
};
struct FormatOutputDeleter {
    void operator()(AVFormatContext* ctx) const;
};
struct CodecContextDeleter {
    void operator()(AVCodecContext* ctx) const { avcodec_free_context(&ctx); }
};
struct FrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct SwsDeleter {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};
struct SwrDeleter {
    void operator()(SwrContext* s) const { swr_free(&s); }
};

using FormatInputPtr = std::unique_ptr<AVFormatContext, FormatInputDeleter>;
using FormatOutputPtr = std::unique_ptr<AVFormatContext, FormatOutputDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;

inline FramePtr makeFrame() { return FramePtr(av_frame_alloc()); }
inline PacketPtr makePacket() { return PacketPtr(av_packet_alloc()); }

// Opens a file for reading and probes its streams. Returns nullptr and sets
// `error` on failure.
FormatInputPtr openInput(const QString& path, QString* error = nullptr);

// Opens a decoder for the given stream. `threads` = 0 lets FFmpeg choose.
CodecContextPtr openDecoder(const AVStream* stream, QString* error = nullptr, int threads = 0);

// Encoded filesystem path suitable for FFmpeg (UTF-8 on every platform).
std::string toFFmpegPath(const QString& path);

// The library version string, e.g. "6.1.1".
QString versionString();

} // namespace lumora::av

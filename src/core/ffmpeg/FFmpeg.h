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
// With `hardware` the graphics card decodes when it supports the codec
// (D3D11VA/DXVA2 on Windows, VideoToolbox on macOS, VA-API/CUDA on Linux);
// pictures it cannot take (e.g. some 4:2:2 profiles) still decode in
// software. Hardware frames come out of avcodec_receive_frame() with
// hw_frames_ctx set and have to be downloaded (av_hwframe_transfer_data).
CodecContextPtr openDecoder(const AVStream* stream, QString* error = nullptr, int threads = 0, bool hardware = false);

// Scaler whose work is split over `threads` slices (0: one per core); use it
// with scaleInto(). sws_setColorspaceDetails() works on it as usual.
SwsPtr makeScaler(int srcW, int srcH, AVPixelFormat srcFmt, int dstW, int dstH, AVPixelFormat dstFmt, int flags,
                  int threads = 0);
// Scales `src` into a packed picture the caller owns (e.g. a QImage's bits).
bool scaleInto(SwsContext* sws, const AVFrame* src, uint8_t* dst, int dstStride, int dstW, int dstH,
               AVPixelFormat dstFmt);

// Encoded filesystem path suitable for FFmpeg (UTF-8 on every platform).
std::string toFFmpegPath(const QString& path);

// The library version string, e.g. "6.1.1".
QString versionString();

} // namespace lumora::av

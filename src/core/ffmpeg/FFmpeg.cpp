#include "FFmpeg.h"

#include <QDir>
#include <map>
#include <mutex>
#include <vector>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

namespace lumora::av {

QString errorString(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

void FormatOutputDeleter::operator()(AVFormatContext* ctx) const {
    if (!ctx)
        return;
    if (ctx->pb && !(ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ctx->pb);
    avformat_free_context(ctx);
}

std::string toFFmpegPath(const QString& path) {
    // FFmpeg's file protocol expects UTF-8 on Windows as well.
    return QDir::toNativeSeparators(path).toStdString();
}

FormatInputPtr openInput(const QString& path, QString* error) {
    AVFormatContext* raw = nullptr;
    const std::string p = toFFmpegPath(path);
    int ret = avformat_open_input(&raw, p.c_str(), nullptr, nullptr);
    if (ret < 0) {
        if (error)
            *error = errorString(ret);
        return nullptr;
    }
    FormatInputPtr ctx(raw);
    ret = avformat_find_stream_info(ctx.get(), nullptr);
    if (ret < 0) {
        if (error)
            *error = errorString(ret);
        return nullptr;
    }
    return ctx;
}

namespace {

// Hardware decoders worth trying, best first.
std::vector<AVHWDeviceType> hardwareTypes() {
#if defined(_WIN32)
    return {AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
#elif defined(__APPLE__)
    return {AV_HWDEVICE_TYPE_VIDEOTOOLBOX};
#else
    return {AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_CUDA};
#endif
}

// One device of each type for the whole program (creating one takes a
// while). nullptr when the computer has none.
AVBufferRef* hardwareDevice(AVHWDeviceType type) {
    static std::mutex mutex;
    static std::map<int, AVBufferRef*> devices; // kept until the program ends
    std::lock_guard lock(mutex);
    auto it = devices.find(type);
    if (it == devices.end()) {
        AVBufferRef* device = nullptr;
        if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0)
            device = nullptr;
        it = devices.emplace(type, device).first;
    }
    return it->second;
}

const AVCodecHWConfig* hardwareConfig(const AVCodec* codec, AVHWDeviceType type) {
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config)
            return nullptr;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && config->device_type == type)
            return config;
    }
}

// Takes the card's format when it is offered; otherwise (the card cannot
// decode this stream, FFmpeg asks again without it) the first software one.
AVPixelFormat pickFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
    if (ctx->hw_device_ctx) {
        const auto type = reinterpret_cast<const AVHWDeviceContext*>(ctx->hw_device_ctx->data)->type;
        if (const AVCodecHWConfig* config = hardwareConfig(ctx->codec, type))
            for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f)
                if (*f == config->pix_fmt)
                    return *f;
    }
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*f);
        if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL))
            return *f;
    }
    return AV_PIX_FMT_NONE;
}

} // namespace

CodecContextPtr openDecoder(const AVStream* stream, QString* error, int threads, bool hardware) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        if (error)
            *error = QStringLiteral("No decoder for codec %1").arg(avcodec_get_name(stream->codecpar->codec_id));
        return nullptr;
    }
    CodecContextPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx)
        return nullptr;
    int ret = avcodec_parameters_to_context(ctx.get(), stream->codecpar);
    if (ret < 0) {
        if (error)
            *error = errorString(ret);
        return nullptr;
    }
    ctx->pkt_timebase = stream->time_base;
    ctx->thread_count = threads;
    if (hardware) {
        for (AVHWDeviceType type : hardwareTypes()) {
            if (!hardwareConfig(codec, type))
                continue;
            if (AVBufferRef* device = hardwareDevice(type)) {
                ctx->hw_device_ctx = av_buffer_ref(device);
                ctx->get_format = pickFormat;
                break;
            }
        }
    }
    ret = avcodec_open2(ctx.get(), codec, nullptr);
    if (ret < 0) {
        if (error)
            *error = errorString(ret);
        return nullptr;
    }
    return ctx;
}

SwsPtr makeScaler(int srcW, int srcH, AVPixelFormat srcFmt, int dstW, int dstH, AVPixelFormat dstFmt, int flags,
                  int threads) {
    SwsPtr sws(sws_alloc_context());
    if (!sws)
        return nullptr;
    av_opt_set_int(sws.get(), "srcw", srcW, 0);
    av_opt_set_int(sws.get(), "srch", srcH, 0);
    av_opt_set_int(sws.get(), "src_format", srcFmt, 0);
    av_opt_set_int(sws.get(), "dstw", dstW, 0);
    av_opt_set_int(sws.get(), "dsth", dstH, 0);
    av_opt_set_int(sws.get(), "dst_format", dstFmt, 0);
    av_opt_set_int(sws.get(), "sws_flags", flags, 0);
    av_opt_set_int(sws.get(), "threads", threads, 0);
    if (sws_init_context(sws.get(), nullptr, nullptr) < 0)
        return nullptr;
    return sws;
}

bool scaleInto(SwsContext* sws, const AVFrame* src, uint8_t* dst, int dstStride, int dstW, int dstH,
               AVPixelFormat dstFmt) {
    // sws_scale_frame() is the call that uses the slice threads; it wants a
    // reference counted picture, so the caller's memory is wrapped in a
    // buffer that does not free it.
    FramePtr out = makeFrame();
    if (!out)
        return false;
    out->format = dstFmt;
    out->width = dstW;
    out->height = dstH;
    out->buf[0] = av_buffer_create(dst, static_cast<size_t>(dstStride) * dstH, [](void*, uint8_t*) {}, nullptr, 0);
    if (!out->buf[0])
        return false;
    out->data[0] = dst;
    out->linesize[0] = dstStride;
    return sws_scale_frame(sws, out.get(), src) >= 0;
}

QString versionString() { return QString::fromUtf8(av_version_info()); }

} // namespace lumora::av

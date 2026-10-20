#include "FFmpeg.h"

#include <QDir>

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

CodecContextPtr openDecoder(const AVStream* stream, QString* error, int threads) {
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
    ret = avcodec_open2(ctx.get(), codec, nullptr);
    if (ret < 0) {
        if (error)
            *error = errorString(ret);
        return nullptr;
    }
    return ctx;
}

QString versionString() { return QString::fromUtf8(av_version_info()); }

} // namespace lumora::av

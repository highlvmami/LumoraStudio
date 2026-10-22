#include "VideoDecoder.h"

#include <QTransform>
#include <atomic>

extern "C" {
#include <libavutil/hwcontext.h>
}

namespace lumora {

namespace {
// Set once a graphics card failed to hand over a picture; later decoders
// then stay in software.
std::atomic_bool g_hardwareFailed{false};
} // namespace

QImage decodeVideoFrame(const QString& path, Ticks t, QString* error) {
    VideoDecoder dec;
    if (!dec.open(path, error))
        return {};
    const AVFrame* f = dec.frameAt(std::max<Ticks>(0, t));
    if (!f || f->width <= 0 || f->height <= 0) {
        if (error)
            *error = QStringLiteral("No picture at this time");
        return {};
    }
    int w = f->width;
    if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0)
        w = std::max(1, static_cast<int>(av_rescale(w, f->sample_aspect_ratio.num, f->sample_aspect_ratio.den)));
    av::SwsPtr sws(sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), w, f->height, AV_PIX_FMT_RGBA64,
                                  SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT, nullptr, nullptr, nullptr));
    if (!sws)
        return {};
    int cs = SWS_CS_DEFAULT;
    switch (f->colorspace) {
    case AVCOL_SPC_BT709: cs = SWS_CS_ITU709; break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: cs = SWS_CS_BT2020; break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: cs = SWS_CS_ITU601; break;
    default: cs = f->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    }
    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(cs), f->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                             sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
    const bool alpha = d && (d->flags & AV_PIX_FMT_FLAG_ALPHA);
    QImage img(w, f->height, alpha ? QImage::Format_RGBA64 : QImage::Format_RGBX64);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int stride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
    sws_scale(sws.get(), f->data, f->linesize, 0, f->height, dst, stride);
    if (const int rot = dec.info().rotation)
        img = img.transformed(QTransform().rotate(rot));
    return img;
}

bool VideoDecoder::open(const QString& path, QString* error, int threads, bool hardware) {
    m_path = path;
    m_threads = threads;
    m_fmt = av::openInput(path, error);
    if (!m_fmt)
        return false;
    const int idx = av_find_best_stream(m_fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        if (error)
            *error = QStringLiteral("No video stream");
        return false;
    }
    m_stream = m_fmt->streams[idx];
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i)
        m_fmt->streams[i]->discard = (static_cast<int>(i) == idx) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    m_codec = av::openDecoder(m_stream, error, threads, hardware && !g_hardwareFailed);
    if (!m_codec)
        return false;
    m_pkt = av::makePacket();
    m_origin = m_fmt->start_time != AV_NOPTS_VALUE ? ticksFromStream(m_fmt->start_time, AVRational{1, AV_TIME_BASE}) : 0;
    // Described from the stream that is already open (probing would open
    // the file a second time, which is slow for big phone videos).
    m_info = videoStreamInfo(m_stream, m_fmt->duration != AV_NOPTS_VALUE
                                           ? ticksFromStream(m_fmt->duration, AVRational{1, AV_TIME_BASE})
                                           : 0);
    return true;
}

namespace {
// Decoding forward up to this far is cheaper than seeking.
constexpr Ticks kForwardLimit = 2 * kTicksPerSecond;
} // namespace

bool VideoDecoder::reachesForward(Ticks t) const {
    return m_current.valid() && t >= m_current.pts && t - m_current.pts < kForwardLimit;
}

const AVFrame* VideoDecoder::peekNext(Ticks* framePts) {
    if (!m_current.valid())
        return nullptr;
    if (!m_lookahead.valid()) {
        DecodedFrame f;
        if (!decodeOne(f))
            return nullptr;
        m_lookahead = std::move(f);
    }
    if (framePts)
        *framePts = m_lookahead.pts;
    return m_lookahead.frame.get();
}

Ticks VideoDecoder::nominalFrameDuration() const { return frameDuration(m_info.frameRate); }

bool VideoDecoder::decodeOne(DecodedFrame& out) {
    auto frame = av::makeFrame();
    while (true) {
        int ret = avcodec_receive_frame(m_codec.get(), frame.get());
        if (ret == 0) {
            if (frame->hw_frames_ctx) {
                auto picture = av::makeFrame();
                if (av_hwframe_transfer_data(picture.get(), frame.get(), 0) < 0 ||
                    av_frame_copy_props(picture.get(), frame.get()) < 0) {
                    const std::int64_t at = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp
                                                                                          : frame->pts;
                    if (!fallBackToSoftware(at != AV_NOPTS_VALUE ? ticksFromStream(at, m_stream->time_base) - m_origin : 0))
                        return false;
                    continue;
                }
                frame = std::move(picture);
            }
            std::int64_t ts = frame->best_effort_timestamp;
            if (ts == AV_NOPTS_VALUE)
                ts = frame->pts;
            out.pts = ts != AV_NOPTS_VALUE ? ticksFromStream(ts, m_stream->time_base) - m_origin : 0;
            out.duration = frame->duration > 0 ? ticksFromStream(frame->duration, m_stream->time_base)
                                               : nominalFrameDuration();
            out.frame = std::move(frame);
            return true;
        }
        if (ret == AVERROR_EOF) {
            m_eof = true;
            return false;
        }
        if (ret != AVERROR(EAGAIN))
            return false;
        if (m_eofSent) {
            m_eof = true;
            return false;
        }
        ret = av_read_frame(m_fmt.get(), m_pkt.get());
        if (ret < 0) {
            avcodec_send_packet(m_codec.get(), nullptr);
            m_eofSent = true;
            continue;
        }
        if (m_pkt->stream_index == m_stream->index)
            avcodec_send_packet(m_codec.get(), m_pkt.get());
        av_packet_unref(m_pkt.get());
    }
}

bool VideoDecoder::fallBackToSoftware(Ticks at) {
    g_hardwareFailed = true;
    auto codec = av::openDecoder(m_stream, nullptr, m_threads);
    if (!codec)
        return false;
    m_codec = std::move(codec);
    seek(at);
    return true;
}

bool VideoDecoder::seek(Ticks t) {
    if (!m_fmt)
        return false;
    const std::int64_t ts = ticksToStreamDown(t + m_origin, m_stream->time_base);
    int ret = avformat_seek_file(m_fmt.get(), m_stream->index, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        ret = av_seek_frame(m_fmt.get(), m_stream->index, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codec.get());
    m_eof = m_eofSent = false;
    m_current = {};
    m_lookahead = {};
    return ret >= 0;
}

bool VideoDecoder::next(DecodedFrame& out) {
    if (m_lookahead.valid()) {
        out = std::move(m_lookahead);
        m_lookahead = {};
        return true;
    }
    return decodeOne(out);
}

const AVFrame* VideoDecoder::frameAt(Ticks t, Ticks* framePts, Ticks* frameDur) {
    if (!m_codec)
        return nullptr;
    auto result = [&]() -> const AVFrame* {
        if (!m_current.valid())
            return nullptr;
        if (framePts)
            *framePts = m_current.pts;
        if (frameDur)
            *frameDur = m_current.duration;
        return m_current.frame.get();
    };

    // Already showing the right frame?
    if (m_current.valid() && t >= m_current.pts &&
        ((m_lookahead.valid() && t < m_lookahead.pts) || (!m_lookahead.valid() && m_eof)))
        return result();

    if (!reachesForward(t))
        seek(t);

    while (true) {
        if (!m_lookahead.valid()) {
            DecodedFrame f;
            if (!decodeOne(f))
                return result(); // EOF: keep the last frame on screen
            m_lookahead = std::move(f);
        }
        if (m_lookahead.pts > t && m_current.valid())
            return result();
        if (m_lookahead.pts > t && !m_current.valid()) {
            // Seeked past the target (first frame starts after t): show it.
            m_current = std::move(m_lookahead);
            m_lookahead = {};
            return result();
        }
        m_current = std::move(m_lookahead);
        m_lookahead = {};
    }
}

QImage FrameConverter::convert(const AVFrame* frame, QSize maxSize) {
    if (!frame || frame->width <= 0 || frame->height <= 0)
        return {};
    int dstW = frame->width, dstH = frame->height;
    // Respect non-square pixels so the preview shows the correct shape.
    if (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0 &&
        frame->sample_aspect_ratio.num != frame->sample_aspect_ratio.den)
        dstW = static_cast<int>(av_rescale(dstW, frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den));
    if (maxSize.isValid() && (dstW > maxSize.width() || dstH > maxSize.height())) {
        const QSize scaled = QSize(dstW, dstH).scaled(maxSize, Qt::KeepAspectRatio);
        dstW = std::max(2, scaled.width());
        dstH = std::max(2, scaled.height());
    }
    const int cs = frame->colorspace;
    const int range = frame->color_range;
    if (!m_sws || m_srcW != frame->width || m_srcH != frame->height || m_srcFmt != frame->format || m_dstW != dstW ||
        m_dstH != dstH || m_cs != cs || m_range != range) {
        m_sws.reset(sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), dstW, dstH,
                                   AV_PIX_FMT_RGB0, SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT, nullptr,
                                   nullptr, nullptr));
        if (!m_sws)
            return {};
        int swsCs = SWS_CS_DEFAULT;
        switch (frame->colorspace) {
        case AVCOL_SPC_BT709: swsCs = SWS_CS_ITU709; break;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: swsCs = SWS_CS_BT2020; break;
        case AVCOL_SPC_SMPTE240M: swsCs = SWS_CS_SMPTE240M; break;
        case AVCOL_SPC_FCC: swsCs = SWS_CS_FCC; break;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: swsCs = SWS_CS_ITU601; break;
        default:
            // Unspecified: HD material is almost always BT.709.
            swsCs = frame->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
        }
        const int srcFull = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        sws_setColorspaceDetails(m_sws.get(), sws_getCoefficients(swsCs), srcFull, sws_getCoefficients(SWS_CS_DEFAULT),
                                 1, 0, 1 << 16, 1 << 16);
        m_srcW = frame->width;
        m_srcH = frame->height;
        m_srcFmt = frame->format;
        m_dstW = dstW;
        m_dstH = dstH;
        m_cs = cs;
        m_range = range;
    }
    QImage img(dstW, dstH, QImage::Format_RGBX8888);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
    sws_scale(m_sws.get(), frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    return img;
}

} // namespace lumora

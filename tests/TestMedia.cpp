#include "TestMedia.h"

#include <QCryptographicHash>
#include <cmath>

#include "ffmpeg/FFmpeg.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace lumora::test {

namespace {
constexpr int kBits = 12;
constexpr int kBlock = 16;

void drawSolid(AVFrame* f, int luma) {
    for (int y = 0; y < f->height; ++y)
        std::fill(f->data[0] + y * f->linesize[0], f->data[0] + y * f->linesize[0] + f->width, static_cast<uint8_t>(luma));
    for (int y = 0; y < f->height / 2; ++y) {
        std::fill(f->data[1] + y * f->linesize[1], f->data[1] + y * f->linesize[1] + f->width / 2, uint8_t(128));
        std::fill(f->data[2] + y * f->linesize[2], f->data[2] + y * f->linesize[2] + f->width / 2, uint8_t(128));
    }
}

void drawIndex(AVFrame* f, int index) {
    // Mid-grey background with a gradient so the encoder has something to do.
    for (int y = 0; y < f->height; ++y)
        for (int x = 0; x < f->width; ++x)
            f->data[0][y * f->linesize[0] + x] = static_cast<uint8_t>(64 + ((x + y + index) % 64));
    for (int y = 0; y < f->height / 2; ++y)
        for (int x = 0; x < f->width / 2; ++x) {
            f->data[1][y * f->linesize[1] + x] = 128;
            f->data[2][y * f->linesize[2] + x] = 128;
        }
    for (int b = 0; b < kBits; ++b) {
        const uint8_t v = (index >> b) & 1 ? 235 : 16;
        const int x0 = 8 + b * (kBlock + 4);
        for (int y = 8; y < 8 + kBlock; ++y)
            for (int x = x0; x < x0 + kBlock; ++x)
                f->data[0][y * f->linesize[0] + x] = v;
    }
}

bool encodeAndWrite(AVCodecContext* c, AVFrame* f, AVFormatContext* oc, AVStream* st) {
    if (avcodec_send_frame(c, f) < 0)
        return false;
    auto pkt = av::makePacket();
    while (avcodec_receive_packet(c, pkt.get()) == 0) {
        if (pkt->duration <= 0 && c->codec_type == AVMEDIA_TYPE_VIDEO)
            pkt->duration = 1;
        av_packet_rescale_ts(pkt.get(), c->time_base, st->time_base);
        pkt->stream_index = st->index;
        if (av_interleaved_write_frame(oc, pkt.get()) < 0)
            return false;
    }
    return true;
}
} // namespace

bool writeTestVideo(const QString& path, const TestVideoSpec& spec, QString* error) {
    AVFormatContext* raw = nullptr;
    const std::string p = path.toStdString();
    if (avformat_alloc_output_context2(&raw, nullptr, nullptr, p.c_str()) < 0) {
        if (error)
            *error = "alloc output";
        return false;
    }
    av::FormatOutputPtr oc(raw);

    const AVCodec* vcodec = avcodec_find_encoder_by_name(spec.encoder);
    av::CodecContextPtr vc(avcodec_alloc_context3(vcodec));
    vc->width = spec.width;
    vc->height = spec.height;
    vc->pix_fmt = AV_PIX_FMT_YUV420P;
    vc->time_base = AVRational{1, spec.fps};
    vc->framerate = AVRational{spec.fps, 1};
    vc->gop_size = spec.gop;
    vc->keyint_min = spec.gop;
    vc->max_b_frames = spec.bFrames;
    vc->bit_rate = 2'000'000;
    vc->flags |= AV_CODEC_FLAG_CLOSED_GOP;
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        vc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* vopts = nullptr;
    av_dict_set(&vopts, "sc_threshold", "1000000000", 0);
    if (QString::fromLatin1(spec.encoder) == "libx264") {
        av_dict_set(&vopts, "preset", "ultrafast", 0);
        av_dict_set(&vopts, "x264-params", QStringLiteral("keyint=%1:min-keyint=%1:scenecut=0:bframes=%2").arg(spec.gop).arg(spec.bFrames).toUtf8().constData(), 0);
    }
    if (avcodec_open2(vc.get(), vcodec, &vopts) < 0) {
        av_dict_free(&vopts);
        if (error)
            *error = "open video encoder";
        return false;
    }
    av_dict_free(&vopts);
    AVStream* vs = avformat_new_stream(oc.get(), nullptr);
    avcodec_parameters_from_context(vs->codecpar, vc.get());
    vs->time_base = vc->time_base;

    av::CodecContextPtr ac;
    AVStream* as = nullptr;
    if (spec.audio) {
        const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        ac.reset(avcodec_alloc_context3(acodec));
        ac->sample_rate = 48000;
        av_channel_layout_default(&ac->ch_layout, 2);
        ac->sample_fmt = AV_SAMPLE_FMT_FLTP;
        ac->bit_rate = 128000;
        ac->time_base = AVRational{1, 48000};
        if (oc->oformat->flags & AVFMT_GLOBALHEADER)
            ac->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(ac.get(), acodec, nullptr) < 0) {
            if (error)
                *error = "open audio encoder";
            return false;
        }
        as = avformat_new_stream(oc.get(), nullptr);
        avcodec_parameters_from_context(as->codecpar, ac.get());
        as->time_base = ac->time_base;
    }

    if (avio_open(&oc->pb, p.c_str(), AVIO_FLAG_WRITE) < 0 || avformat_write_header(oc.get(), nullptr) < 0) {
        if (error)
            *error = "write header";
        return false;
    }

    const int frames = spec.seconds * spec.fps;
    auto vf = av::makeFrame();
    vf->format = vc->pix_fmt;
    vf->width = vc->width;
    vf->height = vc->height;
    av_frame_get_buffer(vf.get(), 0);

    std::int64_t audioSample = 0;
    const std::int64_t totalSamples = static_cast<std::int64_t>(spec.seconds) * 48000;
    for (int i = 0; i < frames; ++i) {
        av_frame_make_writable(vf.get());
        if (spec.solidLuma >= 0)
            drawSolid(vf.get(), spec.solidLuma);
        else
            drawIndex(vf.get(), i);
        vf->pts = i;
        if (!encodeAndWrite(vc.get(), vf.get(), oc.get(), vs))
            return false;
        // Keep audio roughly interleaved with video.
        const std::int64_t audioTarget = static_cast<std::int64_t>(i + 1) * 48000 / spec.fps;
        while (spec.audio && audioSample + ac->frame_size <= std::min(audioTarget + ac->frame_size, totalSamples)) {
            auto af = av::makeFrame();
            af->nb_samples = ac->frame_size;
            af->format = ac->sample_fmt;
            af->sample_rate = 48000;
            av_channel_layout_copy(&af->ch_layout, &ac->ch_layout);
            av_frame_get_buffer(af.get(), 0);
            for (int ch = 0; ch < 2; ++ch) {
                auto* d = reinterpret_cast<float*>(af->data[ch]);
                for (int s = 0; s < af->nb_samples; ++s)
                    d[s] = 0.2f * std::sin(2 * M_PI * 440.0 * (audioSample + s) / 48000.0);
            }
            af->pts = audioSample;
            audioSample += af->nb_samples;
            if (!encodeAndWrite(ac.get(), af.get(), oc.get(), as))
                return false;
        }
    }
    encodeAndWrite(vc.get(), nullptr, oc.get(), vs);
    if (spec.audio)
        encodeAndWrite(ac.get(), nullptr, oc.get(), as);
    av_write_trailer(oc.get());
    return true;
}

int frameIndexOf(const AVFrame* frame) {
    // Convert to gray first so any pixel format works.
    av::SwsPtr sws(sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                  frame->width, frame->height, AV_PIX_FMT_GRAY8, SWS_POINT, nullptr, nullptr,
                                  nullptr));
    std::vector<uint8_t> gray(static_cast<size_t>(frame->width) * frame->height);
    uint8_t* dst[4] = {gray.data(), nullptr, nullptr, nullptr};
    int stride[4] = {frame->width, 0, 0, 0};
    sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, dst, stride);
    int index = 0;
    for (int b = 0; b < kBits; ++b) {
        const int x0 = 8 + b * (kBlock + 4);
        int sum = 0;
        for (int y = 12; y < 8 + kBlock - 4; ++y)
            for (int x = x0 + 4; x < x0 + kBlock - 4; ++x)
                sum += gray[static_cast<size_t>(y) * frame->width + x];
        const int avg = sum / ((kBlock - 8) * (kBlock - 8));
        if (avg > 128)
            index |= 1 << b;
    }
    return index;
}

int frameIndexOf(const QImage& image) {
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    int index = 0;
    for (int b = 0; b < kBits; ++b) {
        const int x0 = 8 + b * (kBlock + 4);
        int sum = 0;
        for (int y = 12; y < 8 + kBlock - 4; ++y)
            for (int x = x0 + 4; x < x0 + kBlock - 4; ++x)
                sum += gray.constScanLine(y)[x];
        if (sum / ((kBlock - 8) * (kBlock - 8)) > 128)
            index |= 1 << b;
    }
    return index;
}

double meanLuma(const QImage& image, const QRect& rect) {
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const QRect r = rect.intersected(gray.rect());
    if (r.isEmpty())
        return 0;
    double sum = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            sum += gray.constScanLine(y)[x];
    return sum / (static_cast<double>(r.width()) * r.height());
}

DecodedSummary decodeAll(const QString& path) {
    DecodedSummary s;
    auto fmt = av::openInput(path);
    if (!fmt)
        return s;
    const int v = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int a = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    s.hasAudio = a >= 0;
    if (fmt->duration != AV_NOPTS_VALUE)
        s.durationSeconds = fmt->duration / static_cast<double>(AV_TIME_BASE);
    auto dec = av::openDecoder(fmt->streams[v], nullptr, 1);
    dec->err_recognition |= AV_EF_EXPLODE;
    auto pkt = av::makePacket();
    auto frame = av::makeFrame();
    auto drain = [&] {
        while (avcodec_receive_frame(dec.get(), frame.get()) == 0) {
            if (frame->decode_error_flags)
                ++s.decodeErrors;
            s.frameIndices.push_back(frameIndexOf(frame.get()));
            av_frame_unref(frame.get());
        }
    };
    while (av_read_frame(fmt.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == v) {
            if (avcodec_send_packet(dec.get(), pkt.get()) < 0)
                ++s.decodeErrors;
            drain();
        } else if (pkt->stream_index == a) {
            ++s.audioPackets;
        }
        av_packet_unref(pkt.get());
    }
    avcodec_send_packet(dec.get(), nullptr);
    drain();
    return s;
}

std::vector<QByteArray> decodedFrameHashes(const QString& path) {
    std::vector<QByteArray> out;
    auto fmt = av::openInput(path);
    if (!fmt)
        return out;
    const int v = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    auto dec = av::openDecoder(fmt->streams[v], nullptr, 1);
    auto pkt = av::makePacket();
    auto frame = av::makeFrame();
    auto drain = [&] {
        while (avcodec_receive_frame(dec.get(), frame.get()) == 0) {
            QCryptographicHash h(QCryptographicHash::Md5);
            const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
            for (int plane = 0; plane < 3; ++plane) {
                const int w = plane == 0 ? frame->width : AV_CEIL_RSHIFT(frame->width, d->log2_chroma_w);
                const int hgt = plane == 0 ? frame->height : AV_CEIL_RSHIFT(frame->height, d->log2_chroma_h);
                for (int y = 0; y < hgt; ++y)
                    h.addData(QByteArrayView(reinterpret_cast<const char*>(frame->data[plane] + y * frame->linesize[plane]), w));
            }
            out.push_back(h.result());
            av_frame_unref(frame.get());
        }
    };
    while (av_read_frame(fmt.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == v) {
            avcodec_send_packet(dec.get(), pkt.get());
            drain();
        }
        av_packet_unref(pkt.get());
    }
    avcodec_send_packet(dec.get(), nullptr);
    drain();
    return out;
}

double audioDuration(const QString& path) {
    auto fmt = av::openInput(path);
    if (!fmt)
        return 0;
    const int a = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (a < 0)
        return 0;
    std::int64_t total = 0;
    auto pkt = av::makePacket();
    while (av_read_frame(fmt.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == a)
            total += pkt->duration;
        av_packet_unref(pkt.get());
    }
    return total * av_q2d(fmt->streams[a]->time_base);
}

std::vector<QByteArray> videoPacketHashes(const QString& path) {
    std::vector<QByteArray> out;
    auto fmt = av::openInput(path);
    if (!fmt)
        return out;
    const int v = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    auto pkt = av::makePacket();
    while (av_read_frame(fmt.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == v)
            out.push_back(QCryptographicHash::hash(
                QByteArrayView(reinterpret_cast<const char*>(pkt->data), pkt->size), QCryptographicHash::Md5));
        av_packet_unref(pkt.get());
    }
    return out;
}

} // namespace lumora::test

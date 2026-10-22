#include "Proxy.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

#include "ffmpeg/FFmpeg.h"
#include "media/VideoDecoder.h"

extern "C" {
#include <libavutil/opt.h>
}

namespace lumora::proxy {

namespace {

constexpr int kLongSide = 1920;

// Copy size: the long side down to kLongSide, even dimensions.
void copySize(int w, int h, int* outW, int* outH) {
    const double k = std::min(1.0, static_cast<double>(kLongSide) / std::max(w, h));
    *outW = std::max(2, static_cast<int>(std::lround(w * k / 2)) * 2);
    *outH = std::max(2, static_cast<int>(std::lround(h * k / 2)) * 2);
}

void fail(QString* error, const QString& message) {
    if (error)
        *error = message;
}

} // namespace

bool wanted(const MediaInfo& info) {
    if (info.kind != MediaKind::Video || !info.video)
        return false;
    return std::max(info.video->width, info.video->height) > kLongSide;
}

QString pathFor(const QString& source) {
    const QFileInfo fi(source);
    const QByteArray key = (fi.absoluteFilePath() + QLatin1Char('|') + QString::number(fi.size()) + QLatin1Char('|') +
                            QString::number(fi.lastModified().toMSecsSinceEpoch()))
                               .toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(20));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/proxies");
    return dir + QLatin1Char('/') + fi.completeBaseName().left(40) + QLatin1Char('-') + hash + QStringLiteral(".mp4");
}

bool make(const QString& source, const QString& dest, const std::function<bool(double)>& progress, bool hardware,
          QString* error) {
    VideoDecoder dec;
    if (!dec.open(source, error, 0, hardware))
        return false;
    const VideoStreamInfo& vi = dec.info();
    if (vi.width <= 0 || vi.height <= 0) {
        fail(error, QStringLiteral("No picture size"));
        return false;
    }
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        fail(error, QStringLiteral("No H.264 encoder"));
        return false;
    }
    QDir().mkpath(QFileInfo(dest).absolutePath());
    const QString temp = dest + QStringLiteral(".part");
    QFile::remove(temp);

    AVFormatContext* rawOut = nullptr;
    const std::string tempPath = av::toFFmpegPath(temp);
    if (avformat_alloc_output_context2(&rawOut, nullptr, "mp4", tempPath.c_str()) < 0 || !rawOut) {
        fail(error, QStringLiteral("Cannot create the preview copy"));
        return false;
    }
    av::FormatOutputPtr out(rawOut);

    int w = 0, h = 0;
    copySize(vi.width, vi.height, &w, &h);
    const AVRational rate = vi.frameRate.num > 0 && vi.frameRate.den > 0 ? vi.frameRate : AVRational{30, 1};
    const AVRational tb = vi.timeBase.num > 0 && vi.timeBase.den > 0 ? vi.timeBase : av_inv_q(rate);

    av::CodecContextPtr enc(avcodec_alloc_context3(codec));
    enc->width = w;
    enc->height = h;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = tb;
    enc->framerate = rate;
    enc->sample_aspect_ratio = vi.sampleAspectRatio.num > 0 ? vi.sampleAspectRatio : AVRational{1, 1};
    enc->color_primaries = vi.colorPrimaries;
    enc->color_trc = vi.colorTrc;
    enc->colorspace = vi.colorSpace;
    enc->color_range = vi.colorRange == AVCOL_RANGE_JPEG ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    enc->chroma_sample_location = vi.chromaLocation;
    // Short groups of pictures so jumping around stays quick; no B-frames
    // so frames come out in order.
    enc->gop_size = std::max(1, static_cast<int>(std::lround(av_q2d(rate) / 2)));
    enc->max_b_frames = 0;
    enc->thread_count = 0;
    if (out->oformat->flags & AVFMT_GLOBALHEADER)
        enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "preset", "veryfast", 0);
    av_dict_set(&opts, "tune", "fastdecode", 0);
    av_dict_set(&opts, "crf", "18", 0);
    int ret = avcodec_open2(enc.get(), codec, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        fail(error, av::errorString(ret));
        return false;
    }

    AVStream* st = avformat_new_stream(out.get(), nullptr);
    st->time_base = tb;
    st->avg_frame_rate = rate;
    st->r_frame_rate = vi.realFrameRate.num > 0 ? vi.realFrameRate : rate;
    avcodec_parameters_from_context(st->codecpar, enc.get());
    // Same rotation as the original: the copy keeps the picture as stored.
    {
        av::FormatInputPtr probe = av::openInput(source);
        if (probe && vi.index >= 0 && vi.index < static_cast<int>(probe->nb_streams)) {
            const AVCodecParameters* par = probe->streams[vi.index]->codecpar;
            if (const AVPacketSideData* sd =
                    av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX)) {
                if (AVPacketSideData* copy = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                                                     &st->codecpar->nb_coded_side_data,
                                                                     AV_PKT_DATA_DISPLAYMATRIX, sd->size, 0))
                    std::copy(sd->data, sd->data + sd->size, copy->data);
            }
        }
    }
    av_dict_set(&out->metadata, kMetadataKey, "1", 0);
    AVDictionary* muxOpts = nullptr;
    av_dict_set(&muxOpts, "movflags", "+faststart+use_metadata_tags", 0);
    ret = avio_open(&out->pb, tempPath.c_str(), AVIO_FLAG_WRITE);
    if (ret >= 0)
        ret = avformat_write_header(out.get(), &muxOpts);
    av_dict_free(&muxOpts);
    if (ret < 0) {
        fail(error, av::errorString(ret));
        return false;
    }

    av::SwsPtr sws;
    int swsKey[3] = {0, 0, -1};
    auto pkt = av::makePacket();
    auto drain = [&]() -> bool {
        while (true) {
            const int r = avcodec_receive_packet(enc.get(), pkt.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                return true;
            if (r < 0)
                return false;
            av_packet_rescale_ts(pkt.get(), enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            if (av_interleaved_write_frame(out.get(), pkt.get()) < 0)
                return false;
        }
    };

    const double total = std::max(1.0, ticksToSeconds(vi.duration > 0 ? vi.duration : dec.info().duration));
    std::int64_t lastPts = AV_NOPTS_VALUE;
    bool ok = dec.seek(0);
    DecodedFrame f;
    while (ok && dec.next(f)) {
        const AVFrame* src = f.frame.get();
        const int key[3] = {src->width, src->height, src->format};
        if (!sws || !std::equal(std::begin(key), std::end(key), std::begin(swsKey))) {
            sws = av::makeScaler(src->width, src->height, static_cast<AVPixelFormat>(src->format), w, h,
                                 AV_PIX_FMT_YUV420P, SWS_AREA | SWS_ACCURATE_RND);
            if (!sws) {
                ok = false;
                break;
            }
            // Same matrix and range on both sides: only size and bit depth
            // change, the colours stay as they are.
            const int full = enc->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
            sws_setColorspaceDetails(sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), full,
                                     sws_getCoefficients(SWS_CS_DEFAULT), full, 0, 1 << 16, 1 << 16);
            std::copy(std::begin(key), std::end(key), std::begin(swsKey));
        }
        auto pic = av::makeFrame();
        pic->format = AV_PIX_FMT_YUV420P;
        pic->width = w;
        pic->height = h;
        if (av_frame_get_buffer(pic.get(), 0) < 0 || sws_scale_frame(sws.get(), pic.get(), src) < 0) {
            ok = false;
            break;
        }
        // Frame times as media time (the original's times counted from its
        // start), which VideoDecoder reads back from zero for proxies.
        std::int64_t pts = ticksToStream(f.pts, enc->time_base);
        if (lastPts != AV_NOPTS_VALUE && pts <= lastPts)
            pts = lastPts + 1;
        lastPts = pts;
        pic->pts = pts;
        pic->duration = ticksToStream(f.duration, enc->time_base);
        pic->color_primaries = enc->color_primaries;
        pic->color_trc = enc->color_trc;
        pic->colorspace = enc->colorspace;
        pic->color_range = enc->color_range;
        if (avcodec_send_frame(enc.get(), pic.get()) < 0 || !drain()) {
            ok = false;
            break;
        }
        if (progress && !progress(std::clamp(ticksToSeconds(f.pts) / total, 0.0, 1.0))) {
            ok = false;
            break;
        }
    }
    if (ok) {
        avcodec_send_frame(enc.get(), nullptr);
        ok = drain() && av_write_trailer(out.get()) >= 0;
    }
    out.reset(); // closes the file
    if (!ok || lastPts == AV_NOPTS_VALUE) {
        QFile::remove(temp);
        if (error && error->isEmpty())
            *error = QStringLiteral("Could not write the preview copy");
        return false;
    }
    QFile::remove(dest);
    if (!QFile::rename(temp, dest)) {
        QFile::remove(temp);
        fail(error, QStringLiteral("Could not save the preview copy"));
        return false;
    }
    if (progress)
        progress(1.0);
    return true;
}

} // namespace lumora::proxy

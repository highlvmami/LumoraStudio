#include "MediaInfo.h"

#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>

#include "ffmpeg/FFmpeg.h"
#include "image/RawDecoder.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
}

namespace lumora {

int bitDepthOf(AVPixelFormat fmt) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(fmt);
    if (!d || d->nb_components == 0)
        return 8;
    return d->comp[0].depth;
}

QStringList videoFileExtensions() {
    return {"mp4", "m4v", "mov", "mkv", "webm", "avi", "mxf", "mts", "m2ts", "ts", "3gp", "wmv", "flv", "mpg",
            "mpeg"};
}

QStringList imageFileExtensions() {
    return QStringList{"jpg", "jpeg", "png", "tif", "tiff", "webp", "bmp"} + raw::fileExtensions();
}

bool isImageFile(const QString& path) {
    return imageFileExtensions().contains(QFileInfo(path).suffix().toLower());
}

static int displayRotation(const AVStream* st) {
    const AVPacketSideData* sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                                         st->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * 4)
        return 0;
    const double angle = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    if (std::isnan(angle))
        return 0;
    // av_display_rotation_get returns counter-clockwise degrees.
    int deg = static_cast<int>(std::lround(-angle)) % 360;
    if (deg < 0)
        deg += 360;
    return deg;
}

static std::optional<MediaInfo> probeImage(const QString& path, QString* error) {
    if (raw::isRawFile(path)) {
        raw::Info ri;
        if (!raw::probe(path, &ri, error))
            return std::nullopt;
        MediaInfo info;
        info.path = path;
        info.kind = MediaKind::Image;
        info.formatName = QStringLiteral("raw");
        info.formatLongName = ri.camera.isEmpty() ? QStringLiteral("RAW") : QStringLiteral("RAW (%1)").arg(ri.camera);
        info.imageWidth = ri.size.width();
        info.imageHeight = ri.size.height();
        info.imageBitDepth = ri.bitDepth;
        info.fileSize = QFileInfo(path).size();
        return info;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!reader.canRead()) {
        if (error)
            *error = reader.errorString();
        return std::nullopt;
    }
    MediaInfo info;
    info.path = path;
    info.kind = MediaKind::Image;
    info.formatName = QString::fromLatin1(reader.format());
    info.formatLongName = info.formatName.toUpper();
    info.imageWidth = size.width();
    info.imageHeight = size.height();
    const QImage::Format fmt = reader.imageFormat();
    switch (fmt) {
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_Grayscale16:
        info.imageBitDepth = 16;
        break;
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
        info.imageBitDepth = 16;
        break;
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        info.imageBitDepth = 32;
        break;
    default:
        info.imageBitDepth = 8;
    }
    info.fileSize = QFileInfo(path).size();
    return info;
}

std::optional<MediaInfo> probeMedia(const QString& path, QString* error) {
    if (isImageFile(path))
        return probeImage(path, error);

    auto fmt = av::openInput(path, error);
    if (!fmt)
        return std::nullopt;

    MediaInfo info;
    info.path = path;
    info.formatName = QString::fromUtf8(fmt->iformat->name);
    info.formatLongName = QString::fromUtf8(fmt->iformat->long_name ? fmt->iformat->long_name : fmt->iformat->name);
    info.bitRate = fmt->bit_rate;
    info.fileSize = QFileInfo(path).size();
    if (fmt->duration != AV_NOPTS_VALUE)
        info.duration = ticksFromStream(fmt->duration, AVRational{1, AV_TIME_BASE});

    const int bestVideo = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int bestAudio = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, bestVideo, nullptr, 0);

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        AVStream* st = fmt->streams[i];
        const AVCodecParameters* par = st->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && !(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
            ++info.videoStreamCount;
            if (static_cast<int>(i) != bestVideo)
                continue;
            VideoStreamInfo v;
            v.index = static_cast<int>(i);
            v.codecId = par->codec_id;
            v.codecName = QString::fromUtf8(avcodec_get_name(par->codec_id));
            if (const char* prof = avcodec_profile_name(par->codec_id, par->profile))
                v.profile = QString::fromUtf8(prof);
            v.width = par->width;
            v.height = par->height;
            v.sampleAspectRatio = par->sample_aspect_ratio;
            v.frameRate = st->avg_frame_rate;
            v.realFrameRate = st->r_frame_rate;
            if (v.frameRate.num <= 0 || v.frameRate.den <= 0)
                v.frameRate = v.realFrameRate;
            if (v.frameRate.num <= 0 || v.frameRate.den <= 0)
                v.frameRate = AVRational{25, 1};
            // Phones record variable frame rate: avg and r rates diverge.
            if (v.realFrameRate.num > 0 && v.realFrameRate.den > 0) {
                const double a = av_q2d(v.frameRate), r = av_q2d(v.realFrameRate);
                v.variableFrameRate = std::abs(a - r) > 0.01 * r;
            }
            v.timeBase = st->time_base;
            v.pixelFormat = static_cast<AVPixelFormat>(par->format);
            v.bitDepth = par->bits_per_raw_sample > 0 ? par->bits_per_raw_sample : bitDepthOf(v.pixelFormat);
            v.colorPrimaries = par->color_primaries;
            v.colorTrc = par->color_trc;
            v.colorSpace = par->color_space;
            v.colorRange = par->color_range;
            v.chromaLocation = par->chroma_location;
            v.bitRate = par->bit_rate;
            v.hasBFrames = par->video_delay > 0;
            v.rotation = displayRotation(st);
            v.startTime = st->start_time != AV_NOPTS_VALUE ? ticksFromStream(st->start_time, st->time_base) : 0;
            v.duration = st->duration != AV_NOPTS_VALUE ? ticksFromStream(st->duration, st->time_base) : info.duration;
            info.video = v;
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++info.audioStreamCount;
            if (static_cast<int>(i) != bestAudio)
                continue;
            AudioStreamInfo a;
            a.index = static_cast<int>(i);
            a.codecId = par->codec_id;
            a.codecName = QString::fromUtf8(avcodec_get_name(par->codec_id));
            a.sampleRate = par->sample_rate;
            a.channels = par->ch_layout.nb_channels;
            char layout[128] = {};
            av_channel_layout_describe(&par->ch_layout, layout, sizeof(layout));
            a.channelLayout = QString::fromUtf8(layout);
            if (const char* sf = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format)))
                a.sampleFormat = QString::fromUtf8(sf);
            a.bitRate = par->bit_rate;
            a.timeBase = st->time_base;
            a.startTime = st->start_time != AV_NOPTS_VALUE ? ticksFromStream(st->start_time, st->time_base) : 0;
            a.duration = st->duration != AV_NOPTS_VALUE ? ticksFromStream(st->duration, st->time_base) : info.duration;
            info.audio = a;
        } else {
            ++info.otherStreamCount;
        }
    }

    if (info.video)
        info.kind = MediaKind::Video;
    else if (info.audio)
        info.kind = MediaKind::Audio;
    else {
        if (error)
            *error = QStringLiteral("No audio or video stream");
        return std::nullopt;
    }
    if (info.duration <= 0 && info.video)
        info.duration = info.video->duration;
    return info;
}

std::vector<Ticks> scanKeyframes(const QString& path, int streamIndex, QString* error) {
    std::vector<Ticks> result;
    auto fmt = av::openInput(path, error);
    if (!fmt || streamIndex < 0 || streamIndex >= static_cast<int>(fmt->nb_streams))
        return result;
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        fmt->streams[i]->discard = (static_cast<int>(i) == streamIndex) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    const AVRational tb = fmt->streams[streamIndex]->time_base;
    auto pkt = av::makePacket();
    while (av_read_frame(fmt.get(), pkt.get()) >= 0) {
        if (pkt->stream_index == streamIndex && (pkt->flags & AV_PKT_FLAG_KEY)) {
            const std::int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
            if (ts != AV_NOPTS_VALUE)
                result.push_back(ticksFromStream(ts, tb));
        }
        av_packet_unref(pkt.get());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

QString formatFrameRate(AVRational rate) {
    if (rate.num <= 0 || rate.den <= 0)
        return QStringLiteral("? fps");
    QString n = QString::number(av_q2d(rate), 'f', 3);
    while (n.endsWith(QLatin1Char('0')))
        n.chop(1);
    if (n.endsWith(QLatin1Char('.')))
        n.chop(1);
    return n + QStringLiteral(" fps");
}

QString MediaInfo::summary() const {
    QStringList parts;
    if (kind == MediaKind::Image) {
        parts << QStringLiteral("%1×%2").arg(imageWidth).arg(imageHeight) << formatName.toUpper()
              << QStringLiteral("%1-bit").arg(imageBitDepth);
        return parts.join(QStringLiteral(" · "));
    }
    if (video) {
        parts << QStringLiteral("%1×%2").arg(video->width).arg(video->height);
        parts << formatFrameRate(video->frameRate);
        parts << video->codecName.toUpper();
        parts << QStringLiteral("%1-bit").arg(video->bitDepth);
        if (video->isHdr())
            parts << QStringLiteral("HDR");
        if (video->variableFrameRate)
            parts << QStringLiteral("VFR");
    }
    if (audio)
        parts << QStringLiteral("%1 %2 kHz").arg(audio->codecName.toUpper()).arg(audio->sampleRate / 1000.0);
    parts << formatSeconds(duration);
    return parts.join(QStringLiteral(" · "));
}

} // namespace lumora

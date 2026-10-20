#include "SubtitleStreams.h"

#include <QDir>
#include <QFileInfo>
#include <cstring>

extern "C" {
#include <libavutil/mem.h>
}

namespace lumora {

namespace {

// Minimal ASS header: the text subtitle encoders take their input as ASS
// events.
const char* kAssHeader =
    "[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 384\r\nPlayResY: 288\r\n\r\n"
    "[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
    "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
    "MarginL, MarginR, MarginV, Encoding\r\n"
    "Style: Default,Arial,16,&Hffffff,&Hffffff,&H0,&H0,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,0\r\n\r\n"
    "[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";

QByteArray assEvent(const QString& text) {
    QString t = text;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char('{'), QStringLiteral("\\{"));
    t.replace(QLatin1Char('\n'), QStringLiteral("\\N"));
    return QByteArray("0,0,Default,,0,0,0,,") + t.toUtf8();
}

} // namespace

AVCodecID SubtitleStreams::codecFor(const QString& muxer) {
    if (muxer == QLatin1String("mp4") || muxer == QLatin1String("mov") || muxer == QLatin1String("ipod"))
        return AV_CODEC_ID_MOV_TEXT;
    if (muxer == QLatin1String("matroska"))
        return AV_CODEC_ID_SUBRIP;
    if (muxer == QLatin1String("webm"))
        return AV_CODEC_ID_WEBVTT;
    return AV_CODEC_ID_NONE;
}

bool SubtitleStreams::addStreams(AVFormatContext* out, const ExportPlan& plan, QString* error) {
    m_out = out;
    const AVCodecID id = static_cast<AVCodecID>(plan.subtitleCodec);
    for (size_t i = 0; i < plan.subtitleStreams.size(); ++i) {
        const auto& sp = plan.subtitleStreams[i];
        const AVCodec* codec = avcodec_find_encoder(id);
        if (!codec) {
            if (error)
                *error = QStringLiteral("no %1 encoder").arg(QString::fromUtf8(avcodec_get_name(id)));
            return false;
        }
        Stream st;
        st.enc.reset(avcodec_alloc_context3(codec));
        AVCodecContext* c = st.enc.get();
        c->time_base = AVRational{1, 1000};
        const size_t len = std::strlen(kAssHeader);
        c->subtitle_header = static_cast<uint8_t*>(av_mallocz(len + 1));
        std::memcpy(c->subtitle_header, kAssHeader, len);
        c->subtitle_header_size = static_cast<int>(len);
        if (out->oformat->flags & AVFMT_GLOBALHEADER)
            c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        const int ret = avcodec_open2(c, codec, nullptr);
        if (ret < 0) {
            if (error)
                *error = av::errorString(ret);
            return false;
        }
        AVStream* s = avformat_new_stream(out, nullptr);
        avcodec_parameters_from_context(s->codecpar, c);
        s->time_base = AVRational{1, 1000};
        if (!sp.language.isEmpty())
            av_dict_set(&s->metadata, "language", sp.language.toUtf8().constData(), 0);
        if (!sp.title.isEmpty())
            av_dict_set(&s->metadata, "title", sp.title.toUtf8().constData(), 0);
        s->disposition = i == 0 ? AV_DISPOSITION_DEFAULT : 0;
        st.index = s->index;
        st.cues = sp.cues;
        m_streams.push_back(std::move(st));
    }
    return true;
}

bool SubtitleStreams::writeUntil(Ticks t, const std::function<bool(AVPacket*, int)>& write) {
    std::vector<uint8_t> buf(64 * 1024);
    for (auto& st : m_streams) {
        while (st.next < st.cues.size() && st.cues[st.next].start < t) {
            const SubtitleCue& cue = st.cues[st.next++];
            AVSubtitle sub{};
            AVSubtitleRect rect{};
            AVSubtitleRect* rects[1] = {&rect};
            const QByteArray event = assEvent(cue.text);
            rect.type = SUBTITLE_ASS;
            rect.ass = const_cast<char*>(event.constData());
            sub.num_rects = 1;
            sub.rects = rects;
            sub.pts = av_rescale(cue.start, AV_TIME_BASE, kTicksPerSecond);
            sub.start_display_time = 0;
            sub.end_display_time = static_cast<uint32_t>(av_rescale(cue.end - cue.start, 1000, kTicksPerSecond));
            const int size = avcodec_encode_subtitle(st.enc.get(), buf.data(), static_cast<int>(buf.size()), &sub);
            if (size <= 0)
                continue;
            auto pkt = av::makePacket();
            if (av_new_packet(pkt.get(), size) < 0)
                return false;
            std::memcpy(pkt->data, buf.data(), size);
            const AVRational tb = m_out->streams[st.index]->time_base;
            pkt->pts = pkt->dts = av_rescale(cue.start, tb.den, static_cast<std::int64_t>(kTicksPerSecond) * tb.num);
            pkt->duration = av_rescale(cue.end - cue.start, tb.den, static_cast<std::int64_t>(kTicksPerSecond) * tb.num);
            pkt->flags |= AV_PKT_FLAG_KEY;
            if (!write(pkt.get(), st.index))
                return false;
        }
    }
    return true;
}

QStringList SubtitleStreams::writeSidecars(const ExportPlan& plan, const QString& videoPath, QString* error) {
    QStringList written;
    const QFileInfo fi(videoPath);
    const bool several = plan.sidecarSubtitles.size() > 1;
    for (size_t i = 0; i < plan.sidecarSubtitles.size(); ++i) {
        const auto& sp = plan.sidecarSubtitles[i];
        QString suffix;
        if (several)
            suffix = QLatin1Char('.') + (sp.language.isEmpty() ? QString::number(i + 1) : sp.language);
        const QString path = fi.dir().filePath(fi.completeBaseName() + suffix + QStringLiteral(".srt"));
        if (!subtitles::writeFile(path, sp.cues, subtitles::Format::Srt, error))
            return written;
        written << path;
    }
    return written;
}

} // namespace lumora

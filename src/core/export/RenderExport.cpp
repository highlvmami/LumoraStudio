#include "RenderExport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QUuid>
#include <algorithm>

#include "export/Encoders.h"
#include "export/ExportCommon.h"
#include "audio/Loudness.h"
#include "export/SubtitleStreams.h"
#include "ffmpeg/FFmpeg.h"
#include "media/MediaPool.h"
#include "render/AudioMixer.h"
#include "render/FrameRenderer.h"

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
}

namespace lumora {

namespace {

using namespace exportutil;

QString tr(const char* text) { return TimelineExporter::tr(text); }

int swsCoefficients(int colorspace, int height) {
    switch (colorspace) {
    case AVCOL_SPC_BT709: return SWS_CS_ITU709;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
    case AVCOL_SPC_SMPTE240M: return SWS_CS_SMPTE240M;
    case AVCOL_SPC_FCC: return SWS_CS_FCC;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: return SWS_CS_ITU601;
    default: return height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    }
}

class RenderSession {
public:
    RenderSession(const ExportPlan& plan, const ExportSettings& settings, const TimelineExporter::ProgressFn& progress,
                  const std::atomic_bool* cancel)
        : m_plan(plan), m_settings(settings), m_progress(progress), m_cancel(cancel) {}

    ExportResult run();

private:
    bool encodePass(int pass);
    bool openVideoEncoder(int pass);
    bool openOutput();
    bool openAudio();
    bool videoFrame(FrameRenderer& renderer, Ticks t, std::int64_t index);
    bool sendVideo(AVFrame* frame, bool write);
    bool writePacket(AVPacket* pkt, int stream, std::int64_t& lastDts);
    bool pumpAudio(Ticks until); // output time
    bool pumpMixedAudio(Ticks until);
    bool pumpCopiedAudio(Ticks until);
    bool encodeAudioFromFifo(bool flush);
    bool flushAudio();
    bool fail(const QString& msg) {
        if (m_error.isEmpty())
            m_error = msg;
        return false;
    }
    bool cancelled() const { return m_cancel && m_cancel->load(); }
    void report(double fraction) {
        if (m_progress)
            m_progress(std::clamp(m_progressBase + fraction * m_progressSpan, 0.0, 1.0));
    }

    const ExportPlan& m_plan;
    const ExportSettings& m_settings;
    const TimelineExporter::ProgressFn& m_progress;
    const std::atomic_bool* m_cancel;
    double m_progressBase = 0, m_progressSpan = 1;

    av::FormatOutputPtr m_out;
    int m_outVideo = -1, m_outAudio = -1;
    std::int64_t m_lastVideoDts = AV_NOPTS_VALUE, m_lastAudioDts = AV_NOPTS_VALUE;

    // Video.
    av::CodecContextPtr m_venc;
    av::FramePtr m_encFrame;
    av::SwsPtr m_rgbSws, m_yuvSws;
    int m_yuvKey[5] = {0, 0, -1, -1, -1};
    encoders::PassInfo m_pass;
    QByteArray m_stats; // in-memory first pass stats (libvpx, libaom)

    // Audio, encoded from the mixer.
    std::unique_ptr<AudioMixer> m_mixer;
    av::CodecContextPtr m_aenc;
    av::SwrPtr m_aswr;
    AVAudioFifo* m_fifo = nullptr;
    std::int64_t m_samplesQueued = 0, m_samplesOut = 0, m_rangeStartSample = 0;
    // Audio, copied.
    size_t m_piece = 0;
    av::FormatInputPtr m_pieceIn;
    int m_pieceStream = -1;
    Ticks m_pieceOrigin = 0;
    av::PacketPtr m_pending;
    Ticks m_pendingTime = 0;
    bool m_audioDone = false;

    SubtitleStreams m_subs;
    std::map<int, std::int64_t> m_lastSubDts;
    bool writeSubtitles(Ticks until) {
        return m_subs.writeUntil(until, [this](AVPacket* p, int idx) {
            auto [it, _] = m_lastSubDts.try_emplace(idx, AV_NOPTS_VALUE);
            return writePacket(p, idx, it->second);
        });
    }

    QString m_error;
    QStringList m_notes;
};

ExportResult RenderSession::run() {
    ExportResult result;
    if (!m_plan.ok() || !m_plan.pool) {
        result.error = m_plan.blockers.join(QLatin1Char('\n'));
        return result;
    }
    const bool twoRuns = m_plan.twoPass && encoders::twoPassNeedsTwoRuns(m_plan.videoEncoder);
    QString statsBase;
    if (twoRuns) {
        statsBase = QDir::temp().filePath(QStringLiteral("lumora-2pass-%1.log")
                                              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        m_pass.statsPath = statsBase;
        m_pass.stats = &m_stats;
    }
    bool ok = true;
    if (twoRuns) {
        m_progressBase = 0.0;
        m_progressSpan = 0.4;
        ok = encodePass(1);
        m_progressBase = 0.4;
        m_progressSpan = 0.6;
        ok = ok && encodePass(2);
    } else {
        ok = encodePass(0);
    }
    // The encoder keeps its statistics files open until it is freed, and
    // Windows does not delete open files.
    m_venc.reset();
    if (twoRuns)
        for (const char* suffix : {"", ".mbtree", ".cutree", ".temp", ".mbtree.temp", ".cutree.temp"})
            QFile::remove(statsBase + QLatin1String(suffix));
    if (m_fifo)
        av_audio_fifo_free(m_fifo);
    m_fifo = nullptr;
    m_out.reset();
    if (!ok) {
        QFile::remove(m_settings.outputPath);
        result.cancelled = cancelled();
        result.error = result.cancelled ? tr("Export cancelled.") : m_error;
        return result;
    }
    result.ok = true;
    result.writtenDuration = m_plan.rangeEnd - m_plan.rangeStart;
    result.fileSize = QFileInfo(m_settings.outputPath).size();
    result.notes = m_notes;
    if (m_progress)
        m_progress(1.0);
    return result;
}

bool RenderSession::encodePass(int pass) {
    const bool final = pass != 1;
    m_pass.pass = pass;
    if (!openVideoEncoder(pass))
        return false;
    if (final && !openOutput())
        return false;

    FrameRenderer renderer(m_plan.pool);
    renderer.setTimeline(m_plan.timeline);
    renderer.setOutputSize(m_plan.pictureSize);
    renderer.setPrecision(FrameRenderer::Precision::Final);
    renderer.setDrawSubtitles(m_plan.burnSubtitles);
    const AVRational rate = m_plan.outputRate;
    renderer.setSampleOffset(frameDuration(rate) / 2);

    const Ticks length = m_plan.rangeEnd - m_plan.rangeStart;
    // Frame n starts at n / rate; every frame that starts inside the range is written.
    const std::int64_t frames =
        std::max<std::int64_t>(1, av_rescale_rnd(length, rate.num, static_cast<std::int64_t>(rate.den) * kTicksPerSecond,
                                                 AV_ROUND_UP));
    for (std::int64_t n = 0; n < frames; ++n) {
        if (cancelled())
            return false;
        const Ticks offset = av_rescale(n, static_cast<std::int64_t>(rate.den) * kTicksPerSecond, rate.num);
        if (!videoFrame(renderer, m_plan.rangeStart + offset, n))
            return false;
        if (final && !pumpAudio(offset + frameDuration(rate)))
            return false;
        report(static_cast<double>(n + 1) / frames);
    }
    if (!sendVideo(nullptr, final))
        return false;
    if (!final) {
        m_venc.reset();
        return true;
    }
    if (!pumpAudio(length) || !flushAudio())
        return false;
    if (!writeSubtitles(INT64_MAX))
        return fail(m_error);
    const int ret = av_write_trailer(m_out.get());
    if (ret < 0)
        return fail(tr("Could not finish the file: %1").arg(av::errorString(ret)));
    return true;
}

bool RenderSession::openVideoEncoder(int pass) {
    const AVCodec* codec = avcodec_find_encoder_by_name(m_plan.videoEncoder.toUtf8().constData());
    if (!codec)
        return fail(tr("Encoder %1 is not available.").arg(m_plan.videoEncoder));
    m_venc.reset(avcodec_alloc_context3(codec));
    AVCodecContext* c = m_venc.get();
    c->width = m_plan.outputSize.width();
    c->height = m_plan.outputSize.height();
    c->sample_aspect_ratio = AVRational{1, 1};
    c->framerate = m_plan.outputRate;
    c->time_base = av_inv_q(m_plan.outputRate);
    c->pix_fmt = static_cast<AVPixelFormat>(m_plan.pixelFormat);
    if (c->pix_fmt == AV_PIX_FMT_NONE)
        c->pix_fmt = choosePixelFormat(c, codec, AV_PIX_FMT_YUV420P);
    c->color_primaries = static_cast<AVColorPrimaries>(m_plan.colorPrimaries);
    c->color_trc = static_cast<AVColorTransferCharacteristic>(m_plan.colorTrc);
    c->colorspace = static_cast<AVColorSpace>(m_plan.colorSpace);
    c->color_range = static_cast<AVColorRange>(m_plan.colorRange);
    c->thread_count = 0;
    const AVOutputFormat* ofmt = av_guess_format(m_plan.containerName.toUtf8().constData(), nullptr, nullptr);
    if (ofmt && (ofmt->flags & AVFMT_GLOBALHEADER))
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (codec->id == AV_CODEC_ID_HEVC &&
        (m_plan.containerName == QLatin1String("mp4") || m_plan.containerName == QLatin1String("mov")))
        c->codec_tag = MKTAG('h', 'v', 'c', '1'); // plays on Apple devices

    VideoEncodeOptions o = m_plan.videoOptions;
    std::int64_t bitrate = m_plan.videoBitrate;
    if (m_plan.mode == ExportMode::MatchSource && encoders::hasRateControl(codec->id)) {
        // Same average bit rate as the source.
        if (bitrate <= 0)
            bitrate = static_cast<std::int64_t>(c->width * c->height * av_q2d(m_plan.outputRate) * 0.15);
        o.rateControl = RateControl::VariableBitrate;
        o.maxBitrate = bitrate * 3 / 2;
        o.speed = 3;
    }
    encoders::PassInfo passInfo = m_pass;
    passInfo.pass = pass;
    AVDictionary* opts = nullptr;
    const QStringList notes = encoders::configure(c, &opts, m_plan.videoEncoder, o, bitrate, passInfo);
    if (pass != 1)
        for (const auto& n : notes)
            if (!m_notes.contains(n))
                m_notes << n;
    const int ret = avcodec_open2(c, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return fail(tr("Cannot start the %1 encoder: %2").arg(m_plan.videoEncoder, av::errorString(ret)));

    m_encFrame = av::makeFrame();
    m_encFrame->format = c->pix_fmt;
    m_encFrame->width = c->width;
    m_encFrame->height = c->height;
    if (av_frame_get_buffer(m_encFrame.get(), 0) < 0)
        return fail(tr("Out of memory."));
    return true;
}

bool RenderSession::openOutput() {
    AVFormatContext* raw = nullptr;
    const std::string outPath = av::toFFmpegPath(m_settings.outputPath);
    int ret = avformat_alloc_output_context2(&raw, nullptr, m_plan.containerName.toUtf8().constData(), outPath.c_str());
    if (ret < 0 || !raw)
        return fail(tr("Cannot create the output file: %1").arg(av::errorString(ret)));
    m_out.reset(raw);

    // Keep the container metadata of the first clip (creation time, location ...).
    for (const auto& t : m_plan.timeline.tracks) {
        bool found = false;
        for (const auto& c : t.clips)
            if (auto m = m_plan.pool->find(c.mediaId)) {
                if (auto in = av::openInput(m->path)) {
                    av_dict_copy(&m_out->metadata, in->metadata, 0);
                    av_dict_set(&m_out->metadata, "encoder", nullptr, 0);
                }
                found = true;
                break;
            }
        if (found)
            break;
    }

    AVStream* vs = avformat_new_stream(m_out.get(), nullptr);
    m_outVideo = vs->index;
    avcodec_parameters_from_context(vs->codecpar, m_venc.get());
    vs->time_base = m_venc->time_base;
    vs->avg_frame_rate = m_venc->framerate;
    vs->sample_aspect_ratio = m_venc->sample_aspect_ratio;

    if (!openAudio())
        return false;

    if (!(m_out->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_out->pb, outPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
            return fail(tr("Cannot write %1: %2").arg(m_settings.outputPath, av::errorString(ret)));
    }
    AVDictionary* opts = nullptr;
    if (m_plan.containerName == QLatin1String("mp4") || m_plan.containerName == QLatin1String("mov"))
        av_dict_set(&opts, "movflags", "+faststart+use_metadata_tags", 0);
    {
        QString subErr;
        if (!m_subs.addStreams(m_out.get(), m_plan, &subErr))
            return fail(TimelineExporter::tr("Cannot add the subtitles: %1").arg(subErr));
    }
    ret = avformat_write_header(m_out.get(), &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return fail(tr("Cannot write the file header: %1").arg(av::errorString(ret)));
    return true;
}

bool RenderSession::openAudio() {
    if (m_plan.audio == AudioStrategy::None) {
        m_audioDone = true;
        return true;
    }
    AVStream* as = avformat_new_stream(m_out.get(), nullptr);
    m_outAudio = as->index;
    if (m_plan.audio == AudioStrategy::Copy) {
        const SegmentPlan& first = m_plan.segments.front();
        QString err;
        auto in = av::openInput(first.path, &err);
        if (!in)
            return fail(tr("Cannot open %1: %2").arg(first.path, err));
        const int idx = av_find_best_stream(in.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (idx < 0)
            return fail(tr("%1 has no audio stream.").arg(first.path));
        const AVStream* src = in->streams[idx];
        avcodec_parameters_copy(as->codecpar, src->codecpar);
        if (!sameContainerFamily(in.get(), m_out->oformat))
            as->codecpar->codec_tag = 0;
        as->time_base = src->time_base;
        av_dict_copy(&as->metadata, src->metadata, 0);
        as->disposition = src->disposition;
        return true;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name(m_plan.audioEncoder.toUtf8().constData());
    if (!codec)
        return fail(tr("Encoder %1 is not available.").arg(m_plan.audioEncoder));
    m_aenc.reset(avcodec_alloc_context3(codec));
    AVCodecContext* c = m_aenc.get();
    const int rate = chooseSampleRate(c, codec, m_plan.audioSampleRate > 0 ? m_plan.audioSampleRate : 48000);
    c->sample_rate = rate;
    av_channel_layout_default(&c->ch_layout, std::clamp(m_plan.audioChannels, 1, 8));
    const auto fmts = supportedSampleFormats(c, codec);
    c->sample_fmt = fmts.empty() ? AV_SAMPLE_FMT_FLTP : fmts.front();
    for (AVSampleFormat f : fmts)
        if ((codec->id == AV_CODEC_ID_FLAC || codec->id == AV_CODEC_ID_ALAC) && av_get_bytes_per_sample(f) == 4)
            c->sample_fmt = f; // keep 24-bit precision of the mix
    if (codec->id == AV_CODEC_ID_FLAC)
        c->bits_per_raw_sample = 24;
    if (codec->id != AV_CODEC_ID_FLAC && codec->id != AV_CODEC_ID_ALAC &&
        !(codec->id >= AV_CODEC_ID_FIRST_AUDIO && codec->id < AV_CODEC_ID_ADPCM_IMA_QT))
        c->bit_rate = m_plan.audioBitrate > 0 ? m_plan.audioBitrate : 320000;
    c->time_base = AVRational{1, rate};
    if (m_out->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int ret = avcodec_open2(c, codec, nullptr);
    if (ret < 0)
        return fail(tr("Cannot start the %1 encoder: %2").arg(m_plan.audioEncoder, av::errorString(ret)));
    avcodec_parameters_from_context(as->codecpar, c);
    as->time_base = c->time_base;

    const int channels = c->ch_layout.nb_channels;
    m_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, channels, 8192);
    SwrContext* swr = nullptr;
    ret = swr_alloc_set_opts2(&swr, &c->ch_layout, c->sample_fmt, rate, &c->ch_layout, AV_SAMPLE_FMT_FLTP, rate, 0,
                              nullptr);
    if (ret < 0 || swr_init(swr) < 0) {
        swr_free(&swr);
        return fail(tr("Cannot create the audio converter."));
    }
    m_aswr.reset(swr);
    m_mixer = std::make_unique<AudioMixer>(m_plan.pool, rate, channels);
    m_mixer->setTimeline(m_plan.timeline);
    if (m_plan.timeline.audioMaster.normalize) {
        const auto r = loudness::measureTimeline(m_plan.timeline, m_plan.pool, m_plan.rangeStart, m_plan.rangeEnd, {}, m_cancel);
        const double gain = loudness::normalizationGain(r.integrated, m_plan.timeline.audioMaster.targetLufs);
        m_mixer->setNormalizationGainDb(gain);
        m_notes << tr("Loudness %1 LUFS, brought to %2 LUFS (%3 dB).")
                       .arg(r.integrated, 0, 'f', 1)
                       .arg(m_plan.timeline.audioMaster.targetLufs, 0, 'f', 1)
                       .arg(gain, 0, 'f', 1);
    }
    m_rangeStartSample = av_rescale(m_plan.rangeStart, rate, kTicksPerSecond);
    return true;
}

bool RenderSession::videoFrame(FrameRenderer& renderer, Ticks t, std::int64_t index) {
    AVCodecContext* c = m_venc.get();
    if (av_frame_make_writable(m_encFrame.get()) < 0)
        return fail(tr("Out of memory."));
    const int dstCs = swsCoefficients(m_plan.colorSpace, c->height);
    const int dstFull = m_plan.colorRange == AVCOL_RANGE_JPEG ? 1 : 0;
    const bool padded = m_plan.pictureSize != m_plan.outputSize;
    const AVFrame* direct = padded ? nullptr : renderer.passthroughFrame(t);
    if (direct) {
        // A plain source frame: convert YUV to YUV without going through RGB.
        const int key[5] = {direct->width, direct->height, direct->format, direct->colorspace, direct->color_range};
        if (!m_yuvSws || !std::equal(std::begin(key), std::end(key), std::begin(m_yuvKey))) {
            m_yuvSws.reset(sws_getContext(direct->width, direct->height, static_cast<AVPixelFormat>(direct->format),
                                          c->width, c->height, c->pix_fmt,
                                          SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_BITEXACT, nullptr,
                                          nullptr, nullptr));
            if (!m_yuvSws)
                return fail(tr("Cannot convert the video frames."));
            sws_setColorspaceDetails(m_yuvSws.get(), sws_getCoefficients(swsCoefficients(direct->colorspace, direct->height)),
                                     direct->color_range == AVCOL_RANGE_JPEG ? 1 : 0, sws_getCoefficients(dstCs), dstFull,
                                     0, 1 << 16, 1 << 16);
            std::copy(std::begin(key), std::end(key), std::begin(m_yuvKey));
        }
        sws_scale(m_yuvSws.get(), direct->data, direct->linesize, 0, direct->height, m_encFrame->data,
                  m_encFrame->linesize);
    } else {
        QImage img = renderer.render(t);
        if (img.isNull())
            return fail(tr("Could not render the frame at %1 s.").arg(ticksToSeconds(t), 0, 'f', 2));
        if (padded) {
            QImage full(m_plan.outputSize, img.format());
            full.fill(Qt::black);
            QPainter p(&full);
            p.drawImage(m_plan.pictureOffset, img);
            p.end();
            img = full;
        }
        if (img.format() != QImage::Format_RGBA64_Premultiplied && img.format() != QImage::Format_RGBA64)
            img = img.convertToFormat(QImage::Format_RGBA64);
        if (!m_rgbSws) {
            m_rgbSws.reset(sws_getContext(img.width(), img.height(), AV_PIX_FMT_RGBA64, c->width, c->height, c->pix_fmt,
                                          SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_BITEXACT, nullptr,
                                          nullptr, nullptr));
            if (!m_rgbSws)
                return fail(tr("Cannot convert the video frames."));
            sws_setColorspaceDetails(m_rgbSws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 1, sws_getCoefficients(dstCs),
                                     dstFull, 0, 1 << 16, 1 << 16);
        }
        const uint8_t* src[4] = {img.constBits(), nullptr, nullptr, nullptr};
        const int stride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
        sws_scale(m_rgbSws.get(), src, stride, 0, img.height(), m_encFrame->data, m_encFrame->linesize);
    }
    m_encFrame->pts = index;
    m_encFrame->duration = 1;
    m_encFrame->pict_type = AV_PICTURE_TYPE_NONE;
    m_encFrame->color_primaries = c->color_primaries;
    m_encFrame->color_trc = c->color_trc;
    m_encFrame->colorspace = c->colorspace;
    m_encFrame->color_range = c->color_range;
    m_encFrame->sample_aspect_ratio = AVRational{1, 1};
    return sendVideo(m_encFrame.get(), m_pass.pass != 1);
}

bool RenderSession::sendVideo(AVFrame* frame, bool write) {
    AVCodecContext* c = m_venc.get();
    int ret = avcodec_send_frame(c, frame);
    if (ret < 0 && ret != AVERROR_EOF)
        return fail(tr("Video encoding failed: %1").arg(av::errorString(ret)));
    auto pkt = av::makePacket();
    while ((ret = avcodec_receive_packet(c, pkt.get())) >= 0) {
        if (write) {
            if (pkt->duration <= 0)
                pkt->duration = 1;
            av_packet_rescale_ts(pkt.get(), c->time_base, m_out->streams[m_outVideo]->time_base);
            if (!writePacket(pkt.get(), m_outVideo, m_lastVideoDts))
                return false;
        }
        av_packet_unref(pkt.get());
    }
    // libvpx / libaom hand out the first pass statistics when flushed.
    if (frame == nullptr && c->stats_out && m_pass.pass == 1)
        m_stats = QByteArray(c->stats_out);
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return fail(tr("Video encoding failed: %1").arg(av::errorString(ret)));
    return true;
}

bool RenderSession::writePacket(AVPacket* pkt, int stream, std::int64_t& lastDts) {
    pkt->stream_index = stream;
    pkt->pos = -1;
    if (pkt->dts != AV_NOPTS_VALUE && lastDts != AV_NOPTS_VALUE && pkt->dts <= lastDts) {
        pkt->dts = lastDts + 1;
        if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts)
            pkt->pts = pkt->dts;
    }
    if (pkt->dts != AV_NOPTS_VALUE)
        lastDts = pkt->dts;
    if (stream == m_outVideo && pkt->dts != AV_NOPTS_VALUE && !m_subs.empty()) {
        AVPacket* held = av_packet_clone(pkt);
        const bool ok = writeSubtitles(ticksFromStream(pkt->dts, m_out->streams[stream]->time_base));
        av_packet_unref(pkt);
        av_packet_move_ref(pkt, held);
        av_packet_free(&held);
        if (!ok)
            return false;
    }
    const int ret = av_interleaved_write_frame(m_out.get(), pkt);
    if (ret < 0)
        return fail(tr("Write error: %1").arg(av::errorString(ret)));
    return true;
}

bool RenderSession::pumpAudio(Ticks until) {
    if (m_audioDone)
        return true;
    until = std::min(until, m_plan.rangeEnd - m_plan.rangeStart);
    return m_plan.audio == AudioStrategy::Copy ? pumpCopiedAudio(until) : pumpMixedAudio(until);
}

bool RenderSession::pumpMixedAudio(Ticks until) {
    const int rate = m_aenc->sample_rate;
    const int channels = m_aenc->ch_layout.nb_channels;
    const std::int64_t target = av_rescale(until, rate, kTicksPerSecond);
    std::vector<float> mixed;
    std::vector<std::vector<float>> planes(channels);
    std::vector<float*> ptrs(channels);
    while (m_samplesQueued < target) {
        const int n = static_cast<int>(std::min<std::int64_t>(4096, target - m_samplesQueued));
        mixed.assign(static_cast<size_t>(n) * channels, 0.0f);
        m_mixer->mix(m_rangeStartSample + m_samplesQueued, n, mixed.data());
        for (int ch = 0; ch < channels; ++ch) {
            planes[ch].resize(n);
            for (int i = 0; i < n; ++i)
                planes[ch][i] = mixed[static_cast<size_t>(i) * channels + ch];
            ptrs[ch] = planes[ch].data();
        }
        if (av_audio_fifo_write(m_fifo, reinterpret_cast<void**>(ptrs.data()), n) < n)
            return fail(tr("Out of memory while buffering audio."));
        m_samplesQueued += n;
        if (!encodeAudioFromFifo(false))
            return false;
    }
    return true;
}

bool RenderSession::encodeAudioFromFifo(bool flush) {
    AVCodecContext* c = m_aenc.get();
    const bool variable = c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE;
    const int frameSize = (c->frame_size > 0 && !variable) ? c->frame_size : 1024;
    auto pkt = av::makePacket();
    while (av_audio_fifo_size(m_fifo) >= frameSize || (flush && av_audio_fifo_size(m_fifo) > 0)) {
        const int n = std::min(frameSize, av_audio_fifo_size(m_fifo));
        auto in = av::makeFrame();
        in->nb_samples = n;
        in->format = AV_SAMPLE_FMT_FLTP;
        in->sample_rate = c->sample_rate;
        av_channel_layout_copy(&in->ch_layout, &c->ch_layout);
        av_frame_get_buffer(in.get(), 0);
        av_audio_fifo_read(m_fifo, reinterpret_cast<void**>(in->data), n);
        auto out = av::makeFrame();
        out->nb_samples = n;
        out->format = c->sample_fmt;
        out->sample_rate = c->sample_rate;
        av_channel_layout_copy(&out->ch_layout, &c->ch_layout);
        av_frame_get_buffer(out.get(), 0);
        swr_convert(m_aswr.get(), out->data, n, const_cast<const uint8_t**>(in->data), n);
        out->pts = m_samplesOut;
        m_samplesOut += n;
        int ret = avcodec_send_frame(c, out.get());
        if (ret < 0)
            return fail(tr("Audio encoding failed: %1").arg(av::errorString(ret)));
        while ((ret = avcodec_receive_packet(c, pkt.get())) >= 0) {
            av_packet_rescale_ts(pkt.get(), c->time_base, m_out->streams[m_outAudio]->time_base);
            if (!writePacket(pkt.get(), m_outAudio, m_lastAudioDts))
                return false;
            av_packet_unref(pkt.get());
        }
    }
    return true;
}

bool RenderSession::pumpCopiedAudio(Ticks until) {
    const AVRational outTb = m_out->streams[m_outAudio]->time_base;
    auto writeAt = [&](AVPacket* p, Ticks outT) -> bool {
        const AVRational tb = m_pieceIn->streams[m_pieceStream]->time_base;
        const std::int64_t delta = (p->dts != AV_NOPTS_VALUE && p->pts != AV_NOPTS_VALUE) ? p->pts - p->dts : 0;
        p->pts = ticksToStream(outT, outTb);
        p->dts = p->pts - av_rescale_q(delta, tb, outTb);
        p->duration = av_rescale_q(p->duration, tb, outTb);
        return writePacket(p, m_outAudio, m_lastAudioDts);
    };
    while (m_piece < m_plan.segments.size()) {
        const SegmentPlan& seg = m_plan.segments[m_piece];
        if (m_pending) {
            if (m_pendingTime > until)
                return true;
            if (!writeAt(m_pending.get(), m_pendingTime))
                return false;
            m_pending.reset();
        }
        if (!m_pieceIn) {
            QString err;
            m_pieceIn = av::openInput(seg.path, &err);
            if (!m_pieceIn)
                return fail(tr("Cannot open %1: %2").arg(seg.path, err));
            m_pieceStream = av_find_best_stream(m_pieceIn.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            if (m_pieceStream < 0)
                return fail(tr("%1 has no audio stream.").arg(seg.path));
            for (unsigned i = 0; i < m_pieceIn->nb_streams; ++i)
                m_pieceIn->streams[i]->discard = static_cast<int>(i) == m_pieceStream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
            m_pieceOrigin = m_pieceIn->start_time != AV_NOPTS_VALUE
                                ? ticksFromStream(m_pieceIn->start_time, {1, AV_TIME_BASE})
                                : 0;
            const AVStream* st = m_pieceIn->streams[m_pieceStream];
            const std::int64_t ts = ticksToStreamDown(seg.requestedIn + m_pieceOrigin, st->time_base);
            if (avformat_seek_file(m_pieceIn.get(), m_pieceStream, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD) < 0)
                av_seek_frame(m_pieceIn.get(), m_pieceStream, ts, AVSEEK_FLAG_BACKWARD);
        }
        auto pkt = av::makePacket();
        const int ret = av_read_frame(m_pieceIn.get(), pkt.get());
        bool pieceDone = ret < 0;
        if (!pieceDone && pkt->stream_index == m_pieceStream) {
            const AVStream* st = m_pieceIn->streams[m_pieceStream];
            const Ticks t = packetTime(pkt.get(), st->time_base, m_pieceOrigin);
            if (t != kInvalidTicks) {
                const Ticks mid = t + ticksFromStream(pkt->duration, st->time_base) / 2;
                if (mid >= seg.requestedOut) {
                    pieceDone = true;
                } else if (mid >= seg.requestedIn) {
                    const Ticks outT = seg.outputStart + (t - seg.requestedIn);
                    if (outT > until) {
                        m_pending = std::move(pkt);
                        m_pendingTime = outT;
                        return true;
                    }
                    if (!writeAt(pkt.get(), outT))
                        return false;
                }
            }
        }
        if (pieceDone) {
            m_pieceIn.reset();
            m_pending.reset();
            ++m_piece;
        }
    }
    m_audioDone = true;
    return true;
}

bool RenderSession::flushAudio() {
    if (!m_aenc)
        return true;
    if (!encodeAudioFromFifo(true))
        return false;
    avcodec_send_frame(m_aenc.get(), nullptr);
    auto pkt = av::makePacket();
    while (avcodec_receive_packet(m_aenc.get(), pkt.get()) >= 0) {
        av_packet_rescale_ts(pkt.get(), m_aenc->time_base, m_out->streams[m_outAudio]->time_base);
        if (!writePacket(pkt.get(), m_outAudio, m_lastAudioDts))
            return false;
        av_packet_unref(pkt.get());
    }
    return true;
}

} // namespace

ExportResult runRenderExport(const ExportPlan& plan, const ExportSettings& settings,
                             const TimelineExporter::ProgressFn& progress, const std::atomic_bool* cancel) {
    RenderSession session(plan, settings, progress, cancel);
    return session.run();
}

} // namespace lumora

#include "TimelineExporter.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <deque>

#include "export/ExportCommon.h"
#include "audio/Loudness.h"
#include "export/RenderExport.h"
#include "export/SubtitleStreams.h"
#include "ffmpeg/FFmpeg.h"
#include "render/AudioMixer.h"

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace lumora {

namespace {

using namespace exportutil;

// ---------------------------------------------------------------------------
// Export session
// ---------------------------------------------------------------------------

class Session {
public:
    Session(const ExportPlan& plan, const ExportSettings& settings, const TimelineExporter::ProgressFn& progress,
            const std::atomic_bool* cancel)
        : m_plan(plan), m_settings(settings), m_progress(progress), m_cancel(cancel) {}

    ExportResult run();

private:
    bool setupOutput();
    bool setupVideoEncoder(const AVFormatContext* src, const AVStream* srcStream);
    bool setupAudioEncoder(const AVStream* srcStream);
    bool processSegment(const SegmentPlan& seg, Ticks& segmentLength);
    bool writePacket(AVPacket* pkt, int outIndex, std::int64_t& lastDts);
    bool encodeVideoFrame(AVFrame* frame);
    bool pushAudioSamples(const float* const* data, int count);
    bool pushSilence(int count);
    bool encodeAudioFromFifo(bool flush);
    // Mixed audio: pushes the mix up to output time `until`.
    bool pumpMix(Ticks until);
    bool flushEncoders();
    bool fail(const QString& msg) {
        if (m_error.isEmpty())
            m_error = msg;
        return false;
    }
    bool cancelled() const { return m_cancel && m_cancel->load(); }
    void report(Ticks done) {
        if (m_progress && m_plan.expectedDuration > 0)
            m_progress(std::clamp(static_cast<double>(done) / m_plan.expectedDuration, 0.0, 1.0));
    }

    const ExportPlan& m_plan;
    const ExportSettings& m_settings;
    const TimelineExporter::ProgressFn& m_progress;
    const std::atomic_bool* m_cancel;

    av::FormatOutputPtr m_out;
    int m_outVideo = -1;
    int m_outAudio = -1;
    std::int64_t m_lastVideoDts = AV_NOPTS_VALUE;
    std::int64_t m_lastAudioDts = AV_NOPTS_VALUE;

    // Video encoding.
    av::CodecContextPtr m_venc;
    av::SwsPtr m_sws;
    av::FramePtr m_convFrame;
    std::int64_t m_lastVideoPts = AV_NOPTS_VALUE;

    // Audio encoding. Samples are kept as planar float and converted to the
    // encoder format through swr right before encoding.
    av::CodecContextPtr m_aenc;
    AVAudioFifo* m_fifo = nullptr;
    av::SwrPtr m_encSwr;
    std::int64_t m_audioSamplesOut = 0; // samples sent to the encoder (= pts)
    std::int64_t m_audioSamplesQueued = 0; // samples written to the fifo so far (all segments)
    int m_audioChannels = 2;

    // Mixed audio (plan.audioMixed): timeline time = output time + offset of
    // the current segment.
    std::unique_ptr<AudioMixer> m_mixer;
    Ticks m_mixOffset = 0;
    bool m_pumping = false;

    Ticks m_cursor = 0; // output time at which the current segment starts
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

ExportResult Session::run() {
    ExportResult result;
    if (!m_plan.ok()) {
        result.error = m_plan.blockers.join(QLatin1Char('\n'));
        return result;
    }
    const QString tmpPath = m_settings.outputPath;
    bool ok = setupOutput();
    for (size_t i = 0; ok && i < m_plan.segments.size(); ++i) {
        Ticks len = 0;
        ok = processSegment(m_plan.segments[i], len);
        m_cursor += len;
        if (cancelled()) {
            ok = false;
            result.cancelled = true;
        }
    }
    if (ok)
        ok = flushEncoders();
    if (ok)
        ok = writeSubtitles(INT64_MAX);
    if (ok) {
        int ret = av_write_trailer(m_out.get());
        if (ret < 0)
            ok = fail(TimelineExporter::tr("Could not finish the file: %1").arg(av::errorString(ret)));
    }
    if (m_fifo)
        av_audio_fifo_free(m_fifo);
    m_fifo = nullptr;
    m_out.reset();
    if (!ok) {
        QFile::remove(tmpPath);
        result.error = result.cancelled ? TimelineExporter::tr("Export cancelled.") : m_error;
        return result;
    }
    result.ok = true;
    result.writtenDuration = m_cursor;
    result.fileSize = QFileInfo(tmpPath).size();
    result.notes = m_notes;
    if (m_progress)
        m_progress(1.0);
    return result;
}

bool Session::setupOutput() {
    const SegmentPlan& first = m_plan.segments.front();
    QString err;
    auto src = av::openInput(first.path, &err);
    if (!src)
        return fail(TimelineExporter::tr("Cannot open %1: %2").arg(first.path, err));
    const int vIdx = av_find_best_stream(src.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int aIdx = av_find_best_stream(src.get(), AVMEDIA_TYPE_AUDIO, -1, vIdx, nullptr, 0);
    if (vIdx < 0)
        return fail(TimelineExporter::tr("The first clip has no video stream."));

    AVFormatContext* raw = nullptr;
    const std::string outPath = av::toFFmpegPath(m_settings.outputPath);
    int ret = avformat_alloc_output_context2(&raw, nullptr, m_plan.containerName.toUtf8().constData(), outPath.c_str());
    if (ret < 0 || !raw)
        return fail(TimelineExporter::tr("Cannot create the output file: %1").arg(av::errorString(ret)));
    m_out.reset(raw);

    // Keep container metadata (creation time, location, camera ...).
    av_dict_copy(&m_out->metadata, src->metadata, 0);
    av_dict_set(&m_out->metadata, "encoder", nullptr, 0);

    // --- video stream ---
    const AVStream* inV = src->streams[vIdx];
    AVStream* outV = avformat_new_stream(m_out.get(), nullptr);
    m_outVideo = outV->index;
    if (m_plan.video == VideoStrategy::Copy) {
        avcodec_parameters_copy(outV->codecpar, inV->codecpar);
        if (!sameContainerFamily(src.get(), m_out->oformat))
            outV->codecpar->codec_tag = 0;
        outV->time_base = inV->time_base;
        outV->avg_frame_rate = inV->avg_frame_rate;
        outV->r_frame_rate = inV->r_frame_rate;
        outV->sample_aspect_ratio = inV->sample_aspect_ratio;
    } else {
        if (!setupVideoEncoder(src.get(), inV))
            return false;
        avcodec_parameters_from_context(outV->codecpar, m_venc.get());
        copySideData(inV->codecpar, outV->codecpar);
        outV->time_base = m_venc->time_base;
        outV->avg_frame_rate = m_venc->framerate;
        outV->sample_aspect_ratio = m_venc->sample_aspect_ratio;
    }
    av_dict_copy(&outV->metadata, inV->metadata, 0);
    av_dict_set(&outV->metadata, "encoder", nullptr, 0);
    outV->disposition = inV->disposition;

    // --- audio stream ---
    if (m_plan.audio != AudioStrategy::None) {
        const AVStream* inA = aIdx >= 0 ? src->streams[aIdx] : nullptr;
        AVStream* outA = avformat_new_stream(m_out.get(), nullptr);
        m_outAudio = outA->index;
        if (m_plan.audio == AudioStrategy::Copy) {
            if (!inA)
                return fail(TimelineExporter::tr("The first clip has no audio stream."));
            avcodec_parameters_copy(outA->codecpar, inA->codecpar);
            if (!sameContainerFamily(src.get(), m_out->oformat))
                outA->codecpar->codec_tag = 0;
            outA->time_base = inA->time_base;
        } else {
            if (!setupAudioEncoder(inA))
                return false;
            avcodec_parameters_from_context(outA->codecpar, m_aenc.get());
            outA->time_base = m_aenc->time_base;
            if (m_plan.audioMixed) {
                m_mixer = std::make_unique<AudioMixer>(m_plan.pool, m_aenc->sample_rate, m_audioChannels);
                m_mixer->setTimeline(m_plan.timeline);
                if (m_plan.timeline.audioMaster.normalize) {
                    const Ticks from = m_settings.rangeStart == kInvalidTicks ? 0 : m_settings.rangeStart;
                    const Ticks to = m_settings.rangeEnd == kInvalidTicks ? m_plan.timeline.duration() : m_settings.rangeEnd;
                    const auto r = loudness::measureTimeline(m_plan.timeline, m_plan.pool, from, to, {}, m_cancel);
                    const double gain = loudness::normalizationGain(r.integrated, m_plan.timeline.audioMaster.targetLufs);
                    m_mixer->setNormalizationGainDb(gain);
                    m_notes << TimelineExporter::tr("Loudness %1 LUFS, brought to %2 LUFS (%3 dB).")
                                   .arg(r.integrated, 0, 'f', 1)
                                   .arg(m_plan.timeline.audioMaster.targetLufs, 0, 'f', 1)
                                   .arg(gain, 0, 'f', 1);
                }
            }
        }
        if (inA) {
            av_dict_copy(&outA->metadata, inA->metadata, 0);
            outA->disposition = inA->disposition;
        }
    }

    if (!(m_out->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_out->pb, outPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
            return fail(TimelineExporter::tr("Cannot write %1: %2").arg(m_settings.outputPath, av::errorString(ret)));
    }
    AVDictionary* opts = nullptr;
    const QString muxer = m_plan.containerName;
    if (muxer == QLatin1String("mp4") || muxer == QLatin1String("mov"))
        av_dict_set(&opts, "movflags", "+faststart+use_metadata_tags", 0);
    {
        QString subErr;
        if (!m_subs.addStreams(m_out.get(), m_plan, &subErr))
            return fail(TimelineExporter::tr("Cannot add the subtitles: %1").arg(subErr));
    }
    ret = avformat_write_header(m_out.get(), &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return fail(TimelineExporter::tr("Cannot write the file header: %1").arg(av::errorString(ret)));
    return true;
}

bool Session::setupVideoEncoder(const AVFormatContext* src, const AVStream* in) {
    const AVCodec* codec = avcodec_find_encoder_by_name(m_plan.videoEncoder.toUtf8().constData());
    if (!codec)
        return fail(TimelineExporter::tr("Encoder %1 is not available.").arg(m_plan.videoEncoder));
    m_venc.reset(avcodec_alloc_context3(codec));
    AVCodecContext* c = m_venc.get();
    const AVCodecParameters* p = in->codecpar;
    c->width = p->width;
    c->height = p->height;
    c->sample_aspect_ratio = p->sample_aspect_ratio;
    AVRational rate = in->avg_frame_rate.num > 0 ? in->avg_frame_rate : in->r_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        rate = AVRational{25, 1};
    c->framerate = rate;
    const bool vfr = in->r_frame_rate.num > 0 && std::abs(av_q2d(in->r_frame_rate) - av_q2d(rate)) > 0.01 * av_q2d(rate);
    if (vfr && codec->id != AV_CODEC_ID_MPEG4 && codec->id != AV_CODEC_ID_DNXHD && codec->id != AV_CODEC_ID_PRORES)
        c->time_base = AVRational{1, 90000}; // keep variable frame timing exact
    else
        c->time_base = av_inv_q(rate);
    c->pix_fmt = choosePixelFormat(c, codec, static_cast<AVPixelFormat>(p->format));
    c->color_primaries = p->color_primaries;
    c->color_trc = p->color_trc;
    c->colorspace = p->color_space;
    c->color_range = p->color_range;
    c->chroma_sample_location = p->chroma_location;
    c->thread_count = 0;
    if (m_out->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* opts = nullptr;
    if (m_plan.mode == ExportMode::LosslessIntermediate) {
        // FFV1 version 3, intra only (every frame is a key frame: ideal for
        // further editing), slice CRCs for integrity checking.
        av_dict_set(&opts, "level", "3", 0);
        av_dict_set(&opts, "slicecrc", "1", 0);
        av_dict_set(&opts, "slices", "16", 0);
        c->gop_size = 1;
    } else {
        std::int64_t bitrate = p->bit_rate;
        if (bitrate <= 0 && src->bit_rate > 0)
            bitrate = src->bit_rate * 9 / 10; // container rate minus a typical audio share
        if (bitrate <= 0)
            bitrate = static_cast<std::int64_t>(c->width) * c->height * av_q2d(rate) * 0.15; // rough fallback
        const QString name = m_plan.videoEncoder;
        if (codec->id == AV_CODEC_ID_PRORES || codec->id == AV_CODEC_ID_DNXHD) {
            c->profile = p->profile; // ProRes / DNxHR quality is defined by the profile
            if (codec->id == AV_CODEC_ID_DNXHD)
                c->bit_rate = bitrate;
        } else {
            c->bit_rate = bitrate;
            c->rc_max_rate = bitrate * 3 / 2;
            c->rc_buffer_size = static_cast<int>(std::min<std::int64_t>(bitrate * 2, INT_MAX));
        }
        if (name == QLatin1String("libx264")) {
            av_dict_set(&opts, "preset", "slow", 0);
        } else if (name == QLatin1String("libx265")) {
            av_dict_set(&opts, "preset", "medium", 0);
            if (QString::fromUtf8(avcodec_get_name(p->codec_id)) == QLatin1String("hevc") && p->codec_tag)
                c->codec_tag = p->codec_tag; // keep hvc1 / hev1 flavour
        } else if (name == QLatin1String("libvpx-vp9")) {
            av_dict_set(&opts, "deadline", "good", 0);
            av_dict_set(&opts, "cpu-used", "2", 0);
            av_dict_set(&opts, "row-mt", "1", 0);
        } else if (name == QLatin1String("libsvtav1")) {
            av_dict_set(&opts, "preset", "6", 0);
        } else if (name == QLatin1String("libaom-av1")) {
            av_dict_set(&opts, "cpu-used", "4", 0);
            av_dict_set(&opts, "row-mt", "1", 0);
        }
        if (in->codecpar->video_delay == 0 && codec->id == AV_CODEC_ID_MPEG4)
            c->max_b_frames = 0;
    }
    int ret = avcodec_open2(c, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return fail(TimelineExporter::tr("Cannot start the %1 encoder: %2").arg(m_plan.videoEncoder, av::errorString(ret)));
    return true;
}

bool Session::setupAudioEncoder(const AVStream* in) {
    const AVCodec* codec = avcodec_find_encoder_by_name(m_plan.audioEncoder.toUtf8().constData());
    if (!codec)
        return fail(TimelineExporter::tr("Encoder %1 is not available.").arg(m_plan.audioEncoder));
    m_aenc.reset(avcodec_alloc_context3(codec));
    AVCodecContext* c = m_aenc.get();
    int rate = in ? in->codecpar->sample_rate : 48000;
    const auto rates = supportedSampleRates(c, codec);
    if (!rates.empty() && std::find(rates.begin(), rates.end(), rate) == rates.end()) {
        int best = rates.front();
        for (int r : rates)
            if (std::abs(r - rate) < std::abs(best - rate))
                best = r;
        rate = best;
    }
    c->sample_rate = rate;
    m_audioChannels = in ? std::clamp(in->codecpar->ch_layout.nb_channels, 1, 8) : 2;
    if (in && in->codecpar->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_copy(&c->ch_layout, &in->codecpar->ch_layout);
    else
        av_channel_layout_default(&c->ch_layout, m_audioChannels);
    m_audioChannels = c->ch_layout.nb_channels;
    const auto fmts = supportedSampleFormats(c, codec);
    c->sample_fmt = fmts.empty() ? AV_SAMPLE_FMT_FLTP : fmts.front();
    if (in && std::find(fmts.begin(), fmts.end(), static_cast<AVSampleFormat>(in->codecpar->format)) != fmts.end())
        c->sample_fmt = static_cast<AVSampleFormat>(in->codecpar->format);
    if (codec->id != AV_CODEC_ID_FLAC && codec->id != AV_CODEC_ID_ALAC)
        c->bit_rate = (in && in->codecpar->bit_rate > 0) ? in->codecpar->bit_rate : 192000 * m_audioChannels / 2;
    if (codec->id == AV_CODEC_ID_FLAC && in && in->codecpar->bits_per_raw_sample > 16)
        c->sample_fmt = AV_SAMPLE_FMT_S32;
    c->time_base = AVRational{1, rate};
    if (m_out->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int ret = avcodec_open2(c, codec, nullptr);
    if (ret < 0)
        return fail(TimelineExporter::tr("Cannot start the %1 encoder: %2").arg(m_plan.audioEncoder, av::errorString(ret)));

    m_fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, m_audioChannels, 4096);
    SwrContext* swr = nullptr;
    ret = swr_alloc_set_opts2(&swr, &c->ch_layout, c->sample_fmt, rate, &c->ch_layout, AV_SAMPLE_FMT_FLTP, rate, 0,
                              nullptr);
    if (ret < 0 || swr_init(swr) < 0) {
        swr_free(&swr);
        return fail(TimelineExporter::tr("Cannot create the audio converter."));
    }
    m_encSwr.reset(swr);
    return true;
}

bool Session::writePacket(AVPacket* pkt, int outIndex, std::int64_t& lastDts) {
    pkt->stream_index = outIndex;
    pkt->pos = -1;
    // Muxers need strictly increasing DTS; tiny overlaps can appear where two
    // segments meet.
    if (pkt->dts != AV_NOPTS_VALUE && lastDts != AV_NOPTS_VALUE && pkt->dts <= lastDts) {
        pkt->dts = lastDts + 1;
        if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts)
            pkt->pts = pkt->dts;
    }
    if (pkt->dts != AV_NOPTS_VALUE)
        lastDts = pkt->dts;
    // Keep the mixed sound just ahead of the picture so the file stays well
    // interleaved.
    if (m_mixer && outIndex == m_outVideo && pkt->dts != AV_NOPTS_VALUE && !m_pumping) {
        const Ticks t = ticksFromStream(pkt->dts, m_out->streams[outIndex]->time_base);
        AVPacket* held = av_packet_clone(pkt);
        const bool ok = pumpMix(std::max<Ticks>(m_cursor, t));
        av_packet_unref(pkt);
        av_packet_move_ref(pkt, held);
        av_packet_free(&held);
        if (!ok)
            return false;
    }
    if (outIndex == m_outVideo && pkt->dts != AV_NOPTS_VALUE && !m_subs.empty()) {
        AVPacket* held = av_packet_clone(pkt);
        const bool ok = writeSubtitles(ticksFromStream(pkt->dts, m_out->streams[outIndex]->time_base));
        av_packet_unref(pkt);
        av_packet_move_ref(pkt, held);
        av_packet_free(&held);
        if (!ok)
            return false;
    }
    const int ret = av_interleaved_write_frame(m_out.get(), pkt);
    if (ret < 0)
        return fail(TimelineExporter::tr("Write error: %1").arg(av::errorString(ret)));
    return true;
}

bool Session::encodeVideoFrame(AVFrame* frame) {
    AVCodecContext* c = m_venc.get();
    int ret = avcodec_send_frame(c, frame);
    if (ret < 0 && ret != AVERROR_EOF)
        return fail(TimelineExporter::tr("Video encoding failed: %1").arg(av::errorString(ret)));
    auto pkt = av::makePacket();
    while ((ret = avcodec_receive_packet(c, pkt.get())) >= 0) {
        // Muxers derive the last sample's length from the packet duration;
        // without it MP4 edit lists would hide the final frame.
        if (pkt->duration <= 0)
            pkt->duration = std::max<std::int64_t>(1, av_rescale_q(1, av_inv_q(c->framerate), c->time_base));
        av_packet_rescale_ts(pkt.get(), c->time_base, m_out->streams[m_outVideo]->time_base);
        if (!writePacket(pkt.get(), m_outVideo, m_lastVideoDts))
            return false;
        av_packet_unref(pkt.get());
    }
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return fail(TimelineExporter::tr("Video encoding failed: %1").arg(av::errorString(ret)));
    return true;
}

bool Session::pushAudioSamples(const float* const* data, int count) {
    if (count <= 0)
        return true;
    if (av_audio_fifo_write(m_fifo, reinterpret_cast<void* const*>(const_cast<float* const*>(data)), count) < count)
        return fail(TimelineExporter::tr("Out of memory while buffering audio."));
    m_audioSamplesQueued += count;
    return encodeAudioFromFifo(false);
}

bool Session::pushSilence(int count) {
    std::vector<std::vector<float>> zeros(m_audioChannels, std::vector<float>(4096, 0.0f));
    std::vector<const float*> ptrs(m_audioChannels);
    for (int ch = 0; ch < m_audioChannels; ++ch)
        ptrs[ch] = zeros[ch].data();
    while (count > 0) {
        const int n = std::min(count, 4096);
        if (!pushAudioSamples(ptrs.data(), n))
            return false;
        count -= n;
    }
    return true;
}

bool Session::encodeAudioFromFifo(bool flush) {
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
        swr_convert(m_encSwr.get(), out->data, n, const_cast<const uint8_t**>(in->data), n);
        out->pts = m_audioSamplesOut;
        m_audioSamplesOut += n;
        int ret = avcodec_send_frame(c, out.get());
        if (ret < 0)
            return fail(TimelineExporter::tr("Audio encoding failed: %1").arg(av::errorString(ret)));
        while ((ret = avcodec_receive_packet(c, pkt.get())) >= 0) {
            av_packet_rescale_ts(pkt.get(), c->time_base, m_out->streams[m_outAudio]->time_base);
            if (!writePacket(pkt.get(), m_outAudio, m_lastAudioDts))
                return false;
            av_packet_unref(pkt.get());
        }
    }
    return true;
}

bool Session::pumpMix(Ticks until) {
    if (!m_mixer)
        return true;
    m_pumping = true;
    const int rate = m_aenc->sample_rate;
    const std::int64_t target = av_rescale(until, rate, kTicksPerSecond);
    const std::int64_t offset = av_rescale(m_mixOffset, rate, kTicksPerSecond);
    constexpr int block = 2048;
    std::vector<float> mixed(static_cast<size_t>(block) * m_audioChannels);
    std::vector<std::vector<float>> planar(m_audioChannels, std::vector<float>(block));
    std::vector<const float*> ptrs(m_audioChannels);
    bool ok = true;
    while (ok && m_audioSamplesQueued < target) {
        const int n = static_cast<int>(std::min<std::int64_t>(block, target - m_audioSamplesQueued));
        m_mixer->mix(m_audioSamplesQueued + offset, n, mixed.data());
        for (int ch = 0; ch < m_audioChannels; ++ch) {
            for (int i = 0; i < n; ++i)
                planar[ch][i] = mixed[static_cast<size_t>(i) * m_audioChannels + ch];
            ptrs[ch] = planar[ch].data();
        }
        ok = pushAudioSamples(ptrs.data(), n);
    }
    m_pumping = false;
    return ok;
}

bool Session::flushEncoders() {
    if (m_aenc) {
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
    }
    if (m_venc)
        return encodeVideoFrame(nullptr);
    return true;
}

bool Session::processSegment(const SegmentPlan& seg, Ticks& segmentLength) {
    QString err;
    auto in = av::openInput(seg.path, &err);
    if (!in)
        return fail(TimelineExporter::tr("Cannot open %1: %2").arg(seg.path, err));
    const int vIdx = av_find_best_stream(in.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int aIdx = seg.hasAudio ? av_find_best_stream(in.get(), AVMEDIA_TYPE_AUDIO, -1, vIdx, nullptr, 0) : -1;
    if (vIdx < 0)
        return fail(TimelineExporter::tr("%1 has no video stream.").arg(seg.path));
    for (unsigned i = 0; i < in->nb_streams; ++i)
        in->streams[i]->discard =
            (static_cast<int>(i) == vIdx || static_cast<int>(i) == aIdx) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    AVStream* vs = in->streams[vIdx];
    AVStream* as = aIdx >= 0 ? in->streams[aIdx] : nullptr;
    const Ticks origin = in->start_time != AV_NOPTS_VALUE ? ticksFromStream(in->start_time, {1, AV_TIME_BASE}) : 0;
    const bool copyVideo = m_plan.video == VideoStrategy::Copy;
    const AVRational outVtb = m_out->streams[m_outVideo]->time_base;
    const AVRational outAtb = m_outAudio >= 0 ? m_out->streams[m_outAudio]->time_base : AVRational{1, 1};

    // Seek to the key frame at or before the start.
    const Ticks seekTo = copyVideo ? seg.actualIn : seg.requestedIn;
    const std::int64_t seekTs = ticksToStreamDown(seekTo + origin, vs->time_base);
    if (avformat_seek_file(in.get(), vIdx, INT64_MIN, seekTs, seekTs, AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(in.get(), vIdx, seekTs, AVSEEK_FLAG_BACKWARD);

    const Ticks requestedOut = seg.requestedOut;
    const Ticks halfFrame = frameDuration(vs->avg_frame_rate.num > 0 ? vs->avg_frame_rate : vs->r_frame_rate) / 2;

    // --- video state ---
    bool vStarted = !copyVideo; // encode path starts by time, not by key frame
    bool vDone = false;
    Ticks segStart = copyVideo ? seg.actualIn : seg.requestedIn; // media time mapped to m_cursor
    Ticks vEnd = segStart;     // end of the last written video frame (media time)
    av::CodecContextPtr vdec;
    if (!copyVideo) {
        vdec = av::openDecoder(vs, &err, 0);
        if (!vdec)
            return fail(TimelineExporter::tr("Cannot decode %1: %2").arg(seg.path, err));
    }

    // --- audio state ---
    const bool copyAudio = m_plan.audio == AudioStrategy::Copy && as;
    const bool encodeAudio = m_plan.audio == AudioStrategy::Encode && !m_plan.audioMixed;
    if (m_mixer) {
        // Timeline position of this segment's first output sample.
        const Clip* clip = m_plan.timeline.findClip(seg.clipId);
        m_mixOffset = clip ? clip->timelineStart + (segStart - clip->sourceIn) - m_cursor : 0;
    }
    bool aDone = !(copyAudio || (encodeAudio && as));
    std::deque<av::PacketPtr> audioQueue; // copy path: packets waiting for the video end to be known
    av::CodecContextPtr adec;
    av::SwrPtr aswr;
    const int encRate = m_aenc ? m_aenc->sample_rate : 0;
    // Samples that this segment must contribute (exact, drift-free).
    std::int64_t segAudioStartSample = m_audioSamplesQueued;
    std::int64_t segAudioWritten = 0;
    if (encodeAudio && as) {
        adec = av::openDecoder(as, &err, 1);
        if (!adec)
            return fail(TimelineExporter::tr("Cannot decode the audio of %1: %2").arg(seg.path, err));
        AVChannelLayout inLayout;
        if (adec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&inLayout, adec->ch_layout.nb_channels);
        else
            av_channel_layout_copy(&inLayout, &adec->ch_layout);
        SwrContext* swr = nullptr;
        int r = swr_alloc_set_opts2(&swr, &m_aenc->ch_layout, AV_SAMPLE_FMT_FLTP, encRate, &inLayout,
                                    adec->sample_fmt, adec->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&inLayout);
        if (r < 0 || swr_init(swr) < 0) {
            swr_free(&swr);
            return fail(TimelineExporter::tr("Cannot create the audio converter."));
        }
        aswr.reset(swr);
    }

    auto audioMid = [&](const AVPacket* p) {
        Ticks t = packetTime(p, as->time_base, origin);
        return t + ticksFromStream(p->duration, as->time_base) / 2;
    };
    auto writeAudioCopy = [&](AVPacket* p) -> bool {
        const Ticks t = packetTime(p, as->time_base, origin);
        const Ticks outT = m_cursor + (t - segStart);
        const std::int64_t delta = (p->dts != AV_NOPTS_VALUE && p->pts != AV_NOPTS_VALUE) ? p->pts - p->dts : 0;
        p->pts = ticksToStream(outT, outAtb);
        p->dts = p->pts - av_rescale_q(delta, as->time_base, outAtb);
        p->duration = av_rescale_q(p->duration, as->time_base, outAtb);
        return writePacket(p, m_outAudio, m_lastAudioDts);
    };
    // Flushes queued audio that lies inside [segStart, limit). When `final`
    // is set, everything at or past `limit` is dropped and audio is done.
    auto drainAudioQueue = [&](Ticks limit, bool final) -> bool {
        while (!audioQueue.empty()) {
            AVPacket* p = audioQueue.front().get();
            const Ticks mid = audioMid(p);
            if (mid < segStart) {
                audioQueue.pop_front();
                continue;
            }
            if (mid >= limit) {
                if (final) {
                    audioQueue.clear();
                    aDone = true;
                }
                return true;
            }
            if (!writeAudioCopy(p))
                return false;
            audioQueue.pop_front();
        }
        return true;
    };

    // Audio decode path: convert a decoded frame and place its samples at the
    // exact output position, padding gaps with silence and trimming overlaps.
    const std::int64_t segAudioTotal =
        encodeAudio ? av_rescale(m_cursor + (requestedOut - segStart), encRate, kTicksPerSecond) - segAudioStartSample
                    : 0;
    auto handleDecodedAudio = [&](AVFrame* f) -> bool {
        const std::int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        if (ts == AV_NOPTS_VALUE)
            return true;
        const Ticks t = ticksFromStream(ts, as->time_base) - origin;
        const int maxOut = swr_get_out_samples(aswr.get(), f->nb_samples);
        std::vector<std::vector<float>> buf(m_audioChannels, std::vector<float>(std::max(maxOut, 1)));
        std::vector<uint8_t*> outPtrs(m_audioChannels);
        for (int ch = 0; ch < m_audioChannels; ++ch)
            outPtrs[ch] = reinterpret_cast<uint8_t*>(buf[ch].data());
        const int got = swr_convert(aswr.get(), outPtrs.data(), maxOut, const_cast<const uint8_t**>(f->extended_data),
                                    f->nb_samples);
        if (got <= 0)
            return true;
        // Position of the first converted sample relative to the segment start.
        std::int64_t pos = av_rescale(t - segStart, encRate, kTicksPerSecond);
        int skip = 0;
        int count = got;
        if (pos < segAudioWritten) {
            skip = static_cast<int>(std::min<std::int64_t>(segAudioWritten - pos, got));
            count -= skip;
            pos += skip;
        } else if (pos > segAudioWritten) {
            const std::int64_t gap = std::min(pos, segAudioTotal) - segAudioWritten;
            if (gap > 0 && !pushSilence(static_cast<int>(gap)))
                return false;
            segAudioWritten += std::max<std::int64_t>(gap, 0);
        }
        count = static_cast<int>(std::min<std::int64_t>(count, segAudioTotal - segAudioWritten));
        if (count > 0) {
            std::vector<const float*> ptrs(m_audioChannels);
            for (int ch = 0; ch < m_audioChannels; ++ch)
                ptrs[ch] = buf[ch].data() + skip;
            if (!pushAudioSamples(ptrs.data(), count))
                return false;
            segAudioWritten += count;
        }
        if (segAudioWritten >= segAudioTotal)
            aDone = true;
        return true;
    };

    auto handleDecodedVideo = [&](AVFrame* f) -> bool {
        const std::int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
        const Ticks t = (ts != AV_NOPTS_VALUE ? ticksFromStream(ts, vs->time_base) : vEnd) - origin;
        const Ticks dur = f->duration > 0 ? ticksFromStream(f->duration, vs->time_base) : 2 * halfFrame;
        if (t + halfFrame <= seg.requestedIn) // frame lies before the in point
            return true;
        if (t + halfFrame > requestedOut) { // first frame past the out point
            vDone = true;
            return true;
        }
        AVFrame* toEncode = f;
        AVCodecContext* c = m_venc.get();
        if (f->format != c->pix_fmt || f->width != c->width || f->height != c->height) {
            if (!m_convFrame) {
                m_convFrame = av::makeFrame();
                m_convFrame->format = c->pix_fmt;
                m_convFrame->width = c->width;
                m_convFrame->height = c->height;
                av_frame_get_buffer(m_convFrame.get(), 0);
            }
            SwsContext* sws = sws_getCachedContext(m_sws.release(), f->width, f->height,
                                                   static_cast<AVPixelFormat>(f->format), c->width, c->height,
                                                   c->pix_fmt,
                                                   SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_BITEXACT,
                                                   nullptr, nullptr, nullptr);
            m_sws.reset(sws);
            if (!sws)
                return fail(TimelineExporter::tr("Cannot convert the video frames."));
            av_frame_make_writable(m_convFrame.get());
            sws_scale(m_sws.get(), f->data, f->linesize, 0, f->height, m_convFrame->data, m_convFrame->linesize);
            av_frame_copy_props(m_convFrame.get(), f);
            toEncode = m_convFrame.get();
        }
        std::int64_t pts = av_rescale_q(m_cursor + (t - segStart), kTickTimeBase, c->time_base);
        if (m_lastVideoPts != AV_NOPTS_VALUE && pts <= m_lastVideoPts)
            pts = m_lastVideoPts + 1;
        m_lastVideoPts = pts;
        toEncode->pts = pts;
        toEncode->duration = std::max<std::int64_t>(1, av_rescale_q(dur, kTickTimeBase, c->time_base));
        toEncode->pict_type = AV_PICTURE_TYPE_NONE;
        vEnd = std::max(vEnd, t + dur);
        report(m_cursor + (t - segStart));
        return encodeVideoFrame(toEncode);
    };

    auto pkt = av::makePacket();
    auto frame = av::makeFrame();
    Ticks firstKey = kInvalidTicks;
    bool eof = false;
    while (!(vDone && aDone)) {
        if (cancelled())
            return false;
        int ret = av_read_frame(in.get(), pkt.get());
        if (ret < 0) {
            eof = true;
            break;
        }
        if (pkt->stream_index == vIdx && !vDone) {
            const Ticks t = packetTime(pkt.get(), vs->time_base, origin);
            if (copyVideo) {
                if (!vStarted) {
                    if ((pkt->flags & AV_PKT_FLAG_KEY) && t != kInvalidTicks && t + halfFrame >= seg.actualIn) {
                        vStarted = true;
                        firstKey = t;
                        segStart = t;
                        vEnd = t;
                    }
                }
                if (vStarted) {
                    if (t != kInvalidTicks && t < firstKey) {
                        // Leading picture of an open GOP: it references the
                        // previous GOP, which is not part of the cut.
                    } else if (t != kInvalidTicks && t + halfFrame > requestedOut) {
                        // First packet (in decode order) at or past the out
                        // point: everything after may depend on it, so stop
                        // here and never write a frame that cannot decode.
                        vDone = true;
                        if (copyAudio && !drainAudioQueue(vEnd, true))
                            return false;
                    } else {
                        const Ticks dur = pkt->duration > 0 ? ticksFromStream(pkt->duration, vs->time_base)
                                                            : 2 * halfFrame;
                        const std::int64_t ptsDelta =
                            (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) ? pkt->pts - pkt->dts : 0;
                        const Ticks outT = m_cursor + (t - segStart);
                        pkt->pts = ticksToStream(outT, outVtb);
                        pkt->dts = pkt->pts - av_rescale_q(ptsDelta, vs->time_base, outVtb);
                        pkt->duration = av_rescale_q(pkt->duration, vs->time_base, outVtb);
                        if (!writePacket(pkt.get(), m_outVideo, m_lastVideoDts))
                            return false;
                        vEnd = std::max(vEnd, t + dur);
                        report(m_cursor + (t - segStart));
                        if (copyAudio && !drainAudioQueue(vEnd, false))
                            return false;
                    }
                }
            } else {
                ret = avcodec_send_packet(vdec.get(), pkt.get());
                while (ret >= 0 && !vDone) {
                    ret = avcodec_receive_frame(vdec.get(), frame.get());
                    if (ret < 0)
                        break;
                    if (!handleDecodedVideo(frame.get()))
                        return false;
                    av_frame_unref(frame.get());
                }
            }
        } else if (as && pkt->stream_index == aIdx && !aDone) {
            if (copyAudio) {
                if (!copyVideo) {
                    const Ticks mid = audioMid(pkt.get());
                    if (mid >= requestedOut)
                        aDone = true;
                    else if (mid >= segStart && !writeAudioCopy(pkt.get()))
                        return false;
                } else if (vDone) {
                    if (audioMid(pkt.get()) >= vEnd)
                        aDone = true;
                    else if (audioMid(pkt.get()) >= segStart && !writeAudioCopy(pkt.get()))
                        return false;
                } else {
                    audioQueue.push_back(av::PacketPtr(av_packet_clone(pkt.get())));
                    if (vStarted && !drainAudioQueue(vEnd, false))
                        return false;
                }
            } else if (encodeAudio) {
                ret = avcodec_send_packet(adec.get(), pkt.get());
                while (ret >= 0 && !aDone) {
                    ret = avcodec_receive_frame(adec.get(), frame.get());
                    if (ret < 0)
                        break;
                    if (!handleDecodedAudio(frame.get()))
                        return false;
                    av_frame_unref(frame.get());
                }
            }
        }
        av_packet_unref(pkt.get());
    }

    if (eof) {
        // Drain decoders.
        if (vdec && !vDone) {
            avcodec_send_packet(vdec.get(), nullptr);
            while (!vDone && avcodec_receive_frame(vdec.get(), frame.get()) >= 0) {
                if (!handleDecodedVideo(frame.get()))
                    return false;
                av_frame_unref(frame.get());
            }
        }
        if (adec && !aDone) {
            avcodec_send_packet(adec.get(), nullptr);
            while (!aDone && avcodec_receive_frame(adec.get(), frame.get()) >= 0) {
                if (!handleDecodedAudio(frame.get()))
                    return false;
                av_frame_unref(frame.get());
            }
        }
        vDone = true;
        if (copyAudio && !drainAudioQueue(vEnd, true))
            return false;
    }

    if (copyVideo) {
        segmentLength = vEnd - segStart;
        if (!vStarted)
            return fail(TimelineExporter::tr("No key frame found in %1.").arg(QFileInfo(seg.path).fileName()));
    } else {
        segmentLength = requestedOut - seg.requestedIn;
    }
    if (m_mixer && !pumpMix(m_cursor + segmentLength))
        return false;
    // Keep encoded audio exactly as long as the segment.
    if (encodeAudio) {
        const std::int64_t target =
            av_rescale(m_cursor + segmentLength, encRate, kTicksPerSecond) - segAudioStartSample;
        if (target > segAudioWritten && !pushSilence(static_cast<int>(target - segAudioWritten)))
            return false;
    }
    return true;
}

} // namespace

ExportResult TimelineExporter::run(const ExportPlan& plan, const ExportSettings& settings, const ProgressFn& progress,
                                   const std::atomic_bool* cancel) {
    ExportResult r;
    if (plan.video == VideoStrategy::Render) {
        r = runRenderExport(plan, settings, progress, cancel);
    } else {
        Session session(plan, settings, progress, cancel);
        r = session.run();
    }
    if (r.ok && !plan.sidecarSubtitles.empty()) {
        QString err;
        const QStringList files = SubtitleStreams::writeSidecars(plan, settings.outputPath, &err);
        if (!err.isEmpty())
            r.notes << tr("The subtitle file could not be written: %1").arg(err);
        for (const auto& f : files)
            r.notes << tr("Subtitles saved to %1").arg(QFileInfo(f).fileName());
    }
    return r;
}

} // namespace lumora

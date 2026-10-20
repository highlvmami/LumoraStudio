#include "AudioDecoder.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace lumora {

bool AudioDecoder::open(const QString& path, int outSampleRate, int outChannels, QString* error) {
    m_outRate = outSampleRate;
    m_outChannels = outChannels;
    m_fmt = av::openInput(path, error);
    if (!m_fmt)
        return false;
    const int idx = av_find_best_stream(m_fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (idx < 0) {
        if (error)
            *error = QStringLiteral("No audio stream");
        return false;
    }
    m_stream = m_fmt->streams[idx];
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i)
        m_fmt->streams[i]->discard = (static_cast<int>(i) == idx) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    m_codec = av::openDecoder(m_stream, error, 1);
    if (!m_codec)
        return false;
    m_origin = m_fmt->start_time != AV_NOPTS_VALUE ? ticksFromStream(m_fmt->start_time, AVRational{1, AV_TIME_BASE}) : 0;

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, m_outChannels);
    AVChannelLayout inLayout;
    if (m_codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&inLayout, m_codec->ch_layout.nb_channels);
    else
        av_channel_layout_copy(&inLayout, &m_codec->ch_layout);
    SwrContext* swr = nullptr;
    int ret = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, m_outRate, &inLayout, m_codec->sample_fmt,
                                  m_codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    if (ret < 0 || swr_init(swr) < 0) {
        swr_free(&swr);
        if (error)
            *error = QStringLiteral("Cannot create audio resampler");
        return false;
    }
    m_swr.reset(swr);
    m_pkt = av::makePacket();
    m_frame = av::makeFrame();
    return true;
}

bool AudioDecoder::seek(Ticks t) {
    if (!m_fmt)
        return false;
    const std::int64_t ts = ticksToStreamDown(t + m_origin, m_stream->time_base);
    int ret = avformat_seek_file(m_fmt.get(), m_stream->index, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codec.get());
    // Drop any samples buffered in the resampler.
    swr_init(m_swr.get());
    m_eofSent = false;
    m_nextPts = kInvalidTicks;
    return ret >= 0;
}

bool AudioDecoder::next(std::vector<float>& samples, Ticks& start) {
    while (true) {
        int ret = avcodec_receive_frame(m_codec.get(), m_frame.get());
        if (ret == 0) {
            const std::int64_t ts = m_frame->best_effort_timestamp != AV_NOPTS_VALUE ? m_frame->best_effort_timestamp
                                                                                     : m_frame->pts;
            Ticks pts = ts != AV_NOPTS_VALUE ? ticksFromStream(ts, m_stream->time_base) - m_origin : m_nextPts;
            if (pts == kInvalidTicks)
                pts = 0;
            const int maxOut = swr_get_out_samples(m_swr.get(), m_frame->nb_samples);
            samples.resize(static_cast<size_t>(maxOut) * m_outChannels);
            uint8_t* out[1] = {reinterpret_cast<uint8_t*>(samples.data())};
            const int got = swr_convert(m_swr.get(), out, maxOut, const_cast<const uint8_t**>(m_frame->extended_data),
                                        m_frame->nb_samples);
            av_frame_unref(m_frame.get());
            if (got <= 0)
                continue;
            samples.resize(static_cast<size_t>(got) * m_outChannels);
            start = pts;
            m_nextPts = pts + av_rescale(got, kTicksPerSecond, m_outRate);
            return true;
        }
        if (ret == AVERROR_EOF)
            return false;
        if (ret != AVERROR(EAGAIN))
            return false;
        if (m_eofSent)
            return false;
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

} // namespace lumora

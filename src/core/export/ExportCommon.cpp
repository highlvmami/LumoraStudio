#include "ExportCommon.h"

#include <QStringList>
#include <algorithm>
#include <cstdlib>

namespace lumora::exportutil {

std::vector<AVPixelFormat> supportedPixelFormats(const AVCodecContext* ctx, const AVCodec* codec) {
    std::vector<AVPixelFormat> out;
    const AVPixelFormat* list = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &n) >= 0 && cfg) {
        list = static_cast<const AVPixelFormat*>(cfg);
        out.assign(list, list + n);
    }
#else
    (void)ctx;
    list = codec->pix_fmts;
    for (; list && *list != AV_PIX_FMT_NONE; ++list)
        out.push_back(*list);
#endif
    return out;
}

std::vector<AVSampleFormat> supportedSampleFormats(const AVCodecContext* ctx, const AVCodec* codec) {
    std::vector<AVSampleFormat> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &cfg, &n) >= 0 && cfg) {
        auto list = static_cast<const AVSampleFormat*>(cfg);
        out.assign(list, list + n);
    }
#else
    (void)ctx;
    for (auto list = codec->sample_fmts; list && *list != AV_SAMPLE_FMT_NONE; ++list)
        out.push_back(*list);
#endif
    return out;
}

std::vector<int> supportedSampleRates(const AVCodecContext* ctx, const AVCodec* codec) {
    std::vector<int> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, &cfg, &n) >= 0 && cfg) {
        auto list = static_cast<const int*>(cfg);
        out.assign(list, list + n);
    }
#else
    (void)ctx;
    for (auto list = codec->supported_samplerates; list && *list; ++list)
        out.push_back(*list);
#endif
    return out;
}

AVPixelFormat choosePixelFormat(const AVCodecContext* ctx, const AVCodec* codec, AVPixelFormat source) {
    auto list = supportedPixelFormats(ctx, codec);
    if (list.empty())
        return source;
    if (std::find(list.begin(), list.end(), source) != list.end())
        return source;
    list.push_back(AV_PIX_FMT_NONE);
    // Pick the format that loses least (bit depth, chroma, alpha).
    return avcodec_find_best_pix_fmt_of_list(list.data(), source, 0, nullptr);
}

void copySideData(const AVCodecParameters* from, AVCodecParameters* to) {
    for (int i = 0; i < from->nb_coded_side_data; ++i) {
        const AVPacketSideData& sd = from->coded_side_data[i];
        if (av_packet_side_data_get(to->coded_side_data, to->nb_coded_side_data, sd.type))
            continue;
        auto* copy = static_cast<uint8_t*>(av_memdup(sd.data, sd.size));
        if (!copy)
            continue;
        if (!av_packet_side_data_add(&to->coded_side_data, &to->nb_coded_side_data, sd.type, copy, sd.size, 0))
            av_free(copy);
    }
}

bool sameContainerFamily(const AVFormatContext* in, const AVOutputFormat* out) {
    const QString inName = QString::fromUtf8(in->iformat->name);
    const QString outName = QString::fromUtf8(out->name);
    if (inName.contains(QLatin1String("mp4")) || inName.contains(QLatin1String("mov")))
        return outName == QLatin1String("mp4") || outName == QLatin1String("mov") || outName == QLatin1String("ipod");
    return inName.split(QLatin1Char(',')).contains(outName);
}

Ticks packetTime(const AVPacket* pkt, AVRational tb, Ticks origin) {
    const std::int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    if (ts == AV_NOPTS_VALUE)
        return kInvalidTicks;
    return ticksFromStream(ts, tb) - origin;
}

int chooseSampleRate(const AVCodecContext* ctx, const AVCodec* codec, int rate) {
    const auto rates = supportedSampleRates(ctx, codec);
    if (rates.empty() || std::find(rates.begin(), rates.end(), rate) != rates.end())
        return rate;
    int best = rates.front();
    for (int r : rates)
        if (std::abs(r - rate) < std::abs(best - rate))
            best = r;
    return best;
}

} // namespace lumora::exportutil

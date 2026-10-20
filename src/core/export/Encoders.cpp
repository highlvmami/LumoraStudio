#include "Encoders.h"

#include <QDir>
#include <QHash>
#include <QMutex>
#include <algorithm>

#include "ffmpeg/FFmpeg.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace lumora::encoders {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("Encoders", text); }

bool startsWith(const QString& s, const char* prefix) { return s.startsWith(QLatin1String(prefix)); }
bool endsWith(const QString& s, const char* suffix) { return s.endsWith(QLatin1String(suffix)); }

bool isNvenc(const QString& e) { return endsWith(e, "_nvenc"); }
bool isQsv(const QString& e) { return endsWith(e, "_qsv"); }
bool isAmf(const QString& e) { return endsWith(e, "_amf"); }
bool isVideoToolbox(const QString& e) { return endsWith(e, "_videotoolbox"); }
bool isVpx(const QString& e) { return startsWith(e, "libvpx"); }

std::vector<AVPixelFormat> supportedFormats(const AVCodec* codec) {
    std::vector<AVPixelFormat> out;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &n) >= 0 && cfg) {
        auto list = static_cast<const AVPixelFormat*>(cfg);
        out.assign(list, list + n);
    }
#else
    for (auto list = codec->pix_fmts; list && *list != AV_PIX_FMT_NONE; ++list)
        out.push_back(*list);
#endif
    // Hardware surface formats need a device context; frames are uploaded
    // by the encoder itself from the software formats.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](AVPixelFormat f) {
                                 const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f);
                                 return !d || (d->flags & AV_PIX_FMT_FLAG_HWACCEL);
                             }),
              out.end());
    return out;
}

int depthOf(int format) {
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(format));
    return d ? d->comp[0].depth : 8;
}

bool isYuv420(const AVPixFmtDescriptor* d) {
    return d && !(d->flags & AV_PIX_FMT_FLAG_RGB) && !(d->flags & AV_PIX_FMT_FLAG_ALPHA) && d->nb_components == 3 &&
           d->log2_chroma_w == 1 && d->log2_chroma_h == 1;
}

QString speedName(const QString& e, int speed) {
    speed = std::clamp(speed, 0, 4);
    if (e == QLatin1String("libx264") || e == QLatin1String("libx265") || isQsv(e)) {
        static const char* names[] = {"veryfast", "faster", "medium", "slow", "veryslow"};
        return QString::fromLatin1(names[speed]);
    }
    if (isNvenc(e)) {
        static const char* names[] = {"p1", "p3", "p4", "p6", "p7"};
        return QString::fromLatin1(names[speed]);
    }
    if (isAmf(e)) {
        static const char* names[] = {"speed", "speed", "balanced", "quality", "quality"};
        return QString::fromLatin1(names[speed]);
    }
    return {};
}

} // namespace

std::vector<VideoCodecChoice> videoCodecs() {
    return {
        {AV_CODEC_ID_H264, QStringLiteral("H.264 / AVC"), false},
        {AV_CODEC_ID_HEVC, QStringLiteral("HEVC / H.265"), false},
        {AV_CODEC_ID_AV1, QStringLiteral("AV1"), false},
        {AV_CODEC_ID_VP9, QStringLiteral("VP9"), false},
        {AV_CODEC_ID_PRORES, QStringLiteral("Apple ProRes"), true},
        {AV_CODEC_ID_DNXHD, QStringLiteral("Avid DNxHR"), true},
        {AV_CODEC_ID_FFV1, tr("FFV1 (lossless)"), true},
    };
}

bool isHardware(const QString& e) { return isNvenc(e) || isQsv(e) || isAmf(e) || isVideoToolbox(e); }

std::vector<EncoderInfo> available(AVCodecID codec) {
    QStringList names;
    switch (codec) {
    case AV_CODEC_ID_H264: names = {"libx264", "h264_nvenc", "h264_qsv", "h264_amf", "h264_videotoolbox"}; break;
    case AV_CODEC_ID_HEVC: names = {"libx265", "hevc_nvenc", "hevc_qsv", "hevc_amf", "hevc_videotoolbox"}; break;
    case AV_CODEC_ID_AV1: names = {"libsvtav1", "libaom-av1", "av1_nvenc", "av1_qsv", "av1_amf"}; break;
    case AV_CODEC_ID_VP9: names = {"libvpx-vp9", "vp9_qsv"}; break;
    case AV_CODEC_ID_PRORES: names = {"prores_ks", "prores_videotoolbox"}; break;
    case AV_CODEC_ID_DNXHD: names = {"dnxhd"}; break;
    case AV_CODEC_ID_FFV1: names = {"ffv1"}; break;
    default: break;
    }
    std::vector<EncoderInfo> out;
    for (const QString& n : names) {
        if (!works(n))
            continue;
        EncoderInfo info;
        info.name = n;
        info.hardware = isHardware(n);
        if (isNvenc(n))
            info.label = QStringLiteral("NVIDIA NVENC");
        else if (isQsv(n))
            info.label = QStringLiteral("Intel Quick Sync");
        else if (isAmf(n))
            info.label = QStringLiteral("AMD AMF");
        else if (isVideoToolbox(n))
            info.label = QStringLiteral("Apple VideoToolbox");
        else if (n == QLatin1String("libx264"))
            info.label = tr("x264 (software)");
        else if (n == QLatin1String("libx265"))
            info.label = tr("x265 (software)");
        else if (n == QLatin1String("libsvtav1"))
            info.label = tr("SVT-AV1 (software)");
        else if (n == QLatin1String("libaom-av1"))
            info.label = tr("libaom (software, slow)");
        else if (n == QLatin1String("libvpx-vp9"))
            info.label = tr("libvpx (software)");
        else
            info.label = tr("%1 (software)").arg(n);
        out.push_back(info);
    }
    return out;
}

bool works(const QString& encoder) {
    static QMutex mutex;
    static QHash<QString, bool> cache;
    QMutexLocker lock(&mutex);
    auto it = cache.constFind(encoder);
    if (it != cache.constEnd())
        return *it;
    const AVCodec* codec = avcodec_find_encoder_by_name(encoder.toUtf8().constData());
    bool ok = codec != nullptr;
    if (ok && isHardware(encoder)) {
        // Present in the build does not mean usable: try to open it.
        av::CodecContextPtr ctx(avcodec_alloc_context3(codec));
        const auto formats = supportedFormats(codec);
        AVPixelFormat fmt = AV_PIX_FMT_NONE;
        for (AVPixelFormat f : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P})
            if (std::find(formats.begin(), formats.end(), f) != formats.end()) {
                fmt = f;
                break;
            }
        if (fmt == AV_PIX_FMT_NONE && !formats.empty())
            fmt = formats.front();
        ctx->width = 640;
        ctx->height = 360;
        ctx->pix_fmt = fmt;
        ctx->time_base = AVRational{1, 25};
        ctx->framerate = AVRational{25, 1};
        ctx->bit_rate = 1000000;
        ok = fmt != AV_PIX_FMT_NONE && avcodec_open2(ctx.get(), codec, nullptr) >= 0;
    }
    cache.insert(encoder, ok);
    return ok;
}

QualityScale qualityScale(const QString& e) {
    QualityScale q;
    if (e == QLatin1String("libx264")) {
        q = {0, 51, 18, 23, true, true};
    } else if (e == QLatin1String("libx265")) {
        q = {0, 51, 20, 26, true, true};
    } else if (e == QLatin1String("libsvtav1") || e == QLatin1String("libaom-av1") || isVpx(e)) {
        q = {1, 63, 24, 32, true, true};
    } else if (isNvenc(e)) {
        q = {1, 51, 19, 25, true, true};
    } else if (isQsv(e)) {
        q = {1, 51, 20, 25, true, true};
    } else if (isAmf(e)) {
        q = {0, 51, 20, 26, true, true};
    } else if (isVideoToolbox(e)) {
        q = {1, 100, 75, 60, false, true};
    } else {
        q.supported = false;
    }
    return q;
}

bool supportsTwoPass(const QString& e) {
    return e == QLatin1String("libx264") || e == QLatin1String("libx265") || isVpx(e) ||
           e == QLatin1String("libaom-av1") || isNvenc(e);
}

bool twoPassNeedsTwoRuns(const QString& e) { return supportsTwoPass(e) && !isNvenc(e); }

bool supportsConstantBitrate(const QString& e) {
    return e != QLatin1String("libsvtav1") && !isVideoToolbox(e) && qualityScale(e).supported;
}

bool hasRateControl(AVCodecID codec) {
    return codec != AV_CODEC_ID_PRORES && codec != AV_CODEC_ID_DNXHD && codec != AV_CODEC_ID_FFV1;
}

std::vector<std::pair<QString, QString>> profiles(AVCodecID codec) {
    if (codec == AV_CODEC_ID_PRORES)
        return {{"proxy", QStringLiteral("422 Proxy")}, {"lt", QStringLiteral("422 LT")},
                {"standard", QStringLiteral("422")},    {"hq", QStringLiteral("422 HQ")},
                {"4444", QStringLiteral("4444")},       {"4444xq", QStringLiteral("4444 XQ")}};
    if (codec == AV_CODEC_ID_DNXHD)
        return {{"lb", QStringLiteral("DNxHR LB")},   {"sq", QStringLiteral("DNxHR SQ")},
                {"hq", QStringLiteral("DNxHR HQ")},   {"hqx", QStringLiteral("DNxHR HQX (10-bit)")},
                {"444", QStringLiteral("DNxHR 444 (10-bit)")}};
    return {};
}

QString defaultProfile(AVCodecID codec) {
    if (codec == AV_CODEC_ID_PRORES || codec == AV_CODEC_ID_DNXHD)
        return QStringLiteral("hq");
    return {};
}

int pixelFormat(const QString& encoder, AVCodecID codec, int sourceFormat, int bitDepth, const QString& profile) {
    const AVCodec* c = avcodec_find_encoder_by_name(encoder.toUtf8().constData());
    if (!c)
        return AV_PIX_FMT_NONE;
    const auto list = supportedFormats(c);
    auto has = [&](AVPixelFormat f) { return std::find(list.begin(), list.end(), f) != list.end(); };
    const AVPixFmtDescriptor* src = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(sourceFormat));
    const bool srcAlpha = src && (src->flags & AV_PIX_FMT_FLAG_ALPHA);

    if (codec == AV_CODEC_ID_PRORES) {
        const QString p = profile.isEmpty() ? defaultProfile(codec) : profile;
        if (p.startsWith(QLatin1String("4444"))) {
            if (srcAlpha && has(AV_PIX_FMT_YUVA444P10LE))
                return AV_PIX_FMT_YUVA444P10LE;
            return has(AV_PIX_FMT_YUV444P10LE) ? AV_PIX_FMT_YUV444P10LE : AV_PIX_FMT_NONE;
        }
        if (has(AV_PIX_FMT_YUV422P10LE))
            return AV_PIX_FMT_YUV422P10LE;
        return has(AV_PIX_FMT_P210LE) ? AV_PIX_FMT_P210LE : AV_PIX_FMT_NONE; // videotoolbox
    }
    if (codec == AV_CODEC_ID_DNXHD) {
        const QString p = profile.isEmpty() ? defaultProfile(codec) : profile;
        if (p == QLatin1String("444"))
            return AV_PIX_FMT_YUV444P10LE;
        if (p == QLatin1String("hqx"))
            return AV_PIX_FMT_YUV422P10LE;
        return AV_PIX_FMT_YUV422P;
    }

    int depth = bitDepth > 0 ? bitDepth : std::clamp(depthOf(sourceFormat), 8, 12);
    if (bitDepth <= 0 && depth == 9)
        depth = 10;
    if (bitDepth <= 0 && depth == 11)
        depth = 12;

    if (codec == AV_CODEC_ID_FFV1) {
        // Lossless: keep the source's chroma and alpha, only change depth when asked.
        std::vector<AVPixelFormat> same;
        for (AVPixelFormat f : list)
            if (depthOf(f) == depth)
                same.push_back(f);
        if (same.empty())
            return AV_PIX_FMT_NONE;
        if (bitDepth <= 0 && has(static_cast<AVPixelFormat>(sourceFormat)))
            return sourceFormat;
        same.push_back(AV_PIX_FMT_NONE);
        return avcodec_find_best_pix_fmt_of_list(same.data(), static_cast<AVPixelFormat>(sourceFormat), srcAlpha,
                                                 nullptr);
    }

    // Delivery codecs: 4:2:0, which every player decodes.
    const AVPixelFormat planar = depth == 8 ? AV_PIX_FMT_YUV420P
                                 : depth == 10 ? AV_PIX_FMT_YUV420P10LE
                                               : AV_PIX_FMT_YUV420P12LE;
    if (has(planar))
        return planar;
    for (AVPixelFormat f : list) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(f);
        if (isYuv420(d) && d->comp[0].depth == depth)
            return f;
    }
    // Some encoders report 10-bit inputs with 16-bit storage (P010 has depth 10).
    return AV_PIX_FMT_NONE;
}

std::vector<int> bitDepths(const QString& encoder, AVCodecID codec, const QString& profile) {
    std::vector<int> out;
    for (int d : {8, 10, 12}) {
        if ((codec == AV_CODEC_ID_PRORES || codec == AV_CODEC_ID_DNXHD)) {
            const int f = pixelFormat(encoder, codec, AV_PIX_FMT_YUV420P, d, profile);
            if (f != AV_PIX_FMT_NONE && depthOf(f) == d)
                out.push_back(d);
            continue;
        }
        if (pixelFormat(encoder, codec, AV_PIX_FMT_YUV420P, d, profile) != AV_PIX_FMT_NONE)
            out.push_back(d);
    }
    return out;
}

QStringList configure(AVCodecContext* c, AVDictionary** opts, const QString& e, const VideoEncodeOptions& o,
                      std::int64_t bitrate, const PassInfo& pass) {
    QStringList notes;
    const AVCodecID codec = c->codec_id;
    auto set = [opts](const char* key, const QString& value) { av_dict_set(opts, key, value.toUtf8().constData(), 0); };
    auto setInt = [opts](const char* key, std::int64_t value) { av_dict_set_int(opts, key, value, 0); };

    // --- intermediates: quality is defined by the profile ---
    if (codec == AV_CODEC_ID_PRORES) {
        const QString p = o.profile.isEmpty() ? defaultProfile(codec) : o.profile;
        if (e == QLatin1String("prores_ks")) {
            set("profile", p);
            set("vendor", QStringLiteral("apl0")); // recognised by Apple software
        } else {
            static const QHash<QString, int> vt = {{"proxy", 0}, {"lt", 1}, {"standard", 2},
                                                   {"hq", 3},    {"4444", 4}, {"4444xq", 5}};
            c->profile = vt.value(p, 3);
        }
        return notes;
    }
    if (codec == AV_CODEC_ID_DNXHD) {
        const QString p = o.profile.isEmpty() ? defaultProfile(codec) : o.profile;
        set("profile", QStringLiteral("dnxhr_") + p);
        return notes;
    }
    if (codec == AV_CODEC_ID_FFV1) {
        setInt("level", 3);
        setInt("slicecrc", 1);
        setInt("slices", 16);
        c->gop_size = 1;
        return notes;
    }

    // --- speed ---
    const int speed = std::clamp(o.speed, 0, 4);
    const QString preset = speedName(e, speed);
    if (!preset.isEmpty())
        set(isAmf(e) ? "quality" : "preset", preset);
    if (e == QLatin1String("libsvtav1")) {
        static const int p[] = {12, 10, 8, 6, 4};
        setInt("preset", p[speed]);
    } else if (e == QLatin1String("libaom-av1")) {
        static const int p[] = {8, 6, 4, 3, 2};
        setInt("cpu-used", p[speed]);
        setInt("row-mt", 1);
    } else if (isVpx(e)) {
        static const int p[] = {5, 4, 2, 1, 0};
        set("deadline", QStringLiteral("good"));
        setInt("cpu-used", p[speed]);
        setInt("row-mt", 1);
    }
    if (isNvenc(e)) {
        set("tune", QStringLiteral("hq"));
        // Smoother quality: B-frames as reference and spatial adaptive quantisation.
        set("b_ref_mode", QStringLiteral("middle"));
        setInt("spatial-aq", 1);
    }

    // --- rate control ---
    RateControl rc = o.rateControl;
    if (rc == RateControl::TargetSize)
        rc = RateControl::VariableBitrate;
    if (rc == RateControl::ConstantBitrate && !supportsConstantBitrate(e)) {
        notes << tr("%1 has no constant bit rate mode; a variable bit rate with the same average is used.").arg(e);
        rc = RateControl::VariableBitrate;
    }
    const QualityScale scale = qualityScale(e);
    if (rc == RateControl::Quality && !scale.supported)
        rc = RateControl::VariableBitrate;

    QString x265;
    auto addX265 = [&x265](const QString& kv) {
        if (!x265.isEmpty())
            x265 += QLatin1Char(':');
        x265 += kv;
    };

    if (rc == RateControl::Quality) {
        const int q = std::clamp(o.quality >= 0 ? o.quality : scale.visuallyLossless, scale.min, scale.max);
        c->bit_rate = 0;
        if (e == QLatin1String("libx264") || e == QLatin1String("libx265") || e == QLatin1String("libsvtav1") ||
            e == QLatin1String("libaom-av1") || isVpx(e)) {
            setInt("crf", q);
        } else if (isNvenc(e)) {
            set("rc", QStringLiteral("vbr"));
            setInt("cq", q);
        } else if (isQsv(e)) {
            c->global_quality = q;
        } else if (isAmf(e)) {
            set("rc", QStringLiteral("cqp"));
            setInt("qp_i", q);
            setInt("qp_p", q);
            setInt("qp_b", q);
        } else if (isVideoToolbox(e)) {
            c->flags |= AV_CODEC_FLAG_QSCALE;
            c->global_quality = q * FF_QP2LAMBDA;
        }
    } else {
        const std::int64_t b = std::max<std::int64_t>(bitrate, 100000);
        c->bit_rate = b;
        if (rc == RateControl::ConstantBitrate) {
            c->rc_min_rate = b;
            c->rc_max_rate = b;
            c->rc_buffer_size = static_cast<int>(std::min<std::int64_t>(b, INT_MAX));
            if (e == QLatin1String("libx264"))
                set("nal-hrd", QStringLiteral("cbr"));
            else if (e == QLatin1String("libx265"))
                addX265(QStringLiteral("strict-cbr=1"));
            else if (isNvenc(e))
                set("rc", QStringLiteral("cbr"));
            else if (isAmf(e))
                set("rc", QStringLiteral("cbr"));
        } else {
            const std::int64_t peak = o.maxBitrate > 0 ? std::max(o.maxBitrate, b) : b * 3 / 2;
            c->rc_max_rate = peak;
            c->rc_buffer_size = static_cast<int>(std::min<std::int64_t>(peak * 2, INT_MAX));
            if (isNvenc(e))
                set("rc", QStringLiteral("vbr"));
            else if (isAmf(e))
                set("rc", QStringLiteral("vbr_peak"));
        }
    }

    // --- two pass ---
    if (pass.pass > 0) {
        if (e == QLatin1String("libx264")) {
            c->flags |= pass.pass == 1 ? AV_CODEC_FLAG_PASS1 : AV_CODEC_FLAG_PASS2;
            set("stats", pass.statsPath);
        } else if (e == QLatin1String("libx265")) {
            // Quoted so that a drive letter's colon is not read as a separator.
            addX265(QStringLiteral("pass=%1").arg(pass.pass));
            addX265(QStringLiteral("stats='%1'").arg(QDir::fromNativeSeparators(pass.statsPath)));
        } else if (isVpx(e) || e == QLatin1String("libaom-av1")) {
            c->flags |= pass.pass == 1 ? AV_CODEC_FLAG_PASS1 : AV_CODEC_FLAG_PASS2;
            if (pass.pass == 2 && pass.stats)
                c->stats_in = av_strdup(pass.stats->constData());
        }
    } else if (o.twoPass && isNvenc(e) && rc != RateControl::Quality) {
        set("multipass", QStringLiteral("fullres"));
    }
    if (!x265.isEmpty())
        set("x265-params", x265);
    return notes;
}

std::int64_t bitrateForSize(double sizeMB, double durationSec, std::int64_t audioBitrate) {
    if (sizeMB <= 0 || durationSec <= 0)
        return 0;
    const double totalBits = sizeMB * 1e6 * 8 * 0.98; // ~2 % container overhead
    const double video = totalBits / durationSec - static_cast<double>(audioBitrate);
    return video < 100000 ? 0 : static_cast<std::int64_t>(video);
}

std::vector<Preset> presets() {
    std::vector<Preset> out;
    auto delivery = [](QSize res, ScaleMode mode, int fps, RateControl rc, std::int64_t bitrate, int quality) {
        VideoEncodeOptions v;
        v.codec = AV_CODEC_ID_H264;
        v.rateControl = rc;
        v.bitrate = bitrate;
        v.quality = quality;
        v.resolution = res;
        v.scaleMode = mode;
        v.frameRate = fps > 0 ? AVRational{fps, 1} : AVRational{0, 1};
        v.bitDepth = 8;
        v.speed = 3;
        return v;
    };
    auto aac = [](std::int64_t bitrate) {
        AudioEncodeOptions a;
        a.codec = AV_CODEC_ID_AAC;
        a.copyWhenPossible = false;
        a.bitrate = bitrate;
        a.sampleRate = 48000;
        return a;
    };
    out.push_back({QStringLiteral("youtube-1080"), tr("YouTube 1080p"), QStringLiteral("mp4"),
                   delivery({1920, 1080}, ScaleMode::Fit, 0, RateControl::Quality, 0, 18), aac(320000)});
    out.push_back({QStringLiteral("youtube-2160"), tr("YouTube 4K"), QStringLiteral("mp4"),
                   delivery({3840, 2160}, ScaleMode::Fit, 0, RateControl::Quality, 0, 18), aac(320000)});
    out.push_back({QStringLiteral("instagram-reels"), tr("Instagram Reels / Stories (9:16)"), QStringLiteral("mp4"),
                   delivery({1080, 1920}, ScaleMode::Fit, 30, RateControl::VariableBitrate, 10000000, -1),
                   aac(256000)});
    out.push_back({QStringLiteral("instagram-feed"), tr("Instagram feed (4:5)"), QStringLiteral("mp4"),
                   delivery({1080, 1350}, ScaleMode::Fit, 30, RateControl::VariableBitrate, 8000000, -1),
                   aac(256000)});
    out.push_back({QStringLiteral("tiktok"), tr("TikTok (9:16)"), QStringLiteral("mp4"),
                   delivery({1080, 1920}, ScaleMode::Fit, 30, RateControl::VariableBitrate, 12000000, -1),
                   aac(256000)});
    out.push_back({QStringLiteral("x"), tr("X (Twitter)"), QStringLiteral("mp4"),
                   delivery({1920, 1080}, ScaleMode::Inside, 0, RateControl::VariableBitrate, 12000000, -1),
                   aac(192000)});
    {
        VideoEncodeOptions v;
        v.codec = AV_CODEC_ID_HEVC;
        v.rateControl = RateControl::Quality;
        v.quality = 20;
        v.bitDepth = 10;
        v.speed = 3;
        AudioEncodeOptions a;
        a.codec = AV_CODEC_ID_FLAC;
        a.copyWhenPossible = true;
        out.push_back({QStringLiteral("archive-hevc"), tr("Archive (HEVC 10-bit, original size)"),
                       QStringLiteral("mkv"), v, a});
    }
    {
        VideoEncodeOptions v;
        v.codec = AV_CODEC_ID_PRORES;
        v.profile = QStringLiteral("hq");
        AudioEncodeOptions a;
        a.codec = AV_CODEC_ID_PCM_S24LE;
        a.copyWhenPossible = false;
        out.push_back({QStringLiteral("prores-hq"), tr("Apple ProRes 422 HQ (editing)"), QStringLiteral("mov"), v, a});
        v.codec = AV_CODEC_ID_DNXHD;
        out.push_back({QStringLiteral("dnxhr-hq"), tr("Avid DNxHR HQ (editing)"), QStringLiteral("mov"), v, a});
    }
    return out;
}

} // namespace lumora::encoders

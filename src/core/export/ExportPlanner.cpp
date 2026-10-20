#include "ExportPlanner.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <map>

#include "ffmpeg/FFmpeg.h"

namespace lumora {

namespace {

struct StreamSignature {
    AVCodecID codec = AV_CODEC_ID_NONE;
    int width = 0, height = 0, format = -1, profile = 0, sampleRate = 0, channels = 0;
    QByteArray extradata;
    bool operator==(const StreamSignature&) const = default;
};

struct SourceStreams {
    bool ok = false;
    StreamSignature video;
    StreamSignature audio;
    bool hasAudio = false;
    bool videoHasBFrames = false;
    int videoDelay = 0;
};

SourceStreams inspect(const QString& path) {
    SourceStreams s;
    auto fmt = av::openInput(path);
    if (!fmt)
        return s;
    const int v = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int a = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, v, nullptr, 0);
    if (v < 0)
        return s;
    const AVCodecParameters* vp = fmt->streams[v]->codecpar;
    s.video = {vp->codec_id, vp->width, vp->height, vp->format, vp->profile, 0, 0,
               QByteArray(reinterpret_cast<const char*>(vp->extradata), vp->extradata_size)};
    s.videoDelay = vp->video_delay;
    s.videoHasBFrames = vp->video_delay > 0;
    if (a >= 0) {
        const AVCodecParameters* ap = fmt->streams[a]->codecpar;
        s.audio = {ap->codec_id, 0, 0, ap->format, ap->profile, ap->sample_rate, ap->ch_layout.nb_channels,
                   QByteArray(reinterpret_cast<const char*>(ap->extradata), ap->extradata_size)};
        s.hasAudio = true;
    }
    s.ok = true;
    return s;
}

const AVOutputFormat* guessFormat(const QString& path) {
    const std::string p = path.toStdString();
    return av_guess_format(nullptr, p.c_str(), nullptr);
}

bool containerSupports(const AVOutputFormat* ofmt, AVCodecID id) {
    if (!ofmt)
        return false;
    const int r = avformat_query_codec(ofmt, id, FF_COMPLIANCE_NORMAL);
    return r != 0; // negative = unknown, assume yes
}

} // namespace

Ticks previousKeyframe(const std::vector<Ticks>& keyframes, Ticks t, Ticks tolerance) {
    if (keyframes.empty())
        return t;
    auto it = std::upper_bound(keyframes.begin(), keyframes.end(), t + tolerance);
    if (it == keyframes.begin())
        return keyframes.front() <= t + tolerance ? keyframes.front() : std::min(t, keyframes.front());
    return *(it - 1);
}

QStringList ExportPlanner::encodersFor(int codecId) {
    QStringList candidates;
    switch (codecId) {
    case AV_CODEC_ID_H264: candidates = {"libx264", "h264_nvenc", "h264_qsv", "h264_amf", "h264_mf"}; break;
    case AV_CODEC_ID_HEVC: candidates = {"libx265", "hevc_nvenc", "hevc_qsv", "hevc_amf", "hevc_mf"}; break;
    case AV_CODEC_ID_AV1: candidates = {"libsvtav1", "libaom-av1", "av1_nvenc", "av1_qsv", "av1_amf"}; break;
    case AV_CODEC_ID_VP9: candidates = {"libvpx-vp9"}; break;
    case AV_CODEC_ID_VP8: candidates = {"libvpx"}; break;
    case AV_CODEC_ID_PRORES: candidates = {"prores_ks", "prores"}; break;
    case AV_CODEC_ID_DNXHD: candidates = {"dnxhd"}; break;
    case AV_CODEC_ID_MPEG4: candidates = {"mpeg4", "libxvid"}; break;
    case AV_CODEC_ID_MJPEG: candidates = {"mjpeg"}; break;
    case AV_CODEC_ID_FFV1: candidates = {"ffv1"}; break;
    case AV_CODEC_ID_MPEG2VIDEO: candidates = {"mpeg2video"}; break;
    case AV_CODEC_ID_AAC: candidates = {"aac", "libfdk_aac"}; break;
    case AV_CODEC_ID_MP3: candidates = {"libmp3lame"}; break;
    case AV_CODEC_ID_OPUS: candidates = {"libopus", "opus"}; break;
    case AV_CODEC_ID_VORBIS: candidates = {"libvorbis", "vorbis"}; break;
    case AV_CODEC_ID_FLAC: candidates = {"flac"}; break;
    case AV_CODEC_ID_AC3: candidates = {"ac3"}; break;
    case AV_CODEC_ID_EAC3: candidates = {"eac3"}; break;
    case AV_CODEC_ID_PCM_S16LE: candidates = {"pcm_s16le"}; break;
    case AV_CODEC_ID_PCM_S24LE: candidates = {"pcm_s24le"}; break;
    case AV_CODEC_ID_ALAC: candidates = {"alac"}; break;
    default: {
        if (const AVCodec* c = avcodec_find_encoder(static_cast<AVCodecID>(codecId)))
            candidates = {QString::fromUtf8(c->name)};
    }
    }
    QStringList available;
    for (const auto& name : candidates) {
        // Hardware encoders are left out of "match source" in 0.1: their
        // quality controls are added together with the export settings in 0.2.
        if (name.contains(QLatin1String("_nvenc")) || name.contains(QLatin1String("_qsv")) ||
            name.contains(QLatin1String("_amf")) || name.contains(QLatin1String("_mf")))
            continue;
        if (avcodec_find_encoder_by_name(name.toUtf8().constData()))
            available << name;
    }
    return available;
}

QString ExportPlanner::suggestedExtension(ExportMode mode, const QString& sourcePath) {
    if (mode == ExportMode::LosslessIntermediate)
        return QStringLiteral("mkv");
    QString ext = QFileInfo(sourcePath).suffix().toLower();
    if (ext.isEmpty())
        ext = QStringLiteral("mkv");
    if (ext == QLatin1String("m4v"))
        ext = QStringLiteral("mp4");
    if (ext == QLatin1String("mts") || ext == QLatin1String("m2ts"))
        ext = QStringLiteral("ts");
    return ext;
}

ExportPlan ExportPlanner::plan(const TimelineState& tl, const MediaPool& pool, const ExportSettings& settings) {
    ExportPlan plan;
    plan.mode = settings.mode;
    if (tl.tracks.empty() || tl.tracks[0].clips.empty()) {
        plan.blockers << tr("The timeline is empty.");
        return plan;
    }
    const Track& vTrack = tl.tracks[0];
    const Track* aTrack = tl.tracks.size() > 1 ? &tl.tracks[1] : nullptr;
    const AVRational rate = (tl.frameRate.num > 0) ? tl.frameRate : AVRational{25, 1};
    const Ticks fd = frameDuration(rate);

    const Ticks rangeStart = settings.rangeStart == kInvalidTicks ? 0 : settings.rangeStart;
    const Ticks rangeEnd = settings.rangeEnd == kInvalidTicks ? INT64_MAX : settings.rangeEnd;

    bool unlinkedAudio = false;
    if (aTrack)
        for (const auto& c : aTrack->clips)
            if (c.linkId.isNull())
                unlinkedAudio = true;
    if (unlinkedAudio)
        plan.notes << tr("Audio-only clips on A1 are not exported in version 0.1.");

    // Build segments from the primary storyline.
    for (const auto& c : vTrack.clips) {
        const Ticks s = std::max(c.timelineStart, rangeStart);
        const Ticks e = std::min(c.timelineEnd(), rangeEnd);
        if (e <= s)
            continue;
        auto media = pool.find(c.mediaId);
        if (!media) {
            plan.blockers << tr("A clip refers to media that is missing from the project.");
            continue;
        }
        SegmentPlan seg;
        seg.clipId = c.id;
        seg.mediaId = c.mediaId;
        seg.path = media->path;
        seg.requestedIn = c.toSource(s);
        seg.requestedOut = c.toSource(e);
        seg.actualIn = seg.requestedIn;
        bool linkedAudio = false;
        if (aTrack && !c.linkId.isNull())
            for (const auto& ac : aTrack->clips)
                if (ac.linkId == c.linkId)
                    linkedAudio = true;
        seg.hasAudio = settings.includeAudio && linkedAudio && media->info.audio.has_value();
        plan.segments.push_back(seg);
    }
    if (plan.segments.empty()) {
        plan.blockers << tr("Nothing to export in the selected range.");
        return plan;
    }

    const AVOutputFormat* ofmt = guessFormat(settings.outputPath);
    if (!ofmt) {
        plan.blockers << tr("Unknown output container for \"%1\".").arg(QFileInfo(settings.outputPath).fileName());
        return plan;
    }
    plan.containerName = QString::fromUtf8(ofmt->name);

    // Inspect every distinct source once.
    std::map<QString, SourceStreams> sources;
    for (const auto& seg : plan.segments)
        if (!sources.count(seg.path))
            sources[seg.path] = inspect(seg.path);
    for (const auto& [path, s] : sources)
        if (!s.ok)
            plan.blockers << tr("Cannot read \"%1\".").arg(QFileInfo(path).fileName());
    if (!plan.blockers.isEmpty())
        return plan;

    const SourceStreams& first = sources[plan.segments.front().path];
    bool videoCompatible = true;
    bool audioCompatible = true;
    bool anyAudio = false, allAudio = true;
    for (const auto& seg : plan.segments) {
        const auto& s = sources[seg.path];
        videoCompatible &= s.video == first.video;
        if (seg.hasAudio) {
            anyAudio = true;
            audioCompatible &= s.hasAudio && first.hasAudio && s.audio == first.audio;
        } else {
            allAudio = false;
        }
    }

    // --- Video strategy ---
    if (settings.mode == ExportMode::StreamCopy) {
        plan.video = VideoStrategy::Copy;
        if (!videoCompatible)
            plan.blockers << tr("The clips use different video encodings, so they cannot be joined without "
                                "re-encoding. Choose \"Match source\" or \"Lossless intermediate\".");
        if (!containerSupports(ofmt, first.video.codec))
            plan.blockers << tr("The %1 container cannot hold %2 video without re-encoding.")
                                 .arg(plan.containerName, QString::fromUtf8(avcodec_get_name(first.video.codec)));

        // Snap cut-in points to key frames.
        int shifted = 0;
        Ticks maxShift = 0;
        for (auto& seg : plan.segments) {
            auto media = pool.find(seg.mediaId);
            if (!media->hasKeyframes())
                MediaPool::scanKeyframes(media);
            const auto kf = media->keyframesCopy();
            seg.actualIn = previousKeyframe(kf, seg.requestedIn, fd / 2);
            if (seg.actualIn > seg.requestedIn)
                seg.actualIn = seg.requestedIn; // within tolerance: treat as exact
            const Ticks shift = seg.requestedIn - seg.actualIn;
            seg.keyframeShiftFrames = static_cast<int>(std::llround(static_cast<double>(shift) / fd));
            if (seg.keyframeShiftFrames > 0) {
                ++shifted;
                maxShift = std::max(maxShift, shift);
            }
        }
        if (shifted > 0)
            plan.notes << tr("%n cut(s) do not start on a key frame. Without re-encoding the clip must start at "
                             "the previous key frame, so up to %1 s extra is included at the start. Use \"Snap cuts "
                             "to key frames\" to see exactly what will be exported.",
                             nullptr, shifted)
                              .arg(ticksToSeconds(maxShift), 0, 'f', 2);
        if (first.videoHasBFrames)
            plan.notes << tr("The video uses B-frames: an out point that falls inside a frame group can end up to "
                             "%n frame(s) earlier so that no damaged frame is ever written.",
                             nullptr, std::max(1, first.videoDelay));
    } else {
        plan.video = VideoStrategy::Encode;
        AVCodecID target = settings.mode == ExportMode::LosslessIntermediate ? AV_CODEC_ID_FFV1 : first.video.codec;
        QStringList enc = encodersFor(target);
        if (enc.isEmpty() && settings.mode == ExportMode::MatchSource) {
            enc = encodersFor(AV_CODEC_ID_H264);
            if (!enc.isEmpty()) {
                plan.notes << tr("No %1 encoder is available in this FFmpeg build; H.264 is used instead.")
                                  .arg(QString::fromUtf8(avcodec_get_name(target)));
                target = AV_CODEC_ID_H264;
            }
        }
        if (enc.isEmpty()) {
            plan.blockers << tr("No encoder available for %1.").arg(QString::fromUtf8(avcodec_get_name(target)));
        } else {
            plan.videoEncoder = enc.front();
            if (!containerSupports(ofmt, target))
                plan.blockers << tr("The %1 container cannot hold %2 video.")
                                     .arg(plan.containerName, QString::fromUtf8(avcodec_get_name(target)));
        }
        if (settings.mode == ExportMode::MatchSource)
            plan.notes << tr("Video is re-encoded with the source's settings (resolution, frame rate, bit depth, "
                             "colour and bit rate). Every re-encode loses a little detail; use the lossless cut "
                             "when you only cut and join.");
    }

    // --- Audio strategy ---
    if (!settings.includeAudio || !anyAudio) {
        plan.audio = AudioStrategy::None;
    } else if (audioCompatible && allAudio && containerSupports(ofmt, first.audio.codec)) {
        plan.audio = AudioStrategy::Copy;
    } else if (settings.mode == ExportMode::StreamCopy) {
        plan.blockers << (allAudio ? tr("The clips use different audio formats, so the audio cannot be copied.")
                                   : tr("Some clips have no audio, so the audio cannot be copied. Turn off audio or "
                                        "choose a mode that re-encodes."));
    } else {
        plan.audio = AudioStrategy::Encode;
        const AVCodecID target =
            settings.mode == ExportMode::LosslessIntermediate ? AV_CODEC_ID_FLAC : first.audio.codec;
        QStringList enc = encodersFor(target);
        if (enc.isEmpty() || !containerSupports(ofmt, target))
            enc = encodersFor(containerSupports(ofmt, AV_CODEC_ID_FLAC) && settings.mode == ExportMode::LosslessIntermediate
                                  ? AV_CODEC_ID_FLAC
                                  : AV_CODEC_ID_AAC);
        if (enc.isEmpty())
            plan.blockers << tr("No audio encoder available.");
        else
            plan.audioEncoder = enc.front();
    }

    for (const auto& seg : plan.segments)
        plan.expectedDuration += seg.requestedOut - seg.actualIn;
    return plan;
}

} // namespace lumora

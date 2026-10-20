#include "ExportPlanner.h"

#include "export/SubtitleStreams.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "color/Lut.h"
#include "export/Encoders.h"
#include "ffmpeg/FFmpeg.h"
#include "render/FrameRenderer.h"

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
    if (tl.tracks.empty() || tl.duration() <= 0) {
        plan.blockers << tr("The timeline is empty.");
        return plan;
    }
    // Audio changes (gain, fades, effects, extra audio clips, master
    // settings) are mixed; the picture can still be copied or encoded clip
    // by clip, so they never cost picture quality.
    const bool mixAudio = settings.includeAudio && tl.hasAudioChanges();
    const bool burn = settings.subtitles == SubtitleMode::BurnIn && tl.hasSubtitles();
    if (settings.mode == ExportMode::Custom || tl.hasVideoEffects() || burn)
        return settings.mode == ExportMode::StreamCopy ? streamCopyBlocked(tl) : planRender(tl, pool, settings);
    if (tl.tracks[0].clips.empty()) {
        if (settings.mode != ExportMode::StreamCopy && mixAudio)
            return planRender(tl, pool, settings);
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
    if (unlinkedAudio && !mixAudio)
        plan.notes << tr("Audio-only clips cannot be joined without re-encoding, so they are left out of a lossless "
                         "cut. Choose another mode to include them.");

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
        seg.hasAudio = settings.includeAudio && !mixAudio && linkedAudio && media->info.audio.has_value();
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
    bool audible = false;
    for (int ti : tl.trackIndices(TrackType::Audio))
        if (!tl.tracks[ti].muted)
            for (const auto& c : tl.tracks[ti].clips)
                if (auto m = pool.find(c.mediaId); m && m->info.audio)
                    audible = true;
    if (mixAudio) {
        if (!audible) {
            plan.audio = AudioStrategy::None;
        } else {
            plan.audio = AudioStrategy::Encode;
            plan.audioMixed = true;
            plan.timeline = tl;
            plan.pool = &pool;
            AVCodecID target = settings.mode == ExportMode::LosslessIntermediate ? AV_CODEC_ID_FLAC
                               : first.hasAudio                                 ? first.audio.codec
                                                                                : AV_CODEC_ID_AAC;
            QStringList enc = encodersFor(target);
            if (enc.isEmpty() || !containerSupports(ofmt, target)) {
                target = plan.containerName == QLatin1String("webm") ? AV_CODEC_ID_OPUS : AV_CODEC_ID_AAC;
                enc = encodersFor(target);
            }
            if (enc.isEmpty()) {
                plan.blockers << tr("No audio encoder available.");
            } else {
                plan.audioEncoder = enc.front();
            }
        }
    } else if (!settings.includeAudio || !anyAudio) {
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
    planSubtitles(plan, tl, settings);
    return plan;
}

void ExportPlanner::planSubtitles(ExportPlan& plan, const TimelineState& tl, const ExportSettings& settings) {
    if (!tl.hasSubtitles() || settings.subtitles == SubtitleMode::Off)
        return;
    if (settings.subtitles == SubtitleMode::BurnIn) {
        if (plan.video == VideoStrategy::Render) {
            plan.burnSubtitles = true;
        }
        return;
    }
    // Timeline -> output time. The render path writes the range as it is;
    // clip by clip exports shift each clip by the frames added before its
    // cut (key frame snapping).
    struct Span {
        Ticks tlStart, tlEnd, outStart;
    };
    std::vector<Span> spans;
    if (plan.video == VideoStrategy::Render) {
        spans.push_back({plan.rangeStart, plan.rangeEnd, 0});
    } else {
        Ticks out = 0;
        for (const auto& seg : plan.segments) {
            const Clip* c = tl.findClip(seg.clipId);
            if (!c)
                continue;
            const Ticks tlStart = c->timelineStart + (seg.requestedIn - c->sourceIn);
            const Ticks shift = seg.requestedIn - seg.actualIn;
            spans.push_back({tlStart, tlStart + (seg.requestedOut - seg.requestedIn), out + shift});
            out += seg.requestedOut - seg.actualIn;
        }
    }
    std::vector<SubtitleStreamPlan> streams;
    for (int ti : tl.trackIndices(TrackType::Subtitle)) {
        const Track& t = tl.tracks[ti];
        if (t.hidden || t.clips.empty())
            continue;
        SubtitleStreamPlan sp;
        sp.language = t.language;
        for (const auto& cue : tl.subtitleCues(ti)) {
            for (const auto& span : spans) {
                const Ticks s = std::max(cue.start, span.tlStart), e = std::min(cue.end, span.tlEnd);
                if (e <= s)
                    continue;
                const Ticks os = span.outStart + (s - span.tlStart), oe = span.outStart + (e - span.tlStart);
                // A cue across a cut continues in the next clip: keep it one cue.
                if (!sp.cues.empty() && sp.cues.back().text == cue.text && std::abs(os - sp.cues.back().end) < kTicksPerSecond / 20)
                    sp.cues.back().end = std::max(sp.cues.back().end, oe);
                else
                    sp.cues.push_back({os, oe, cue.text});
            }
        }
        if (!sp.cues.empty())
            streams.push_back(sp);
    }
    if (streams.empty())
        return;
    if (settings.subtitles == SubtitleMode::Embed) {
        const AVCodecID codec = SubtitleStreams::codecFor(plan.containerName);
        if (codec != AV_CODEC_ID_NONE && avcodec_find_encoder(codec)) {
            plan.subtitleCodec = codec;
            plan.subtitleStreams = streams;
            return;
        }
        plan.notes << tr("The %1 container cannot hold subtitles, so they are saved as .srt next to the video.")
                          .arg(plan.containerName);
    }
    plan.sidecarSubtitles = streams;
}

AVRational ExportPlanner::timelineRate(const TimelineState& tl, const MediaPool& pool) {
    if (tl.frameRate.num > 0 && tl.frameRate.den > 0)
        return tl.frameRate;
    for (int ti : tl.trackIndices(TrackType::Video))
        for (const auto& c : tl.tracks[ti].clips)
            if (auto m = pool.find(c.mediaId); m && m->info.video && m->info.video->frameRate.num > 0)
                return m->info.video->frameRate;
    return AVRational{25, 1};
}

ExportPlan ExportPlanner::streamCopyBlocked(const TimelineState&) {
    ExportPlan plan;
    plan.mode = ExportMode::StreamCopy;
    plan.blockers << tr("The timeline has titles, transitions, colour grading, effects, audio changes or more than one "
                        "track. These "
                        "have to be drawn into the picture, which a lossless cut cannot do. Choose \"Match source\", "
                        "\"Lossless intermediate\" or \"Custom\".");
    return plan;
}

ExportPlan ExportPlanner::planRender(const TimelineState& tl, const MediaPool& pool, const ExportSettings& settings) {
    ExportPlan plan;
    plan.mode = settings.mode;
    plan.video = VideoStrategy::Render;
    plan.timeline = tl;
    plan.pool = &pool;
    plan.rangeStart = settings.rangeStart == kInvalidTicks ? 0 : std::max<Ticks>(0, settings.rangeStart);
    plan.rangeEnd = settings.rangeEnd == kInvalidTicks ? tl.duration() : std::min(tl.duration(), settings.rangeEnd);
    if (plan.rangeEnd <= plan.rangeStart) {
        plan.blockers << tr("Nothing to export in the selected range.");
        return plan;
    }
    const double seconds = ticksToSeconds(plan.rangeEnd - plan.rangeStart);

    // Every clip must have its media.
    for (const auto& t : tl.tracks)
        for (const auto& c : t.clips)
            if (c.kind == ClipKind::Media && !pool.find(c.mediaId)) {
                plan.blockers << tr("A clip refers to media that is missing from the project.");
                return plan;
            }
    // A LUT that cannot be read would silently change the look of the export.
    for (const auto& t : tl.tracks)
        for (const auto& c : t.clips)
            if (c.color.hasLut()) {
                QString err;
                if (!luts::get(c.color.lutPath, &err)) {
                    plan.blockers << tr("The LUT %1 cannot be read: %2").arg(QFileInfo(c.color.lutPath).fileName(), err);
                    return plan;
                }
            }

    const AVOutputFormat* ofmt = guessFormat(settings.outputPath);
    if (!ofmt) {
        plan.blockers << tr("Unknown output container for \"%1\".").arg(QFileInfo(settings.outputPath).fileName());
        return plan;
    }
    plan.containerName = QString::fromUtf8(ofmt->name);

    // The reference source: first video clip of the storyline, else of any track.
    std::optional<VideoStreamInfo> src;
    for (int ti : tl.trackIndices(TrackType::Video)) {
        for (const auto& c : tl.tracks[ti].clips)
            if (auto m = pool.find(c.mediaId); c.kind == ClipKind::Media && m && m->info.video) {
                src = m->info.video;
                break;
            }
        if (src)
            break;
    }

    // --- frame size and rate ---
    const QSize canvas = FrameRenderer::defaultCanvasSize(tl, pool);
    const bool custom = settings.mode == ExportMode::Custom;
    VideoEncodeOptions o = custom ? settings.video : VideoEncodeOptions{};
    auto even = [](int v) { return std::max(2, v & ~1); };
    QSize output = canvas, picture = canvas;
    if (custom && o.resolution.isValid() && !o.resolution.isEmpty()) {
        const QSize wanted(even(o.resolution.width()), even(o.resolution.height()));
        QSize fit = canvas.scaled(wanted, Qt::KeepAspectRatio);
        fit = QSize(even(fit.width()), even(fit.height()));
        if (o.scaleMode == ScaleMode::Fit) {
            output = wanted;
            picture = fit;
        } else {
            picture = (canvas.width() <= wanted.width() && canvas.height() <= wanted.height()) ? canvas : fit;
            output = picture;
        }
    }
    plan.outputSize = output;
    plan.pictureSize = picture;
    plan.pictureOffset = QPoint(((output.width() - picture.width()) / 2) & ~1, ((output.height() - picture.height()) / 2) & ~1);
    plan.outputRate = (custom && o.frameRate.num > 0 && o.frameRate.den > 0) ? o.frameRate : timelineRate(tl, pool);
    if (picture != canvas)
        plan.notes << tr("The picture is scaled from %1×%2 to %3×%4%5.")
                          .arg(canvas.width())
                          .arg(canvas.height())
                          .arg(picture.width())
                          .arg(picture.height())
                          .arg(picture != output ? tr(" with black bars to fill %1×%2").arg(output.width()).arg(output.height())
                                                 : QString());

    // --- video encoder ---
    AVCodecID codec = AV_CODEC_ID_NONE;
    if (settings.mode == ExportMode::LosslessIntermediate) {
        codec = AV_CODEC_ID_FFV1;
        plan.videoEncoder = QStringLiteral("ffv1");
    } else if (settings.mode == ExportMode::MatchSource) {
        codec = src ? src->codecId : AV_CODEC_ID_H264;
        QStringList enc = encodersFor(codec);
        if (enc.isEmpty()) {
            plan.notes << tr("No %1 encoder is available in this FFmpeg build; H.264 is used instead.")
                              .arg(QString::fromUtf8(avcodec_get_name(codec)));
            codec = AV_CODEC_ID_H264;
            enc = encodersFor(codec);
        }
        if (!enc.isEmpty())
            plan.videoEncoder = enc.front();
        plan.videoBitrate = src ? src->bitRate : 0;
    } else {
        codec = o.codec;
        const auto list = encoders::available(codec);
        for (const auto& e : list)
            if (e.name == o.encoder)
                plan.videoEncoder = e.name;
        if (plan.videoEncoder.isEmpty() && !list.empty()) {
            plan.videoEncoder = list.front().name;
            if (!o.encoder.isEmpty())
                plan.notes << tr("The %1 encoder does not work on this computer; %2 is used instead.")
                                  .arg(o.encoder, list.front().label);
        }
    }
    if (plan.videoEncoder.isEmpty()) {
        plan.blockers << tr("No encoder available for %1.").arg(QString::fromUtf8(avcodec_get_name(codec)));
        return plan;
    }
    if (!containerSupports(ofmt, codec))
        plan.blockers << tr("The %1 container cannot hold %2 video.")
                             .arg(plan.containerName, QString::fromUtf8(avcodec_get_name(codec)));
    plan.hardwareEncoder = encoders::isHardware(plan.videoEncoder);
    o.codec = codec;
    o.encoder = plan.videoEncoder;
    if (custom) {
        if (o.profile.isEmpty())
            o.profile = encoders::defaultProfile(codec);
        const int srcFormat = src ? src->pixelFormat : AV_PIX_FMT_YUV420P;
        plan.pixelFormat = encoders::pixelFormat(plan.videoEncoder, codec, srcFormat, o.bitDepth, o.profile);
        if (plan.pixelFormat == AV_PIX_FMT_NONE)
            plan.blockers << tr("The %1 encoder cannot write %2-bit video.").arg(plan.videoEncoder).arg(o.bitDepth);
        if (encoders::hasRateControl(codec)) {
            if (o.rateControl == RateControl::TargetSize) {
                const std::int64_t audio = settings.includeAudio ? (settings.audio.bitrate > 0 ? settings.audio.bitrate : 320000) : 0;
                plan.videoBitrate = encoders::bitrateForSize(o.targetSizeMB, seconds, audio);
                if (plan.videoBitrate <= 0)
                    plan.blockers << tr("%1 MB is too small for %2 s of video.").arg(o.targetSizeMB).arg(seconds, 0, 'f', 1);
            } else if (o.rateControl != RateControl::Quality) {
                plan.videoBitrate = o.bitrate;
                if (plan.videoBitrate <= 0 && src && src->bitRate > 0)
                    plan.videoBitrate = static_cast<std::int64_t>(
                        static_cast<double>(src->bitRate) * output.width() * output.height() /
                        std::max(1, src->width * src->height));
                if (plan.videoBitrate <= 0)
                    plan.videoBitrate = static_cast<std::int64_t>(output.width() * output.height() * av_q2d(plan.outputRate) * 0.1);
            } else if (!encoders::qualityScale(plan.videoEncoder).supported) {
                plan.blockers << tr("The %1 encoder has no constant quality mode; choose a bit rate.").arg(plan.videoEncoder);
            }
            if (o.twoPass && o.rateControl != RateControl::Quality) {
                if (encoders::supportsTwoPass(plan.videoEncoder))
                    plan.twoPass = true;
                else
                    plan.notes << tr("%1 cannot encode in two passes; one pass is used.").arg(plan.videoEncoder);
            }
        }
    }
    plan.videoOptions = o;

    // --- colour description of the output ---
    if (src) {
        plan.colorPrimaries = src->colorPrimaries;
        plan.colorTrc = src->colorTrc;
        plan.colorSpace = src->colorSpace;
        plan.colorRange = src->colorRange;
    }
    if (plan.colorSpace == AVCOL_SPC_UNSPECIFIED || plan.colorSpace == AVCOL_SPC_RGB) {
        const bool hd = (src ? src->height : output.height()) >= 720;
        plan.colorSpace = hd ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        if (plan.colorPrimaries == AVCOL_PRI_UNSPECIFIED)
            plan.colorPrimaries = hd ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        if (plan.colorTrc == AVCOL_TRC_UNSPECIFIED)
            plan.colorTrc = hd ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
    }
    if (plan.colorRange == AVCOL_RANGE_UNSPECIFIED || (custom && codec != AV_CODEC_ID_FFV1))
        plan.colorRange = AVCOL_RANGE_MPEG; // what players expect from delivery files

    // --- audio ---
    // Audio clips that will sound.
    std::vector<const Clip*> audible;
    for (int ti : tl.trackIndices(TrackType::Audio)) {
        if (tl.tracks[ti].muted)
            continue;
        for (const auto& c : tl.tracks[ti].clips)
            if (c.timelineEnd() > plan.rangeStart && c.timelineStart < plan.rangeEnd)
                if (auto m = pool.find(c.mediaId); m && m->info.audio)
                    audible.push_back(&c);
    }
    std::optional<AudioStreamInfo> asrc;
    for (const Clip* c : audible)
        if (!asrc)
            asrc = pool.find(c->mediaId)->info.audio;
    if (!settings.includeAudio || audible.empty()) {
        plan.audio = AudioStrategy::None;
    } else {
        // Copy when the audio is exactly the storyline cut together: only
        // A1, each piece belonging to a storyline clip, no gaps, gain, fades
        // or crossfades, every piece in the same format.
        bool simple = audible.size() > 0;
        const Track& v1 = tl.tracks[0];
        auto dur = [&pool](const QUuid& id) {
            auto m = pool.find(id);
            return m ? m->info.duration : Ticks(0);
        };
        std::vector<SegmentPlan> pieces;
        for (int k = 0; simple && k < static_cast<int>(v1.clips.size()); ++k) {
            const Clip& vc = v1.clips[k];
            const Ticks s = std::max(vc.timelineStart, plan.rangeStart), e = std::min(vc.timelineEnd(), plan.rangeEnd);
            if (e <= s)
                continue;
            if (tl.transitionDuration(k, dur) > 0 || (k + 1 < static_cast<int>(v1.clips.size()) &&
                                                       tl.transitionDuration(k + 1, dur) > 0))
                simple = false;
            const Clip* ac = nullptr;
            for (const Clip* c : audible)
                if (!vc.linkId.isNull() && c->linkId == vc.linkId)
                    ac = c;
            if (!ac || ac->gainDb != 0.0 || ac->fadeIn > 0 || ac->fadeOut > 0 || !ac->audio.isIdentity() ||
                !tl.audioMaster.isIdentity() || ac->timelineStart != vc.timelineStart ||
                ac->duration() != vc.duration() || ac->sourceIn != vc.sourceIn || ac->mediaId != vc.mediaId)
                simple = false;
            if (!simple)
                break;
            SegmentPlan seg;
            seg.clipId = ac->id;
            seg.mediaId = ac->mediaId;
            seg.path = pool.find(ac->mediaId)->path;
            seg.requestedIn = seg.actualIn = ac->toSource(s);
            seg.requestedOut = ac->toSource(e);
            seg.hasAudio = true;
            seg.outputStart = s - plan.rangeStart;
            pieces.push_back(seg);
        }
        Ticks covered = 0;
        for (const auto& p : pieces)
            covered += p.requestedOut - p.requestedIn;
        simple = simple && pieces.size() == audible.size() && covered == plan.rangeEnd - plan.rangeStart;
        if (simple) {
            std::map<QString, SourceStreams> sources;
            for (const auto& p : pieces)
                if (!sources.count(p.path))
                    sources[p.path] = inspect(p.path);
            const SourceStreams& first = sources[pieces.front().path];
            for (const auto& [path, st] : sources)
                simple = simple && st.ok && st.hasAudio && st.audio == first.audio;
            simple = simple && containerSupports(ofmt, first.audio.codec);
        }
        bool allowCopy = settings.mode != ExportMode::Custom ||
                         (settings.audio.copyWhenPossible &&
                          (settings.audio.codec == AV_CODEC_ID_NONE || (asrc && settings.audio.codec == asrc->codecId)) &&
                          settings.audio.bitrate <= 0 &&
                          (settings.audio.sampleRate <= 0 || (asrc && settings.audio.sampleRate == asrc->sampleRate)));
        if (simple && allowCopy) {
            plan.audio = AudioStrategy::Copy;
            plan.segments = pieces;
        } else {
            plan.audio = AudioStrategy::Encode;
            AVCodecID acodec = AV_CODEC_ID_AAC;
            if (settings.mode == ExportMode::LosslessIntermediate)
                acodec = AV_CODEC_ID_FLAC;
            else if (settings.mode == ExportMode::MatchSource && asrc)
                acodec = asrc->codecId;
            else if (custom && settings.audio.codec != AV_CODEC_ID_NONE)
                acodec = settings.audio.codec;
            else if (custom && plan.containerName == QLatin1String("webm"))
                acodec = AV_CODEC_ID_OPUS;
            QStringList enc = encodersFor(acodec);
            if (enc.isEmpty() || !containerSupports(ofmt, acodec)) {
                if (custom && settings.audio.codec != AV_CODEC_ID_NONE) {
                    plan.blockers << tr("The %1 container cannot hold %2 audio.")
                                         .arg(plan.containerName, QString::fromUtf8(avcodec_get_name(acodec)));
                } else {
                    acodec = settings.mode == ExportMode::LosslessIntermediate && containerSupports(ofmt, AV_CODEC_ID_FLAC)
                                 ? AV_CODEC_ID_FLAC
                                 : (plan.containerName == QLatin1String("webm") ? AV_CODEC_ID_OPUS : AV_CODEC_ID_AAC);
                    enc = encodersFor(acodec);
                }
            }
            if (enc.isEmpty())
                plan.blockers << tr("No audio encoder available.");
            else
                plan.audioEncoder = enc.front();
            plan.audioSampleRate = custom && settings.audio.sampleRate > 0 ? settings.audio.sampleRate
                                   : asrc && asrc->sampleRate > 0       ? asrc->sampleRate
                                                                        : 48000;
            plan.audioChannels = asrc ? std::clamp(asrc->channels, 1, 8) : 2;
            plan.audioBitrate = custom && settings.audio.bitrate > 0 ? settings.audio.bitrate
                                : settings.mode == ExportMode::MatchSource && asrc && asrc->bitRate > 0
                                    ? asrc->bitRate
                                    : std::min<std::int64_t>(640000, 160000 * std::max(2, plan.audioChannels));
            if (settings.mode != ExportMode::Custom || settings.audio.copyWhenPossible)
                plan.notes << tr("The audio is mixed (several clips, gain, fades or crossfades) and encoded as %1.")
                                  .arg(QString::fromUtf8(avcodec_get_name(acodec)));
        }
    }

    if (settings.mode == ExportMode::MatchSource)
        plan.notes << tr("The timeline is rendered with its titles, transitions and effects and encoded with the "
                         "source's settings. Frames without effects are converted directly from the source.");
    plan.expectedDuration = plan.rangeEnd - plan.rangeStart;
    planSubtitles(plan, tl, settings);
    return plan;
}

} // namespace lumora

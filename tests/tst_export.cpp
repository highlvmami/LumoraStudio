#include <QTemporaryDir>
#include <QtTest>

#include "TestMedia.h"
#include "ffmpeg/FFmpeg.h"
#include "export/Encoders.h"
#include "export/ExportPlanner.h"
#include "export/TimelineExporter.h"
#include "media/MediaPool.h"
#include "media/VideoDecoder.h"
#include "timeline/Subtitles.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::test;

namespace {
constexpr int kFps = 25;
Ticks frames(int n) { return n * frameDuration(AVRational{kFps, 1}); }

Ticks sec(double s) { return ticksFromSeconds(s); }

// Decoded picture of frame `index` of a file, as an image.
QImage frameImage(const QString& path, int index) {
    VideoDecoder dec;
    if (!dec.open(path))
        return {};
    const Ticks fd = frameDuration(dec.info().frameRate);
    FrameConverter conv;
    const AVFrame* f = dec.frameAt(index * fd + fd / 2);
    return f ? conv.convert(f) : QImage();
}

std::vector<int> range(int from, int to) {
    std::vector<int> v;
    for (int i = from; i < to; ++i)
        v.push_back(i);
    return v;
}
} // namespace

class ExportTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_src, m_src2;
    MediaPool m_pool;
    MediaItemPtr m_item, m_item2;

    QString out(const QString& name) const { return m_dir.filePath(name); }

    ExportResult exportTimeline(Timeline& tl, ExportMode mode, const QString& path, ExportPlan* planOut = nullptr) {
        ExportSettings s;
        s.mode = mode;
        s.outputPath = path;
        return exportWith(tl, s, planOut);
    }

    ExportResult exportWith(Timeline& tl, ExportSettings s, ExportPlan* planOut = nullptr) {
        ExportPlan plan = ExportPlanner::plan(tl.state(), m_pool, s);
        if (planOut)
            *planOut = plan;
        if (!plan.ok())
            qWarning() << "plan blockers:" << plan.blockers;
        TimelineExporter exporter;
        return exporter.run(plan, s);
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        if (qEnvironmentVariableIsSet("LUMORA_KEEP_TEST_FILES")) {
            m_dir.setAutoRemove(false);
            qInfo() << "test files in" << m_dir.path();
        }
        m_src = m_dir.filePath("source.mp4");
        m_src2 = m_dir.filePath("source2.mp4");
        TestVideoSpec spec;
        QString err;
        QVERIFY2(writeTestVideo(m_src, spec, &err), qPrintable(err));
        QVERIFY2(writeTestVideo(m_src2, spec, &err), qPrintable(err));
        m_item = m_pool.import(m_src, &err);
        QVERIFY2(m_item, qPrintable(err));
        m_item2 = m_pool.import(m_src2, &err);
        QVERIFY(m_item2);
        MediaPool::scanKeyframes(m_item);
        MediaPool::scanKeyframes(m_item2);
        QCOMPARE(m_item->keyframesCopy().size(), size_t(10));
        const auto src = decodeAll(m_src);
        QCOMPARE(src.frameIndices, range(0, 100));
    }

    void wholeClipStreamCopyIsBitExact() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, m_item->info.duration, true, true);
        const QString path = out("whole.mp4");
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(videoPacketHashes(path), videoPacketHashes(m_src));
        const auto d = decodeAll(path);
        QCOMPARE(d.frameIndices, range(0, 100));
        QCOMPARE(d.decodeErrors, 0);
        QVERIFY(d.hasAudio);
    }

    void keyframeAlignedCutCopiesExactFrames() {
        Timeline tl;
        tl.insertMedia(m_item->id, frames(20), frames(60), true, true);
        const QString path = out("cut.mp4");
        ExportPlan plan;
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(plan.segments.front().keyframeShiftFrames, 0);
        const auto d = decodeAll(path);
        QCOMPARE(d.frameIndices, range(20, 60));
        QCOMPARE(d.decodeErrors, 0);
        // Every packet is a byte-identical copy of the source.
        const auto srcHashes = videoPacketHashes(m_src);
        for (const auto& h : videoPacketHashes(path))
            QVERIFY(std::find(srcHashes.begin(), srcHashes.end(), h) != srcHashes.end());
        QCOMPARE(videoPacketHashes(path).size(), size_t(40));
        QVERIFY(std::abs(audioDuration(path) - 1.6) < 0.05);
    }

    void nonKeyframeInSnapsToPreviousKeyframe() {
        Timeline tl;
        tl.insertMedia(m_item->id, frames(25), frames(60), true, true);
        const QString path = out("snap.mp4");
        ExportPlan plan;
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(plan.segments.front().keyframeShiftFrames, 5);
        QVERIFY(!plan.notes.isEmpty());
        const auto d = decodeAll(path);
        QCOMPARE(d.frameIndices, range(20, 60));
    }

    void outPointInsideGopNeverWritesBrokenFrames() {
        Timeline tl;
        tl.insertMedia(m_item->id, frames(40), frames(55), true, true);
        const QString path = out("gopout.mp4");
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const auto d = decodeAll(path);
        QCOMPARE(d.decodeErrors, 0);
        QVERIFY(!d.frameIndices.empty());
        QCOMPARE(d.frameIndices.front(), 40);
        // Contiguous frames, ending at most two frames (the B-frame delay) early.
        for (size_t i = 1; i < d.frameIndices.size(); ++i)
            QCOMPARE(d.frameIndices[i], d.frameIndices[i - 1] + 1);
        QVERIFY(d.frameIndices.back() <= 54);
        QVERIFY(d.frameIndices.back() >= 52);
    }

    void splitRemoveAndJoinStreamCopy() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, m_item->info.duration, true, true);
        QVERIFY(tl.splitAt(frames(30)));
        QVERIFY(tl.splitAt(frames(60)));
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(3));
        QVERIFY(tl.removeClips({tl.tracks()[0].clips[1].id}));
        QCOMPARE(tl.duration(), frames(70));
        const QString path = out("joined.mp4");
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const auto d = decodeAll(path);
        std::vector<int> expected = range(0, 30);
        const auto tail = range(60, 100);
        expected.insert(expected.end(), tail.begin(), tail.end());
        QCOMPARE(d.frameIndices, expected);
        QCOMPARE(d.decodeErrors, 0);
        QVERIFY(std::abs(audioDuration(path) - 2.8) < 0.06);
    }

    void joinTwoFilesStreamCopy() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, frames(40), true, true);
        tl.insertMedia(m_item2->id, frames(10), frames(50), true, true);
        const QString path = out("twofiles.mp4");
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const auto d = decodeAll(path);
        std::vector<int> expected = range(0, 40);
        const auto tail = range(10, 50);
        expected.insert(expected.end(), tail.begin(), tail.end());
        QCOMPARE(d.frameIndices, expected);
        QCOMPARE(d.decodeErrors, 0);
    }

    void matchSourceIsFrameExact() {
        Timeline tl;
        tl.insertMedia(m_item->id, frames(13), frames(47), true, true);
        const QString path = out("match.mp4");
        ExportPlan plan;
        auto r = exportTimeline(tl, ExportMode::MatchSource, path, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(plan.videoEncoder, QStringLiteral("mpeg4"));
        QCOMPARE(int(plan.audio), int(AudioStrategy::Copy));
        auto info = probeMedia(path);
        QVERIFY(info && info->video);
        QCOMPARE(info->video->width, 320);
        QCOMPARE(info->video->height, 240);
        QCOMPARE(info->video->pixelFormat, AV_PIX_FMT_YUV420P);
        QCOMPARE(av_q2d(info->video->frameRate), 25.0);
        const auto d = decodeAll(path);
        QCOMPARE(d.frameIndices, range(13, 47));
    }

    void losslessIntermediateIsPixelExact() {
        Timeline tl;
        tl.insertMedia(m_item->id, frames(13), frames(47), true, true);
        tl.splitAt(frames(10));
        const QString path = out("lossless.mkv");
        ExportPlan plan;
        auto r = exportTimeline(tl, ExportMode::LosslessIntermediate, path, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(plan.videoEncoder, QStringLiteral("ffv1"));
        const auto src = decodedFrameHashes(m_src);
        const auto dst = decodedFrameHashes(path);
        QCOMPARE(dst.size(), size_t(34));
        for (int i = 0; i < 34; ++i)
            QCOMPARE(dst[i], src[13 + i]);
    }

    void h264WithBPyramidCutsCleanly() {
        if (!avcodec_find_encoder_by_name("libx264"))
            QSKIP("FFmpeg build has no libx264");
        const QString src = m_dir.filePath("h264.mp4");
        TestVideoSpec spec;
        spec.encoder = "libx264";
        spec.gop = 25;
        spec.bFrames = 3;
        spec.seconds = 6;
        QVERIFY(writeTestVideo(src, spec));
        auto item = m_pool.import(src);
        MediaPool::scanKeyframes(item);
        Timeline tl;
        tl.insertMedia(item->id, 0, item->info.duration, true, true);
        tl.splitAt(frames(50));
        tl.splitAt(frames(100));
        tl.removeClips({tl.tracks()[0].clips[1].id});
        const QString path = out("h264cut.mp4");
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path);
        QVERIFY2(r.ok, qPrintable(r.error));
        const auto d = decodeAll(path);
        QCOMPARE(d.decodeErrors, 0);
        std::vector<int> expected = range(0, 50);
        const auto tail = range(100, 150);
        expected.insert(expected.end(), tail.begin(), tail.end());
        QCOMPARE(d.frameIndices, expected);

        // Out point in the middle of a GOP: may end early, never corrupt.
        Timeline tl2;
        tl2.insertMedia(item->id, frames(25), frames(63), true, true);
        const QString path2 = out("h264mid.mp4");
        r = exportTimeline(tl2, ExportMode::StreamCopy, path2);
        QVERIFY2(r.ok, qPrintable(r.error));
        const auto d2 = decodeAll(path2);
        QCOMPARE(d2.decodeErrors, 0);
        QCOMPARE(d2.frameIndices.front(), 25);
        for (size_t i = 1; i < d2.frameIndices.size(); ++i)
            QCOMPARE(d2.frameIndices[i], d2.frameIndices[i - 1] + 1);
        QVERIFY(d2.frameIndices.back() <= 62);
        QVERIFY(d2.frameIndices.back() >= 58);

        // Match source re-encodes with libx264 at the source settings.
        Timeline tl3;
        tl3.insertMedia(item->id, frames(7), frames(33), true, true);
        ExportPlan plan;
        r = exportTimeline(tl3, ExportMode::MatchSource, out("h264match.mp4"), &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(plan.videoEncoder, QStringLiteral("libx264"));
        QCOMPARE(decodeAll(out("h264match.mp4")).frameIndices, range(7, 33));
    }

    void mismatchedSourcesBlockStreamCopy() {
        const QString other = m_dir.filePath("other.mp4");
        TestVideoSpec spec;
        spec.width = 352;
        spec.seconds = 1;
        QVERIFY(writeTestVideo(other, spec));
        auto item = m_pool.import(other);
        Timeline tl;
        tl.insertMedia(m_item->id, 0, frames(20), true, true);
        tl.insertMedia(item->id, 0, frames(20), true, true);
        ExportSettings s;
        s.mode = ExportMode::StreamCopy;
        s.outputPath = out("mismatch.mp4");
        const auto plan = ExportPlanner::plan(tl.state(), m_pool, s);
        QVERIFY(!plan.ok());
        s.mode = ExportMode::MatchSource;
        const auto plan2 = ExportPlanner::plan(tl.state(), m_pool, s);
        QVERIFY2(plan2.ok(), qPrintable(plan2.blockers.join('\n')));
    }

    void streamCopyIsBlockedByEffects() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, frames(40), true, true);
        tl.setFades(tl.tracks()[0].clips[0].id, frames(5), 0);
        ExportSettings s;
        s.mode = ExportMode::StreamCopy;
        s.outputPath = out("blocked.mp4");
        QVERIFY(!ExportPlanner::plan(tl.state(), m_pool, s).ok());
        s.mode = ExportMode::MatchSource;
        const auto plan = ExportPlanner::plan(tl.state(), m_pool, s);
        QVERIFY2(plan.ok(), qPrintable(plan.blockers.join('\n')));
        QCOMPARE(int(plan.video), int(VideoStrategy::Render));
    }

    void customH264KeepsEveryFrame() {
        if (!avcodec_find_encoder_by_name("libx264"))
            QSKIP("FFmpeg build has no libx264");
        Timeline tl;
        tl.insertMedia(m_item->id, frames(13), frames(47), true, true);
        ExportSettings s;
        s.mode = ExportMode::Custom;
        s.outputPath = out("custom.mp4");
        s.video.codec = AV_CODEC_ID_H264;
        s.video.quality = 16;
        ExportPlan plan;
        auto r = exportWith(tl, s, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(plan.video), int(VideoStrategy::Render));
        QCOMPARE(plan.videoEncoder, QStringLiteral("libx264"));
        QCOMPARE(int(plan.audio), int(AudioStrategy::Copy)); // plain cut: audio is copied untouched
        const auto info = probeMedia(s.outputPath);
        QVERIFY(info && info->video && info->audio);
        QCOMPARE(info->video->codecId, AV_CODEC_ID_H264);
        QCOMPARE(info->video->pixelFormat, AV_PIX_FMT_YUV420P);
        QCOMPARE(info->video->colorRange, AVCOL_RANGE_MPEG);
        const auto d = decodeAll(s.outputPath);
        QCOMPARE(d.decodeErrors, 0);
        QCOMPARE(d.frameIndices, range(13, 47));
        QVERIFY(std::abs(audioDuration(s.outputPath) - 34 / 25.0) < 0.05);
    }

    void customScalesWithBarsAndChangesFrameRate() {
        if (!avcodec_find_encoder_by_name("libx264"))
            QSKIP("FFmpeg build has no libx264");
        Timeline tl;
        tl.insertMedia(m_item->id, 0, frames(20), true, true);
        ExportSettings s;
        s.mode = ExportMode::Custom;
        s.outputPath = out("scaled.mp4");
        s.video.resolution = QSize(640, 360);
        s.video.scaleMode = ScaleMode::Fit;
        s.video.frameRate = AVRational{50, 1};
        s.video.quality = 14;
        ExportPlan plan;
        auto r = exportWith(tl, s, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(plan.outputSize, QSize(640, 360));
        QCOMPARE(plan.pictureSize, QSize(480, 360));
        const auto info = probeMedia(s.outputPath);
        QCOMPARE(info->video->width, 640);
        QCOMPARE(info->video->height, 360);
        QCOMPARE(av_q2d(info->video->frameRate), 50.0);
        const QImage img = frameImage(s.outputPath, 3);
        QVERIFY(!img.isNull());
        QVERIFY(meanLuma(img, QRect(0, 0, 70, 360)) < 20);     // pillar box
        QVERIFY(meanLuma(img, QRect(570, 0, 70, 360)) < 20);
        QCOMPARE(decodeAll(s.outputPath).frameIndices.size(), size_t(40));

        // Inside: no bars and never upscaled.
        s.video.scaleMode = ScaleMode::Inside;
        s.video.frameRate = AVRational{0, 1};
        plan = ExportPlanner::plan(tl.state(), m_pool, s);
        QCOMPARE(plan.outputSize, QSize(320, 240));
        s.video.resolution = QSize(160, 160);
        plan = ExportPlanner::plan(tl.state(), m_pool, s);
        QCOMPARE(plan.outputSize, QSize(160, 120));
    }

    void bitRateModesHitTheirTarget() {
        if (!avcodec_find_encoder_by_name("libx264"))
            QSKIP("FFmpeg build has no libx264");
        Timeline tl;
        tl.insertMedia(m_item->id, 0, m_item->info.duration, true, true);
        ExportSettings s;
        s.mode = ExportMode::Custom;
        s.includeAudio = false;
        s.outputPath = out("target.mp4");
        s.video.rateControl = RateControl::TargetSize;
        s.video.targetSizeMB = 0.4;
        s.video.twoPass = true;
        ExportPlan plan;
        auto r = exportWith(tl, s, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(plan.twoPass);
        qInfo() << "target size: wanted 400000, got" << r.fileSize;
        QVERIFY(r.fileSize < 400000 * 1.05);
        QVERIFY(r.fileSize > 400000 * 0.6);
        QCOMPARE(decodeAll(s.outputPath).frameIndices, range(0, 100));
        // The first pass statistics are cleaned up.
        QVERIFY(QDir::temp().entryList({"lumora-2pass-*"}).isEmpty());

        s.outputPath = out("cbr.mp4");
        s.video.rateControl = RateControl::ConstantBitrate;
        s.video.bitrate = 600000;
        s.video.twoPass = false;
        r = exportWith(tl, s, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        const double kbps = r.fileSize * 8 / 4.0 / 1000;
        qInfo() << "CBR 600 kbit/s ->" << kbps;
        QVERIFY(kbps > 450 && kbps < 700);
    }

    void renderedTimelineHasTitlesTransitionsAndMixedAudio() {
        const QString brightPath = m_dir.filePath("bright.mp4"), darkPath = m_dir.filePath("dark.mp4");
        TestVideoSpec spec;
        spec.solidLuma = 200;
        QVERIFY(writeTestVideo(brightPath, spec));
        spec.solidLuma = 40;
        QVERIFY(writeTestVideo(darkPath, spec));
        auto bright = m_pool.import(brightPath), dark = m_pool.import(darkPath);
        Timeline tl;
        tl.insertMedia(bright->id, 0, sec(2), true, true);
        tl.insertMedia(dark->id, sec(1), sec(3), true, true);
        QVERIFY(tl.setTransition(tl.tracks()[0].clips[1].id, {TransitionType::CrossDissolve, sec(1)}));
        const int v2 = tl.addTrack(TrackType::Video);
        TitleData title;
        title.text = QStringLiteral("Lumora");
        title.color = QColor(255, 0, 0);
        title.size = 0.25;
        title.y = 0.5;
        title.shadow = false;
        title.animationIn = title.animationOut = TitleAnimation::None;
        tl.addTitle(title, sec(1), v2, sec(3));
        // An audio-only clip on its own track.
        const int a2 = tl.audioTrackFor(v2);
        tl.placeMedia(m_item->id, 0, sec(1), false, true, a2, sec(0.5));

        ExportPlan plan;
        auto r = exportTimeline(tl, ExportMode::MatchSource, out("rendered.mp4"), &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(plan.video), int(VideoStrategy::Render));
        QCOMPARE(int(plan.audio), int(AudioStrategy::Encode));
        QCOMPARE(plan.videoEncoder, QStringLiteral("mpeg4"));
        const auto d = decodeAll(out("rendered.mp4"));
        QCOMPARE(d.decodeErrors, 0);
        QCOMPARE(int(d.frameIndices.size()), 100); // 4 s at 25 fps
        QVERIFY(std::abs(audioDuration(out("rendered.mp4")) - 4.0) < 0.05);
        const QRect all(0, 0, 320, 240);
        QVERIFY(meanLuma(frameImage(out("rendered.mp4"), 10), all) > 180);
        const double mid = meanLuma(frameImage(out("rendered.mp4"), 50), all);
        QVERIFY2(mid > 100 && mid < 140, qPrintable(QString::number(mid)));
        // The title (red) is on screen from 3 s.
        auto redPixels = [](const QImage& img) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QColor c = img.pixelColor(x, y);
                    n += c.red() > 150 && c.green() < 90 ? 1 : 0;
                }
            return n;
        };
        QCOMPARE(redPixels(frameImage(out("rendered.mp4"), 70)), 0);
        QVERIFY(redPixels(frameImage(out("rendered.mp4"), 85)) > 300);
    }

    void intermediatePresetsWriteTheRightFormat() {
        if (!avcodec_find_encoder_by_name("prores_ks"))
            QSKIP("FFmpeg build has no prores_ks");
        Timeline tl;
        tl.insertMedia(m_item->id, 0, frames(10), true, true);
        ExportSettings s;
        s.mode = ExportMode::Custom;
        for (const auto& p : encoders::presets())
            if (p.id == QLatin1String("prores-hq")) {
                s.video = p.video;
                s.audio = p.audio;
            }
        s.outputPath = out("prores.mov");
        ExportPlan plan;
        auto r = exportWith(tl, s, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        const auto info = probeMedia(s.outputPath);
        QCOMPARE(info->video->codecId, AV_CODEC_ID_PRORES);
        QCOMPARE(info->video->pixelFormat, AV_PIX_FMT_YUV422P10LE);
        QCOMPARE(info->video->profile, QStringLiteral("HQ"));
        QCOMPARE(info->audio->codecId, AV_CODEC_ID_PCM_S24LE);
        QCOMPARE(decodeAll(s.outputPath).frameIndices, range(0, 10));
    }

    void everyPresetPlans() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, frames(10), true, true);
        for (const auto& p : encoders::presets()) {
            if (encoders::available(p.video.codec).empty())
                continue;
            ExportSettings s;
            s.mode = ExportMode::Custom;
            s.video = p.video;
            s.audio = p.audio;
            s.outputPath = out(QStringLiteral("preset.%1").arg(p.extension));
            const auto plan = ExportPlanner::plan(tl.state(), m_pool, s);
            QVERIFY2(plan.ok(), qPrintable(p.id + ": " + plan.blockers.join('\n')));
        }
    }

    void otherEncodersInOneAndTwoPasses() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, frames(20), true, true);
        const std::pair<AVCodecID, const char*> list[] = {
            {AV_CODEC_ID_HEVC, "mkv"}, {AV_CODEC_ID_VP9, "webm"}, {AV_CODEC_ID_AV1, "mkv"}};
        for (const auto& [codec, ext] : list) {
            for (const auto& enc : encoders::available(codec)) {
                if (enc.hardware)
                    continue;
                for (int mode = 0; mode < 2; ++mode) {
                    ExportSettings s;
                    s.mode = ExportMode::Custom;
                    s.video.codec = codec;
                    s.video.encoder = enc.name;
                    s.video.speed = 0;
                    if (mode == 1) {
                        s.video.rateControl = RateControl::VariableBitrate;
                        s.video.bitrate = 500000;
                        s.video.twoPass = true;
                    }
                    s.outputPath = out(QStringLiteral("%1-%2.%3").arg(enc.name).arg(mode).arg(ext));
                    ExportPlan plan;
                    auto r = exportWith(tl, s, &plan);
                    QVERIFY2(r.ok, qPrintable(enc.name + ": " + r.error));
                    const auto d = decodeAll(s.outputPath);
                    QVERIFY2(d.decodeErrors == 0, qPrintable(enc.name));
                    QCOMPARE(d.frameIndices, range(0, 20));
                    QVERIFY(d.hasAudio);
                }
            }
        }
    }

    void audioEffectsKeepThePictureCopied() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, m_item->info.duration, true, true);
        AudioEffects fx;
        fx.eqLow = 3;
        QVERIFY(tl.setAudioEffects(tl.tracks()[1].clips[0].id, fx));
        tl.setGain(tl.tracks()[1].clips[0].id, -3);
        const int a2 = tl.addTrack(TrackType::Audio);
        tl.placeMedia(m_item2->id, 0, sec(1), false, true, a2, sec(1));
        ExportPlan plan;
        const QString path = out("mixed-copy.mp4");
        auto r = exportTimeline(tl, ExportMode::StreamCopy, path, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(plan.video), int(VideoStrategy::Copy));
        QVERIFY(plan.audioMixed);
        QCOMPARE(videoPacketHashes(path), videoPacketHashes(m_src)); // picture bit exact
        QVERIFY(std::abs(audioDuration(path) - 4.0) < 0.06);
        QCOMPARE(decodeAll(path).decodeErrors, 0);
    }

    void subtitlesAreEmbeddedBurntOrSaved() {
        Timeline tl;
        tl.insertMedia(m_item->id, 0, m_item->info.duration, true, true);
        tl.addSubtitleTrack({{sec(1), sec(2), QStringLiteral("Merhaba")}, {sec(2.5), sec(3), QStringLiteral("Dünya")}},
                            QStringLiteral("tur"));
        // MKV: a SubRip stream, the picture still copied.
        ExportPlan plan;
        auto r = exportTimeline(tl, ExportMode::StreamCopy, out("subs.mkv"), &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(plan.video), int(VideoStrategy::Copy));
        QCOMPARE(int(plan.subtitleStreams.size()), 1);
        auto countSubs = [](const QString& path, QString* lang) {
            auto in = av::openInput(path);
            if (!in)
                return -1;
            const int idx = av_find_best_stream(in.get(), AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
            if (idx < 0)
                return 0;
            if (auto* e = av_dict_get(in->streams[idx]->metadata, "language", nullptr, 0))
                *lang = QString::fromUtf8(e->value);
            int n = 0;
            auto pkt = av::makePacket();
            while (av_read_frame(in.get(), pkt.get()) >= 0) {
                n += pkt->stream_index == idx && pkt->size > 2; // mp4 fills gaps with empty samples
                av_packet_unref(pkt.get());
            }
            return n;
        };
        QString lang;
        QCOMPARE(countSubs(out("subs.mkv"), &lang), 2);
        QCOMPARE(lang, QStringLiteral("tur"));
        // MP4: mov_text.
        r = exportTimeline(tl, ExportMode::StreamCopy, out("subs.mp4"), &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(countSubs(out("subs.mp4"), &lang), 2);
        // Sidecar.
        ExportSettings s;
        s.mode = ExportMode::StreamCopy;
        s.outputPath = out("side.mp4");
        s.subtitles = SubtitleMode::Sidecar;
        r = exportWith(tl, s);
        QVERIFY2(r.ok, qPrintable(r.error));
        const auto cues = subtitles::readFile(out("side.srt"));
        QCOMPARE(int(cues.size()), 2);
        QCOMPARE(cues[1].text, QStringLiteral("Dünya"));
        // Burnt in: a lossless cut is not possible, the render path draws them.
        s.subtitles = SubtitleMode::BurnIn;
        QVERIFY(!ExportPlanner::plan(tl.state(), m_pool, s).ok());
        s.mode = ExportMode::MatchSource;
        s.outputPath = out("burnt.mp4");
        r = exportWith(tl, s, &plan);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(plan.burnSubtitles);
        QCOMPARE(countSubs(out("burnt.mp4"), &lang), 0);
    }

};

QTEST_MAIN(ExportTest)
#include "tst_export.moc"

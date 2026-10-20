#include <QTemporaryDir>
#include <QtTest>

#include "TestMedia.h"
#include "ffmpeg/FFmpeg.h"
#include "export/ExportPlanner.h"
#include "export/TimelineExporter.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::test;

namespace {
constexpr int kFps = 25;
Ticks frames(int n) { return n * frameDuration(AVRational{kFps, 1}); }

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
};

QTEST_GUILESS_MAIN(ExportTest)
#include "tst_export.moc"

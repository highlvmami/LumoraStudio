#include <QUndoStack>
#include <QtTest>

#include "timeline/Timeline.h"
#include "undo/TimelineCommands.h"

using namespace lumora;

namespace {
Ticks sec(double s) { return ticksFromSeconds(s); }
}

class TimelineTest : public QObject {
    Q_OBJECT

private slots:
    void ticksRepresentCommonRatesExactly() {
        // 1001-based NTSC rates and integer rates divide the tick base.
        QCOMPARE(kTicksPerSecond % 24, Ticks(0));
        QCOMPARE((kTicksPerSecond * 1001) % 30000, Ticks(0));
        QCOMPARE((kTicksPerSecond * 1001) % 60000, Ticks(0));
        QCOMPARE((kTicksPerSecond * 1001) % 24000, Ticks(0));
        QCOMPARE(kTicksPerSecond % 48000, Ticks(0));
        QCOMPARE(kTicksPerSecond % 44100, Ticks(0));
        QCOMPARE(formatTimecode(sec(3661) + frameDuration({25, 1}) * 7, {25, 1}), QStringLiteral("01:01:01:07"));
    }

    void insertCreatesLinkedClips() {
        Timeline tl;
        const QUuid media = QUuid::createUuid();
        tl.insertMedia(media, 0, sec(10), true, true);
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(1));
        QCOMPARE(tl.tracks()[1].clips.size(), size_t(1));
        QCOMPARE(tl.tracks()[0].clips[0].linkId, tl.tracks()[1].clips[0].linkId);
        QCOMPARE(tl.duration(), sec(10));
    }

    void splitRemoveRipple() {
        Timeline tl;
        const QUuid media = QUuid::createUuid();
        tl.insertMedia(media, 0, sec(10), true, true);
        QVERIFY(tl.splitAt(sec(4)));
        QVERIFY(!tl.splitAt(sec(4))); // already a boundary
        const auto& v = tl.tracks()[0].clips;
        QCOMPARE(v.size(), size_t(2));
        QCOMPARE(v[0].sourceOut, sec(4));
        QCOMPARE(v[1].sourceIn, sec(4));
        QCOMPARE(v[1].timelineStart, sec(4));
        // Audio is split at the same place and stays linked pairwise.
        const auto& a = tl.tracks()[1].clips;
        QCOMPARE(a.size(), size_t(2));
        QCOMPARE(a[1].linkId, v[1].linkId);
        QVERIFY(a[0].linkId != a[1].linkId);

        QVERIFY(tl.removeClips({v[0].id}));
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(1));
        QCOMPARE(tl.tracks()[1].clips.size(), size_t(1));
        QCOMPARE(tl.tracks()[0].clips[0].timelineStart, Ticks(0)); // gap closed
        QCOMPARE(tl.tracks()[1].clips[0].timelineStart, Ticks(0));
        QCOMPARE(tl.duration(), sec(6));
    }

    void removeRange() {
        Timeline tl;
        tl.insertMedia(QUuid::createUuid(), 0, sec(10), true, true);
        QVERIFY(tl.removeRange(sec(2), sec(5)));
        QCOMPARE(tl.duration(), sec(7));
        const auto& v = tl.tracks()[0].clips;
        QCOMPARE(v.size(), size_t(2));
        QCOMPARE(v[0].sourceOut, sec(2));
        QCOMPARE(v[1].sourceIn, sec(5));
        QCOMPARE(v[1].timelineStart, sec(2));
    }

    void trimKeepsLinkedInSync() {
        Timeline tl;
        tl.insertMedia(QUuid::createUuid(), 0, sec(10), true, true);
        const QUuid id = tl.tracks()[0].clips[0].id;
        QVERIFY(tl.trimIn(id, sec(1)));
        QVERIFY(tl.trimOut(id, sec(8), sec(10)));
        QCOMPARE(tl.tracks()[1].clips[0].sourceIn, sec(1));
        QCOMPARE(tl.tracks()[1].clips[0].sourceOut, sec(8));
        QVERIFY(tl.trimOut(id, sec(20), sec(10)));          // clamped to media length
        QCOMPARE(tl.tracks()[0].clips[0].sourceOut, sec(10));
        QVERIFY(tl.trimIn(id, sec(-5)));                     // clamped to 0
        QCOMPARE(tl.tracks()[0].clips[0].sourceIn, Ticks(0));
    }

    void moveAndJoin() {
        Timeline tl;
        const QUuid m = QUuid::createUuid();
        tl.insertMedia(m, 0, sec(10), true, true);
        tl.splitAt(sec(3));
        const QUuid first = tl.tracks()[0].clips[0].id;
        QVERIFY(tl.canJoinWithNext(first));
        QVERIFY(tl.moveClip(first, 1));
        QCOMPARE(tl.tracks()[0].clips[1].id, first);
        QCOMPARE(tl.tracks()[0].clips[1].timelineStart, sec(7));
        QCOMPARE(tl.tracks()[1].clips[1].timelineStart, sec(7)); // linked audio followed
        QVERIFY(!tl.canJoinWithNext(tl.tracks()[0].clips[0].id));
        QVERIFY(tl.moveClip(first, 0));
        QVERIFY(tl.joinWithNext(first));
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(1));
        QCOMPARE(tl.tracks()[1].clips.size(), size_t(1));
        QCOMPARE(tl.tracks()[0].clips[0].sourceOut, sec(10));
    }

    void insertInTheMiddle() {
        Timeline tl;
        const QUuid a = QUuid::createUuid(), b = QUuid::createUuid();
        tl.insertMedia(a, 0, sec(4), true, true);
        tl.insertMedia(a, sec(4), sec(8), true, true);
        tl.insertMedia(b, 0, sec(1), true, false, sec(4.5)); // near the boundary at 4 s
        const auto& v = tl.tracks()[0].clips;
        QCOMPARE(v.size(), size_t(3));
        QCOMPARE(v[1].mediaId, b);
        QCOMPARE(tl.tracks()[1].clips[1].timelineStart, sec(5)); // audio of 2nd clip moved
    }

    void undoRedoSnapshots() {
        Timeline tl;
        QUndoStack stack;
        runTimelineEdit(&stack, &tl, "insert", [&] { return !tl.insertMedia(QUuid::createUuid(), 0, sec(10), true, true).isNull(); });
        runTimelineEdit(&stack, &tl, "split", [&] { return tl.splitAt(sec(5)); });
        QVERIFY(!runTimelineEdit(&stack, &tl, "noop", [&] { return tl.splitAt(sec(5)); }));
        QCOMPARE(stack.count(), 2);
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(2));
        stack.undo();
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(1));
        stack.undo();
        QVERIFY(tl.isEmpty());
        stack.redo();
        stack.redo();
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(2));
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "tst_timeline.moc"

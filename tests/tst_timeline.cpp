#include <QUndoStack>
#include <QtTest>

#include "timeline/Subtitles.h"
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

    void connectedClipsFollowTheStoryline() {
        Timeline tl;
        const QUuid m = QUuid::createUuid();
        tl.insertMedia(m, 0, sec(4), true, true);
        tl.insertMedia(m, sec(10), sec(14), true, true);
        const int v2 = tl.addTrack(TrackType::Video);
        TitleData title;
        title.text = "Hello";
        const QUuid t = tl.addTitle(title, sec(2), v2, sec(5));
        QVERIFY(!t.isNull());
        QCOMPARE(tl.clip(t)->timelineStart, sec(5));
        QCOMPARE(tl.clip(t)->anchorId, tl.tracks()[0].clips[1].id);
        // Ripple-delete the first clip: the title keeps its place on the
        // picture of the second clip.
        QVERIFY(tl.removeClips({tl.tracks()[0].clips[0].id}));
        QCOMPARE(tl.clip(t)->timelineStart, sec(1));
        // Trimming the start of the anchor clip moves the title with the picture.
        QVERIFY(tl.trimIn(tl.tracks()[0].clips[0].id, sec(10.5)));
        QCOMPARE(tl.clip(t)->timelineStart, sec(0.5));
        // Splitting keeps the absolute position and re-anchors.
        QVERIFY(tl.splitAt(sec(0.25)));
        QCOMPARE(tl.clip(t)->timelineStart, sec(0.5));
        QCOMPARE(tl.clip(t)->anchorId, tl.tracks()[0].clips[1].id);
    }

    void placeOnUpperTrackCreatesPairedAudio() {
        Timeline tl;
        const QUuid m = QUuid::createUuid(), b = QUuid::createUuid();
        tl.insertMedia(m, 0, sec(10), true, true);
        const int v2 = tl.addTrack(TrackType::Video);
        const QUuid id = tl.placeMedia(b, 0, sec(3), true, true, v2, sec(2));
        QVERIFY(!id.isNull());
        const auto audio = tl.state().trackIndices(TrackType::Audio);
        QCOMPARE(audio.size(), size_t(2));
        QCOMPARE(tl.tracks()[audio[1]].name, QStringLiteral("A2"));
        QCOMPARE(tl.tracks()[audio[1]].clips.size(), size_t(1));
        QCOMPARE(tl.tracks()[audio[1]].clips[0].timelineStart, sec(2));
        QCOMPARE(tl.linkedClips(id).size(), size_t(2));
        // Moving the video moves its audio.
        QVERIFY(tl.moveClipTo(id, v2, sec(5)));
        QCOMPARE(tl.clip(id)->timelineStart, sec(5));
        QCOMPARE(tl.tracks()[audio[1]].clips[0].timelineStart, sec(5));
        // Overwrite: a title placed over the middle splits the clip below
        // (video only, so the pair is unlinked rather than desynchronised).
        TitleData title;
        const QUuid t = tl.addTitle(title, sec(1), v2, sec(6));
        const auto& clips = tl.tracks()[v2].clips;
        QCOMPARE(clips.size(), size_t(3));
        QCOMPARE(clips[0].timelineEnd(), sec(6));
        QCOMPARE(clips[1].id, t);
        QCOMPARE(clips[2].timelineStart, sec(7));
        QCOMPARE(clips[2].sourceIn, sec(2));
        QVERIFY(clips[0].linkId.isNull());
        QVERIFY(tl.tracks()[audio[1]].clips[0].linkId.isNull());
    }

    void liftAndInsertStorylineClips() {
        Timeline tl;
        const QUuid m = QUuid::createUuid();
        tl.insertMedia(m, 0, sec(4), true, true);
        tl.insertMedia(m, sec(4), sec(8), true, true);
        tl.insertMedia(m, sec(8), sec(12), true, true);
        const QUuid middle = tl.tracks()[0].clips[1].id;
        const int v2 = tl.addTrack(TrackType::Video);
        QVERIFY(tl.moveClipTo(middle, v2, sec(1)));
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(2));
        QCOMPARE(tl.duration(), sec(8));
        QCOMPARE(tl.clip(middle)->timelineStart, sec(1));
        const int a2 = tl.state().trackIndices(TrackType::Audio)[1];
        QCOMPARE(tl.tracks()[a2].clips.size(), size_t(1)); // its audio went to A2
        // And back into the storyline, at the end.
        QVERIFY(tl.moveClipTo(middle, 0, sec(100)));
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(3));
        QCOMPARE(tl.tracks()[0].clips[2].id, middle);
        QCOMPARE(tl.tracks()[1].clips.size(), size_t(3));
        QVERIFY(tl.tracks()[a2].clips.empty());
        QCOMPARE(tl.duration(), sec(12));
    }

    void transitionDurationIsLimitedByHandles() {
        Timeline tl;
        const QUuid m = QUuid::createUuid();
        tl.insertMedia(m, sec(1), sec(5), true, true);  // 1 s before, 5 s after available (media is 10 s)
        tl.insertMedia(m, sec(0.4), sec(6), true, true); // only 0.4 s before the in point
        const QUuid second = tl.tracks()[0].clips[1].id;
        QVERIFY(tl.setTransition(second, {TransitionType::CrossDissolve, sec(2)}));
        QVERIFY(!tl.setTransition(tl.tracks()[0].clips[0].id, {TransitionType::CrossDissolve, sec(1)}));
        auto len = [](const QUuid&) { return sec(10); };
        QCOMPARE(tl.state().transitionDuration(1, len), sec(0.8));
        QVERIFY(tl.state().hasEffects());
        QVERIFY(tl.setTransition(second, {TransitionType::None, 0}));
        QVERIFY(!tl.state().hasEffects());
    }

    void removeTrackAndFlags() {
        Timeline tl;
        const int v2 = tl.addTrack(TrackType::Video);
        TitleData title;
        tl.addTitle(title, sec(2), v2, 0);
        QVERIFY(!tl.removeTrack(0));
        QVERIFY(tl.setTrackFlags(v2, false, false, true));
        QVERIFY(tl.tracks()[v2].hidden);
        QVERIFY(tl.removeTrack(v2));
        QCOMPARE(tl.tracks().size(), size_t(2));
        QVERIFY(tl.isEmpty());
    }

    void titleDurationOverwritesOnFreeTracks() {
        Timeline tl;
        const int v2 = tl.addTrack(TrackType::Video);
        TitleData title;
        const QUuid a = tl.addTitle(title, sec(2), v2, 0);
        const QUuid b = tl.addTitle(title, sec(2), v2, sec(3));
        QVERIFY(tl.setDuration(a, sec(4)));
        QCOMPARE(tl.clip(a)->timelineEnd(), sec(4));
        QCOMPARE(tl.clip(b)->timelineStart, sec(4)); // trimmed, not overlapped
        QCOMPARE(tl.clip(b)->duration(), sec(1));
    }

    void freezeFrameRipplesTheStoryline() {
        Timeline tl;
        tl.setFrameRate({25, 1});
        const QUuid m = QUuid::createUuid();
        tl.insertMedia(m, sec(1), sec(11), true, true);
        const int v2 = tl.addTrack(TrackType::Video);
        TitleData title;
        title.text = "Later";
        const QUuid t = tl.addTitle(title, sec(1), v2, sec(6));
        const QUuid early = tl.addTitle(title, sec(1), v2, sec(1));
        const QUuid clip = tl.tracks()[0].clips[0].id;

        const QUuid f = tl.freezeFrame(clip, sec(4), sec(2));
        QVERIFY(!f.isNull());
        const auto& v = tl.tracks()[0].clips;
        QCOMPARE(v.size(), size_t(3));
        QCOMPARE(v[0].sourceIn, sec(1));
        QCOMPARE(v[0].sourceOut, sec(5));
        QCOMPARE(v[1].id, f);
        QCOMPARE(int(v[1].kind), int(ClipKind::Freeze));
        QCOMPARE(v[1].timelineStart, sec(4));
        QCOMPARE(v[1].duration(), sec(2));
        // The held frame is the one that was under the playhead (source 5 s),
        // for the whole length of the freeze.
        QCOMPARE(v[1].mediaTime(sec(4)), sec(5));
        QCOMPARE(v[1].mediaTime(sec(5.9)), sec(5));
        QCOMPARE(v[2].sourceIn, sec(5));
        QCOMPARE(v[2].timelineStart, sec(6));
        QCOMPARE(tl.duration(), sec(12));
        QVERIFY(tl.state().hasVideoEffects());
        QVERIFY(!tl.canJoinWithNext(v[0].id));
        QVERIFY(!tl.canJoinWithNext(v[1].id));
        // The sound pauses: the second half of the audio moves with its picture.
        const auto& a = tl.tracks()[1].clips;
        QCOMPARE(a.size(), size_t(2));
        QCOMPARE(a[0].timelineEnd(), sec(4));
        QCOMPARE(a[1].timelineStart, sec(6));
        QCOMPARE(a[1].sourceIn, sec(5));
        QCOMPARE(a[1].linkId, v[2].linkId);
        // Connected clips after the freeze move with the picture; earlier ones stay.
        QCOMPARE(tl.clip(t)->timelineStart, sec(8));
        QCOMPARE(tl.clip(early)->timelineStart, sec(1));

        // A freeze is as long as you like and splits into two freezes of the same frame.
        QVERIFY(tl.setDuration(f, sec(3)));
        QCOMPARE(tl.tracks()[0].clips[2].timelineStart, sec(7));
        QVERIFY(tl.trimOut(f, sec(4), 0));
        QCOMPARE(tl.clip(f)->duration(), sec(4));
        QVERIFY(tl.splitAt(sec(5)));
        QCOMPARE(tl.tracks()[0].clips[2].kind, ClipKind::Freeze);
        QCOMPARE(tl.tracks()[0].clips[2].mediaTime(sec(6)), sec(5));

        // Freezing a freeze clip holds the same frame again.
        const QUuid f2 = tl.freezeFrame(f, sec(4.5), sec(1));
        QVERIFY(!f2.isNull());
        QCOMPARE(tl.clip(f2)->freezeTime, sec(5));
    }

    void freezeFrameAtClipStartAndOnUpperTracks() {
        Timeline tl;
        tl.setFrameRate({25, 1});
        const QUuid m = QUuid::createUuid();
        tl.insertMedia(m, 0, sec(4), true, true);
        tl.insertMedia(m, sec(10), sec(14), true, true);
        // At the first frame of a clip: the freeze goes in front of it.
        const QUuid second = tl.tracks()[0].clips[1].id;
        const QUuid f = tl.freezeFrame(second, sec(4), sec(1));
        QVERIFY(!f.isNull());
        QCOMPARE(tl.tracks()[0].clips.size(), size_t(3));
        QCOMPARE(tl.clip(f)->freezeTime, sec(10));
        QCOMPARE(tl.clip(second)->timelineStart, sec(5));
        // Outside the clip, on audio or with no duration: nothing happens.
        QVERIFY(tl.freezeFrame(second, sec(1), sec(1)).isNull());
        QVERIFY(tl.freezeFrame(tl.tracks()[1].clips[0].id, sec(1), sec(1)).isNull());
        QVERIFY(tl.freezeFrame(second, sec(6), 0).isNull());

        // On V2 the clips after the freeze on that track move later, with
        // their linked audio; the storyline is untouched.
        const int v2 = tl.addTrack(TrackType::Video);
        const QUuid a = tl.placeMedia(m, 0, sec(2), true, true, v2, sec(20));
        const QUuid b = tl.placeMedia(m, 0, sec(2), true, true, v2, sec(23));
        const Ticks storyline = tl.tracks()[0].clips.back().timelineEnd();
        const QUuid f3 = tl.freezeFrame(a, sec(21), sec(1.5));
        QVERIFY(!f3.isNull());
        QCOMPARE(tl.clip(f3)->timelineStart, sec(21));
        QCOMPARE(tl.clip(f3)->freezeTime, sec(1));
        QCOMPARE(tl.clip(b)->timelineStart, sec(24.5));
        for (const auto& id : tl.linkedClips(b))
            QCOMPARE(tl.clip(id)->timelineStart, sec(24.5));
        QCOMPARE(tl.tracks()[0].clips.back().timelineEnd(), storyline);
    }

    void freezeFrameUndoes() {
        Timeline tl;
        QUndoStack stack;
        tl.insertMedia(QUuid::createUuid(), 0, sec(10), true, true);
        const TimelineState before = tl.state();
        const QUuid id = tl.tracks()[0].clips[0].id;
        QVERIFY(runTimelineEdit(&stack, &tl, "freeze", [&] { return !tl.freezeFrame(id, sec(5), sec(2)).isNull(); }));
        QCOMPARE(tl.duration(), sec(12));
        stack.undo();
        QCOMPARE(tl.state(), before);
        stack.redo();
        QCOMPARE(tl.duration(), sec(12));
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

    void subtitleFilesRoundTrip() {
        const QString srt = QStringLiteral(
            "1\r\n00:00:01,000 --> 00:00:02,500\r\n<i>Merhaba</i> dünya\r\n\r\n"
            "2\r\n00:00:03,000 --> 00:00:04,000\r\nİkinci\r\nsatır\r\n\r\n");
        QString err;
        const auto cues = subtitles::parse(srt, &err);
        QCOMPARE(int(cues.size()), 2);
        QCOMPARE(cues[0].start, ticksFromSeconds(1.0));
        QCOMPARE(cues[0].end, ticksFromSeconds(2.5));
        QCOMPARE(cues[0].text, QStringLiteral("Merhaba dünya"));
        QCOMPARE(cues[1].text, QStringLiteral("İkinci\nsatır"));
        QCOMPARE(subtitles::parse(subtitles::write(cues, subtitles::Format::Srt)), cues);
        const QString vtt = subtitles::write(cues, subtitles::Format::Vtt);
        QVERIFY(vtt.startsWith(QLatin1String("WEBVTT")));
        QVERIFY(vtt.contains(QLatin1String("00:00:01.000 --> 00:00:02.500")));
        QCOMPARE(subtitles::parse(vtt), cues);
        // VTT with cue settings and short times.
        const auto v = subtitles::parse(QStringLiteral("WEBVTT\n\n01:02.250 --> 01:03.000 align:start\nSelam\n"));
        QCOMPARE(int(v.size()), 1);
        QCOMPARE(v[0].start, ticksFromSeconds(62.25));
        QVERIFY(subtitles::parse(QStringLiteral("not a subtitle"), &err).empty());
        QVERIFY(!err.isEmpty());
        QCOMPARE(subtitles::isoLanguage(QStringLiteral("tr")), QStringLiteral("tur"));
    }

    void subtitleTrackEditing() {
        Timeline tl;
        const int s = tl.addSubtitleTrack({{ticksFromSeconds(1), ticksFromSeconds(2), QStringLiteral("a")},
                                           {ticksFromSeconds(1.5), ticksFromSeconds(3), QStringLiteral("b")}},
                                          QStringLiteral("tur"));
        QCOMPARE(tl.tracks()[s].name, QStringLiteral("S1"));
        QVERIFY(tl.tracks()[s].isSubtitle());
        QCOMPARE(int(tl.tracks()[s].clips.size()), 2);
        // Overlapping cues were made to follow each other.
        QCOMPARE(tl.tracks()[s].clips[0].timelineEnd(), ticksFromSeconds(1.5));
        const QUuid id = tl.addSubtitle(s, ticksFromSeconds(5), ticksFromSeconds(1), QStringLiteral("c"));
        QVERIFY(!id.isNull());
        QVERIFY(tl.setSubtitleText(id, QStringLiteral("d")));
        QVERIFY(tl.setDuration(id, ticksFromSeconds(2)));
        QVERIFY(tl.trimIn(id, -ticksFromSeconds(0.5)));
        const auto cues = tl.state().subtitleCues(s, ticksFromSeconds(1));
        QCOMPARE(int(cues.size()), 3);
        QCOMPARE(cues[0].start, Ticks(0));
        QCOMPARE(cues[2].text, QStringLiteral("d"));
        // Subtitles alone are no effect: a lossless cut stays possible.
        tl.insertMedia(QUuid::createUuid(), 0, ticksFromSeconds(4), true, true);
        QVERIFY(!tl.state().hasEffects());
        QVERIFY(tl.state().hasSubtitles());
        // Media cannot go onto a subtitle track.
        QVERIFY(tl.placeMedia(QUuid::createUuid(), 0, ticksFromSeconds(1), true, false, s, 0).isNull());
    }

};

QTEST_GUILESS_MAIN(TimelineTest)
#include "tst_timeline.moc"

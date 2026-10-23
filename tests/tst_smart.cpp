#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "TestMedia.h"
#include "ai/SmartCut.h"
#include "audio/Beats.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::test;

namespace {
Ticks sec(double s) { return ticksFromSeconds(s); }

// A click track: short decaying noise bursts every `period` seconds.
std::vector<float> clicks(double seconds, double first, double period, int rate) {
    std::vector<float> v(static_cast<size_t>(seconds * rate), 0.0f);
    quint32 seed = 11;
    for (double t = first; t < seconds; t += period) {
        const size_t at = static_cast<size_t>(t * rate);
        for (size_t i = 0; i < static_cast<size_t>(rate / 50) && at + i < v.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            const float noise = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
            v[at + i] += noise * 1.6f * std::exp(-static_cast<float>(i) / (rate / 400.0f));
        }
    }
    return v;
}
} // namespace

class SmartTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    MediaPool m_pool;
    MediaItemPtr m_tone;   // 4 s, 440 Hz tone
    MediaItemPtr m_silent; // 4 s, no sound

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        QString err;
        TestVideoSpec spec;
        QVERIFY2(writeTestVideo(m_dir.filePath("tone.mp4"), spec, &err), qPrintable(err));
        spec.audio = false;
        QVERIFY2(writeTestVideo(m_dir.filePath("silent.mp4"), spec, &err), qPrintable(err));
        m_tone = m_pool.import(m_dir.filePath("tone.mp4"));
        m_silent = m_pool.import(m_dir.filePath("silent.mp4"));
        QVERIFY(m_tone && m_silent);
    }

    void fillersAreRecognised() {
        for (const char* w : {"ııı", "Iıı,", "Eee...", "eh", "hmm", "Hmm.", "Um,", "uh", "ımm", "Ööö", "aah", "erm"})
            QVERIFY2(smartcut::isFiller(QString::fromUtf8(w)), w);
        for (const char* w : {"evet", "a", "e-posta", "bir", "the", "şey", "ama", "hı-hı-hı tamam", "um-brella", "", "..."})
            QVERIFY2(!smartcut::isFiller(QString::fromUtf8(w)), w);
    }

    void silencesKeepABreathAroundSpeech() {
        // 1 s quiet, 2 s speech, 1 s pause, 2 s speech, 0.3 s pause (too
        // short), 1 s speech, 1 s quiet at the end. 10 ms per level.
        std::vector<float> lv;
        auto add = [&](double s, float db) { lv.insert(lv.end(), static_cast<size_t>(std::lround(s * 100)), db); };
        add(1, -65), add(2, -18), add(1, -62), add(2, -20), add(0.3, -64), add(1, -19), add(1, -66);
        const double thr = smartcut::autoThreshold(lv);
        QVERIFY2(thr > -60 && thr < -25, qPrintable(QString::number(thr)));
        smartcut::Options o;
        const auto cuts = smartcut::findSilences(lv, o);
        QCOMPARE(cuts.size(), size_t(3));
        QCOMPARE(cuts[0].from, Ticks(0));
        QCOMPARE(cuts[0].to, sec(0.85));
        QCOMPARE(cuts[1].from, sec(3.15));
        QCOMPARE(cuts[1].to, sec(3.85));
        QCOMPARE(cuts[2].from, sec(7.45));
        QCOMPARE(cuts[2].to, sec(8.3)); // the end goes completely
        // A click inside a pause does not split it.
        lv[350] = -10;
        QCOMPARE(smartcut::findSilences(lv, o).size(), size_t(3));
    }

    void cutsMerge() {
        using smartcut::Cut;
        using smartcut::CutKind;
        std::vector<Cut> cuts{{sec(5), sec(6), CutKind::Silence, {}},
                              {sec(1), sec(2), CutKind::Silence, {}},
                              {sec(1.9), sec(2.4), CutKind::Filler, QStringLiteral("eee")},
                              {sec(8), sec(8.01), CutKind::Silence, {}}};
        const auto m = smartcut::merge(cuts, sec(0.05));
        QCOMPARE(m.size(), size_t(2));
        QCOMPARE(m[0].from, sec(1));
        QCOMPARE(m[0].to, sec(2.4));
        QCOMPARE(m[0].kind, CutKind::Filler);
        QCOMPARE(m[1].from, sec(5));
    }

    void silenceOnTheTimelineIsFoundAndCut() {
        // tone 2 s | silent video 2 s | tone 2 s, and music under it all on A2.
        Timeline tl;
        tl.insertMedia(m_tone->id, 0, sec(2), true, true);
        tl.insertMedia(m_silent->id, 0, sec(2), true, false);
        tl.insertMedia(m_tone->id, sec(2), sec(4), true, true);
        const int a2 = tl.addTrack(TrackType::Audio);
        const QUuid music = tl.placeMedia(m_tone->id, 0, sec(4), false, true, a2, 0);
        QVERIFY(!music.isNull());
        smartcut::Options o;
        auto r = smartcut::findCuts(tl.state(), m_pool, o);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.cuts.size(), size_t(1)); // the music does not hide the pause
        QVERIFY(std::llabs(r.cuts[0].from - sec(2.15)) <= sec(0.02));
        QVERIFY(std::llabs(r.cuts[0].to - sec(3.85)) <= sec(0.02));

        std::vector<std::pair<Ticks, Ticks>> ranges;
        for (const auto& c : r.cuts)
            ranges.emplace_back(c.from, c.to);
        QVERIFY(tl.removeRanges(ranges, true));
        const Ticks removed = r.cuts[0].to - r.cuts[0].from;
        QCOMPARE(tl.tracks()[0].clips.back().timelineEnd(), sec(6) - removed);
        // The music on A2 is untouched.
        const Clip* m = tl.clip(music);
        QVERIFY(m);
        QCOMPARE(m->timelineStart, Ticks(0));
        QCOMPARE(m->duration(), sec(4));
    }

    void beatsOfAClickTrack() {
        constexpr int rate = 22050;
        const auto pcm = clicks(12.0, 0.25, 0.5, rate); // 120 BPM
        const auto b = beats::detect(pcm, rate);
        QVERIFY2(std::abs(b.bpm - 120.0) < 2.0, qPrintable(QString::number(b.bpm)));
        QVERIFY(b.times.size() >= 20);
        for (double t : b.times) {
            const double k = std::round((t - 0.25) / 0.5);
            QVERIFY2(std::abs(t - (0.25 + k * 0.5)) < 0.03, qPrintable(QString::number(t)));
        }
        // 100 BPM.
        const auto b2 = beats::detect(clicks(12.0, 0.1, 0.6, rate), rate);
        QVERIFY2(std::abs(b2.bpm - 100.0) < 2.0, qPrintable(QString::number(b2.bpm)));
        // Silence has no beat.
        QVERIFY(beats::detect(std::vector<float>(rate * 5, 0.0f), rate).times.size() < 2);
    }

    void cutsFollowTheBeat() {
        Timeline tl;
        tl.setFrameRate({25, 1});
        const QUuid media = QUuid::createUuid();
        for (int i = 0; i < 4; ++i)
            tl.insertMedia(media, 0, sec(1.3), true, false);
        std::vector<Ticks> beats;
        for (int i = 0; i < 20; ++i)
            beats.push_back(sec(0.48 * i + 0.48)); // 12 frames apart
        auto dur = [](const QUuid&) { return sec(10); };
        // Snap: cuts at 1.3, 2.6, 3.9 move to the nearest beats.
        Timeline snap = Timeline();
        snap.setState(tl.state());
        QVERIFY(beats::cutToBeats(snap, beats, 0, dur) > 0);
        const auto& s = snap.tracks()[0].clips;
        QCOMPARE(s[0].timelineEnd(), sec(1.44));
        QCOMPARE(s[1].timelineEnd(), sec(2.88));
        QCOMPARE(s[2].timelineEnd(), sec(4.32));
        // Every 2 beats: the first clip runs to the second beat, then 2 beats each.
        QVERIFY(beats::cutToBeats(tl, beats, 2, dur) > 0);
        const auto& v = tl.tracks()[0].clips;
        QCOMPARE(v[0].timelineEnd(), sec(0.96));
        QCOMPARE(v[1].timelineEnd(), sec(1.92));
        QCOMPARE(v[2].timelineEnd(), sec(2.88));
        QCOMPARE(v[3].timelineEnd(), sec(3.84));
        // Not more than the media holds.
        Timeline shortMedia;
        shortMedia.setFrameRate({25, 1});
        shortMedia.insertMedia(media, 0, sec(0.6), true, false);
        shortMedia.insertMedia(media, 0, sec(0.6), true, false);
        beats::cutToBeats(shortMedia, beats, 4, [](const QUuid&) { return sec(1.2); });
        QCOMPARE(shortMedia.tracks()[0].clips[0].timelineEnd(), sec(0.96));
    }

    void musicClipIsFound() {
        Timeline tl;
        tl.insertMedia(m_tone->id, 0, sec(4), true, true);
        QVERIFY(beats::findMusicClip(tl.state()).isNull()); // only the sound of a video
        const int a2 = tl.addTrack(TrackType::Audio);
        const QUuid music = tl.placeMedia(m_tone->id, 0, sec(3), false, true, a2, 0);
        QCOMPARE(beats::findMusicClip(tl.state()), music);
        // The tone has no beat.
        const auto r = beats::clipBeats(tl.state(), m_pool, music);
        QVERIFY(!r.ok());
    }
};

QTEST_GUILESS_MAIN(SmartTest)
#include "tst_smart.moc"

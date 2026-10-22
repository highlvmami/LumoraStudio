#include <QTemporaryDir>
#include <QtTest>
#include <chrono>
#include <cmath>
#include <thread>

#include "TestMedia.h"
#include "media/MediaPool.h"
#include "render/AudioMixer.h"
#include "render/FrameRenderer.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::test;

namespace {
constexpr int kFps = 25;
Ticks frames(int n) { return n * frameDuration(AVRational{kFps, 1}); }
Ticks sec(double s) { return ticksFromSeconds(s); }

double rms(const std::vector<float>& s, size_t from, size_t to) {
    double sum = 0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(s[i]) * s[i];
    return to > from ? std::sqrt(sum / (to - from)) : 0.0;
}
} // namespace

class RenderTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    MediaPool m_pool;
    MediaItemPtr m_pattern, m_bright, m_dark;

    QImage renderAt(const Timeline& tl, Ticks t, FrameRenderer::Precision p = FrameRenderer::Precision::Final) {
        FrameRenderer r(&m_pool);
        r.setTimeline(tl.state());
        r.setOutputSize(QSize(320, 240));
        r.setPrecision(p);
        return r.render(t);
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        TestVideoSpec spec;
        QString err;
        QVERIFY2(writeTestVideo(m_dir.filePath("pattern.mp4"), spec, &err), qPrintable(err));
        spec.solidLuma = 200;
        QVERIFY(writeTestVideo(m_dir.filePath("bright.mp4"), spec, &err));
        spec.solidLuma = 50;
        QVERIFY(writeTestVideo(m_dir.filePath("dark.mp4"), spec, &err));
        m_pattern = m_pool.import(m_dir.filePath("pattern.mp4"));
        m_bright = m_pool.import(m_dir.filePath("bright.mp4"));
        m_dark = m_pool.import(m_dir.filePath("dark.mp4"));
        QVERIFY(m_pattern && m_bright && m_dark);
    }

    void showsTheRightSourceFrame() {
        Timeline tl;
        tl.insertMedia(m_pattern->id, frames(10), frames(60), true, true);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(5))), 15);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(5), FrameRenderer::Precision::Preview)), 15);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(49))), 59);
        // Outside the timeline: black.
        QVERIFY(meanLuma(renderAt(tl, frames(80)), QRect(0, 0, 320, 240)) < 1.0);
    }

    void hardwareDecodingShowsTheSameFrames() {
        // With or without a graphics card that decodes (without one it falls
        // back to software), the preview shows the same frames.
        Timeline tl;
        tl.insertMedia(m_pattern->id, frames(10), frames(60), true, true);
        FrameRenderer r(&m_pool);
        r.setTimeline(tl.state());
        r.setOutputSize(QSize(320, 240));
        r.setPrecision(FrameRenderer::Precision::Preview);
        r.setHardwareDecoding(true);
        QCOMPARE(frameIndexOf(r.render(frames(5))), 15);
        QCOMPARE(frameIndexOf(r.render(frames(6))), 16);
        QCOMPARE(frameIndexOf(r.render(frames(49))), 59);
    }

    void prefetchedDecodersShowTheRightFrames() {
        // A cut inside one file and a jump to another part of it: playback
        // prepares both decoders ahead and the frames stay exact.
        Timeline tl;
        tl.insertMedia(m_pattern->id, 0, frames(40), true, false);
        QVERIFY(tl.splitAt(frames(20)));
        tl.insertMedia(m_pattern->id, frames(60), frames(90), true, false);
        FrameRenderer r(&m_pool);
        r.setTimeline(tl.state());
        r.setOutputSize(QSize(320, 240));
        QCOMPARE(frameIndexOf(r.render(0)), 0);
        r.prefetch(0, sec(2));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (int f = 1; f < 70; ++f) {
            const int expected = f < 40 ? f : 60 + (f - 40);
            QCOMPARE(frameIndexOf(r.render(frames(f))), expected);
            r.prefetch(frames(f), sec(2));
        }
        QCOMPARE(r.prefetchedDecoders(), 2);
    }

    void crossDissolveBlendsBothClips() {
        Timeline tl;
        tl.insertMedia(m_bright->id, 0, sec(2), true, false);        // 2 s of media after the out point
        tl.insertMedia(m_dark->id, sec(1), sec(3), true, false);     // 1 s before the in point
        const QUuid second = tl.tracks()[0].clips[1].id;
        QVERIFY(tl.setTransition(second, {TransitionType::CrossDissolve, sec(1)}));
        const QRect all(0, 0, 320, 240);
        const double bright = meanLuma(renderAt(tl, sec(1)), all);
        const double dark = meanLuma(renderAt(tl, sec(3)), all);
        const double mid = meanLuma(renderAt(tl, sec(2)), all);
        QVERIFY2(bright > 200 && dark < 50, qPrintable(QStringLiteral("%1 %2").arg(bright).arg(dark)));
        QVERIFY2(std::abs(mid - (bright + dark) / 2) < 6, qPrintable(QString::number(mid)));
        // A quarter into the transition the outgoing clip dominates.
        const double quarter = meanLuma(renderAt(tl, sec(1.75)), all);
        QVERIFY(quarter > mid + 20);

        QVERIFY(tl.setTransition(second, {TransitionType::DipToBlack, sec(1)}));
        QVERIFY(meanLuma(renderAt(tl, sec(2)), all) < 3);
        QVERIFY(tl.setTransition(second, {TransitionType::WipeRight, sec(1)}));
        const QImage wipe = renderAt(tl, sec(2));
        QVERIFY(meanLuma(wipe, QRect(0, 0, 100, 240)) < 50);    // left: new (dark) clip
        QVERIFY(meanLuma(wipe, QRect(220, 0, 100, 240)) > 200); // right: old (bright) clip
    }

    void upperTracksAreCompositedWithTransform() {
        Timeline tl;
        tl.insertMedia(m_dark->id, 0, sec(4), true, false);
        const int v2 = tl.addTrack(TrackType::Video);
        const QUuid id = tl.placeMedia(m_bright->id, 0, sec(2), true, false, v2, sec(1));
        QVERIFY(tl.setTransform(id, {0.25, 0.0, 0.5, 0.0, 1.0})); // half size, right half of the frame
        const QImage img = renderAt(tl, sec(1.5));
        QVERIFY(meanLuma(img, QRect(10, 10, 100, 220)) < 50);    // storyline shows on the left
        QVERIFY(meanLuma(img, QRect(180, 80, 100, 80)) > 200);   // the inset on the right
        // Outside the V2 clip only the storyline is visible.
        QVERIFY(meanLuma(renderAt(tl, sec(3.5)), QRect(180, 80, 100, 80)) < 50);
        // Opacity 50 % blends with the storyline.
        QVERIFY(tl.setTransform(id, {0.0, 0.0, 1.0, 0.0, 0.5}));
        const double half = meanLuma(renderAt(tl, sec(1.5)), QRect(0, 0, 320, 240));
        QVERIFY2(half > 110 && half < 140, qPrintable(QString::number(half)));
        // Hidden tracks are not drawn.
        QVERIFY(tl.setTrackFlags(v2, false, false, true));
        QVERIFY(meanLuma(renderAt(tl, sec(1.5)), QRect(0, 0, 320, 240)) < 50);
    }

    void stickerPictureKeepsItsTransparency() {
        // A square PNG: transparent on the left, red on the right.
        QImage png(100, 100, QImage::Format_ARGB32);
        png.fill(Qt::transparent);
        for (int y = 0; y < 100; ++y)
            for (int x = 50; x < 100; ++x)
                png.setPixel(x, y, qRgba(255, 0, 0, 255));
        const QString path = m_dir.filePath("sticker.png");
        QVERIFY(png.save(path));
        auto sticker = m_pool.import(path);
        QVERIFY(sticker && sticker->info.kind == MediaKind::Image);

        Timeline tl;
        tl.insertMedia(m_dark->id, 0, sec(4), true, false);
        const int v2 = tl.addTrack(TrackType::Video);
        const QUuid id = tl.addStill(sticker->id, sec(2), v2, sec(1)); // fits as 240 x 240 in the middle
        QVERIFY(!id.isNull());
        QCOMPARE(tl.clip(id)->kind, ClipKind::Freeze);
        QVERIFY(tl.state().hasVideoEffects());
        for (auto precision : {FrameRenderer::Precision::Final, FrameRenderer::Precision::Preview}) {
            const QImage img = renderAt(tl, sec(1.5), precision).convertToFormat(QImage::Format_RGB32);
            const QRgb left = img.pixel(100, 120), right = img.pixel(220, 120);
            QVERIFY2(qRed(left) < 70 && qRed(left) == qGreen(left), qPrintable(QString::number(left, 16))); // the video shows through
            QVERIFY2(qRed(right) > 240 && qGreen(right) < 15, qPrintable(QString::number(right, 16)));
        }
        // Moved, shrunk and turned like any clip.
        QVERIFY(tl.setTransform(id, {-0.25, 0.0, 0.5, 180.0, 1.0}));
        const QImage moved = renderAt(tl, sec(1.5)).convertToFormat(QImage::Format_RGB32);
        QVERIFY(qRed(moved.pixel(60, 120)) > 240);  // turned: red is now on the left of the sticker
        QVERIFY(qRed(moved.pixel(220, 120)) < 70);
        // Pictures last as long as you like.
        QVERIFY(tl.trimOut(id, sec(30), 0));
        QCOMPARE(tl.clip(id)->duration(), sec(30));
        QVERIFY(qRed(renderAt(tl, sec(20)).convertToFormat(QImage::Format_RGB32).pixel(60, 120)) > 240);
    }

    void loopingClipsRepeatTheirMedia() {
        Timeline tl;
        const Ticks length = m_pattern->info.duration; // 100 frames
        tl.insertMedia(m_pattern->id, frames(10), length, true, false);
        const QUuid id = tl.tracks()[0].clips[0].id;
        QVERIFY(!tl.trimOut(id, frames(200), length)); // not past the media yet
        QVERIFY(tl.setLoop(id, true, length));
        QVERIFY(tl.state().hasVideoEffects());
        QVERIFY(tl.trimOut(id, frames(250), length));
        QCOMPARE(tl.clip(id)->duration(), frames(240));
        // Frames 10..99 play, then 10..99 again.
        QCOMPARE(frameIndexOf(renderAt(tl, frames(5))), 15);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(95))), 15);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(200))), 30);
        // Cutting keeps the rhythm.
        QVERIFY(tl.splitAt(frames(150)));
        QCOMPARE(frameIndexOf(renderAt(tl, frames(200))), 30);
        QVERIFY(!tl.canJoinWithNext(id) || tl.joinWithNext(id));
        // Off: back to what the media holds.
        const QUuid first = tl.tracks()[0].clips[0].id;
        QVERIFY(tl.setLoop(first, false, length));
        QVERIFY(tl.clip(first)->sourceOut <= length);
    }

    void fadesAndTitles() {
        Timeline tl;
        tl.insertMedia(m_bright->id, 0, sec(2), true, false);
        const QUuid clip = tl.tracks()[0].clips[0].id;
        QVERIFY(tl.setFades(clip, sec(1), 0));
        const QRect all(0, 0, 320, 240);
        QVERIFY(meanLuma(renderAt(tl, 0), all) < 3);
        const double half = meanLuma(renderAt(tl, sec(0.5)), all);
        QVERIFY2(half > 90 && half < 130, qPrintable(QString::number(half)));

        const int v2 = tl.addTrack(TrackType::Video);
        TitleData title;
        title.text = QStringLiteral("LUMORA");
        title.size = 0.2;
        title.y = 0.5;
        title.color = QColor(255, 0, 0);
        title.shadow = false;
        title.animationIn = TitleAnimation::None;
        tl.addTitle(title, sec(1), v2, sec(1));
        const QImage img = renderAt(tl, sec(1.5));
        int red = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c = img.pixelColor(x, y);
                red += c.red() > 200 && c.green() < 60 ? 1 : 0;
            }
        QVERIFY2(red > 500, qPrintable(QString::number(red)));
    }

    void passthroughOnlyForPlainFrames() {
        Timeline tl;
        tl.insertMedia(m_pattern->id, 0, sec(2), true, false);
        FrameRenderer r(&m_pool);
        r.setTimeline(tl.state());
        r.setOutputSize(QSize(320, 240));
        const AVFrame* f = r.passthroughFrame(frames(7));
        QVERIFY(f);
        QCOMPARE(frameIndexOf(f), 7);
        r.setOutputSize(QSize(640, 480));
        QVERIFY(!r.passthroughFrame(frames(7)));
        r.setOutputSize(QSize(320, 240));
        tl.setFades(tl.tracks()[0].clips[0].id, sec(1), 0);
        r.setTimeline(tl.state());
        QVERIFY(!r.passthroughFrame(frames(7)));
        QVERIFY(r.passthroughFrame(sec(1.5)));
    }

    void clipColourIsRenderedInPreviewAndExport() {
        Timeline tl;
        tl.insertMedia(m_dark->id, 0, sec(2), true, false);
        const QUuid id = tl.tracks()[0].clips[0].id;
        const QRect all(0, 0, 320, 240);
        const double before = meanLuma(renderAt(tl, sec(1)), all);
        QVERIFY(!tl.state().hasEffects());
        ColorGrade g;
        g.exposure = 1.0;
        QVERIFY(tl.setColor(id, g));
        QVERIFY(!tl.setColor(id, g)); // unchanged
        QVERIFY(tl.state().hasEffects());
        const double final = meanLuma(renderAt(tl, sec(1)), all);
        const double preview = meanLuma(renderAt(tl, sec(1), FrameRenderer::Precision::Preview), all);
        QVERIFY2(final > before + 12, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(final)));
        QVERIFY(std::abs(final - preview) < 2);
        FrameRenderer r(&m_pool);
        r.setTimeline(tl.state());
        r.setOutputSize(QSize(320, 240));
        QVERIFY(!r.passthroughFrame(sec(1)));
    }

    void mixerGainFadesAndMute() {
        Timeline tl;
        tl.insertMedia(m_pattern->id, 0, sec(4), true, true);
        AudioMixer mixer(&m_pool, 48000, 2);
        mixer.setTimeline(tl.state());
        std::vector<float> buf(48000 * 2);
        mixer.mix(48000, 48000, buf.data());
        const double full = rms(buf, 0, buf.size());
        QVERIFY2(std::abs(full - 0.2 / std::sqrt(2.0)) < 0.01, qPrintable(QString::number(full)));

        tl.setGain(tl.tracks()[1].clips[0].id, -6.0206);
        mixer.setTimeline(tl.state());
        mixer.mix(48000, 48000, buf.data());
        QVERIFY(std::abs(rms(buf, 0, buf.size()) - full / 2) < 0.01);

        tl.setTrackFlags(1, false, true, false);
        mixer.setTimeline(tl.state());
        mixer.mix(48000, 48000, buf.data());
        QCOMPARE(rms(buf, 0, buf.size()), 0.0);
    }

    void mixerPlacesAudioAtTheRightTime() {
        Timeline tl;
        const int a2 = tl.addTrack(TrackType::Audio);
        tl.placeMedia(m_pattern->id, 0, sec(1), false, true, a2, sec(1));
        AudioMixer mixer(&m_pool, 48000, 2);
        mixer.setTimeline(tl.state());
        std::vector<float> buf(96000 * 2);
        mixer.mix(0, 96000, buf.data());
        QCOMPARE(rms(buf, 0, 47000 * 2), 0.0);                  // silence before 1 s
        QVERIFY(rms(buf, 49000 * 2, 95000 * 2) > 0.1);          // the clip after
    }

    void mixerCrossfadesUnderTransitions() {
        Timeline tl;
        tl.insertMedia(m_pattern->id, 0, sec(2), true, true);
        tl.insertMedia(m_pattern->id, sec(2), sec(4), true, true);
        QVERIFY(tl.setTransition(tl.tracks()[0].clips[1].id, {TransitionType::CrossDissolve, sec(1)}));
        AudioMixer mixer(&m_pool, 48000, 2);
        mixer.setTimeline(tl.state());
        std::vector<float> buf(48000 * 2);
        mixer.mix(72000, 48000, buf.data()); // 1.5 s .. 2.5 s: the whole transition
        // Equal-power crossfade of two copies of the same tone: never silent.
        for (int part = 0; part < 10; ++part)
            QVERIFY(rms(buf, part * 9600, (part + 1) * 9600) > 0.05);
    }
};

QTEST_MAIN(RenderTest)
#include "tst_render.moc"

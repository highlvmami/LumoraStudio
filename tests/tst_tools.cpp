// Speed changes, picture effects, colour filters, title templates, collages,
// GIF export, live recording and batch photo processing.

#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "TestMedia.h"
#include "color/ColorGrade.h"
#include "color/ColorProcessor.h"
#include "color/Looks.h"
#include "export/GifExport.h"
#include "export/LiveWriter.h"
#include "ffmpeg/FFmpeg.h"
#include "image/BatchPhoto.h"
#include "media/MediaPool.h"
#include "project/Project.h"
#include "render/Collage.h"
#include "render/Effects.h"
#include "render/FrameRenderer.h"
#include "render/TitleTemplates.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::test;

namespace {
constexpr int kFps = 25;
Ticks frames(int n) { return n * frameDuration(AVRational{kFps, 1}); }

int countVideoFrames(const QString& path, double* seconds = nullptr, bool* audio = nullptr) {
    auto in = av::openInput(path);
    if (!in)
        return -1;
    int video = -1;
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        if (in->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video < 0)
            video = int(i);
        if (audio && in->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            *audio = true;
    }
    int n = 0;
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(in.get(), pkt) >= 0) {
        n += pkt->stream_index == video;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    if (seconds)
        *seconds = in->duration > 0 ? in->duration / double(AV_TIME_BASE) : 0;
    return n;
}
} // namespace

class ToolsTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    MediaPool m_pool;
    MediaItemPtr m_pattern;

    QImage renderAt(const Timeline& tl, Ticks t) {
        FrameRenderer r(&m_pool);
        r.setTimeline(tl.state());
        r.setOutputSize(QSize(320, 240));
        r.setPrecision(FrameRenderer::Precision::Final);
        return r.render(t);
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        TestVideoSpec spec;
        QString err;
        QVERIFY2(writeTestVideo(m_dir.filePath("pattern.mp4"), spec, &err), qPrintable(err));
        m_pattern = m_pool.import(m_dir.filePath("pattern.mp4"));
        QVERIFY(m_pattern);
    }

    // --- speed ---------------------------------------------------------------

    void constantSpeedMapsTime() {
        TimeRemap r;
        r.speed = 2.0;
        r.mediaIn = 0;
        r.mediaOut = frames(100);
        r.length = frames(50);
        QCOMPARE(r.map(frames(10)), frames(20));
        QCOMPARE(r.unmap(frames(20)), frames(10));
        QCOMPARE(r.mediaRate(frames(10)), 2.0);
        r.reverse = true;
        QCOMPARE(r.map(0), frames(100) - 1);
        QVERIFY(r.mediaRate(frames(10)) < 0);
    }

    void rampIsInvertible() {
        TimeRemap r;
        r.ramp = speedRampPoints(SpeedRampPreset::Hero);
        QVERIFY(!r.ramp.empty());
        r.mediaIn = frames(10);
        r.mediaOut = frames(90);
        r.length = Ticks(double(r.mediaOut - r.mediaIn) / r.rampAverage());
        Ticks last = -1;
        for (int i = 0; i <= 20; ++i) {
            const Ticks v = r.length * i / 20;
            const Ticks m = r.map(v);
            QVERIFY(m >= last); // never runs backwards
            last = m;
            QVERIFY(std::llabs(r.unmap(m) - v) < frames(1));
        }
        QVERIFY(std::llabs(r.map(0) - frames(10)) < frames(1) / 10);
        QVERIFY(std::llabs(r.map(r.length) - frames(90)) < frames(1) / 2);
        QCOMPARE(speedRampPresetOf(r.ramp), SpeedRampPreset::Hero);
    }

    void doubleSpeedShowsEverySecondFrame() {
        Timeline tl;
        const QUuid id = tl.insertMedia(m_pattern->id, frames(10), frames(60), true, true);
        QVERIFY(tl.setSpeed(id, 2.0, false, {}, m_pattern->info.duration));
        const Clip* c = tl.clip(id);
        QVERIFY(c->hasSpeedChange());
        QCOMPARE(c->duration(), frames(25));
        QCOMPARE(c->mediaRange().first, frames(10));
        QCOMPARE(frameIndexOf(renderAt(tl, frames(0))), 10);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(5))), 20);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(20))), 50);
        // The linked sound follows.
        for (const QUuid& l : tl.linkedClips(id))
            QCOMPARE(tl.clip(l)->duration(), frames(25));
        // Back to normal speed: the original range again.
        QVERIFY(tl.setSpeed(id, 1.0, false, {}, m_pattern->info.duration));
        QCOMPARE(tl.clip(id)->duration(), frames(50));
        QCOMPARE(frameIndexOf(renderAt(tl, frames(5))), 15);
    }

    void reversePlaysBackwards() {
        Timeline tl;
        const QUuid id = tl.insertMedia(m_pattern->id, frames(10), frames(60), true, false);
        QVERIFY(tl.setSpeed(id, 1.0, true, {}, m_pattern->info.duration));
        QCOMPARE(tl.clip(id)->duration(), frames(50));
        QCOMPARE(frameIndexOf(renderAt(tl, frames(0))), 59);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(49))), 10);
        QCOMPARE(frameIndexOf(renderAt(tl, frames(20))), 39);
    }

    void speedSurvivesSplitAndSave() {
        Timeline tl;
        const QUuid id = tl.insertMedia(m_pattern->id, 0, frames(80), true, false);
        QVERIFY(tl.setSpeed(id, 2.0, false, {}, m_pattern->info.duration));
        QVERIFY(tl.splitAt(frames(20)));
        QCOMPARE(frameIndexOf(renderAt(tl, frames(25))), 50);
        TimeRemap r;
        r.speed = 0.5;
        r.ramp = speedRampPoints(SpeedRampPreset::Montage);
        r.length = frames(40);
        r.mediaOut = frames(20);
        QCOMPARE(TimeRemap::fromJson(r.toJson()), r);
    }

    // --- picture effects ------------------------------------------------------

    void chromaKeyRemovesTheScreen() {
        QImage img(64, 32, QImage::Format_ARGB32);
        img.fill(QColor(0, 177, 64));
        QPainter(&img).fillRect(QRect(32, 0, 32, 32), QColor(200, 80, 60));
        PictureEffects e;
        e.chromaKey.enabled = true;
        fx::applyPictureEffects(img, e);
        QVERIFY(img.hasAlphaChannel());
        QVERIFY(qAlpha(img.pixel(5, 5)) < 10);
        QVERIFY(qAlpha(img.pixel(50, 5)) > 245);
        // The subject keeps its colour.
        QVERIFY(std::abs(qRed(img.pixel(50, 5)) - 200) < 8);
    }

    void denoiseSmoothsGrain() {
        QImage img(64, 64, QImage::Format_RGB32);
        quint32 seed = 1;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                seed = seed * 1664525u + 1013904223u;
                const int v = 128 + int((seed >> 24) % 41) - 20;
                img.setPixel(x, y, qRgb(v, v, v));
            }
        auto spread = [](const QImage& im) {
            double s = 0, s2 = 0;
            for (int y = 0; y < im.height(); ++y)
                for (int x = 0; x < im.width(); ++x) {
                    const int v = qGray(im.pixel(x, y));
                    s += v;
                    s2 += v * v;
                }
            const double n = im.width() * im.height();
            return std::sqrt(s2 / n - (s / n) * (s / n));
        };
        const double before = spread(img);
        PictureEffects e;
        e.denoise = 0.8;
        fx::applyPictureEffects(img, e);
        QVERIFY(spread(img) < before * 0.6);
    }

    void inBetweenFramesAreBetween() {
        QImage a(32, 32, QImage::Format_ARGB32_Premultiplied), b = a;
        a.fill(QColor(0, 0, 0));
        b.fill(QColor(200, 200, 200));
        const QImage mid = fx::blendFrames(a, b, 0.5);
        QVERIFY(std::abs(qRed(mid.pixel(10, 10)) - 100) <= 2);
        // A square that moves: the in-between picture has it half way.
        QImage c(96, 64, QImage::Format_ARGB32_Premultiplied), d = c;
        c.fill(Qt::black);
        d.fill(Qt::black);
        QPainter(&c).fillRect(QRect(20, 20, 20, 20), Qt::white);
        QPainter(&d).fillRect(QRect(28, 20, 20, 20), Qt::white);
        const QImage half = fx::interpolateFrames(c, d, 0.5);
        QCOMPARE(half.size(), c.size());
        QVERIFY(qGray(half.pixel(30, 30)) > 200);  // inside both
        QVERIFY(qGray(half.pixel(42, 30)) > 170); // where it is half way
        QVERIFY(qGray(half.pixel(22, 30)) < 85);  // where it has left (a plain blend gives grey)
    }

    // --- colour filters and titles ------------------------------------------------

    void looksChangeTheColours() {
        QVERIFY(looks().size() >= 10);
        const Look* bw = findLook(QStringLiteral("bw"));
        QVERIFY(bw);
        ColorGrade g;
        g.look = bw->id;
        QVERIFY(!g.isIdentity());
        QImage img(8, 8, QImage::Format_RGBA64);
        img.fill(QColor(220, 60, 40));
        const QImage out = ColorProcessor(g).applied(img);
        const QColor px = out.pixelColor(4, 4);
        QVERIFY(std::abs(px.red() - px.green()) < 6 && std::abs(px.green() - px.blue()) < 6);
        g.lookAmount = 0.0;
        const QColor none = ColorProcessor(g).applied(img).pixelColor(4, 4);
        QVERIFY(std::abs(none.red() - 220) < 3);
        // Stored with the grade.
        g.lookAmount = 0.6;
        QCOMPARE(ColorGrade::fromJson(g.toJson()), g);
    }

    void titleTemplatesKeepTheText() {
        QVERIFY(titleTemplates().size() >= 10);
        TitleData t;
        t.text = QStringLiteral("Merhaba");
        for (const auto& tpl : titleTemplates()) {
            const TitleData out = applyTitleTemplate(t, tpl);
            QCOMPARE(out.text, t.text);
        }
        const TitleTemplate* lower = findTitleTemplate(QStringLiteral("lowerThird"));
        QVERIFY(lower);
        const TitleData out = applyTitleTemplate(t, *lower);
        QCOMPARE(TitleData::fromJson(out.toJson()), out);
    }

    // --- project file --------------------------------------------------------------

    void projectKeepsSpeedAndEffects() {
        Project p;
        auto item = p.media()->import(m_dir.filePath("pattern.mp4"));
        QVERIFY(item);
        const QUuid id = p.timeline()->insertMedia(item->id, 0, frames(50), true, true);
        QVERIFY(p.timeline()->setSpeed(id, 0.5, true, {}, item->info.duration));
        PictureEffects e;
        e.chromaKey.enabled = true;
        e.vignette = 0.4;
        QVERIFY(p.timeline()->setPictureEffects(id, e));
        const QString path = m_dir.filePath("speed.lumora");
        QString err;
        QVERIFY2(p.save(path, &err), qPrintable(err));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("version")).toInt(), 8);
        Project q;
        QVERIFY2(q.load(path, &err), qPrintable(err));
        const Clip* c = q.timeline()->clip(id);
        QVERIFY(c);
        QCOMPARE(c->remap, p.timeline()->clip(id)->remap);
        QCOMPARE(c->effects, e);
        QCOMPARE(c->duration(), frames(100));
    }

    // --- collage ---------------------------------------------------------------------

    void collageCellsAreFilled() {
        const CollageLayout* side = findCollageLayout(QStringLiteral("sideBySide"));
        QVERIFY(side);
        QCOMPARE(int(side->cells.size()), 2);
        // A 16:9 video in the left half of a 16:9 frame: full height, cut at the sides.
        const ClipTransform t = collageTransform(side->cells[0], 16.0 / 9, 16.0 / 9);
        QCOMPARE(t.scale, 1.0);
        QCOMPARE(t.x, -0.25);
        QCOMPARE(t.y, 0.0);
        QCOMPARE(t.crop, QRectF(0.25, 0, 0.5, 1));
        // Rendered: the left half shows the clip, the right half what is below.
        Timeline tl;
        tl.insertMedia(m_pattern->id, 0, frames(50), true, false);
        const int v2 = tl.addTrack(TrackType::Video);
        TimelineState state = tl.state();
        Clip c = state.tracks[0].clips[0];
        QVERIFY(tl.placeMedia(m_pattern->id, 0, frames(50), true, false, v2, 0).isNull() == false);
        const QUuid top = tl.tracks()[v2].clips.front().id;
        QVERIFY(tl.setTransform(top, collageTransform(side->cells[0], 4.0 / 3, 4.0 / 3, 0.0)));
        QVERIFY(tl.clip(top)->transform.hasCrop());
        const ClipTransform g = collageTransform(side->cells[1], 4.0 / 3, 4.0 / 3, 0.02);
        QVERIFY(g.x > 0 && g.crop.width() < 0.5);
    }

    // --- GIF ---------------------------------------------------------------------------

    void paletteAndQuantize() {
        QImage img(40, 20, QImage::Format_RGB32);
        img.fill(QColor(255, 0, 0));
        QPainter(&img).fillRect(QRect(20, 0, 20, 20), QColor(0, 0, 255));
        const QVector<QRgb> pal = gif::makePalette({img}, 16);
        QVERIFY(pal.size() >= 2 && pal.size() <= 16);
        const auto idx = gif::quantize(img, pal, true);
        QCOMPARE(pal[idx[0]], qRgb(255, 0, 0));
        QCOMPARE(pal[idx[30]], qRgb(0, 0, 255));
    }

    void exportsAGif() {
        Timeline tl;
        tl.insertMedia(m_pattern->id, 0, frames(50), true, false);
        GifSettings g;
        g.from = 0;
        g.to = frames(25);
        g.width = 160;
        g.fps = 10;
        g.colors = 64;
        const QString path = m_dir.filePath("clip.gif");
        QString err;
        QVERIFY2(gif::exportTimeline(tl.state(), m_pool, g, path, {}, nullptr, &err), qPrintable(err));
        double seconds = 0;
        QCOMPARE(countVideoFrames(path, &seconds), 10);
        QVERIFY(std::abs(seconds - 1.0) < 0.15);
        const QImage first(path);
        QCOMPARE(first.width(), 160);
    }

    // --- recording ---------------------------------------------------------------------

    void liveWriterWritesVideoAndSound() {
        const QString path = m_dir.filePath("rec.mp4");
        LiveWriter w;
        LiveWriter::Options o;
        o.size = QSize(320, 180);
        o.frameRate = 10;
        o.sampleRate = 48000;
        o.channels = 1;
        QString err;
        QVERIFY2(w.open(path, o, &err), qPrintable(err));
        std::vector<std::int16_t> tone(4800);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = std::int16_t(8000 * std::sin(i * 0.05));
        for (int i = 0; i < 20; ++i) {
            QImage img(320, 180, QImage::Format_RGB32);
            img.fill(QColor(i * 10, 100, 200));
            w.addPicture(img, i * 100);
            w.addSound(tone.data(), int(tone.size()));
        }
        QVERIFY2(w.finish(&err), qPrintable(err));
        bool audio = false;
        double seconds = 0;
        QCOMPARE(countVideoFrames(path, &seconds, &audio), 20);
        QVERIFY(audio);
        QVERIFY(std::abs(seconds - 2.0) < 0.3);
    }

    // --- batch photos ------------------------------------------------------------------

    void batchResizesMarksAndRenames() {
        QImage photo(400, 200, QImage::Format_RGB32);
        photo.fill(QColor(30, 60, 90));
        const QString src = m_dir.filePath("holiday.png");
        QVERIFY(photo.save(src));
        BatchPhotoSettings s;
        s.resize = BatchPhotoSettings::Resize::LongEdge;
        s.pixels = 100;
        s.watermark = BatchPhotoSettings::Watermark::Text;
        s.watermarkText = QStringLiteral("LUMORA");
        s.watermarkOpacity = 1.0;
        s.watermarkSize = 0.5;
        s.format.format = PhotoFormat::Png;
        s.folder = m_dir.filePath("out");
        s.pattern = QStringLiteral("{n}-{name}");
        BatchPhoto batch(s);
        QCOMPARE(batch.targetSize(QSize(400, 200)), QSize(100, 50));
        QCOMPARE(batch.targetSize(QSize(50, 20)), QSize(50, 20)); // not enlarged
        const QString out = batch.outputPath(src, 2, 12);
        QCOMPARE(QFileInfo(out).fileName(), QStringLiteral("03-holiday.png"));
        QString err;
        QVERIFY2(batch.process(src, out, &err), qPrintable(err));
        const QImage result(out);
        QCOMPARE(result.size(), QSize(100, 50));
        // The mark is in the lower right; the upper left is untouched.
        int bright = 0;
        for (int y = 25; y < 50; ++y)
            for (int x = 50; x < 100; ++x)
                bright += qGray(result.pixel(x, y)) > 150;
        QVERIFY(bright > 20);
        QVERIFY(qGray(result.pixel(5, 5)) < 80);
        // The original is never the target.
        BatchPhotoSettings same = s;
        same.folder.clear();
        same.pattern = QStringLiteral("{name}");
        QVERIFY(BatchPhoto(same).outputPath(src, 0, 1) != src);
    }
};

QTEST_MAIN(ToolsTest)
#include "tst_tools.moc"

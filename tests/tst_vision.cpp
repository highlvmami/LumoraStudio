#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "TestMedia.h"
#include "ai/Redaction.h"
#include "ai/Reframe.h"
#include "ai/Vision.h"
#include "image/PhotoAdjustments.h"
#include "image/PhotoProcessor.h"
#include "media/MediaPool.h"
#include "render/FrameRenderer.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::test;

namespace {

Ticks frames(int n) { return frameDuration({25, 1}) * n; }

// Busy picture: a checkerboard of 4 px squares.
QImage checkers(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, ((x / 4 + y / 4) % 2) ? qRgb(240, 240, 240) : qRgb(10, 10, 10));
    return img;
}

double variance(const QImage& img, const QRect& r) {
    double sum = 0, sq = 0;
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            const double v = qGray(img.pixel(x, y));
            sum += v;
            sq += v * v;
            ++n;
        }
    const double mean = sum / n;
    return sq / n - mean * mean;
}

bool modelsPresent() { return !qEnvironmentVariable("LUMORA_VISION_MODELS").isEmpty() && ai::visionAvailable(); }

// A car's number plate, drawn: white plate, black letters, blue strip.
QImage plateScene() {
    QImage img(960, 640, QImage::Format_RGB32);
    img.fill(QColor(90, 110, 130));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(150, 20, 25));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(160, 220, 640, 300), 40, 40); // the back of the car
    p.setBrush(QColor(30, 30, 30));
    p.drawRect(QRectF(160, 470, 640, 50));
    const QRectF plate(340, 380, 280, 62);
    p.setBrush(Qt::white);
    p.setPen(QPen(Qt::black, 3));
    p.drawRoundedRect(plate, 6, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 60, 170));
    p.drawRect(QRectF(plate.left() + 2, plate.top() + 2, 26, plate.height() - 4));
    p.setPen(Qt::black);
    QFont f(QStringLiteral("DejaVu Sans"));
    f.setPixelSize(44);
    f.setBold(true);
    p.setFont(f);
    p.drawText(plate.adjusted(30, 0, 0, 0), Qt::AlignCenter, QStringLiteral("34 ABC 123"));
    return img;
}

} // namespace

class VisionTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    MediaPool m_pool;

private slots:
    void trackBoxesAreInterpolated() {
        RedactionTrack t;
        t.boxes = {{0, QRectF(0.1, 0.1, 0.2, 0.2)}, {frames(10), QRectF(0.3, 0.1, 0.2, 0.4)}};
        const QRectF mid = t.rectAt(frames(5), 0);
        QVERIFY(std::abs(mid.x() - 0.2) < 1e-9 && std::abs(mid.height() - 0.3) < 1e-9);
        QVERIFY(t.rectAt(frames(12), frames(1)).isEmpty());
        QCOMPARE(t.rectAt(frames(11), frames(1)), QRectF(0.3, 0.1, 0.2, 0.4));

        Redaction r;
        r.enabled = true;
        r.tracks.push_back(t);
        RedactionTrack plate;
        plate.kind = RedactionKind::Plate;
        plate.boxes = {{0, QRectF(0.6, 0.6, 0.1, 0.05)}};
        r.tracks.push_back(plate);
        QCOMPARE(r.regionsAt(frames(2)).size(), size_t(2));
        QVERIFY(r.regionsAt(frames(2)).front().ellipse);
        r.plates = false;
        QCOMPARE(r.regionsAt(frames(2)).size(), size_t(1));
        r.tracks[0].enabled = false;
        QVERIFY(!r.isActive());
    }

    void redactionSurvivesJson() {
        Redaction r;
        r.enabled = true;
        r.style = RedactionStyle::Pixelate;
        r.strength = 0.25;
        r.plates = false;
        r.analyzedFrom = frames(2);
        r.analyzedTo = frames(9);
        RedactionTrack t;
        t.kind = RedactionKind::Plate;
        t.enabled = false;
        t.boxes = {{frames(3), QRectF(0.125, 0.25, 0.5, 0.0625)}, {frames(4), QRectF(0.25, 0.25, 0.5, 0.0625)}};
        r.tracks.push_back(t);
        QCOMPARE(Redaction::fromJson(r.toJson()), r);

        PhotoAdjustments a;
        a.redaction = r;
        a.background.enabled = true;
        a.background.model = QStringLiteral("quality");
        a.background.softness = 0.25;
        QVERIFY(a.hasToneChanges());
        QCOMPARE(PhotoAdjustments::fromJson(a.toJson()), a);
    }

    void trackerJoinsDetectionsFrameToFrame() {
        ai::DetectionTracker tracker(RedactionKind::Face, frames(12));
        for (int i = 0; i < 30; ++i) {
            if (i >= 12 && i < 18)
                continue; // turned away for a moment: the gap is bridged
            std::vector<QRectF> boxes{QRectF(0.1 + 0.01 * i, 0.2, 0.1, 0.15)};
            std::vector<float> scores{0.9f};
            if (i >= 5 && i < 25) {
                boxes.push_back(QRectF(0.7, 0.5, 0.08, 0.12)); // a second, still face
                scores.push_back(0.85f);
            }
            if (i == 20) {
                boxes.push_back(QRectF(0.45, 0.8, 0.05, 0.05)); // noise
                scores.push_back(0.62f);
            }
            tracker.add(frames(i), boxes, scores);
        }
        const auto tracks = tracker.finish();
        QCOMPARE(tracks.size(), size_t(2));
        QCOMPARE(tracks[0].start(), Ticks(0));
        QCOMPARE(tracks[0].end(), frames(29));
        QCOMPARE(tracks[1].start(), frames(5));
        // Linear movement is stored with very few boxes.
        QVERIFY2(tracks[0].boxes.size() < 9, qPrintable(QString::number(tracks[0].boxes.size())));
        const QRectF at15 = tracks[0].rectAt(frames(15), 0);
        QVERIFY(std::abs(at15.center().x() - (0.1 + 0.15 + 0.05)) < 0.01);
    }

    void regionsAreHidden() {
        const QImage src = checkers(200, 100);
        const QRect inside(60, 20, 60, 50);
        const std::vector<Redaction::Region> regions{{QRectF(0.3, 0.2, 0.3, 0.5), false}};
        for (auto style : {RedactionStyle::Blur, RedactionStyle::Pixelate, RedactionStyle::Fill}) {
            QImage img = src;
            ai::hideRegions(img, regions, style, 1.0);
            QVERIFY(variance(img, inside.adjusted(4, 4, -4, -4)) < variance(src, inside) / 4);
            // Outside the region nothing changes.
            QCOMPARE(img.copy(0, 0, 55, 100), src.copy(0, 0, 55, 100));
            QCOMPARE(img.copy(125, 0, 75, 100), src.copy(125, 0, 75, 100));
            if (style == RedactionStyle::Fill)
                QCOMPARE(qGray(img.pixel(90, 45)), 0);
        }
    }

    void matteMakesTheBackgroundTransparent() {
        QImage matte(100, 50, QImage::Format_Grayscale8);
        matte.fill(0);
        for (int y = 0; y < 50; ++y)
            std::fill_n(matte.scanLine(y), 50, uchar(255));
        QImage img(100, 50, QImage::Format_RGB32);
        img.fill(Qt::red);
        ai::applyMatte(img, matte, 0.5);
        QCOMPARE(qAlpha(img.pixel(10, 10)), 255);
        QCOMPARE(qAlpha(img.pixel(90, 10)), 0);
        QCOMPARE(qRed(img.pixel(10, 10)), 255);

        QImage deep(100, 50, QImage::Format_RGBA64);
        deep.fill(Qt::white);
        ai::applyMatte(deep, matte, 0.0);
        QCOMPARE(deep.format(), QImage::Format_RGBA64);
        QCOMPARE(deep.pixelColor(10, 10).alpha(), 255);
        QCOMPARE(deep.pixelColor(90, 10).alpha(), 0);
    }

    void rendererHidesTheRegions() {
        QVERIFY(m_dir.isValid());
        TestVideoSpec spec;
        spec.seconds = 1;
        spec.solidLuma = 200;
        QString err;
        QVERIFY2(writeTestVideo(m_dir.filePath("bright.mp4"), spec, &err), qPrintable(err));
        auto media = m_pool.import(m_dir.filePath("bright.mp4"));
        QVERIFY(media);
        Timeline tl;
        const QUuid id = tl.insertMedia(media->id, 0, frames(25), true, false);
        Redaction r;
        r.enabled = true;
        r.style = RedactionStyle::Fill;
        RedactionTrack t;
        t.kind = RedactionKind::Plate;
        t.boxes = {{0, QRectF(0.0, 0.0, 0.5, 0.5)}, {frames(24), QRectF(0.0, 0.0, 0.5, 0.5)}};
        r.tracks.push_back(t);
        QVERIFY(tl.setRedaction(id, r));
        QVERIFY(tl.state().hasVideoEffects());
        for (auto precision : {FrameRenderer::Precision::Preview, FrameRenderer::Precision::Final}) {
            FrameRenderer fr(&m_pool);
            fr.setTimeline(tl.state());
            fr.setOutputSize(QSize(320, 240));
            fr.setPrecision(precision);
            QVERIFY(!fr.passthroughFrame(frames(5)));
            const QImage img = fr.render(frames(5));
            QVERIFY(meanLuma(img, QRect(10, 10, 100, 90)) < 5);
            QVERIFY(meanLuma(img, QRect(200, 150, 100, 80)) > 150);
        }
    }

    void facesAreFound() {
        const QString path = qEnvironmentVariable("LUMORA_FACE_IMAGE");
        if (!modelsPresent() || path.isEmpty())
            QSKIP("set LUMORA_VISION_MODELS and LUMORA_FACE_IMAGE to run");
        const QImage img(path);
        QVERIFY(!img.isNull());
        QString err;
        const auto faces = ai::detectFaces(img, 0.6f, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(faces.size(), size_t(1));
        const QRectF f = faces.front().rect;
        QVERIFY(f.width() > img.width() / 8.0 && f.width() < img.width() * 0.8);
        // Upside down there is no face for YuNet; the result must not crash
        // and the big picture path (tiles) must find the face too.
        const QImage big = img.scaled(img.width() * 4, img.height() * 4);
        QVERIFY(!ai::detectFaces(big, 0.6f, &err).empty());

        const auto photo = ai::findRedactions(img, ai::RedactionOptions{true, false});
        QVERIFY2(photo.ok(), qPrintable(photo.error));
        QCOMPARE(photo.tracks.size(), size_t(1));
        QVERIFY(photo.tracks.front().boxes.front().rect.contains(QRectF(f.x() / img.width(), f.y() / img.height(),
                                                                        f.width() / img.width(), f.height() / img.height())));

        // Photo pipeline: the face area is hidden, the rest untouched.
        PhotoAdjustments a;
        a.redaction.enabled = true;
        a.redaction.tracks = photo.tracks;
        const QImage out = PhotoProcessor::apply(img.convertToFormat(QImage::Format_RGBA64), a);
        const QRect fr = f.toRect();
        QVERIFY(variance(out.convertToFormat(QImage::Format_RGB32), fr.adjusted(fr.width() / 4, fr.height() / 4, -fr.width() / 4, -fr.height() / 4)) <
                variance(img, fr.adjusted(fr.width() / 4, fr.height() / 4, -fr.width() / 4, -fr.height() / 4)) / 3);
    }

    void reframeFindsThePerson() {
        const QString path = qEnvironmentVariable("LUMORA_FACE_IMAGE");
        if (!modelsPresent() || path.isEmpty())
            QSKIP("set LUMORA_VISION_MODELS and LUMORA_FACE_IMAGE to run");
        const QImage face = QImage(path).convertToFormat(QImage::Format_Grayscale8).scaled(200, 200, Qt::KeepAspectRatio,
                                                                                         Qt::SmoothTransformation);
        TestVideoSpec spec;
        spec.seconds = 2;
        spec.width = 640;
        spec.height = 360;
        spec.audio = false;
        spec.paint = [face](int) {
            QImage img(640, 360, QImage::Format_Grayscale8);
            img.fill(90);
            QPainter p(&img);
            p.drawImage(QPointF(400, 80), face); // right of centre
            return img;
        };
        QString err;
        QVERIFY2(writeTestVideo(m_dir.filePath("person.mp4"), spec, &err), qPrintable(err));
        Clip clip;
        clip.sourceIn = 0;
        clip.sourceOut = ticksFromSeconds(2);
        const auto r = ai::findSubject(m_dir.filePath("person.mp4"), clip);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.faces, 1);
        QVERIFY(r.samples.size() >= 8);
        for (const auto& s : r.samples) {
            QCOMPARE(s.track, 0);
            QVERIFY2(std::abs(s.centre.x() - 500.0 / 640) < 0.05, qPrintable(QString::number(s.centre.x())));
        }
    }

    void facesAreFollowedInVideo() {
        const QString path = qEnvironmentVariable("LUMORA_FACE_IMAGE");
        if (!modelsPresent() || path.isEmpty())
            QSKIP("set LUMORA_VISION_MODELS and LUMORA_FACE_IMAGE to run");
        const QImage face = QImage(path).convertToFormat(QImage::Format_Grayscale8).scaled(240, 240, Qt::KeepAspectRatio,
                                                                                         Qt::SmoothTransformation);
        TestVideoSpec spec;
        spec.seconds = 2;
        spec.width = 640;
        spec.height = 360;
        spec.audio = false;
        spec.paint = [face](int i) {
            QImage img(640, 360, QImage::Format_Grayscale8);
            img.fill(90);
            QPainter p(&img);
            p.drawImage(QPointF(40 + 6 * i, 60), face); // walks to the right
            return img;
        };
        QString err;
        QVERIFY2(writeTestVideo(m_dir.filePath("face.mp4"), spec, &err), qPrintable(err));
        const auto result = ai::findRedactions(m_dir.filePath("face.mp4"), 0, ticksFromSeconds(2), ai::RedactionOptions{});
        QVERIFY2(result.ok(), qPrintable(result.error));
        int faces = 0;
        for (const auto& t : result.tracks)
            if (t.kind == RedactionKind::Face) {
                ++faces;
                QVERIFY(t.end() - t.start() > ticksFromSeconds(1.5));
                // The box moved with the face: 6 px a frame, 0.94 of the width in two seconds.
                const double moved = t.rectAt(t.end(), 0).center().x() - t.rectAt(t.start(), 0).center().x();
                QVERIFY2(std::abs(moved - (6.0 * 49) / 640) < 0.05, qPrintable(QString::number(moved)));
            }
        QCOMPARE(faces, 1);
    }

    void platesAreFound() {
        if (!modelsPresent())
            QSKIP("set LUMORA_VISION_MODELS to run");
        QString err;
        const auto plates = ai::detectPlates(plateScene(), 0.3f, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!plates.empty());
        QVERIFY2(plates.front().rect.intersects(QRectF(340, 380, 280, 62)),
                 qPrintable(QStringLiteral("%1,%2 %3x%4").arg(plates.front().rect.x()).arg(plates.front().rect.y())
                                .arg(plates.front().rect.width()).arg(plates.front().rect.height())));
    }

    void subjectIsCutOut() {
        if (!modelsPresent())
            QSKIP("set LUMORA_VISION_MODELS to run");
        QImage img(480, 320, QImage::Format_RGB32);
        img.fill(QColor(225, 228, 232));
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 40, 30));
            p.drawEllipse(QPointF(240, 170), 90, 110);
        }
        QString err;
        const QImage matte = ai::subjectMatte(img, QStringLiteral("fast"), &err);
        QVERIFY2(!matte.isNull(), qPrintable(err));
        QCOMPARE(matte.size(), img.size());
        QVERIFY(qGray(matte.pixel(240, 170)) > 200);
        QVERIFY(qGray(matte.pixel(15, 15)) < 40);
        QVERIFY(qGray(matte.pixel(465, 305)) < 40);

        PhotoAdjustments a;
        a.background.enabled = true;
        const QImage out = PhotoProcessor::apply(img.convertToFormat(QImage::Format_RGBA64), a);
        QVERIFY(out.hasAlphaChannel());
        QVERIFY(out.pixelColor(240, 170).alpha() > 220);
        QVERIFY(out.pixelColor(15, 15).alpha() < 30);
    }
};

QTEST_MAIN(VisionTest)
#include "tst_vision.moc"

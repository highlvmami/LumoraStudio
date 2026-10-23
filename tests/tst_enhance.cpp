#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "ai/Enhance.h"
#include "ai/Vision.h"
#include "image/PhotoAdjustments.h"
#include "image/PhotoProcessor.h"
#include "render/Effects.h"
#include "timeline/ClipEffects.h"

using namespace lumora;

namespace {

bool modelReady(const char* id) {
    return !qEnvironmentVariable("LUMORA_VISION_MODELS").isEmpty() && ai::visionModelReady(QString::fromLatin1(id));
}

// How sharp a picture is: the average size of the difference between
// neighbouring pixels.
double sharpness(const QImage& img) {
    const QImage g = img.convertToFormat(QImage::Format_Grayscale8);
    double sum = 0;
    for (int y = 1; y < g.height() - 1; ++y)
        for (int x = 1; x < g.width() - 1; ++x)
            sum += std::abs(4 * g.pixelColor(x, y).red() - g.pixelColor(x - 1, y).red() - g.pixelColor(x + 1, y).red() -
                            g.pixelColor(x, y - 1).red() - g.pixelColor(x, y + 1).red());
    return sum / ((g.width() - 2) * (g.height() - 2));
}

double meanGray(const QImage& img, const QRect& r) {
    double sum = 0;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            sum += qGray(img.pixel(x, y));
    return sum / (r.width() * r.height());
}

} // namespace

class EnhanceTest : public QObject {
    Q_OBJECT

private slots:
    void faceTemplateAlignsToItself() {
        ai::Detection d;
        d.landmarks = {QPointF(192.98, 239.95), QPointF(318.90, 240.19), QPointF(256.63, 314.02), QPointF(201.26, 371.41),
                       QPointF(313.09, 371.15)};
        QTransform t = ai::faceAlignment(d);
        QVERIFY(t.isIdentity() || (std::abs(t.m11() - 1) < 1e-6 && std::abs(t.m12()) < 1e-6 && std::abs(t.dx()) < 1e-4));
        // Half as big, turned and moved: the alignment undoes it.
        QTransform move;
        move.translate(40, 70).rotate(20).scale(0.5, 0.5);
        for (auto& p : d.landmarks)
            p = move.map(p);
        t = ai::faceAlignment(d);
        const QPointF back = t.map(d.landmarks[2]);
        QVERIFY(std::abs(back.x() - 256.63) < 0.01 && std::abs(back.y() - 314.02) < 0.01);
    }

    void missingModelIsReported() {
        const QByteArray models = qgetenv("LUMORA_VISION_MODELS");
        qputenv("LUMORA_VISION_MODELS", "/nonexistent");
        QString err;
        QImage img(64, 64, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QVERIFY(ai::upscale(img, 2, {}, &err).isNull());
        QVERIFY(!err.isEmpty());
        err.clear();
        QImage mask(64, 64, QImage::Format_Grayscale8);
        mask.fill(255);
        QVERIFY(ai::inpaint(img, mask, {}, &err).isNull());
        QVERIFY(!err.isEmpty());
        qputenv("LUMORA_VISION_MODELS", models);
    }

    void enlargingAddsDetail() {
        const QString path = qEnvironmentVariable("LUMORA_FACE_IMAGE");
        if (!modelReady("upscale-x2") || path.isEmpty())
            QSKIP("set LUMORA_VISION_MODELS (with real_esrgan_x2.onnx) and LUMORA_FACE_IMAGE to run");
        // Bigger than one tile, with a transparent corner.
        QImage small = QImage(path).scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 20; ++x)
                small.setPixel(x, y, qRgba(0, 0, 0, 0));
        double last = -1;
        QString err;
        const QImage up = ai::upscale(small, 2, [&](double p) {
            last = p;
            return true;
        }, &err);
        QVERIFY2(!up.isNull(), qPrintable(err));
        QCOMPARE(last, 1.0);
        QCOMPARE(up.size(), small.size() * 2);
        QCOMPARE(up.format(), QImage::Format_RGBA64);
        QCOMPARE(up.pixelColor(5, 5).alpha(), 0);
        QCOMPARE(up.pixelColor(200, 200).alpha(), 255);
        const QImage stretched = small.scaled(up.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // The same picture, more detail than stretching it.
        const QRect all(40, 40, up.width() - 80, up.height() - 80);
        QVERIFY(std::abs(meanGray(up.convertToFormat(QImage::Format_RGB32), all) - meanGray(stretched, all)) < 4);
        QVERIFY(sharpness(up) > sharpness(stretched) * 1.2);
        // Cancelling gives nothing.
        QVERIFY(ai::upscale(small, 2, [](double) { return false; }).isNull());
    }

    void facesAreRestored() {
        const QString path = qEnvironmentVariable("LUMORA_FACE_IMAGE");
        if (!modelReady("face-restore") || !modelReady("face") || path.isEmpty())
            QSKIP("set LUMORA_VISION_MODELS (with gfpgan_1.4.onnx) and LUMORA_FACE_IMAGE to run");
        // An old, soft photo: shrunk and blown up again, with room around.
        const QImage face = QImage(path).scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                .scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage img(face.width() + 200, face.height() + 200, QImage::Format_RGB32);
        img.fill(QColor(120, 120, 120));
        QPainter(&img).drawImage(100, 100, face);
        QString err;
        const auto faces = ai::detectFaces(img, 0.6f, &err);
        QCOMPARE(faces.size(), size_t(1));
        const QImage out = ai::restoreFaces(img, 1.0, {}, &err);
        QVERIFY2(!out.isNull(), qPrintable(err));
        QCOMPARE(out.size(), img.size());
        const QRect fr = faces.front().rect.toRect();
        QVERIFY(sharpness(out.copy(fr)) > sharpness(img.copy(fr)) * 1.3);
        // Away from the face nothing changes.
        QCOMPARE(out.convertToFormat(QImage::Format_RGB32).pixel(5, 5), img.pixel(5, 5));
        // At strength 0 the photo stays as it was.
        const QImage none = ai::restoreFaces(img, 0.0, {}, &err);
        QCOMPARE(none.convertToFormat(QImage::Format_RGB32).pixel(fr.center()), img.pixel(fr.center()));

        // In the photo pipeline a new strength only mixes: no second run.
        const QImage working = img.convertToFormat(QImage::Format_RGBX64);
        PhotoRetouch r;
        r.faces = 1.0;
        QVERIFY(!PhotoProcessor::retouchReady(working, r));
        const QImage full = PhotoProcessor::retouched(working, r, {}, &err);
        QVERIFY2(!full.isNull(), qPrintable(err));
        QVERIFY(!full.hasAlphaChannel());
        r.faces = 0.5;
        QVERIFY(PhotoProcessor::retouchReady(working, r));
        const QImage half = PhotoProcessor::retouched(working, r);
        const QColor a = QColor(img.pixel(fr.center())), b = full.pixelColor(fr.center()), h = half.pixelColor(fr.center());
        QVERIFY(std::abs(h.red() - (a.red() + b.red()) / 2) <= 2);
    }

    void objectsAreRemoved() {
        if (!modelReady("inpaint"))
            QSKIP("set LUMORA_VISION_MODELS (with inpainting_lama_2025jan.onnx) to run");
        // A smooth sky with a red ball in it, and a small dark bird far away.
        QImage img(900, 600, QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                img.setPixel(x, y, qRgb(90 + y / 8, 140 + y / 10, 220));
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(220, 30, 20));
            p.drawEllipse(QPointF(250, 300), 60, 60);
            p.setBrush(QColor(30, 30, 30));
            p.drawEllipse(QPointF(780, 100), 10, 6);
        }
        QImage mask(img.size(), QImage::Format_Grayscale8);
        mask.fill(0);
        {
            QPainter p(&mask);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(250, 300), 68, 68);
            p.drawEllipse(QPointF(780, 100), 16, 12);
        }
        QString err;
        const QImage out = ai::inpaint(img, mask, {}, &err);
        QVERIFY2(!out.isNull(), qPrintable(err));
        const QImage rgb = out.convertToFormat(QImage::Format_RGB32);
        // The ball and the bird are gone: sky colour where they were.
        for (QPoint p : {QPoint(250, 300), QPoint(230, 280), QPoint(780, 100)}) {
            const QColor c = rgb.pixelColor(p), sky = QColor(img.pixel(p.x(), p.y() - 75 < 0 ? p.y() + 30 : p.y() - 75));
            QVERIFY2(std::abs(c.red() - sky.red()) < 30 && std::abs(c.blue() - sky.blue()) < 30,
                     qPrintable(QStringLiteral("%1,%2: %3").arg(p.x()).arg(p.y()).arg(c.name())));
        }
        // Outside the mask the pixels are untouched.
        QCOMPARE(rgb.pixel(10, 10), img.pixel(10, 10));
        QCOMPARE(rgb.pixel(500, 500), img.pixel(500, 500));
        QCOMPARE(rgb.pixel(250, 210), img.pixel(250, 210));
    }

    void retouchSurvivesJson() {
        PhotoAdjustments a;
        QVERIFY(!a.hasAiChanges());
        a.retouch.faces = 0.7;
        BrushStroke s;
        s.points = {QPointF(0.2, 0.3), QPointF(0.4, 0.3)};
        s.radius = 0.05;
        a.retouch.erase.push_back(s);
        QVERIFY(a.hasAiChanges() && a.hasToneChanges());
        const PhotoAdjustments b = PhotoAdjustments::fromJson(a.toJson());
        QCOMPARE(b.retouch, a.retouch);
        QVERIFY(!PhotoAdjustments::fromJson(PhotoAdjustments().toJson()).retouch.isActive());
    }

    void photoPipelineRemovesAndEnlarges() {
        if (!modelReady("inpaint") || !modelReady("upscale-x2"))
            QSKIP("set LUMORA_VISION_MODELS (with the object removal and 2x models) to run");
        QImage img(320, 240, QImage::Format_RGB32);
        img.fill(QColor(200, 210, 190));
        QPainter(&img).fillRect(140, 100, 40, 40, Qt::black);
        const QImage working = img.convertToFormat(QImage::Format_RGBX64);
        PhotoAdjustments a;
        BrushStroke s;
        s.points = {QPointF(0.5, 0.5)};
        s.radius = 0.15; // of the height: 36 px, covers the square
        s.hardness = 1.0;
        a.retouch.erase.push_back(s);
        const QImage out = PhotoProcessor::apply(working, a);
        QVERIFY(!out.hasAlphaChannel());
        QVERIFY(qGray(out.convertToFormat(QImage::Format_RGB32).pixel(160, 120)) > 150);
        // The second time comes from the cache, as the same image.
        QCOMPARE(PhotoProcessor::retouched(working, a.retouch).cacheKey(), PhotoProcessor::retouched(working, a.retouch).cacheKey());

        QTemporaryDir dir;
        PhotoExportSettings es;
        es.enlarge = 2;
        QString err;
        double last = -1;
        QVERIFY2(PhotoExporter::save(dir.filePath(QStringLiteral("big.png")), img, working, a, es, &err, [&](double p) {
            last = p;
            return true;
        }), qPrintable(err));
        QCOMPARE(last, 1.0);
        const QImage saved(dir.filePath(QStringLiteral("big.png")));
        QCOMPARE(saved.size(), img.size() * 2);
        QCOMPARE(PhotoExporter::sourceBitDepth(saved), 16);
        QVERIFY(!saved.hasAlphaChannel());
        QVERIFY(qGray(saved.pixel(320, 240)) > 150);
        // Cancelling saves nothing.
        QVERIFY(!PhotoExporter::save(dir.filePath(QStringLiteral("no.png")), img, working, PhotoAdjustments(), es, &err,
                                     [](double) { return false; }));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("no.png"))));
    }

    void aiInBetweenFrames() {
        TimeRemap r;
        r.blending = FrameBlending::AiFlow;
        QCOMPARE(TimeRemap::fromJson(r.toJson()).blending, FrameBlending::AiFlow);
        const QString path = qEnvironmentVariable("LUMORA_FACE_IMAGE");
        if (!modelReady("frame-flow") || path.isEmpty())
            QSKIP("set LUMORA_VISION_MODELS (with the RAFT model) and LUMORA_FACE_IMAGE to run");
        // The picture moves 24 px left and 12 px down between the frames.
        const QImage big = QImage(path).scaled(1040, 800).convertToFormat(QImage::Format_RGB32);
        const QImage a = big.copy(40, 40, 960, 720), b = big.copy(64, 28, 960, 720);
        std::vector<float> fx, fy;
        QString err;
        QVERIFY2(ai::measureFlow(a, b, fx, fy, &err), qPrintable(err));
        QCOMPARE(fx.size(), size_t(ai::kFlowWidth * ai::kFlowHeight));
        const size_t mid = fx.size() / 2 + ai::kFlowWidth / 2;
        QVERIFY(std::abs(fx[mid] - (-24.0f / 2)) < 1.0f); // 960 -> 480: half
        QVERIFY(std::abs(fy[mid] - (12.0f / 2)) < 1.0f);  // 720 -> 360: half
        const fx::MotionField m = fx::measureMotion(a, b, true);
        QCOMPARE(m.width, ai::kFlowWidth);
        // Half way: the picture has moved half the distance.
        const QImage half = fx::interpolateFrames(a, b, 0.5, &m);
        const QImage expected = big.copy(52, 34, 960, 720);
        double diff = 0;
        const QImage h = half.convertToFormat(QImage::Format_RGB32);
        for (int y = 100; y < 620; y += 4)
            for (int x = 100; x < 860; x += 4)
                diff += std::abs(qGray(h.pixel(x, y)) - qGray(expected.pixel(x, y)));
        diff /= (520 / 4) * (760 / 4);
        QVERIFY2(diff < 6, qPrintable(QString::number(diff)));
    }
};

QTEST_MAIN(EnhanceTest)
#include "tst_enhance.moc"

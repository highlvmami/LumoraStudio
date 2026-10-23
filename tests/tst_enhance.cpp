#include <QFileInfo>
#include <QPainter>
#include <QtTest>
#include <cmath>

#include "ai/Enhance.h"
#include "ai/Vision.h"

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
};

QTEST_MAIN(EnhanceTest)
#include "tst_enhance.moc"

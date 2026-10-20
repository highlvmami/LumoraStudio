#include <QImageReader>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include "image/PhotoLibrary.h"
#include "image/PhotoProcessor.h"

using namespace lumora;

namespace {
QImage makePhoto(int w, int h) {
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb((x * 255) / (w - 1), (y * 255) / (h - 1), (x * y) % 256));
    return img;
}
} // namespace

class PhotoTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_src;

private slots:
    void initTestCase() {
        m_src = m_dir.filePath("photo.png");
        QVERIFY(makePhoto(200, 100).save(m_src));
    }

    void neutralSettingsAreBitExact() {
        QImage original;
        const QImage working = PhotoProcessor::load(m_src, &original);
        QVERIFY(!working.isNull());
        const QString out = m_dir.filePath("same.png");
        QVERIFY(PhotoExporter::save(out, original, working, {}, {}));
        QImage a(m_src), b(out);
        QCOMPARE(b.size(), a.size());
        QCOMPARE(b.convertToFormat(QImage::Format_RGB888), a.convertToFormat(QImage::Format_RGB888));
    }

    void cropIsExactAndLossless() {
        QImage original;
        const QImage working = PhotoProcessor::load(m_src, &original);
        PhotoAdjustments adj;
        adj.crop = QRectF(0.25, 0.5, 0.5, 0.5);
        const QString out = m_dir.filePath("crop.png");
        QVERIFY(PhotoExporter::save(out, original, working, adj, {}));
        QImage b(out);
        QCOMPARE(b.size(), QSize(100, 50));
        const QImage expected = QImage(m_src).copy(50, 50, 100, 50).convertToFormat(QImage::Format_RGB888);
        QCOMPARE(b.convertToFormat(QImage::Format_RGB888), expected);
    }

    void exposureDoublesLinearLight() {
        QImage img(1, 1, QImage::Format_RGBX64);
        // sRGB 0.5 -> linear 0.214; +1 EV -> 0.428 linear -> sRGB 0.686
        img.setPixelColor(0, 0, QColor::fromRgbF(0.5f, 0.5f, 0.5f));
        PhotoAdjustments adj;
        adj.exposure = 1.0;
        const QImage out = PhotoProcessor::apply(img, adj);
        QVERIFY(std::abs(out.pixelColor(0, 0).redF() - 0.6858) < 0.002);
        adj.exposure = -1.0;
        const QImage dark = PhotoProcessor::apply(img, adj);
        QVERIFY(dark.pixelColor(0, 0).redF() < 0.5f);
    }

    void contrastPivotsAroundMiddleGrey() {
        QImage img(3, 1, QImage::Format_RGBX64);
        const float grey = 0.4614f; // sRGB value of linear 0.18
        img.setPixelColor(0, 0, QColor::fromRgbF(grey, grey, grey));
        img.setPixelColor(1, 0, QColor::fromRgbF(0.2f, 0.2f, 0.2f));
        img.setPixelColor(2, 0, QColor::fromRgbF(0.8f, 0.8f, 0.8f));
        PhotoAdjustments adj;
        adj.contrast = 50;
        const QImage out = PhotoProcessor::apply(img, adj);
        QVERIFY(std::abs(out.pixelColor(0, 0).redF() - grey) < 0.002);
        QVERIFY(out.pixelColor(1, 0).redF() < 0.2f);
        QVERIFY(out.pixelColor(2, 0).redF() > 0.8f);
    }

    void temperatureWarmsAndCools() {
        QImage img(1, 1, QImage::Format_RGBX64);
        img.setPixelColor(0, 0, QColor::fromRgbF(0.5f, 0.5f, 0.5f));
        PhotoAdjustments adj;
        adj.temperature = 50;
        QColor warm = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(warm.redF() > warm.blueF());
        adj.temperature = -50;
        QColor cool = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(cool.blueF() > cool.redF());
        adj.temperature = 0;
        adj.tint = 50;
        QColor magenta = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(magenta.greenF() < magenta.redF());
    }

    void adjustedSaveIs16Bit() {
        QImage original;
        const QImage working = PhotoProcessor::load(m_src, &original);
        PhotoAdjustments adj;
        adj.exposure = 0.5;
        const QString out = m_dir.filePath("adjusted.png");
        QVERIFY(PhotoExporter::save(out, original, working, adj, {}));
        QImageReader r(out);
        const QImage img = r.read();
        QVERIFY(PhotoExporter::sourceBitDepth(img) == 16);
        // And the original file is untouched.
        QCOMPARE(QImage(m_src).convertToFormat(QImage::Format_RGB888), makePhoto(200, 100));
    }

    void undoMergesSliderDrags() {
        PhotoLibrary lib;
        QUndoStack stack;
        const QUuid id = lib.add(m_src);
        PhotoAdjustments a, b, c;
        b.exposure = 0.3;
        c.exposure = 0.6;
        stack.push(new PhotoAdjustCommand(&lib, id, a, b, "exp", PhotoAdjustCommand::Exposure));
        stack.push(new PhotoAdjustCommand(&lib, id, b, c, "exp", PhotoAdjustCommand::Exposure));
        QCOMPARE(stack.count(), 1);
        QCOMPARE(lib.find(id)->adjustments.exposure, 0.6);
        stack.undo();
        QCOMPARE(lib.find(id)->adjustments.exposure, 0.0);
        stack.redo();
        QCOMPARE(lib.find(id)->adjustments.exposure, 0.6);
    }
};

QTEST_MAIN(PhotoTest)
#include "tst_photo.moc"

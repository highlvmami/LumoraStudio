#include <cstring>
#include <QImageReader>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

#include "image/PhotoLibrary.h"
#include "image/PhotoProcessor.h"
#include "image/RawDecoder.h"
#include "media/MediaInfo.h"

using namespace lumora;

namespace {
QImage makePhoto(int w, int h) {
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb((x * 255) / (w - 1), (y * 255) / (h - 1), (x * y) % 256));
    return img;
}

// A tiny linear DNG (already demosaiced, camera space = linear sRGB): enough
// for LibRaw to develop it like a real camera file. Left half a strong red,
// right half a mid grey.
QByteArray makeLinearDng(int w, int h) {
    QByteArray pixels;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const quint16 rgb[3] = {x < w / 2 ? quint16(30000) : quint16(9000),
                                    x < w / 2 ? quint16(1500) : quint16(9000),
                                    x < w / 2 ? quint16(1500) : quint16(9000)};
            pixels.append(reinterpret_cast<const char*>(rgb), 6); // little endian host assumed
        }
    struct Entry {
        quint16 tag, type;
        quint32 count;
        QByteArray data; // value bytes
    };
    auto shorts = [](std::initializer_list<quint16> v) {
        QByteArray b;
        for (quint16 x : v)
            b.append(reinterpret_cast<const char*>(&x), 2);
        return b;
    };
    auto longs = [](std::initializer_list<quint32> v) {
        QByteArray b;
        for (quint32 x : v)
            b.append(reinterpret_cast<const char*>(&x), 4);
        return b;
    };
    auto rationals = [](std::initializer_list<double> v) {
        QByteArray b;
        for (double x : v) {
            const qint32 n = qint32(std::lround(x * 10000)), d = 10000;
            b.append(reinterpret_cast<const char*>(&n), 4);
            b.append(reinterpret_cast<const char*>(&d), 4);
        }
        return b;
    };
    auto ascii = [](const char* s) { return QByteArray(s, int(std::strlen(s)) + 1); };
    const quint32 W = quint32(w), H = quint32(h);
    std::vector<Entry> e = {
        {254, 4, 1, longs({0})},
        {256, 4, 1, longs({W})},
        {257, 4, 1, longs({H})},
        {258, 3, 3, shorts({16, 16, 16})},
        {259, 3, 1, shorts({1})},
        {262, 3, 1, shorts({34892})}, // LinearRaw
        {271, 2, 7, ascii("Lumora")},
        {272, 2, 5, ascii("Test")},
        {273, 4, 1, longs({0})}, // strip offset, patched below
        {274, 3, 1, shorts({1})},
        {277, 3, 1, shorts({3})},
        {278, 4, 1, longs({H})},
        {279, 4, 1, longs({quint32(pixels.size())})},
        {284, 3, 1, shorts({1})},
        {50706, 1, 4, QByteArray("\x01\x04\x00\x00", 4)},
        {50708, 2, 12, ascii("Lumora Test")},
        {50717, 4, 1, longs({65535})},
        // XYZ (D65) to linear sRGB.
        {50721, 10, 9, rationals({3.2406, -1.5372, -0.4986, -0.9689, 1.8758, 0.0415, 0.0557, -0.2040, 1.0570})},
        {50728, 5, 3, rationals({1, 1, 1})},
        {50778, 3, 1, shorts({21})}, // D65
    };
    // Layout: header, IFD, out-of-line values, pixels.
    const quint32 ifdOffset = 8;
    const quint32 ifdSize = 2 + quint32(e.size()) * 12 + 4;
    QByteArray extra;
    const quint32 extraStart = ifdOffset + ifdSize;
    std::vector<quint32> where(e.size(), 0);
    for (size_t i = 0; i < e.size(); ++i)
        if (e[i].data.size() > 4) {
            where[i] = extraStart + quint32(extra.size());
            extra.append(e[i].data);
            if (extra.size() % 2)
                extra.append('\0');
        }
    const quint32 pixelStart = extraStart + quint32(extra.size());
    for (auto& x : e)
        if (x.tag == 273)
            x.data = longs({pixelStart});
    QByteArray out("II*\0", 4);
    out.append(longs({ifdOffset}));
    out.append(shorts({quint16(e.size())}));
    for (size_t i = 0; i < e.size(); ++i) {
        out.append(shorts({e[i].tag, e[i].type}));
        out.append(longs({e[i].count}));
        if (e[i].data.size() > 4)
            out.append(longs({where[i]}));
        else
            out.append(e[i].data.leftJustified(4, '\0'));
    }
    out.append(longs({0}));
    out.append(extra);
    out.append(pixels);
    return out;
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
        adj.color.exposure = 1.0;
        const QImage out = PhotoProcessor::apply(img, adj);
        QVERIFY(std::abs(out.pixelColor(0, 0).redF() - 0.6858) < 0.002);
        adj.color.exposure = -1.0;
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
        adj.color.contrast = 50;
        const QImage out = PhotoProcessor::apply(img, adj);
        QVERIFY(std::abs(out.pixelColor(0, 0).redF() - grey) < 0.002);
        QVERIFY(out.pixelColor(1, 0).redF() < 0.2f);
        QVERIFY(out.pixelColor(2, 0).redF() > 0.8f);
    }

    void temperatureWarmsAndCools() {
        QImage img(1, 1, QImage::Format_RGBX64);
        img.setPixelColor(0, 0, QColor::fromRgbF(0.5f, 0.5f, 0.5f));
        PhotoAdjustments adj;
        adj.color.temperature = 50;
        QColor warm = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(warm.redF() > warm.blueF());
        adj.color.temperature = -50;
        QColor cool = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(cool.blueF() > cool.redF());
        adj.color.temperature = 0;
        adj.color.tint = 50;
        QColor magenta = PhotoProcessor::apply(img, adj).pixelColor(0, 0);
        QVERIFY(magenta.greenF() < magenta.redF());
    }

    void adjustedSaveIs16Bit() {
        QImage original;
        const QImage working = PhotoProcessor::load(m_src, &original);
        PhotoAdjustments adj;
        adj.color.exposure = 0.5;
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
        b.color.exposure = 0.3;
        c.color.exposure = 0.6;
        stack.push(new PhotoAdjustCommand(&lib, id, a, b, "exp", PhotoAdjustCommand::Exposure));
        stack.push(new PhotoAdjustCommand(&lib, id, b, c, "exp", PhotoAdjustCommand::Exposure));
        QCOMPARE(stack.count(), 1);
        QCOMPARE(lib.find(id)->adjustments.color.exposure, 0.6);
        stack.undo();
        QCOMPARE(lib.find(id)->adjustments.color.exposure, 0.0);
        stack.redo();
        QCOMPARE(lib.find(id)->adjustments.color.exposure, 0.6);
    }

    void rawPhotosAreDeveloped() {
        if (!raw::available())
            QSKIP("Built without LibRaw");
        QVERIFY(isImageFile(QStringLiteral("a/b/IMG_0001.CR2")));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("çekim.dng"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeLinearDng(64, 48));
        f.close();

        const auto info = probeMedia(path);
        QVERIFY(info);
        QCOMPARE(info->kind, MediaKind::Image);
        QCOMPARE(info->imageWidth, 64);
        QCOMPARE(info->imageHeight, 48);

        QString err;
        QImage original;
        const QImage img = PhotoProcessor::load(path, &original, &err);
        QVERIFY2(!img.isNull(), qPrintable(err));
        QCOMPARE(img.size(), QSize(64, 48));
        QCOMPARE(img.format(), QImage::Format_RGBX64);
        const QRgba64 red = img.pixelColor(10, 24).rgba64();
        const QRgba64 grey = img.pixelColor(54, 24).rgba64();
        QVERIFY2(red.red() > 3 * red.green() && red.red() > 3 * red.blue(), "red stays red");
        QVERIFY2(std::abs(int(grey.red()) - int(grey.green())) < 3000
                     && std::abs(int(grey.blue()) - int(grey.green())) < 3000,
                 "grey stays neutral (camera white balance)");
        QVERIFY(grey.green() > 10000 && grey.green() < 60000);

        const QImage thumb = raw::thumbnail(path, QSize(32, 32));
        QVERIFY(!thumb.isNull());
        QVERIFY(thumb.width() <= 32 && thumb.height() <= 32);

        QVERIFY(raw::decode(dir.filePath(QStringLiteral("missing.nef")), &err).isNull());
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(PhotoTest)
#include "tst_photo.moc"

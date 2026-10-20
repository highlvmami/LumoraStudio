#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "color/ColorProcessor.h"
#include "color/Lut.h"
#include "color/Scopes.h"
#include "image/PhotoProcessor.h"

using namespace lumora;

namespace {

QImage solid(int w, int h, QColor c, QImage::Format f = QImage::Format_RGBX64) {
    QImage img(w, h, f);
    img.fill(c);
    return img;
}

// A grey ramp from black (left) to white (right).
QImage ramp(int w, int h) {
    QImage img(w, h, QImage::Format_RGBX64);
    for (int x = 0; x < w; ++x) {
        const double v = double(x) / (w - 1);
        for (int y = 0; y < h; ++y)
            img.setPixelColor(x, y, QColor::fromRgbF(v, v, v));
    }
    return img;
}

QByteArray swapRedBlueCube(int n) {
    QByteArray s = "TITLE \"swap\"\n# comment\nLUT_3D_SIZE " + QByteArray::number(n) + "\n";
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r)
                s += QByteArray::number(double(b) / (n - 1)) + ' ' + QByteArray::number(double(g) / (n - 1)) + ' ' +
                     QByteArray::number(double(r) / (n - 1)) + '\n';
    return s;
}

double maxDifference(const QImage& a, const QImage& b) {
    double d = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
            d = std::max<double>({d, std::abs(p.redF() - q.redF()), std::abs(p.greenF() - q.greenF()),
                          std::abs(p.blueF() - q.blueF())});
        }
    return d;
}

} // namespace

class ColorTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private slots:
    void neutralGradeIsIdentity() {
        ColorGrade g;
        QVERIFY(g.isIdentity());
        QVERIFY(ColorProcessor(g).isIdentity());
        QImage img = ramp(64, 4);
        const QImage before = img;
        ColorProcessor(g).apply(img);
        QCOMPARE(img, before);
        g.lutPath = QStringLiteral("x.cube");
        g.lutAmount = 0;
        QVERIFY(g.isIdentity()); // a LUT at 0 % does nothing
    }

    void gradeJsonRoundTrip() {
        ColorGrade g;
        g.exposure = 0.5;
        g.highlights = -40;
        g.saturation = 20;
        g.gamma = {0.2, -0.1, 0.3};
        g.lutPath = QStringLiteral("/luts/film.cube");
        g.lutAmount = 0.7;
        QCOMPARE(ColorGrade::fromJson(g.toJson()), g);
        QVERIFY(ColorGrade().toJson().isEmpty());
    }

    void saturationAndVibrance() {
        QImage img = solid(1, 1, QColor::fromRgbF(0.8, 0.3, 0.2));
        ColorGrade g;
        g.saturation = -100;
        const QColor grey = ColorProcessor(g).applied(img).pixelColor(0, 0);
        QVERIFY(std::abs(grey.redF() - grey.greenF()) < 0.002 && std::abs(grey.greenF() - grey.blueF()) < 0.002);
        g.saturation = 50;
        const QColor more = ColorProcessor(g).applied(img).pixelColor(0, 0);
        QVERIFY(more.redF() - more.blueF() > 0.8 - 0.2);
        // Vibrance boosts a muted colour more than a saturated one.
        ColorGrade v;
        v.vibrance = 80;
        const QImage muted = solid(1, 1, QColor::fromRgbF(0.55, 0.5, 0.45));
        const QImage vivid = solid(1, 1, QColor::fromRgbF(0.9, 0.1, 0.1));
        const QColor m = ColorProcessor(v).applied(muted).pixelColor(0, 0);
        const QColor s = ColorProcessor(v).applied(vivid).pixelColor(0, 0);
        QVERIFY((m.redF() - m.blueF()) / 0.1 > 1.3);
        QVERIFY((s.redF() - s.blueF()) / 0.8 < 1.15);
    }

    void tonesTargetTheirRange() {
        const QImage img = ramp(256, 1);
        ColorGrade g;
        g.shadows = 100;
        const QImage sh = ColorProcessor(g).applied(img);
        const double darkGain = sh.pixelColor(60, 0).redF() - img.pixelColor(60, 0).redF();
        const double brightGain = sh.pixelColor(220, 0).redF() - img.pixelColor(220, 0).redF();
        QVERIFY2(darkGain > 0.05 && brightGain < darkGain / 3, qPrintable(QStringLiteral("%1 %2").arg(darkGain).arg(brightGain)));
        g = {};
        g.highlights = -100;
        const QImage hi = ColorProcessor(g).applied(img);
        QVERIFY(hi.pixelColor(200, 0).redF() < img.pixelColor(200, 0).redF() - 0.05);
        QVERIFY(std::abs(hi.pixelColor(30, 0).redF() - img.pixelColor(30, 0).redF()) < 0.02);
        // Black stays black, white stays white unless blacks / whites move.
        QCOMPARE(hi.pixelColor(0, 0).redF(), 0.0f);
    }

    void wheelsPushHue() {
        const QImage grey = solid(1, 1, QColor::fromRgbF(0.5, 0.5, 0.5));
        ColorGrade g;
        g.gain = {1.0, 0.0, 0.0}; // towards red
        QColor c = ColorProcessor(g).applied(grey).pixelColor(0, 0);
        QVERIFY(c.redF() > 0.55 && c.blueF() < 0.5 && c.greenF() < 0.5);
        g = {};
        g.lift.master = 0.5; // raises blacks
        c = ColorProcessor(g).applied(solid(1, 1, Qt::black)).pixelColor(0, 0);
        QVERIFY(c.redF() > 0.05);
        g = {};
        g.gamma.master = 0.5; // brighter mid tones, same ends
        const QImage r = ColorProcessor(g).applied(ramp(256, 1));
        QVERIFY(r.pixelColor(128, 0).redF() > 0.55);
        QVERIFY(r.pixelColor(255, 0).redF() > 0.999);
    }

    void premultipliedAlphaIsKept() {
        QImage img(1, 1, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(100, 50, 25, 128));
        ColorGrade g;
        g.exposure = 1;
        ColorProcessor(g).apply(img);
        QCOMPARE(qAlpha(img.pixel(0, 0)), 128);
        QVERIFY(img.pixelColor(0, 0).red() > 100);
    }

    void cubeParsingAndLookup() {
        QString err;
        const Lut lut = Lut::parseCube(swapRedBlueCube(5), &err);
        QVERIFY2(!lut.isNull(), qPrintable(err));
        QCOMPARE(lut.size(), 5);
        QCOMPARE(lut.title(), QStringLiteral("swap"));
        float rgb[3] = {0.9f, 0.3f, 0.1f};
        lut.apply(rgb);
        QVERIFY(std::abs(rgb[0] - 0.1f) < 1e-4f && std::abs(rgb[2] - 0.9f) < 1e-4f);

        const Lut one = Lut::parseCube("LUT_1D_SIZE 2\n1 1 1\n0 0 0\n", &err); // inverts
        QVERIFY2(!one.isNull() && !one.is3D(), qPrintable(err));
        float px[3] = {0.25f, 0.5f, 1.0f};
        one.apply(px);
        QVERIFY(std::abs(px[0] - 0.75f) < 1e-5f && std::abs(px[2]) < 1e-5f);

        QVERIFY(Lut::parseCube("LUT_3D_SIZE 2\n0 0 0\n", &err).isNull());
        QVERIFY(err.contains(QLatin1String("entries")));
        QVERIFY(Lut::parseCube("0 0 0\n", &err).isNull());
        QVERIFY(Lut::parseCube("LUT_3D_SIZE 2\n0 0 x\n", &err).isNull());
    }

    void lutInGradeAndSaveRoundTrip() {
        const QString path = m_dir.filePath("swap.cube");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(swapRedBlueCube(9));
        }
        ColorGrade g;
        g.lutPath = path;
        g.lutAmount = 1.0;
        const QImage img = solid(1, 1, QColor::fromRgbF(0.8, 0.4, 0.2));
        QColor c = ColorProcessor(g).applied(img).pixelColor(0, 0);
        QVERIFY(std::abs(c.redF() - 0.2) < 0.003 && std::abs(c.blueF() - 0.8) < 0.003);
        g.lutAmount = 0.5;
        c = ColorProcessor(g).applied(img).pixelColor(0, 0);
        QVERIFY(std::abs(c.redF() - 0.5) < 0.003);

        // A missing LUT is reported and skipped.
        ColorGrade missing;
        missing.lutPath = m_dir.filePath("nope.cube");
        QVERIFY(!ColorProcessor(missing).lutError().isEmpty());

        // Baking a grade and reading it back gives the same look.
        ColorGrade look;
        look.contrast = 40;
        look.temperature = 30;
        const QString saved = m_dir.filePath("look.cube");
        QString err;
        QVERIFY2(ColorProcessor(look).bake(33, QStringLiteral("look")).save(saved, &err), qPrintable(err));
        ColorGrade viaLut;
        viaLut.lutPath = saved;
        const QImage r = ramp(128, 2);
        QVERIFY(maxDifference(ColorProcessor(look).applied(r), ColorProcessor(viaLut).applied(r)) < 2.0 / 255);
    }

    void scopes() {
        const QImage grey = solid(64, 32, QColor(128, 128, 128), QImage::Format_RGB32);
        const auto h = scopes::histogram(grey);
        QCOMPARE(h.pixels, quint64(64 * 32));
        QCOMPARE(h.bins[3][128], quint32(64 * 32));
        QCOMPARE(h.bins[0][128], quint32(64 * 32));

        // Waveform: a flat mid grey is one line half way up.
        const QImage wf = scopes::render(grey, scopes::Kind::Waveform, QSize(100, 101));
        QVERIFY(qAlpha(wf.pixel(50, 50)) > 0);
        QCOMPARE(qAlpha(wf.pixel(50, 10)), 0);
        // Vectorscope: grey sits in the centre, saturated red away from it.
        const QImage vs = scopes::render(grey, scopes::Kind::Vectorscope, QSize(101, 101));
        QCOMPARE(vs.size(), QSize(101, 101));
        QVERIFY(qAlpha(vs.pixel(50, 50)) > 0);
        const QPointF red = scopes::vectorscopePosition(1, 0, 0);
        QVERIFY(red.y() > 0.4 && red.x() < 0);
        QVERIFY(!scopes::render(grey, scopes::Kind::Parade, QSize(90, 50)).isNull());
        QVERIFY(!scopes::render(grey, scopes::Kind::Histogram, QSize(256, 50)).isNull());
    }

    void adjustmentLayerWithMask() {
        const QImage img = solid(100, 50, QColor::fromRgbF(0.5, 0.5, 0.5));
        PhotoAdjustments adj;
        PhotoLayer l;
        l.id = QUuid::createUuid();
        l.grade.exposure = 1.0;
        l.mask.shape = MaskShape::Rectangle;
        l.mask.rect = QRectF(0, 0, 0.5, 1); // left half
        l.mask.feather = 0.01;
        adj.layers.push_back(l);
        QVERIFY(adj.hasToneChanges());
        const QImage out = PhotoProcessor::apply(img, adj);
        QVERIFY(out.pixelColor(25, 25).redF() > 0.65);
        QVERIFY(std::abs(out.pixelColor(75, 25).redF() - 0.5) < 0.002);

        // Inverted, half opacity.
        adj.layers[0].mask.invert = true;
        adj.layers[0].opacity = 0.5;
        const QImage inv = PhotoProcessor::apply(img, adj);
        QVERIFY(std::abs(inv.pixelColor(25, 25).redF() - 0.5) < 0.002);
        const double right = inv.pixelColor(75, 25).redF();
        QVERIFY(right > 0.55 && right < out.pixelColor(25, 25).redF());

        // Hidden layers do nothing.
        adj.layers[0].visible = false;
        QVERIFY(!adj.hasToneChanges());
    }

    void linearAndEllipseMasks() {
        LayerMask m;
        m.shape = MaskShape::Linear;
        m.rect = QRectF(0, 0, 1, 1);
        m.feather = 0.2;
        QVERIFY(m.valueAt(0.5, 0.05, 1.0) > 0.95); // top opaque
        QVERIFY(m.valueAt(0.5, 0.95, 1.0) < 0.05); // bottom clear
        QVERIFY(std::abs(m.valueAt(0.5, 0.5, 1.0) - 0.5) < 0.01);
        m.angle = 90; // rotated: left / right
        QVERIFY(std::abs(m.valueAt(0.05, 0.5, 1.0) - m.valueAt(0.95, 0.5, 1.0)) > 0.9);
        m.shape = MaskShape::Ellipse;
        m.rect = QRectF(0.25, 0.25, 0.5, 0.5);
        m.feather = 0.1;
        QVERIFY(m.valueAt(0.5, 0.5, 1.0) > 0.99);
        QVERIFY(m.valueAt(0.27, 0.27, 1.0) < 0.01); // inside the box, outside the ellipse
    }

    void imageLayerBlendModes() {
        const QString top = m_dir.filePath("top.png");
        QVERIFY(solid(10, 10, QColor::fromRgbF(0.5, 0.5, 0.5), QImage::Format_RGB888).save(top));
        const QImage base = solid(100, 100, QColor::fromRgbF(0.6, 0.6, 0.6));
        PhotoAdjustments adj;
        PhotoLayer l;
        l.id = QUuid::createUuid();
        l.kind = LayerKind::Image;
        l.imagePath = top;
        l.scale = 0.5; // 50 x 50 in the centre
        l.blend = BlendMode::Multiply;
        adj.layers.push_back(l);
        const QImage out = PhotoProcessor::apply(base, adj);
        QVERIFY(std::abs(out.pixelColor(50, 50).redF() - 0.3) < 0.01); // 0.6 * 0.5
        QVERIFY(std::abs(out.pixelColor(5, 5).redF() - 0.6) < 0.002);  // outside the picture
        adj.layers[0].blend = BlendMode::Screen;
        QVERIFY(std::abs(PhotoProcessor::apply(base, adj).pixelColor(50, 50).redF() - 0.8) < 0.01);
        adj.layers[0].blend = BlendMode::Normal;
        adj.layers[0].x = 0.25; // moved right by a quarter of the width
        const QImage moved = PhotoProcessor::apply(base, adj);
        QVERIFY(std::abs(moved.pixelColor(30, 50).redF() - 0.6) < 0.002);
        QVERIFY(std::abs(moved.pixelColor(80, 50).redF() - 0.5) < 0.01);
    }

    void adjustmentsJson() {
        PhotoAdjustments a;
        a.color.exposure = 0.5;
        a.color.lift = {0.1, 0.2, -0.1};
        a.crop = QRectF(0.1, 0.1, 0.5, 0.5);
        PhotoLayer l;
        l.id = QUuid::createUuid();
        l.name = QStringLiteral("Sky");
        l.blend = BlendMode::SoftLight;
        l.opacity = 0.75;
        l.mask.shape = MaskShape::Linear;
        l.mask.angle = 15;
        l.mask.invert = true;
        l.grade.saturation = 30;
        a.layers.push_back(l);
        PhotoLayer img;
        img.id = QUuid::createUuid();
        img.kind = LayerKind::Image;
        img.imagePath = QStringLiteral("/a/b.png");
        img.scale = 0.3;
        a.layers.push_back(img);
        QCOMPARE(PhotoAdjustments::fromJson(a.toJson()), a);

        // 0.1 / 0.2 projects stored the four settings at the top level.
        QJsonObject old{{"exposure", 1.0}, {"contrast", 20.0}, {"temperature", -10.0}, {"tint", 5.0}};
        const PhotoAdjustments b = PhotoAdjustments::fromJson(old);
        QCOMPARE(b.color.exposure, 1.0);
        QCOMPARE(b.color.contrast, 20.0);
        QCOMPARE(b.color.temperature, -10.0);
        QCOMPARE(b.color.tint, 5.0);
    }
};

QTEST_MAIN(ColorTest)
#include "tst_color.moc"

#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <numbers>
#include <random>

#include "TestMedia.h"
#include "media/MediaPool.h"
#include "motion/Analysis.h"
#include "motion/Motion.h"
#include "project/Project.h"
#include "render/FrameRenderer.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::motion;

namespace {

// A large picture with blobs of every size, so corners are everywhere.
QImage texture(int w, int h, unsigned seed) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(QColor(120, 120, 120));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> x(0, w), y(0, h), r(3, 30);
    std::uniform_int_distribution<int> grey(0, 255);
    for (int i = 0; i < w * h / 300; ++i) {
        const int g = grey(rng);
        p.setBrush(QColor(g, g, g));
        p.setPen(Qt::NoPen);
        const double rad = r(rng);
        if (i % 2)
            p.drawEllipse(QPointF(x(rng), y(rng)), rad, rad * 0.7);
        else
            p.drawRect(QRectF(x(rng), y(rng), rad, rad * 1.3));
    }
    return img;
}

// `big` seen through a w x h window centred on (cx, cy), rotated by `angle`.
QImage view(const QImage& big, int w, int h, double cx, double cy, double angle = 0) {
    QImage img(w, h, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(w / 2.0, h / 2.0);
    p.rotate(angle);
    p.translate(-cx, -cy);
    p.drawImage(0, 0, big);
    return img;
}

// Whole pixels: QPainter draws plain translations at rounded positions.
double jitter(int i) { return std::round(6.0 * std::sin(i * 1.7) + 3.0 * std::cos(i * 0.9)); }

} // namespace

class MotionTest : public QObject {
    Q_OBJECT

private slots:
    void similarityFitIgnoresOutliers() {
        Similarity truth;
        truth.a = std::cos(0.05) * 1.02;
        truth.b = std::sin(0.05) * 1.02;
        truth.tx = 4.5;
        truth.ty = -2.0;
        std::vector<QPointF> from, to;
        for (int i = 0; i < 40; ++i) {
            const QPointF p(10 + (i * 37) % 300, 5 + (i * 53) % 200);
            from.push_back(p);
            to.push_back(i % 5 == 0 ? p + QPointF(40, -30) : truth.map(p)); // 20% outliers
        }
        int inliers = 0;
        auto s = fitSimilarity(from, to, 1.0, 6, &inliers);
        QVERIFY(s);
        QCOMPARE(inliers, 32);
        QVERIFY(std::abs(s->tx - truth.tx) < 1e-6);
        QVERIFY(std::abs(s->angleDegrees() - 0.05 * 180 / std::numbers::pi) < 1e-6);
        QVERIFY(std::abs(s->scale() - 1.02) < 1e-9);
    }

    void cameraMotionIsMeasured() {
        const QImage big = texture(1200, 800, 7);
        const auto a = Pyramid::build(grayFromImage(view(big, 480, 270, 600, 400)), 3);
        // The camera moves right/down and turns: the content moves left/up
        // and turns the other way.
        const auto b = Pyramid::build(grayFromImage(view(big, 480, 270, 607.5, 403.0, 1.5)), 3);
        auto s = estimateMotion(a, b);
        QVERIFY(s);
        const QPointF c(240, 135);
        const QPointF d = s->map(c) - c;
        QVERIFY2(std::abs(d.x() + 7.5) < 0.3 && std::abs(d.y() + 3.0) < 0.3, qPrintable(QStringLiteral("%1 %2").arg(d.x()).arg(d.y())));
        QVERIFY2(std::abs(s->angleDegrees() - 1.5) < 0.1, qPrintable(QString::number(s->angleDegrees())));
    }

    void regionTrackerFollowsAnObject() {
        const QImage bg = texture(640, 360, 3);
        const QImage object = texture(80, 60, 11);
        auto frame = [&](int i) {
            QImage img = bg;
            QPainter p(&img);
            p.drawImage(QPointF(100 + 6.0 * i, 120 + 2.5 * i), object);
            return grayFromImage(img);
        };
        RegionTracker tracker(frame(0), QRectF(100, 120, 80, 60));
        for (int i = 1; i <= 40; ++i) {
            bool lost = true;
            const QRectF r = tracker.update(frame(i), &lost);
            QVERIFY(!lost);
            QVERIFY2(std::abs(r.center().x() - (140 + 6.0 * i)) < 1.5 && std::abs(r.center().y() - (150 + 2.5 * i)) < 1.5,
                     qPrintable(QStringLiteral("frame %1: %2,%3").arg(i).arg(r.center().x()).arg(r.center().y())));
        }
        // Nothing to follow on a flat picture.
        QImage flat(640, 360, QImage::Format_RGB32);
        flat.fill(Qt::gray);
        bool lost = false;
        tracker.update(grayFromImage(flat), &lost);
        QVERIFY(lost);
    }

    void stabilizationSmoothsThePath() {
        Stabilization s;
        s.enabled = true;
        s.smoothing = 1.0;
        const Ticks fd = frameDuration({25, 1});
        for (int i = 0; i < 250; ++i)
            s.path.push_back({i * fd, 0.002 * i + jitter(i) / 1000.0, jitter(i + 50) / 1000.0, 0.3 * std::sin(i * 2.1)});
        // Corrected path = raw + correction: the shake is gone, the pan stays.
        double rawShake = 0, fixedShake = 0;
        for (int i = 60; i < 190; ++i) {
            auto at = [&](int k) {
                const auto c = s.correctionAt(k * fd);
                return QPointF(s.path[k].x + c.x, s.path[k].angle + c.angle);
            };
            rawShake += std::abs((s.path[i + 1].x - s.path[i].x) - 0.002);
            fixedShake += std::abs((at(i + 1).x() - at(i).x()) - 0.002);
            QVERIFY(std::abs(at(i).y()) < 0.05);
        }
        QVERIFY2(fixedShake < rawShake / 10, qPrintable(QStringLiteral("%1 vs %2").arg(fixedShake).arg(rawShake)));
        const double zoom = s.requiredZoom(16.0 / 9.0);
        QVERIFY(zoom > 1.0 && zoom <= 1.5);
        // Round trip through JSON keeps it.
        const Stabilization back = Stabilization::fromJson(s.toJson());
        QCOMPARE(back.path.size(), s.path.size());
        QVERIFY(std::abs(back.path[17].x - s.path[17].x) < 1e-5);
        QCOMPARE(back.enabled, true);
    }

    void shakyVideoIsAnalysedAndRendered() {
        QTemporaryDir dir;
        const QImage big = texture(640, 480, 5);
        test::TestVideoSpec spec;
        spec.seconds = 2;
        spec.audio = false;
        spec.bFrames = 0;
        spec.paint = [&](int i) { return view(big, 320, 240, 320 + jitter(i), 240 + jitter(i + 7)); };
        const QString path = dir.filePath("shaky.mp4");
        QVERIFY(test::writeTestVideo(path, spec));

        const auto camera = analyzeCameraPath(path, 0, ticksFromSeconds(2));
        QCOMPARE(camera.size(), size_t(50));
        // The content moves against the camera: -jitter, as a fraction of 320 x 240.
        for (int i = 1; i < 50; ++i) {
            const double dx = (camera[i].x - camera[0].x) * 320, dy = (camera[i].y - camera[0].y) * 240;
            QVERIFY2(std::abs(dx + (jitter(i) - jitter(0))) < 0.6 && std::abs(dy + (jitter(i + 7) - jitter(7))) < 0.6,
                     qPrintable(QStringLiteral("frame %1: %2 %3").arg(i).arg(dx).arg(dy)));
        }

        // Rendered with stabilisation the picture barely moves between frames.
        MediaPool pool;
        auto item = pool.import(path);
        QVERIFY(item);
        Timeline tl;
        tl.setFrameRate({25, 1});
        const QUuid id = tl.insertMedia(item->id, 0, item->info.duration, true, false);
        Stabilization st;
        st.enabled = true;
        st.smoothing = 2.0;
        st.path = camera;
        st.zoom = st.requiredZoom(320.0 / 240.0);
        QVERIFY(tl.setStabilization(id, st));
        QVERIFY(tl.state().hasVideoEffects());
        FrameRenderer r(&pool);
        r.setTimeline(tl.state());
        r.setCanvasSize({320, 240});
        r.setOutputSize({320, 240});
        QVERIFY(!r.passthroughFrame(0));
        const Ticks fd = frameDuration({25, 1});
        auto moveBetween = [&](int a, int b) {
            const auto pa = Pyramid::build(grayFromImage(r.render(a * fd + fd / 2)), 3);
            const auto pb = Pyramid::build(grayFromImage(r.render(b * fd + fd / 2)), 3);
            auto s = estimateMotion(pa, pb);
            if (!s)
                return 99.0;
            const QPointF c(160, 120);
            const QPointF d = s->map(c) - c;
            return std::hypot(d.x(), d.y());
        };
        double stabilized = 0, shaky = 0;
        for (int i = 20; i < 30; ++i) {
            stabilized += moveBetween(i, i + 1);
            shaky += std::hypot(jitter(i + 1) - jitter(i), jitter(i + 8) - jitter(i + 7));
        }
        QVERIFY2(stabilized < shaky / 3, qPrintable(QStringLiteral("%1 vs %2").arg(stabilized).arg(shaky)));
    }

    void titleFollowsATrackedObject() {
        QTemporaryDir dir;
        const QImage bg = texture(320, 240, 21);
        const QImage object = texture(48, 40, 22);
        test::TestVideoSpec spec;
        spec.seconds = 2;
        spec.audio = false;
        spec.bFrames = 0;
        spec.paint = [&](int i) {
            QImage img = bg;
            QPainter p(&img);
            p.drawImage(QPointF(40 + 3.0 * i, 60 + 1.0 * i), object);
            return img;
        };
        const QString path = dir.filePath("object.mp4");
        QVERIFY(test::writeTestVideo(path, spec));
        Project project;
        auto item = project.media()->import(path);
        QVERIFY(item);
        Timeline* tl = project.timeline();
        tl->setFrameRate({25, 1});
        tl->insertMedia(item->id, 0, item->info.duration, true, false);
        const int v2 = tl->addTrack(TrackType::Video);
        TitleData title;
        title.text = "Ball";
        const QUuid t = tl->addTitle(title, ticksFromSeconds(1.6), v2, 0);

        const QRectF region(40.0 / 320, 60.0 / 240, 48.0 / 320, 40.0 / 240);
        const MotionPath m = trackRegion(tl->state(), *project.media(), 0, ticksFromSeconds(1.6), region, {25, 1});
        QCOMPARE(m.points.size(), size_t(40));
        const Ticks fd = frameDuration({25, 1});
        for (int i = 0; i < 40; ++i) {
            const QPointF o = m.offsetAt(i * fd);
            QVERIFY2(std::abs(o.x() * 320 - 3.0 * i) < 1.5 && std::abs(o.y() * 240 - 1.0 * i) < 1.5,
                     qPrintable(QStringLiteral("frame %1: %2 %3").arg(i).arg(o.x() * 320).arg(o.y() * 240)));
        }
        QVERIFY(tl->setMotionPath(t, m));
        QVERIFY(tl->state().hasVideoEffects());
        // Saved and loaded with the project (format 5); split keeps it in place.
        const QByteArray json = project.toJson();
        QVERIFY(json.contains("\"version\": 5"));
        Project q;
        QString err;
        QVERIFY2(q.fromJson(json, {}, &err), qPrintable(err));
        QCOMPARE(q.timeline()->state(), tl->state());
        QVERIFY(tl->splitAt(20 * fd));
        const Clip* right = tl->clipAt(v2, 25 * fd);
        QVERIFY(right && right->id != t);
        QVERIFY(std::abs(right->motion.offsetAt(5 * fd).x() * 320 - 75.0) < 1.5);
    }
};

QTEST_GUILESS_MAIN(MotionTest)
#include "tst_motion.moc"

#include <QTemporaryDir>
#include <QtTest>

#include "TestMedia.h"
#include "audio/AudioEffects.h"
#include "project/Project.h"
#include "timeline/Subtitles.h"

using namespace lumora;

class ProjectTest : public QObject {
    Q_OBJECT

private slots:
    void roundTrip() {
        QTemporaryDir dir;
        const QString video = dir.filePath("clip.mp4");
        test::TestVideoSpec spec;
        spec.seconds = 2;
        QVERIFY(test::writeTestVideo(video, spec));
        const QString photo = dir.filePath("photo.png");
        QImage(10, 10, QImage::Format_RGB888).save(photo);

        Project p;
        auto item = p.media()->import(video);
        QVERIFY(item);
        p.timeline()->setFrameRate({25, 1});
        p.timeline()->insertMedia(item->id, 0, item->info.duration, true, true);
        p.timeline()->splitAt(ticksFromSeconds(1));
        // Version 2 content: titles on V2, transitions, transforms, fades, gain.
        Timeline* tl = p.timeline();
        tl->setTransition(tl->tracks()[0].clips[1].id, {TransitionType::DipToBlack, ticksFromSeconds(0.4)});
        const int v2 = tl->addTrack(TrackType::Video);
        TitleData title;
        title.text = QStringLiteral("Merhaba dünya");
        title.color = QColor(250, 200, 10);
        title.animationIn = TitleAnimation::Typewriter;
        const QUuid titleId = tl->addTitle(title, ticksFromSeconds(1), v2, ticksFromSeconds(0.5));
        tl->setTransform(titleId, {0.1, -0.2, 1.5, 12, 0.8});
        tl->setFades(titleId, ticksFromSeconds(0.2), ticksFromSeconds(0.3));
        tl->setGain(tl->tracks()[1].clips[0].id, -6);
        // Version 3: clip colour, photo layers.
        ColorGrade grade;
        grade.saturation = -20;
        grade.gain = {0.1, 0.0, 0.2};
        grade.lutPath = dir.filePath("look.cube");
        grade.lutAmount = 0.5;
        tl->setColor(tl->tracks()[0].clips[0].id, grade);
        PhotoAdjustments adj;
        adj.color.exposure = 1.25;
        adj.crop = QRectF(0.1, 0.2, 0.5, 0.5);
        PhotoLayer layer;
        layer.id = QUuid::createUuid();
        layer.name = QStringLiteral("Gökyüzü");
        layer.mask.shape = MaskShape::Ellipse;
        layer.grade.exposure = -0.5;
        adj.layers.push_back(layer);
        p.photos()->add(photo, adj);
        const QString projectPath = dir.filePath("test.lumora");
        QString err;
        QVERIFY2(p.save(projectPath, &err), qPrintable(err));

        Project q;
        QStringList missing;
        QVERIFY2(q.load(projectPath, &err, &missing), qPrintable(err));
        QVERIFY(missing.isEmpty());
        QCOMPARE(q.timeline()->state(), p.timeline()->state());
        QCOMPARE(q.media()->items().size(), size_t(1));
        QCOMPARE(q.media()->items()[0]->id, item->id);
        QCOMPARE(q.photos()->entries().size(), size_t(1));
        QCOMPARE(q.photos()->entries()[0].adjustments, adj);
    }

    void relativePathsSurviveMovingTheFolder() {
        QTemporaryDir dir;
        QDir(dir.path()).mkdir("a");
        const QString video = dir.filePath("a/clip.mp4");
        test::TestVideoSpec spec;
        spec.seconds = 1;
        QVERIFY(test::writeTestVideo(video, spec));
        Project p;
        auto item = p.media()->import(video);
        p.timeline()->insertMedia(item->id, 0, item->info.duration, true, true);
        QVERIFY(p.save(dir.filePath("a/p.lumora")));
        QVERIFY(QDir(dir.path()).rename("a", "b"));
        Project q;
        QStringList missing;
        QVERIFY(q.load(dir.filePath("b/p.lumora"), nullptr, &missing));
        QVERIFY(missing.isEmpty());
        QVERIFY(q.media()->items()[0]->path.endsWith("b/clip.mp4"));
    }

    void subtitlesAndAudioSurviveSaving() {
        Project p;
        Timeline* tl = p.timeline();
        const int s = tl->addSubtitleTrack({{0, ticksFromSeconds(1), QStringLiteral("Selam\nnasılsın")}}, QStringLiteral("tur"));
        SubtitleStyle style;
        style.size = 0.07;
        style.backgroundOpacity = 0.5;
        QVERIFY(tl->setSubtitleStyle(s, style));
        AudioMaster m;
        m.normalize = true;
        m.targetLufs = -16;
        QVERIFY(tl->setAudioMaster(m));
        const QByteArray json = p.toJson();
        QVERIFY(json.contains("\"version\": 3"));
        Project q;
        QString err;
        QVERIFY2(q.fromJson(json, {}, &err), qPrintable(err));
        QCOMPARE(q.timeline()->state(), tl->state());
        // Without subtitles the file stays readable by 0.3.
        Project r;
        QVERIFY(r.toJson().contains("\"version\": 2"));
    }

    void freezeFramesSurviveSaving() {
        Project p;
        Timeline* tl = p.timeline();
        tl->setFrameRate({25, 1});
        tl->insertMedia(QUuid::createUuid(), 0, ticksFromSeconds(4), true, true);
        const QUuid f = tl->freezeFrame(tl->tracks()[0].clips[0].id, ticksFromSeconds(2), ticksFromSeconds(3));
        QVERIFY(!f.isNull());
        ColorGrade g;
        g.saturation = 0.5;
        QVERIFY(tl->setColor(f, g));
        const QByteArray json = p.toJson();
        QVERIFY(json.contains("\"version\": 4"));
        QVERIFY(json.contains("\"freeze\""));
        Project q;
        QString err;
        QVERIFY2(q.fromJson(json, {}, &err), qPrintable(err));
        QCOMPARE(q.timeline()->state(), tl->state());
        QCOMPARE(q.timeline()->clip(f)->freezeTime, ticksFromSeconds(2));
    }

    void stickersSurviveSaving() {
        QTemporaryDir dir;
        QImage png(40, 20, QImage::Format_ARGB32);
        png.fill(Qt::transparent);
        QVERIFY(png.save(dir.filePath("sticker.png")));
        Project p;
        Timeline* tl = p.timeline();
        tl->setFrameRate({25, 1});
        tl->insertMedia(QUuid::createUuid(), 0, ticksFromSeconds(4), true, false);
        const QUuid video = tl->tracks()[0].clips[0].id;
        QVERIFY(tl->setLoop(video, true, ticksFromSeconds(4)));
        auto item = p.media()->import(dir.filePath("sticker.png"));
        QVERIFY(item);
        const int v2 = tl->addTrack(TrackType::Video);
        const QUuid still = tl->addStill(item->id, ticksFromSeconds(5), v2, ticksFromSeconds(1), {0.3, -0.2, 0.35, 15.0, 1.0});
        QVERIFY(!still.isNull());
        const QByteArray json = p.toJson();
        QVERIFY(json.contains("\"version\": 7"));
        Project q;
        QString err;
        QVERIFY2(q.fromJson(json, {}, &err), qPrintable(err));
        QCOMPARE(q.timeline()->state(), tl->state());
        QVERIFY(q.timeline()->clip(video)->loops());

        PhotoLayer l;
        l.id = QUuid::createUuid();
        l.kind = LayerKind::Image;
        l.imagePath = QStringLiteral("clip.mp4");
        l.frameTime = ticksFromSeconds(1.5);
        l.rotation = -30;
        QCOMPARE(PhotoLayer::fromJson(l.toJson()), l);
    }

};

QTEST_GUILESS_MAIN(ProjectTest)
#include "tst_project.moc"

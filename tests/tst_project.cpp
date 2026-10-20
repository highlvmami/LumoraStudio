#include <QTemporaryDir>
#include <QtTest>

#include "TestMedia.h"
#include "project/Project.h"

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
        PhotoAdjustments adj;
        adj.exposure = 1.25;
        adj.crop = QRectF(0.1, 0.2, 0.5, 0.5);
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
};

QTEST_GUILESS_MAIN(ProjectTest)
#include "tst_project.moc"

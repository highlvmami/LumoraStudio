#include <QtTest>

#include "update/UpdateManifest.h"

using namespace lumora;

namespace {

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray sha(const QByteArray& data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(); }

} // namespace

class UpdateTest : public QObject {
    Q_OBJECT

private slots:
    void readsTheManifest() {
        const auto m = update::Manifest::fromJson(R"({"version":"0.5.1",
            "files":{"LumoraStudio.exe":"AB12","translations/qt_tr.qm":"cd34"},
            "patch":{"from":"0.5.0","files":["LumoraStudio.exe"]}})");
        QVERIFY(m);
        QCOMPARE(m->version, QStringLiteral("0.5.1"));
        QCOMPARE(m->files.size(), 2);
        QCOMPARE(m->files.value(QStringLiteral("LumoraStudio.exe")), QByteArray("ab12"));
        QCOMPARE(m->patchFrom, QStringLiteral("0.5.0"));
        QVERIFY(m->patchFiles.contains(QStringLiteral("LumoraStudio.exe")));
    }

    void rejectsPathsOutsideTheAppFolder() {
        QVERIFY(!update::Manifest::fromJson(R"({"files":{"../evil.dll":"00"}})"));
        QVERIFY(!update::Manifest::fromJson(R"({"files":{"a/../../evil.dll":"00"}})"));
        QVERIFY(!update::Manifest::fromJson(R"({"files":{}})"));
        QVERIFY(!update::Manifest::fromJson("not json"));
    }

    void findsWhatChangedAndWhetherThePatchIsEnough() {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("LumoraStudio.exe")), "old exe");
        writeFile(dir.filePath(QStringLiteral("avcodec-62.dll")), "codec");
        writeFile(dir.filePath(QStringLiteral("translations/qt_tr.qm")), "qt strings");

        update::Manifest m;
        m.files.insert(QStringLiteral("LumoraStudio.exe"), sha("new exe"));
        m.files.insert(QStringLiteral("avcodec-62.dll"), sha("codec"));
        m.files.insert(QStringLiteral("translations/qt_tr.qm"), sha("qt strings"));
        m.files.insert(QStringLiteral("lumora_tr.qm"), sha("app strings")); // new file
        m.patchFiles = {QStringLiteral("LumoraStudio.exe"), QStringLiteral("lumora_tr.qm")};

        const QStringList outdated = update::outdatedFiles(m, dir.path());
        QCOMPARE(outdated, (QStringList{QStringLiteral("LumoraStudio.exe"), QStringLiteral("lumora_tr.qm")}));
        QVERIFY(update::patchCovers(m, outdated));

        // An install that is further behind needs the whole package.
        writeFile(dir.filePath(QStringLiteral("avcodec-62.dll")), "older codec");
        QVERIFY(!update::patchCovers(m, update::outdatedFiles(m, dir.path())));

        m.patchFiles.clear();
        QVERIFY(!update::patchCovers(m, outdated));
    }
};

QTEST_GUILESS_MAIN(UpdateTest)
#include "tst_update.moc"

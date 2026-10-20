#include "Updater.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>
#include <QVersionNumber>

namespace lumora {

namespace {

const char* const kLatestRelease = "https://api.github.com/repos/highlvmami/LumoraStudio/releases/latest";
const char* const kWindowsAsset = "LumoraStudio-windows-x64.zip";
const char* const kAutoKey = "updates/autoCheck";
const char* const kLastKey = "updates/lastCheck";

QNetworkRequest request(const QUrl& url) {
    QNetworkRequest r(url);
    // GitHub's API refuses requests without a user agent.
    r.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("LumoraStudio/%1").arg(QCoreApplication::applicationVersion()));
    r.setRawHeader("Accept", "application/vnd.github+json");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}

QVersionNumber version(QString s) {
    if (s.startsWith(QLatin1Char('v')) || s.startsWith(QLatin1Char('V')))
        s.remove(0, 1);
    return QVersionNumber::fromString(s);
}

// Extracts a release zip with the tar.exe that ships with Windows 10 and later.
bool extractZip(const QString& zip, const QString& dir) {
    QProcess tar;
    tar.start(QDir(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"))).filePath(QStringLiteral("System32/tar.exe")),
              {QStringLiteral("-xf"), QDir::toNativeSeparators(zip), QStringLiteral("-C"), QDir::toNativeSeparators(dir)});
    return tar.waitForFinished(300000) && tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0;
}

} // namespace

Updater::Updater(QMainWindow* window) : QObject(window), m_window(window) {}

bool Updater::autoCheck() { return QSettings().value(QLatin1String(kAutoKey), true).toBool(); }

void Updater::setAutoCheck(bool on) { QSettings().setValue(QLatin1String(kAutoKey), on); }

QDateTime Updater::lastCheck() { return QSettings().value(QLatin1String(kLastKey)).toDateTime(); }

bool Updater::isNewer(const QString& tag, const QString& current) {
    const QVersionNumber latest = version(tag);
    return !latest.isNull() && latest > version(current);
}

void Updater::checkOnStartup() {
    if (!autoCheck())
        return;
    const QDateTime last = QSettings().value(QLatin1String(kLastKey)).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 20 * 3600)
        return;
    // Let the window settle first; the check runs in the background.
    QTimer::singleShot(4000, this, [this] { check(false); });
}

void Updater::checkNow() { check(true); }

void Updater::check(bool interactive) {
    if (m_busy)
        return;
    m_busy = true;
    QNetworkReply* reply = m_net.get(request(QUrl(QString::fromLatin1(kLatestRelease))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive] {
        reply->deleteLater();
        m_busy = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status != 404) {
            if (interactive)
                QMessageBox::warning(m_window, tr("Check for updates"),
                                     tr("Could not reach GitHub: %1").arg(reply->errorString()));
            return;
        }
        QSettings().setValue(QLatin1String(kLastKey), QDateTime::currentDateTimeUtc());
        emit checked();
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        if (status == 404 || !isNewer(release.value(QLatin1String("tag_name")).toString(),
                                      QCoreApplication::applicationVersion())) {
            if (interactive)
                QMessageBox::information(m_window, tr("Check for updates"),
                                         tr("LumoraStudio %1 is the latest version.")
                                             .arg(QCoreApplication::applicationVersion()));
            return;
        }
        offer(release, interactive);
    });
}

void Updater::offer(const QJsonObject& release, bool interactive) {
    const QString tag = release.value(QLatin1String("tag_name")).toString();
    const QString newVersion = version(tag).toString();
    const QUrl page(release.value(QLatin1String("html_url")).toString());
    QJsonObject asset;
    for (const QJsonValue& a : release.value(QLatin1String("assets")).toArray())
        if (a.toObject().value(QLatin1String("name")).toString() == QLatin1String(kWindowsAsset))
            asset = a.toObject();

#ifdef Q_OS_WIN
    const bool automatic = !asset.isEmpty() && canReplaceFiles();
#else
    const bool automatic = false;
#endif

    QMessageBox box(m_window);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Update available"));
    box.setText(tr("LumoraStudio %1 is available. You have %2.")
                    .arg(newVersion, QCoreApplication::applicationVersion()));
    box.setInformativeText(automatic ? tr("Install it now? LumoraStudio will close, update itself and start "
                                          "again. You will be asked to save your project first.")
                                     : tr("Open the download page?"));
    const QString notes = release.value(QLatin1String("body")).toString().trimmed();
    if (!notes.isEmpty())
        box.setDetailedText(notes);
    QPushButton* yes = box.addButton(automatic ? tr("Update now") : tr("Open download page"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    QPushButton* never = interactive ? nullptr : box.addButton(tr("Don't check automatically"), QMessageBox::DestructiveRole);
    box.setDefaultButton(yes);
    box.exec();
    if (box.clickedButton() == never) {
        setAutoCheck(false);
        return;
    }
    if (box.clickedButton() != yes)
        return;
    if (automatic)
        download(asset, newVersion);
    else
        QDesktopServices::openUrl(page);
}

bool Updater::canReplaceFiles() const {
    // A portable copy in the user's folders can update itself; one under
    // Program Files would need administrator rights.
    QTemporaryFile probe(QCoreApplication::applicationDirPath() + QStringLiteral("/.lumora-update-XXXXXX"));
    return probe.open();
}

void Updater::download(const QJsonObject& asset, const QString& newVersion) {
    const QUrl url(asset.value(QLatin1String("browser_download_url")).toString());
    // GitHub publishes "sha256:<hex>" for every release asset.
    const QString digest = asset.value(QLatin1String("digest")).toString();
    const QByteArray expected =
        digest.startsWith(QLatin1String("sha256:")) ? digest.mid(7).toLatin1().toLower() : QByteArray();

    const QString zipPath =
        QDir::temp().filePath(QStringLiteral("LumoraStudio-%1-%2.zip")
                                  .arg(newVersion, QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    auto* file = new QSaveFile(zipPath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(m_window, tr("Update"), tr("Cannot write %1.").arg(QDir::toNativeSeparators(zipPath)));
        file->deleteLater();
        return;
    }
    auto* hash = new QCryptographicHash(QCryptographicHash::Sha256);

    m_progress = new QProgressDialog(tr("Downloading LumoraStudio %1…").arg(newVersion), tr("Cancel"), 0, 100, m_window);
    m_progress->setWindowTitle(tr("Update"));
    m_progress->setMinimumDuration(0);
    m_progress->setAutoClose(false);
    m_progress->setAutoReset(false);
    m_progress->setValue(0);

    QNetworkRequest req = request(url);
    req.setRawHeader("Accept", "application/octet-stream");
    m_reply = m_net.get(req);
    connect(m_progress, &QProgressDialog::canceled, m_reply, &QNetworkReply::abort);
    connect(m_reply, &QNetworkReply::downloadProgress, m_progress, [this](qint64 got, qint64 total) {
        if (total > 0)
            m_progress->setValue(static_cast<int>(got * 100 / total));
    });
    connect(m_reply, &QNetworkReply::readyRead, this, [this, file, hash] {
        const QByteArray data = m_reply->readAll();
        hash->addData(data);
        file->write(data);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, file, hash, expected, zipPath] {
        QNetworkReply* reply = m_reply;
        reply->deleteLater();
        const QByteArray rest = reply->readAll();
        hash->addData(rest);
        file->write(rest);
        const QByteArray got = hash->result().toHex();
        delete hash;
        if (m_progress)
            m_progress->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            file->cancelWriting();
            file->deleteLater();
            if (reply->error() != QNetworkReply::OperationCanceledError)
                QMessageBox::warning(m_window, tr("Update"), tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        if (!expected.isEmpty() && got != expected) {
            file->cancelWriting();
            file->deleteLater();
            QMessageBox::warning(m_window, tr("Update"),
                                 tr("The downloaded file is damaged (its checksum does not match). Nothing was "
                                    "changed; please try again later."));
            return;
        }
        const bool committed = file->commit();
        file->deleteLater();
        if (!committed) {
            QMessageBox::warning(m_window, tr("Update"), tr("Cannot write %1.").arg(QDir::toNativeSeparators(zipPath)));
            return;
        }
        install(zipPath);
    });
}

void Updater::install(const QString& zipPath) {
    const QString work = QDir::temp().filePath(QStringLiteral("lumora-update-%1")
                                                   .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    auto cleanup = [work, zipPath] {
        QDir(work).removeRecursively();
        QFile::remove(zipPath);
    };
    QString source;
    if (QDir().mkpath(work) && extractZip(zipPath, work)) {
        QDirIterator it(work, {QStringLiteral("LumoraStudio.exe")}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext())
            source = QFileInfo(it.next()).absolutePath();
    }
    if (source.isEmpty()) {
        cleanup();
        QMessageBox::warning(m_window, tr("Update"), tr("The downloaded package could not be unpacked."));
        return;
    }
    // Closing goes through the usual "save changes?" question; if the user
    // cancels it, nothing is replaced.
    if (!m_window->close()) {
        cleanup();
        return;
    }

    // Windows lets a running program and its loaded libraries be renamed but
    // not overwritten: move each old file aside as *.old, copy the new one in,
    // and put everything back if any step fails. The *.old files are removed
    // on the next start.
    const QDir dest(QCoreApplication::applicationDirPath());
    QList<std::pair<QString, QString>> moved; // target, its .old name
    QStringList added;
    bool ok = true;
    QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
    while (ok && it.hasNext()) {
        const QString from = it.next();
        const QString target = dest.filePath(QDir(source).relativeFilePath(from));
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (QFile::exists(target)) {
            QString old = target + QStringLiteral(".old");
            if (QFile::exists(old) && !QFile::remove(old))
                old = target + QStringLiteral(".%1.old").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
            if (!QFile::rename(target, old)) {
                ok = false;
                break;
            }
            moved.append({target, old});
        } else {
            added.append(target);
        }
        ok = QFile::copy(from, target);
    }
    if (!ok) {
        for (const QString& f : added)
            QFile::remove(f);
        for (const auto& [target, old] : moved) {
            QFile::remove(target);
            QFile::rename(old, target);
        }
    }
    cleanup();
    if (!ok)
        QMessageBox::warning(nullptr, tr("Update"),
                             tr("LumoraStudio could not be updated, so the current version was kept. Close any "
                                "other copy of LumoraStudio and try again."));
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    QCoreApplication::quit();
}

void Updater::removeLeftovers() {
    QDirIterator it(QCoreApplication::applicationDirPath(), {QStringLiteral("*.old")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        QFile::remove(it.next());
}

} // namespace lumora

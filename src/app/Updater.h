#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QMainWindow;
class QNetworkReply;
class QProgressDialog;

namespace lumora {

// Keeps the app up to date from the project's GitHub releases. On Windows
// the release package is downloaded, checked against its published SHA-256
// and copied over the app folder, keeping the old files until the new
// version has started; elsewhere the release page is opened.
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QMainWindow* window);

    // Asks GitHub at most once a day, unless automatic checks are turned off.
    void checkOnStartup();
    // Menu command: always asks and reports "up to date" or errors.
    void checkNow();

    static bool autoCheck();
    static void setAutoCheck(bool on);
    static QDateTime lastCheck();

    // "v0.2.1" / "0.2.1" newer than the running version?
    static bool isNewer(const QString& tag, const QString& current);
    // Deletes the files an update moved aside; call once at start-up.
    static void removeLeftovers();

signals:
    // A check reached GitHub (whatever it found).
    void checked();

private:
    void check(bool interactive);
    void offer(const QJsonObject& release, bool interactive);
    void download(const QJsonObject& asset, const QString& version);
    void install(const QString& zipPath);
    bool canReplaceFiles() const;

    QWidget* dialogParent() const;
    QMainWindow* m_window;
    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    QPointer<QProgressDialog> m_progress;
    bool m_busy = false;
};

} // namespace lumora

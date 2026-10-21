#include "Tasks.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>

#include "ai/Vision.h"

namespace lumora {

namespace {
struct Tasks {
    Q_DECLARE_TR_FUNCTIONS(lumora::Tasks)
};
} // namespace

bool downloadFile(QWidget* parent, const QUrl& url, const QString& dest, const QString& label, QString* error) {
    QNetworkAccessManager net;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QSaveFile file(dest);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    QProgressDialog dlg(label, QCoreApplication::translate("lumora::MainWindow", "Cancel"), 0, 1000, parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    QNetworkReply* reply = net.get(req);
    QObject::connect(reply, &QNetworkReply::readyRead, &dlg, [&] { file.write(reply->readAll()); });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &dlg, [&](qint64 got, qint64 total) {
        if (total > 0)
            dlg.setValue(static_cast<int>(got * 1000 / total));
        dlg.setLabelText(QStringLiteral("%1\n%2 / %3 MB").arg(label).arg(got / 1000000).arg(total > 0 ? total / 1000000 : 0));
    });
    QObject::connect(&dlg, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    file.write(reply->readAll());
    const bool cancelled = dlg.wasCanceled();
    const bool ok = reply->error() == QNetworkReply::NoError && !cancelled;
    if (!ok && !cancelled)
        *error = reply->errorString();
    reply->deleteLater();
    if (!ok) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool ensureVisionModels(QWidget* parent, const QStringList& ids, const QString& title) {
    if (!ai::visionAvailable()) {
        QMessageBox::information(parent, title, Tasks::tr("This build of LumoraStudio has no AI tools."));
        return false;
    }
    for (const QString& id : ids) {
        if (ai::visionModelReady(id))
            continue;
        const ai::VisionModel m = ai::visionModel(id);
        QString error;
        if (!downloadFile(parent, m.url, ai::visionModelPath(id), Tasks::tr("Downloading the %1 model (%2 MB, once)…").arg(m.label).arg(m.sizeMB),
                          &error)) {
            if (!error.isEmpty())
                QMessageBox::warning(parent, title, Tasks::tr("The %1 model could not be downloaded: %2").arg(m.label, error));
            return false;
        }
    }
    return true;
}

} // namespace lumora

#include "Tasks.h"

#include <QComboBox>
#include <QFileInfo>
#include <QSettings>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>

#include "ai/SpeechToText.h"
#include "ai/Translate.h"
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

bool ensureTranslationModels(QWidget* parent, const QStringList& ids, const QString& title) {
    if (!translate::available()) {
        QMessageBox::information(parent, title, Tasks::tr("This build of LumoraStudio has no AI tools."));
        return false;
    }
    for (const QString& id : ids) {
        if (translate::modelReady(id))
            continue;
        const translate::Model m = translate::model(id);
        const QStringList pair = id.split(QLatin1Char('-'));
        const QString name = Tasks::tr("%1 → %2").arg(translate::languageName(pair.value(0)), translate::languageName(pair.value(1)));
        const QString dir = translate::modelDir(id);
        for (const auto& file : translate::modelFiles()) {
            const QString dest = dir + QLatin1Char('/') + file.local;
            if (QFileInfo::exists(dest))
                continue;
            QString error;
            if (!downloadFile(parent, translate::fileUrl(id, file), dest,
                              Tasks::tr("Downloading the %1 translation model (about %2 MB, once)…").arg(name).arg(m.sizeMB), &error)) {
                if (!error.isEmpty())
                    QMessageBox::warning(parent, title, Tasks::tr("The %1 translation model could not be downloaded: %2").arg(name, error));
                return false;
            }
        }
    }
    return true;
}

QComboBox* speechLanguageBox() {
    auto* language = new QComboBox;
    const std::pair<QString, QString> languages[] = {
        {QStringLiteral("tr"), Tasks::tr("Turkish")}, {QStringLiteral("en"), Tasks::tr("English")},
        {QStringLiteral("de"), Tasks::tr("German")},  {QStringLiteral("fr"), Tasks::tr("French")},
        {QStringLiteral("es"), Tasks::tr("Spanish")}, {QStringLiteral("ar"), Tasks::tr("Arabic")},
        {QStringLiteral("ru"), Tasks::tr("Russian")}, {QStringLiteral("auto"), Tasks::tr("Detect automatically")}};
    for (const auto& [code, name] : languages)
        language->addItem(name, code);
    language->setCurrentIndex(
        std::max(0, language->findData(QSettings().value(QStringLiteral("speech/language"), QStringLiteral("tr")))));
    return language;
}

QComboBox* speechModelBox() {
    auto* model = new QComboBox;
    for (const auto& m : speech::models()) {
        const bool have = QFileInfo::exists(speech::modelPath(m.id));
        model->addItem(have ? Tasks::tr("%1 · downloaded").arg(m.label) : Tasks::tr("%1 · %2 MB download").arg(m.label).arg(m.sizeMB),
                       m.id);
    }
    model->setCurrentIndex(std::max(0, model->findData(QSettings().value(QStringLiteral("speech/model"), speech::defaultModel()))));
    return model;
}

QString ensureSpeechModel(QWidget* parent, QComboBox* language, QComboBox* model, const QString& title) {
    if (!speech::available()) {
        QMessageBox::information(parent, title, Tasks::tr("This build of LumoraStudio has no speech recognition."));
        return {};
    }
    const QString modelId = model->currentData().toString();
    QSettings settings;
    settings.setValue(QStringLiteral("speech/language"), language->currentData().toString());
    settings.setValue(QStringLiteral("speech/model"), modelId);
    const QString path = speech::modelPath(modelId);
    if (!QFileInfo::exists(path)) {
        QString error;
        if (!downloadFile(parent, speech::modelUrl(modelId), path, Tasks::tr("Downloading the speech recognition model…"), &error)) {
            if (!error.isEmpty())
                QMessageBox::warning(parent, title, Tasks::tr("The model could not be downloaded: %1").arg(error));
            return {};
        }
    }
    return path;
}

} // namespace lumora

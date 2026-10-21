#pragma once

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QWidget;

namespace lumora {

// Reports progress (0..1); returning false cancels the work.
using TaskProgress = std::function<bool(double)>;

// Runs `work` on a worker thread behind a modal progress dialog. Returns
// nullopt when the user cancels.
template <class T>
std::optional<T> withProgress(QWidget* parent, const QString& label, std::function<T(const TaskProgress&)> work) {
    auto progress = std::make_shared<std::atomic<int>>(0);
    auto cancel = std::make_shared<std::atomic_bool>(false);
    QProgressDialog dlg(label, QCoreApplication::translate("lumora::MainWindow", "Cancel"), 0, 1000, parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    QObject::connect(&dlg, &QProgressDialog::canceled, [cancel] { *cancel = true; });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &dlg, [&] { dlg.setValue(*progress); });
    timer.start(100);
    QFutureWatcher<T> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([work = std::move(work), progress, cancel] {
        return work([progress, cancel](double p) {
            *progress = static_cast<int>(std::clamp(p, 0.0, 1.0) * 1000);
            return !*cancel;
        });
    }));
    loop.exec();
    if (*cancel)
        return std::nullopt;
    return watcher.result();
}

// Downloads `url` to `dest` behind a progress dialog. False when it failed
// (with `error` set) or was cancelled (`error` empty).
bool downloadFile(QWidget* parent, const QUrl& url, const QString& dest, const QString& label, QString* error);

// Makes sure the picture models `ids` (ai::visionModels) are on this
// computer, downloading the missing ones. False (after telling the user why)
// when one is still missing.
bool ensureVisionModels(QWidget* parent, const QStringList& ids, const QString& title);

} // namespace lumora

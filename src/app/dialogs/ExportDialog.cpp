#include "ExportDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>

#include "export/ExportPlanner.h"
#include "export/TimelineExporter.h"
#include "project/Project.h"
#include "undo/TimelineCommands.h"

namespace lumora {

ExportDialog::ExportDialog(Project* project, QUndoStack* undo, QWidget* parent)
    : QDialog(parent), m_project(project), m_undo(undo) {
    setWindowTitle(tr("Export video"));
    setMinimumWidth(620);
    auto* layout = new QVBoxLayout(this);

    auto* modeBox = new QGroupBox(tr("Quality"));
    auto* modeLayout = new QVBoxLayout(modeBox);
    m_modes = new QButtonGroup(this);
    struct ModeText {
        ExportMode mode;
        QString title;
        QString help;
    };
    const ModeText modes[] = {
        {ExportMode::StreamCopy, tr("Lossless cut (no re-encoding) — recommended"),
         tr("Video and audio are copied bit for bit. Quality is exactly the source's and export takes seconds. "
            "Cuts start on key frames.")},
        {ExportMode::MatchSource, tr("Same as source (re-encode)"),
         tr("Frame-exact cuts. Codec, resolution, frame rate, bit depth, colour and bit rate are taken from the "
            "source. Audio is copied when possible.")},
        {ExportMode::LosslessIntermediate, tr("Lossless intermediate (FFV1 / MKV)"),
         tr("Frame-exact and mathematically lossless. Very large files; ideal for archiving or further editing.")},
    };
    for (const auto& m : modes) {
        auto* rb = new QRadioButton(m.title);
        auto* help = new QLabel(m.help);
        help->setWordWrap(true);
        help->setContentsMargins(24, 0, 0, 6);
        help->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        m_modes->addButton(rb, static_cast<int>(m.mode));
        modeLayout->addWidget(rb);
        modeLayout->addWidget(help);
    }
    m_modes->button(static_cast<int>(ExportMode::StreamCopy))->setChecked(true);
    layout->addWidget(modeBox);

    auto* form = new QFormLayout;
    auto* pathRow = new QHBoxLayout;
    m_path = new QLineEdit;
    auto* browseBtn = new QPushButton(tr("Browse…"));
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseBtn);
    form->addRow(tr("File:"), pathRow);
    m_audio = new QCheckBox(tr("Include audio"));
    m_audio->setChecked(true);
    form->addRow(QString(), m_audio);
    layout->addLayout(form);

    auto* planBox = new QGroupBox(tr("What will happen"));
    auto* planLayout = new QVBoxLayout(planBox);
    m_planLabel = new QLabel;
    m_planLabel->setWordWrap(true);
    m_planLabel->setTextFormat(Qt::RichText);
    planLayout->addWidget(m_planLabel);
    m_snapBtn = new QPushButton(tr("Snap cuts to key frames"));
    m_snapBtn->setToolTip(tr("Moves each clip's start back to the previous key frame on the timeline, so the "
                             "timeline shows exactly what the lossless export contains. Can be undone."));
    planLayout->addWidget(m_snapBtn, 0, Qt::AlignLeft);
    layout->addWidget(planBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_exportBtn = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    m_exportBtn->setDefault(true);
    layout->addWidget(buttons);

    // Default output name next to the first source.
    const auto& clips = project->timeline()->tracks()[0].clips;
    QString base = QDir::homePath() + QStringLiteral("/LumoraStudio");
    QString srcPath;
    if (!clips.empty())
        if (auto m = project->media()->find(clips.front().mediaId))
            srcPath = m->path;
    if (!srcPath.isEmpty()) {
        const QFileInfo fi(srcPath);
        base = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("_edit");
    }
    m_path->setText(QDir::toNativeSeparators(base + QLatin1Char('.') +
                                             ExportPlanner::suggestedExtension(ExportMode::StreamCopy, srcPath)));

    connect(m_modes, &QButtonGroup::idClicked, this, [this] {
        updateExtension();
        replan();
    });
    connect(m_audio, &QCheckBox::toggled, this, &ExportDialog::replan);
    connect(m_path, &QLineEdit::editingFinished, this, &ExportDialog::replan);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(m_snapBtn, &QPushButton::clicked, this, &ExportDialog::snapCuts);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::startExport);
    replan();
}

ExportSettings ExportDialog::settings() const {
    ExportSettings s;
    s.mode = static_cast<ExportMode>(m_modes->checkedId());
    s.outputPath = QDir::fromNativeSeparators(m_path->text().trimmed());
    s.includeAudio = m_audio->isChecked();
    return s;
}

void ExportDialog::updateExtension() {
    const auto& clips = m_project->timeline()->tracks()[0].clips;
    QString src;
    if (!clips.empty())
        if (auto m = m_project->media()->find(clips.front().mediaId))
            src = m->path;
    const QString ext = ExportPlanner::suggestedExtension(settings().mode, src);
    QFileInfo fi(settings().outputPath);
    m_path->setText(QDir::toNativeSeparators(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
                                             QLatin1Char('.') + ext));
}

void ExportDialog::browse() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export video"), m_path->text(),
        tr("Video files (*.mp4 *.mov *.mkv *.webm *.avi *.mxf *.ts);;All files (*)"));
    if (path.isEmpty())
        return;
    m_path->setText(QDir::toNativeSeparators(path));
    replan();
}

void ExportDialog::replan() {
    m_plan = ExportPlanner::plan(m_project->timeline()->state(), *m_project->media(), settings());
    QString html;
    auto line = [&](const QString& color, const QString& text) {
        html += QStringLiteral("<p style='margin:2px 0'><span style='color:%1'>●</span> %2</p>").arg(color, text);
    };
    if (!m_plan.segments.empty()) {
        if (m_plan.video == VideoStrategy::Copy)
            line(QStringLiteral("#3fb950"), tr("Video: copied unchanged (no quality loss)"));
        else if (m_plan.mode == ExportMode::LosslessIntermediate)
            line(QStringLiteral("#3fb950"), tr("Video: encoded losslessly with %1").arg(m_plan.videoEncoder));
        else if (!m_plan.videoEncoder.isEmpty())
            line(QStringLiteral("#d29922"), tr("Video: re-encoded with %1 using the source settings").arg(m_plan.videoEncoder));
        switch (m_plan.audio) {
        case AudioStrategy::Copy: line(QStringLiteral("#3fb950"), tr("Audio: copied unchanged")); break;
        case AudioStrategy::Encode:
            line(m_plan.audioEncoder == QLatin1String("flac") ? QStringLiteral("#3fb950") : QStringLiteral("#d29922"),
                 tr("Audio: encoded with %1").arg(m_plan.audioEncoder));
            break;
        case AudioStrategy::None: line(QStringLiteral("#8b8b95"), tr("Audio: none")); break;
        }
        line(QStringLiteral("#8b8b95"), tr("Container: %1 · %n clip(s) · about %2", nullptr,
                                           static_cast<int>(m_plan.segments.size()))
                                            .arg(m_plan.containerName, formatSeconds(m_plan.expectedDuration)));
    }
    for (const auto& n : m_plan.notes)
        line(QStringLiteral("#d29922"), n.toHtmlEscaped());
    for (const auto& b : m_plan.blockers)
        line(QStringLiteral("#f85149"), b.toHtmlEscaped());
    m_planLabel->setText(html);

    bool shifted = false;
    for (const auto& s : m_plan.segments)
        shifted |= s.keyframeShiftFrames > 0;
    m_snapBtn->setVisible(m_plan.mode == ExportMode::StreamCopy && shifted);
    m_exportBtn->setEnabled(m_plan.ok());
}

void ExportDialog::snapCuts() {
    Timeline* tl = m_project->timeline();
    runTimelineEdit(m_undo, tl, tr("Snap cuts to key frames"), [&] {
        bool any = false;
        for (const auto& seg : m_plan.segments)
            if (seg.keyframeShiftFrames > 0)
                if (const Clip* c = tl->clip(seg.clipId); c && c->sourceIn == seg.requestedIn)
                    any |= tl->trimIn(seg.clipId, seg.actualIn);
        return any;
    });
    replan();
}

void ExportDialog::startExport() {
    const ExportSettings s = settings();
    if (QFileInfo::exists(s.outputPath)) {
        for (const auto& seg : m_plan.segments)
            if (QFileInfo(seg.path) == QFileInfo(s.outputPath)) {
                QMessageBox::warning(this, tr("Export video"), tr("The output file cannot be one of the source files."));
                return;
            }
        if (QMessageBox::question(this, tr("Export video"), tr("%1 already exists. Replace it?").arg(s.outputPath)) !=
            QMessageBox::Yes)
            return;
    }
    if (runExport(this, m_plan, s))
        accept();
}

bool ExportDialog::runExport(QWidget* parent, const ExportPlan& plan, const ExportSettings& settings) {
    QProgressDialog progress(tr("Exporting…"), tr("Cancel"), 0, 1000, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    std::atomic_bool cancel{false};
    std::atomic<int> value{0};

    QFutureWatcher<ExportResult> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<ExportResult>::finished, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, [&] { cancel = true; });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] { progress.setValue(value.load()); });
    timer.start(100);
    QElapsedTimer elapsed;
    elapsed.start();
    watcher.setFuture(QtConcurrent::run([&]() {
        TimelineExporter exporter;
        return exporter.run(plan, settings, [&](double f) { value = static_cast<int>(f * 1000); }, &cancel);
    }));
    loop.exec();
    timer.stop();
    progress.close();
    const ExportResult r = watcher.result();
    if (r.cancelled)
        return false;
    if (!r.ok) {
        QMessageBox::critical(parent, tr("Export failed"), r.error);
        return false;
    }
    QMessageBox box(QMessageBox::Information, tr("Export finished"),
                    tr("Saved %1 (%2) in %3 s.")
                        .arg(QFileInfo(settings.outputPath).fileName(), QLocale().formattedDataSize(r.fileSize))
                        .arg(elapsed.elapsed() / 1000.0, 0, 'f', 1),
                    QMessageBox::Ok, parent);
    auto* open = box.addButton(tr("Show in folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(settings.outputPath).absolutePath()));
    return true;
}

} // namespace lumora

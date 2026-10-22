#include "GifDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

#include "Tasks.h"
#include "export/GifExport.h"
#include "project/Project.h"
#include "render/FrameRenderer.h"

namespace lumora {

GifDialog::GifDialog(Project* project, Ticks selectionFrom, Ticks selectionTo, QWidget* parent)
    : QDialog(parent), m_project(project), m_selFrom(selectionFrom), m_selTo(selectionTo) {
    setWindowTitle(tr("Save as GIF"));
    setMinimumWidth(420);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("A short moving picture that plays everywhere: in messages, on websites and in documents. "
                                "GIF has only 256 colours, so keep it short and small."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    m_part = new QComboBox;
    m_part->addItem(tr("Whole video"), 0);
    if (m_selTo > m_selFrom)
        m_part->addItem(tr("Selected clips"), 1);
    m_part->addItem(tr("From – to"), 2);
    form->addRow(tr("Part"), m_part);
    const double total = ticksToSeconds(m_project->timeline()->duration());
    m_from = new QDoubleSpinBox;
    m_to = new QDoubleSpinBox;
    for (QDoubleSpinBox* sb : {m_from, m_to}) {
        sb->setRange(0, std::max(0.1, total));
        sb->setDecimals(2);
        sb->setSingleStep(0.5);
        sb->setSuffix(tr(" s"));
    }
    auto* range = new QHBoxLayout;
    range->addWidget(m_from, 1);
    range->addWidget(new QLabel(QStringLiteral("–")));
    range->addWidget(m_to, 1);
    form->addRow(tr("Time"), range);

    m_width = new QComboBox;
    for (int w : {320, 480, 640, 800, 1080})
        m_width->addItem(tr("%1 pixels wide").arg(w), w);
    m_width->setCurrentIndex(1);
    form->addRow(tr("Size"), m_width);
    m_fps = new QComboBox;
    for (double f : {10.0, 12.5, 15.0, 20.0, 25.0})
        m_fps->addItem(tr("%1 pictures per second").arg(QLocale().toString(f)), f);
    m_fps->setCurrentIndex(2);
    form->addRow(tr("Smoothness"), m_fps);
    m_colors = new QSpinBox;
    m_colors->setRange(16, 256);
    m_colors->setValue(256);
    m_colors->setToolTip(tr("Fewer colours make a smaller file"));
    form->addRow(tr("Colours"), m_colors);
    m_dither = new QCheckBox(tr("Smooth colour steps (dithering)"));
    m_dither->setChecked(true);
    m_dither->setToolTip(tr("Mixes dots of nearby colours so skies and faces show no bands; the file gets a little bigger"));
    form->addRow(QString(), m_dither);
    m_loop = new QComboBox;
    m_loop->addItem(tr("Forever"), 0);
    m_loop->addItem(tr("Once"), 1);
    m_loop->addItem(tr("3 times"), 3);
    form->addRow(tr("Repeat"), m_loop);
    layout->addLayout(form);
    m_estimate = new QLabel;
    m_estimate->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    m_estimate->setWordWrap(true);
    layout->addWidget(m_estimate);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* saveBtn = buttons->addButton(tr("Save GIF…"), QDialogButtonBox::AcceptRole);
    saveBtn->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &GifDialog::save);
    layout->addWidget(buttons);

    QSettings s;
    m_width->setCurrentIndex(std::max(0, m_width->findData(s.value(QStringLiteral("gif/width"), 480).toInt())));
    m_fps->setCurrentIndex(std::max(0, m_fps->findData(s.value(QStringLiteral("gif/fps"), 15.0).toDouble())));
    m_colors->setValue(s.value(QStringLiteral("gif/colors"), 256).toInt());
    m_dither->setChecked(s.value(QStringLiteral("gif/dither"), true).toBool());
    if (m_selTo > m_selFrom)
        m_part->setCurrentIndex(1);

    connect(m_part, &QComboBox::currentIndexChanged, this, &GifDialog::updateRange);
    for (QDoubleSpinBox* sb : {m_from, m_to})
        connect(sb, &QDoubleSpinBox::valueChanged, this, &GifDialog::updateEstimate);
    for (QComboBox* c : {m_width, m_fps})
        connect(c, &QComboBox::currentIndexChanged, this, &GifDialog::updateEstimate);
    connect(m_colors, &QSpinBox::valueChanged, this, &GifDialog::updateEstimate);
    updateRange();
}

void GifDialog::updateRange() {
    const int part = m_part->currentData().toInt();
    const double total = ticksToSeconds(m_project->timeline()->duration());
    if (part == 0) {
        m_from->setValue(0);
        m_to->setValue(total);
    } else if (part == 1) {
        m_from->setValue(ticksToSeconds(m_selFrom));
        m_to->setValue(ticksToSeconds(m_selTo));
    }
    m_from->setEnabled(part == 2);
    m_to->setEnabled(part == 2);
    updateEstimate();
}

void GifDialog::updateEstimate() {
    const double seconds = std::max(0.0, m_to->value() - m_from->value());
    const QSize canvas = FrameRenderer::defaultCanvasSize(m_project->timeline()->state(), *m_project->media());
    const int w = m_width->currentData().toInt();
    const int h = canvas.width() > 0 ? w * canvas.height() / canvas.width() : w * 9 / 16;
    // Rough: about 0.35 bytes per pixel and frame for video, less with fewer colours.
    const double bytes = seconds * m_fps->currentData().toDouble() * w * h * 0.35 * (0.5 + 0.5 * m_colors->value() / 256.0);
    QString text = tr("%1 × %2, %3 s, about %4").arg(w).arg(h).arg(QLocale().toString(seconds, 'f', 1), QLocale().formattedDataSize(qint64(bytes)));
    if (seconds > 15)
        text += QLatin1Char('\n') + tr("Long GIFs get very big; a short part or an MP4 video is often better.");
    m_estimate->setText(text);
}

void GifDialog::save() {
    GifSettings g;
    g.from = ticksFromSeconds(m_from->value());
    g.to = ticksFromSeconds(m_to->value());
    if (g.to <= g.from) {
        QMessageBox::information(this, windowTitle(), tr("Choose a part that is longer than zero."));
        return;
    }
    g.width = m_width->currentData().toInt();
    g.fps = m_fps->currentData().toDouble();
    g.colors = m_colors->value();
    g.dither = m_dither->isChecked();
    g.loops = m_loop->currentData().toInt();
    QSettings s;
    s.setValue(QStringLiteral("gif/width"), g.width);
    s.setValue(QStringLiteral("gif/fps"), g.fps);
    s.setValue(QStringLiteral("gif/colors"), g.colors);
    s.setValue(QStringLiteral("gif/dither"), g.dither);

    QString name = m_project->filePath().isEmpty() ? tr("animation") : QFileInfo(m_project->filePath()).completeBaseName();
    const QString path = QFileDialog::getSaveFileName(this, windowTitle(),
                                                      QDir(s.value(QStringLiteral("dirs/export")).toString()).filePath(name + QStringLiteral(".gif")),
                                                      tr("GIF animation (*.gif)"));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/export"), QFileInfo(path).absolutePath());
    const TimelineState state = m_project->timeline()->state();
    const MediaPool* pool = m_project->media();
    auto result = withProgress<QString>(this, tr("Making the GIF…"), [state, pool, g, path](const TaskProgress& progress) {
        std::atomic_bool cancel = false;
        QString error;
        const bool ok = gif::exportTimeline(state, *pool, g, path, [&](double f) { cancel = !progress(f); }, &cancel, &error);
        return ok ? QString() : (error.isEmpty() ? QStringLiteral("?") : error);
    });
    if (!result) {
        QFile::remove(path);
        return;
    }
    if (!result->isEmpty()) {
        QFile::remove(path);
        QMessageBox::warning(this, windowTitle(), tr("The GIF could not be saved: %1").arg(*result));
        return;
    }
    m_saved = path;
    accept();
}

} // namespace lumora

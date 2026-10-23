#include "PhotoExportDialog.h"

#include <QApplication>
#include <QColorSpace>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "Tasks.h"
#include "Theme.h"
#include "ai/Vision.h"
#include "image/PhotoProcessor.h"

namespace lumora {

PhotoExportDialog::PhotoExportDialog(const QString& sourcePath, const QImage& original, const QImage& working,
                                     const PhotoAdjustments& adj, QWidget* parent)
    : QDialog(parent), m_source(sourcePath), m_original(original), m_working(working), m_adj(adj) {
    setWindowTitle(tr("Export photo"));
    setMinimumWidth(520);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_format = new QComboBox;
    m_format->addItem(tr("PNG — lossless"), static_cast<int>(PhotoFormat::Png));
    if (PhotoExporter::formatAvailable(PhotoFormat::Tiff))
        m_format->addItem(tr("TIFF — lossless"), static_cast<int>(PhotoFormat::Tiff));
    m_format->addItem(tr("JPEG — smaller, lossy"), static_cast<int>(PhotoFormat::Jpeg));
    if (PhotoExporter::formatAvailable(PhotoFormat::WebP))
        m_format->addItem(tr("WebP — smaller, lossy"), static_cast<int>(PhotoFormat::WebP));
    form->addRow(tr("Format:"), m_format);
    m_depth = new QComboBox;
    m_depth->addItem(tr("Automatic (keep quality)"), 0);
    m_depth->addItem(tr("8-bit"), 8);
    m_depth->addItem(tr("16-bit"), 16);
    form->addRow(tr("Bit depth:"), m_depth);
    m_size = new QComboBox;
    const QSize out = adj.cropRect(working.size()).size();
    m_size->addItem(tr("Original (%1 × %2 px)").arg(out.width()).arg(out.height()), 1);
    if (ai::visionAvailable()) {
        m_size->addItem(tr("2× larger with AI (%1 × %2 px)").arg(out.width() * 2).arg(out.height() * 2), 2);
        m_size->addItem(tr("4× larger with AI (%1 × %2 px)").arg(out.width() * 4).arg(out.height() * 4), 4);
    }
    m_size->setToolTip(tr("Enlarges small or old photos and draws in fine detail instead of stretching the pixels. "
                          "Runs on this computer and takes a while."));
    form->addRow(tr("Size:"), m_size);
    auto* q = new QHBoxLayout;
    m_quality = new QSlider(Qt::Horizontal);
    m_quality->setRange(50, 100);
    m_quality->setValue(95);
    m_qualityLabel = new QLabel;
    m_qualityLabel->setMinimumWidth(40);
    q->addWidget(m_quality);
    q->addWidget(m_qualityLabel);
    form->addRow(tr("Quality:"), q);
    auto* pathRow = new QHBoxLayout;
    m_path = new QLineEdit;
    auto* browse = new QPushButton(tr("Browse…"));
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);
    form->addRow(tr("File:"), pathRow);
    layout->addLayout(form);
    m_note = new QLabel;
    m_note->setWordWrap(true);
    layout->addWidget(m_note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* saveBtn = buttons->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    saveBtn->setDefault(true);
    markPrimary(saveBtn);
    layout->addWidget(buttons);

    const QFileInfo fi(sourcePath);
    m_path->setText(QDir::toNativeSeparators(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
                                             QStringLiteral("_edit.png")));

    connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
        const auto f = static_cast<PhotoFormat>(m_format->currentData().toInt());
        QFileInfo fi(QDir::fromNativeSeparators(m_path->text()));
        m_path->setText(QDir::toNativeSeparators(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
                                                 QLatin1Char('.') + PhotoExporter::extension(f)));
        updateUi();
    });
    connect(m_depth, &QComboBox::currentIndexChanged, this, &PhotoExportDialog::updateUi);
    connect(m_size, &QComboBox::currentIndexChanged, this, &PhotoExportDialog::updateUi);
    connect(m_quality, &QSlider::valueChanged, this, &PhotoExportDialog::updateUi);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString p = QFileDialog::getSaveFileName(this, tr("Export photo"), m_path->text());
        if (!p.isEmpty())
            m_path->setText(QDir::toNativeSeparators(p));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &PhotoExportDialog::save);
    updateUi();
}

void PhotoExportDialog::updateUi() {
    const auto f = static_cast<PhotoFormat>(m_format->currentData().toInt());
    const bool lossy = !PhotoExporter::isLossless({f});
    m_quality->setEnabled(lossy);
    m_depth->setEnabled(!lossy);
    m_qualityLabel->setText(lossy ? QStringLiteral("%1%").arg(m_quality->value()) : QStringLiteral("—"));
    const int srcDepth = PhotoExporter::sourceBitDepth(m_original);
    const int enlarge = m_size->currentData().toInt();
    int depth = m_depth->currentData().toInt();
    if (depth == 0)
        depth = (m_adj.hasToneChanges() || srcDepth > 8 || enlarge > 1) ? 16 : 8;
    QString note;
    if (enlarge > 1)
        note = tr("Enlarged %1 times by the AI model: new detail is drawn in, so faces and fine textures can look a "
                  "little different from the original.").arg(enlarge) + QLatin1Char(' ');
    if (lossy)
        note += tr("JPEG and WebP discard detail to make files smaller. Choose PNG or TIFF to keep full quality.");
    else if (!m_adj.hasToneChanges() && enlarge == 1 && depth == srcDepth)
        note += tr("Lossless: the pixels are written exactly as in the original (only the crop is applied).");
    else if (depth == 16)
        note += tr("Lossless 16-bit: the adjustments are rendered in 32-bit float and stored with 16 bits per "
                  "channel, so no banding is introduced.");
    else
        note += tr("8-bit output after adjustments: smaller, but subtle gradients may show slight banding.");
    if (m_original.colorSpace().isValid())
        note += QLatin1Char(' ') + tr("The colour profile (%1) is kept.").arg(m_original.colorSpace().description());
    m_note->setText(note);
}

void PhotoExportDialog::save() {
    PhotoExportSettings s;
    s.format = static_cast<PhotoFormat>(m_format->currentData().toInt());
    s.bitDepth = m_depth->currentData().toInt();
    s.quality = m_quality->value();
    s.enlarge = m_size->currentData().toInt();
    const QString path = QDir::fromNativeSeparators(m_path->text().trimmed());
    if (QFileInfo(path) == QFileInfo(m_source)) {
        QMessageBox::warning(this, windowTitle(), tr("Choose a different file: the original is never overwritten."));
        return;
    }
    if (QFileInfo::exists(path) &&
        QMessageBox::question(this, windowTitle(), tr("%1 already exists. Replace it?").arg(path)) != QMessageBox::Yes)
        return;
    QStringList models;
    if (s.enlarge > 1)
        models << (s.enlarge == 4 ? QStringLiteral("upscale-x4") : QStringLiteral("upscale-x2"));
    if (!m_adj.retouch.erase.empty())
        models << QStringLiteral("inpaint");
    if (m_adj.retouch.faces > 0.0)
        models << QStringLiteral("face") << QStringLiteral("face-restore");
    if (!models.isEmpty() && !ensureVisionModels(this, models, windowTitle()))
        return;
    QString err;
    bool ok = false;
    if (s.enlarge > 1 || m_adj.retouch.isActive()) {
        // The models take a while at full size: in the background, with
        // progress and a way out.
        const QImage original = m_original, working = m_working;
        const PhotoAdjustments adj = m_adj;
        const auto result = withProgress<std::pair<bool, QString>>(this, tr("Saving the photo…"), [=](const TaskProgress& progress) {
            QString why;
            const bool done = PhotoExporter::save(path, original, working, adj, s, &why, progress);
            return std::make_pair(done, why);
        });
        if (!result)
            return; // cancelled
        ok = result->first;
        err = result->second;
    } else {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        ok = PhotoExporter::save(path, m_original, m_working, m_adj, s, &err);
        QApplication::restoreOverrideCursor();
    }
    if (!ok) {
        QMessageBox::critical(this, windowTitle(), tr("Could not save: %1").arg(err));
        return;
    }
    m_saved = path;
    accept();
}

} // namespace lumora

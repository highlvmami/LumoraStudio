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
    int depth = m_depth->currentData().toInt();
    if (depth == 0)
        depth = (m_adj.hasToneChanges() || srcDepth > 8) ? 16 : 8;
    QString note;
    if (lossy)
        note = tr("JPEG and WebP discard detail to make files smaller. Choose PNG or TIFF to keep full quality.");
    else if (!m_adj.hasToneChanges() && depth == srcDepth)
        note = tr("Lossless: the pixels are written exactly as in the original (only the crop is applied).");
    else if (depth == 16)
        note = tr("Lossless 16-bit: the adjustments are rendered in 32-bit float and stored with 16 bits per "
                  "channel, so no banding is introduced.");
    else
        note = tr("8-bit output after adjustments: smaller, but subtle gradients may show slight banding.");
    if (m_original.colorSpace().isValid())
        note += QLatin1Char(' ') + tr("The colour profile (%1) is kept.").arg(m_original.colorSpace().description());
    m_note->setText(note);
}

void PhotoExportDialog::save() {
    PhotoExportSettings s;
    s.format = static_cast<PhotoFormat>(m_format->currentData().toInt());
    s.bitDepth = m_depth->currentData().toInt();
    s.quality = m_quality->value();
    const QString path = QDir::fromNativeSeparators(m_path->text().trimmed());
    if (QFileInfo(path) == QFileInfo(m_source)) {
        QMessageBox::warning(this, windowTitle(), tr("Choose a different file: the original is never overwritten."));
        return;
    }
    if (QFileInfo::exists(path) &&
        QMessageBox::question(this, windowTitle(), tr("%1 already exists. Replace it?").arg(path)) != QMessageBox::Yes)
        return;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = PhotoExporter::save(path, m_original, m_working, m_adj, s, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, windowTitle(), tr("Could not save: %1").arg(err));
        return;
    }
    m_saved = path;
    accept();
}

} // namespace lumora

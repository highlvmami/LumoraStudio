#include "SmartToolsPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lumora {

namespace {

QLabel* note() {
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    return l;
}

QSpinBox* percent() {
    auto* sb = new QSpinBox;
    sb->setRange(0, 100);
    sb->setSuffix(QStringLiteral(" %"));
    return sb;
}

} // namespace

SmartToolsPanel::SmartToolsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 6, 0, 0);

    auto* redactBox = new QGroupBox(tr("Hide faces and plates"));
    auto* rf = new QFormLayout(redactBox);
    m_redactInfo = note();
    rf->addRow(m_redactInfo);
    m_find = new QPushButton(tr("Find faces and plates"));
    m_clear = new QPushButton(tr("Remove"));
    auto* row = new QHBoxLayout;
    row->addWidget(m_find, 1);
    row->addWidget(m_clear);
    rf->addRow(row);
    m_faces = new QCheckBox(tr("Faces"));
    m_plates = new QCheckBox(tr("Number plates"));
    auto* kinds = new QHBoxLayout;
    kinds->addWidget(m_faces);
    kinds->addWidget(m_plates);
    kinds->addStretch();
    rf->addRow(tr("Hide"), kinds);
    m_style = new QComboBox;
    m_style->addItem(tr("Blur"), static_cast<int>(RedactionStyle::Blur));
    m_style->addItem(tr("Pixelate"), static_cast<int>(RedactionStyle::Pixelate));
    m_style->addItem(tr("Black box"), static_cast<int>(RedactionStyle::Fill));
    rf->addRow(tr("Look"), m_style);
    m_strength = percent();
    rf->addRow(tr("Strength"), m_strength);
    m_list = new QListWidget;
    m_list->setToolTip(tr("Untick what should stay visible"));
    rf->addRow(m_list);
    layout->addWidget(redactBox);

    auto* bgBox = new QGroupBox(tr("Background"));
    auto* bf = new QFormLayout(bgBox);
    m_bgOn = new QCheckBox(tr("Remove the background"));
    m_bgOn->setToolTip(tr("Keeps the person or object in front and makes the rest transparent"));
    bf->addRow(m_bgOn);
    m_bgModel = new QComboBox;
    m_bgModel->addItem(tr("Fast"), QStringLiteral("fast"));
    m_bgModel->addItem(tr("Detailed (slow)"), QStringLiteral("quality"));
    bf->addRow(tr("Model"), m_bgModel);
    m_bgSoftness = percent();
    bf->addRow(tr("Edge softness"), m_bgSoftness);
    m_bgInfo = note();
    m_bgInfo->setText(tr("Export as PNG, TIFF or WebP to keep the transparency; JPEG puts the subject on white."));
    bf->addRow(m_bgInfo);
    layout->addWidget(bgBox);
    layout->addStretch();

    connect(m_find, &QPushButton::clicked, this, &SmartToolsPanel::findRequested);
    connect(m_clear, &QPushButton::clicked, this, [this] {
        Redaction r;
        r.style = m_redaction.style;
        r.strength = m_redaction.strength;
        emit redactionEdited(r, tr("Remove hidden regions"), {});
    });
    for (QCheckBox* cb : {m_faces, m_plates})
        connect(cb, &QCheckBox::toggled, this, [this] {
            if (m_syncing)
                return;
            Redaction r = m_redaction;
            r.faces = m_faces->isChecked();
            r.plates = m_plates->isChecked();
            emit redactionEdited(r, tr("Hide faces and plates"), {});
        });
    connect(m_style, &QComboBox::currentIndexChanged, this, [this] {
        if (m_syncing)
            return;
        Redaction r = m_redaction;
        r.style = static_cast<RedactionStyle>(m_style->currentData().toInt());
        emit redactionEdited(r, tr("Change how regions are hidden"), {});
    });
    connect(m_strength, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_syncing)
            return;
        Redaction r = m_redaction;
        r.strength = v / 100.0;
        emit redactionEdited(r, tr("Change hiding strength"), QStringLiteral("redact-strength"));
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (m_syncing)
            return;
        const int i = item->data(Qt::UserRole).toInt();
        Redaction r = m_redaction;
        if (i < 0 || i >= static_cast<int>(r.tracks.size()))
            return;
        r.tracks[i].enabled = item->checkState() == Qt::Checked;
        emit redactionEdited(r, r.tracks[i].enabled ? tr("Hide region") : tr("Show region"), {});
    });
    connect(m_bgOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_syncing)
            return;
        BackgroundRemoval b = m_background;
        b.enabled = on;
        b.model = m_bgModel->currentData().toString();
        emit backgroundEdited(b, on ? tr("Remove the background") : tr("Keep the background"), {});
    });
    connect(m_bgModel, &QComboBox::currentIndexChanged, this, [this] {
        if (m_syncing)
            return;
        BackgroundRemoval b = m_background;
        b.model = m_bgModel->currentData().toString();
        emit backgroundEdited(b, tr("Change background model"), {});
    });
    connect(m_bgSoftness, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_syncing)
            return;
        BackgroundRemoval b = m_background;
        b.softness = v / 100.0;
        emit backgroundEdited(b, tr("Change edge softness"), QStringLiteral("bg-softness"));
    });
    setState({}, {}, false);
}

void SmartToolsPanel::setState(const Redaction& r, const BackgroundRemoval& b, bool editable) {
    m_syncing = true;
    m_redaction = r;
    m_background = b;
    const bool found = !r.tracks.empty();
    const int faces = r.count(RedactionKind::Face), plates = r.count(RedactionKind::Plate);
    m_redactInfo->setText(!r.enabled ? tr("Blurs every face and number plate in the photo. Runs on this computer.")
                          : found    ? tr("Found %1 face(s) and %2 number plate(s).").arg(faces).arg(plates)
                                     : tr("No faces or number plates were found in this photo."));
    m_find->setEnabled(editable);
    m_find->setText(r.enabled ? tr("Search again") : tr("Find faces and plates"));
    m_clear->setEnabled(editable && r.enabled);
    for (QWidget* w : std::initializer_list<QWidget*>{m_faces, m_plates, m_style, m_strength, m_list})
        w->setEnabled(editable && found);
    m_faces->setChecked(r.faces);
    m_plates->setChecked(r.plates);
    m_style->setCurrentIndex(std::max(0, m_style->findData(static_cast<int>(r.style))));
    m_strength->setValue(qRound(r.strength * 100));
    m_list->setVisible(found);
    m_list->clear();
    int faceNo = 0, plateNo = 0;
    for (int i = 0; i < static_cast<int>(r.tracks.size()); ++i) {
        const RedactionTrack& t = r.tracks[i];
        auto* item = new QListWidgetItem(t.kind == RedactionKind::Plate ? tr("Plate %1").arg(++plateNo) : tr("Face %1").arg(++faceNo), m_list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(t.enabled ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, i);
    }
    m_list->setFixedHeight(std::min(150, m_list->count() * m_list->fontMetrics().height() * 3 / 2 + 8));
    m_bgOn->setEnabled(editable);
    m_bgOn->setChecked(b.enabled);
    m_bgModel->setCurrentIndex(std::max(0, m_bgModel->findData(b.model)));
    m_bgModel->setEnabled(editable && b.enabled);
    m_bgSoftness->setValue(qRound(b.softness * 100));
    m_bgSoftness->setEnabled(editable && b.enabled);
    m_syncing = false;
}

} // namespace lumora

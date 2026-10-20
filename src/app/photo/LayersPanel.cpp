#include "LayersPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "color/ColorPanel.h"
#include "color/SliderRow.h"
#include "media/MediaInfo.h"

namespace lumora {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;

QString blendModeName(BlendMode m) {
    switch (m) {
    case BlendMode::Normal: return LayersPanel::tr("Normal");
    case BlendMode::Multiply: return LayersPanel::tr("Multiply");
    case BlendMode::Screen: return LayersPanel::tr("Screen");
    case BlendMode::Overlay: return LayersPanel::tr("Overlay");
    case BlendMode::SoftLight: return LayersPanel::tr("Soft light");
    case BlendMode::Darken: return LayersPanel::tr("Darken");
    case BlendMode::Lighten: return LayersPanel::tr("Lighten");
    case BlendMode::Add: return LayersPanel::tr("Add");
    case BlendMode::Difference: return LayersPanel::tr("Difference");
    }
    return {};
}

QToolButton* toolButton(const QIcon& icon, const QString& text, const QString& tip) {
    auto* b = new QToolButton;
    b->setIcon(icon);
    b->setText(text);
    b->setToolTip(tip);
    b->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    b->setAutoRaise(true);
    return b;
}
} // namespace

LayersPanel::LayersPanel(QWidget* parent) : QWidget(parent) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(8, 8, 8, 8);

    auto* bar = new QHBoxLayout;
    bar->setSpacing(2);
    m_addAdjustment = toolButton(Icons::add(), tr("Adjustment"),
                                 tr("Add an adjustment layer: colour settings applied through a mask"));
    m_addPicture = toolButton(Icons::import(), tr("Picture…"), tr("Add another picture as a layer on top"));
    m_up = toolButton({}, {}, tr("Move the layer up"));
    m_up->setArrowType(Qt::UpArrow);
    m_down = toolButton({}, {}, tr("Move the layer down"));
    m_down->setArrowType(Qt::DownArrow);
    m_remove = toolButton(Icons::remove(), {}, tr("Delete the layer"));
    bar->addWidget(m_addAdjustment);
    bar->addWidget(m_addPicture);
    bar->addStretch();
    bar->addWidget(m_up);
    bar->addWidget(m_down);
    bar->addWidget(m_remove);
    l->addLayout(bar);

    m_list = new QListWidget;
    m_list->setMinimumHeight(90);
    m_list->setMaximumHeight(120);
    m_list->setToolTip(tr("Top layer first. The tick shows or hides a layer; the photo's own settings are below "
                          "all layers."));
    l->addWidget(m_list);

    m_editors = new QWidget;
    auto* el = new QVBoxLayout(m_editors);
    el->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout;
    m_name = new QLineEdit;
    m_blend = new QComboBox;
    for (BlendMode m : {BlendMode::Normal, BlendMode::Multiply, BlendMode::Screen, BlendMode::Overlay, BlendMode::SoftLight,
                        BlendMode::Darken, BlendMode::Lighten, BlendMode::Add, BlendMode::Difference})
        m_blend->addItem(blendModeName(m), static_cast<int>(m));
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Blend"), m_blend);
    el->addLayout(form);
    m_opacity = new SliderRow(tr("Opacity (%)"), 0, 100, 1, 0);
    m_opacity->setDefault(100);
    el->addWidget(m_opacity);

    m_pictureBox = new QGroupBox(tr("Placement"));
    auto* pl = new QVBoxLayout(m_pictureBox);
    m_picX = new SliderRow(tr("Horizontal (%)"), -100, 100, 1, 0);
    m_picY = new SliderRow(tr("Vertical (%)"), -100, 100, 1, 0);
    m_picScale = new SliderRow(tr("Size (%)"), 1, 400, 1, 0);
    m_picScale->setDefault(100);
    pl->addWidget(m_picX);
    pl->addWidget(m_picY);
    pl->addWidget(m_picScale);
    el->addWidget(m_pictureBox);

    auto* maskBox = new QGroupBox(tr("Mask"));
    auto* ml = new QVBoxLayout(maskBox);
    m_maskShape = new QComboBox;
    m_maskShape->addItem(tr("Whole picture"), static_cast<int>(MaskShape::None));
    m_maskShape->addItem(tr("Rectangle"), static_cast<int>(MaskShape::Rectangle));
    m_maskShape->addItem(tr("Ellipse"), static_cast<int>(MaskShape::Ellipse));
    m_maskShape->addItem(tr("Gradient"), static_cast<int>(MaskShape::Linear));
    m_maskShape->addItem(tr("Brush"), static_cast<int>(MaskShape::Brush));
    ml->addWidget(m_maskShape);
    m_maskEditors = new QWidget;
    auto* mel = new QVBoxLayout(m_maskEditors);
    mel->setContentsMargins(0, 0, 0, 0);
    m_maskHint = new QLabel(tr("Drag the outline on the photo to move the mask."));
    m_maskHint->setWordWrap(true);
    m_maskHint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    m_maskX = new SliderRow(tr("Centre X (%)"), 0, 100, 1, 0);
    m_maskY = new SliderRow(tr("Centre Y (%)"), 0, 100, 1, 0);
    m_maskW = new SliderRow(tr("Width (%)"), 1, 200, 1, 0);
    m_maskH = new SliderRow(tr("Height (%)"), 1, 200, 1, 0);
    m_maskAngle = new SliderRow(tr("Angle (°)"), -180, 180, 1, 0);
    m_maskFeather = new SliderRow(tr("Softness (%)"), 0, 100, 1, 0);
    m_maskFeather->setDefault(15);
    m_maskInvert = new QCheckBox(tr("Invert"));
    for (QWidget* w : std::initializer_list<QWidget*>{m_maskHint, m_maskX, m_maskY, m_maskW, m_maskH, m_maskAngle,
                                                      m_maskFeather})
        mel->addWidget(w);
    ml->addWidget(m_maskEditors);

    // Brush masks: the settings belong to the brush, not to the layer, and
    // are remembered between sessions.
    m_brushEditors = new QWidget;
    auto* bel = new QVBoxLayout(m_brushEditors);
    bel->setContentsMargins(0, 0, 0, 0);
    auto* brushHint = new QLabel(tr("Paint on the photo where the layer should show. Hold Alt to erase."));
    brushHint->setWordWrap(true);
    brushHint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    QSettings bs;
    m_brushSize = new SliderRow(tr("Brush size (%)"), 1, 50, 0.5, 1);
    m_brushSize->setDefault(8);
    m_brushSize->setValue(bs.value(QStringLiteral("photo/brushSize"), 8.0).toDouble());
    m_brushHardness = new SliderRow(tr("Hardness (%)"), 0, 100, 1, 0);
    m_brushHardness->setDefault(50);
    m_brushHardness->setValue(bs.value(QStringLiteral("photo/brushHardness"), 50.0).toDouble());
    m_brushFlow = new SliderRow(tr("Flow (%)"), 1, 100, 1, 0);
    m_brushFlow->setDefault(100);
    m_brushFlow->setValue(bs.value(QStringLiteral("photo/brushFlow"), 100.0).toDouble());
    m_brushErase = new QCheckBox(tr("Erase"));
    m_brushErase->setToolTip(tr("Take away from the mask instead of adding (or hold Alt while painting)"));
    m_brushOverlay = new QCheckBox(tr("Show the mask on the photo"));
    m_brushOverlay->setChecked(bs.value(QStringLiteral("photo/brushOverlay"), true).toBool());
    m_brushClear = new QPushButton(Icons::reset(), tr("Clear brush strokes"));
    for (QWidget* w : std::initializer_list<QWidget*>{brushHint, m_brushSize, m_brushHardness, m_brushFlow,
                                                      m_brushErase, m_brushOverlay, m_brushClear})
        bel->addWidget(w);
    ml->addWidget(m_brushEditors);
    ml->addWidget(m_maskInvert);
    el->addWidget(maskBox);

    m_colorBox = new QGroupBox(tr("Layer colour"));
    auto* cl = new QVBoxLayout(m_colorBox);
    cl->setContentsMargins(0, 0, 0, 0);
    m_color = new ColorPanel;
    cl->addWidget(m_color);
    el->addWidget(m_colorBox);
    l->addWidget(m_editors);
    l->addStretch();

    connect(m_addAdjustment, &QToolButton::clicked, this, &LayersPanel::addAdjustment);
    connect(m_addPicture, &QToolButton::clicked, this, &LayersPanel::addPicture);
    connect(m_remove, &QToolButton::clicked, this, &LayersPanel::removeCurrent);
    connect(m_up, &QToolButton::clicked, this, [this] { moveCurrent(+1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveCurrent(-1); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (m_syncing)
            return;
        m_current = it ? it->data(kIdRole).toUuid() : QUuid();
        syncEditors();
        emit currentLayerChanged();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (m_syncing)
            return;
        const QUuid id = it->data(kIdRole).toUuid();
        const bool visible = it->checkState() == Qt::Checked;
        auto layers = m_layers;
        for (auto& l : layers)
            if (l.id == id)
                l.visible = visible;
        commit(layers, visible ? tr("Show layer") : tr("Hide layer"));
    });
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        editCurrent(tr("Rename layer"), {}, [&](PhotoLayer& l) { l.name = m_name->text().trimmed(); });
    });
    connect(m_blend, &QComboBox::activated, this, [this] {
        editCurrent(tr("Blend mode"), {},
                    [&](PhotoLayer& l) { l.blend = static_cast<BlendMode>(m_blend->currentData().toInt()); });
    });
    auto bind = [this](SliderRow* s, const QString& text, const QString& field,
                       const std::function<void(PhotoLayer&, double)>& set) {
        connect(s, &SliderRow::valueChanged, this, [this, text, field, set](double v) {
            editCurrent(text, field, [&](PhotoLayer& l) { set(l, v); });
        });
    };
    bind(m_opacity, tr("Layer opacity"), QStringLiteral("opacity"), [](PhotoLayer& l, double v) { l.opacity = v / 100; });
    bind(m_picX, tr("Move picture"), QStringLiteral("x"), [](PhotoLayer& l, double v) { l.x = v / 100; });
    bind(m_picY, tr("Move picture"), QStringLiteral("y"), [](PhotoLayer& l, double v) { l.y = v / 100; });
    bind(m_picScale, tr("Picture size"), QStringLiteral("scale"), [](PhotoLayer& l, double v) { l.scale = v / 100; });
    bind(m_maskX, tr("Move mask"), QStringLiteral("maskX"), [](PhotoLayer& l, double v) {
        l.mask.rect.moveCenter(QPointF(v / 100, l.mask.rect.center().y()));
    });
    bind(m_maskY, tr("Move mask"), QStringLiteral("maskY"), [](PhotoLayer& l, double v) {
        l.mask.rect.moveCenter(QPointF(l.mask.rect.center().x(), v / 100));
    });
    bind(m_maskW, tr("Mask size"), QStringLiteral("maskW"), [](PhotoLayer& l, double v) {
        const QPointF c = l.mask.rect.center();
        l.mask.rect.setWidth(v / 100);
        l.mask.rect.moveCenter(c);
    });
    bind(m_maskH, tr("Mask size"), QStringLiteral("maskH"), [](PhotoLayer& l, double v) {
        const QPointF c = l.mask.rect.center();
        l.mask.rect.setHeight(v / 100);
        l.mask.rect.moveCenter(c);
    });
    bind(m_maskAngle, tr("Mask angle"), QStringLiteral("maskAngle"), [](PhotoLayer& l, double v) { l.mask.angle = v; });
    bind(m_maskFeather, tr("Mask softness"), QStringLiteral("maskFeather"),
         [](PhotoLayer& l, double v) { l.mask.feather = v / 100; });
    connect(m_maskShape, &QComboBox::activated, this, [this] {
        editCurrent(tr("Mask shape"), {}, [&](PhotoLayer& l) {
            l.mask.shape = static_cast<MaskShape>(m_maskShape->currentData().toInt());
            if (l.mask.shape == MaskShape::Linear && l.mask.rect == LayerMask().rect)
                l.mask.rect = QRectF(0, 0, 1, 1); // a gradient across the middle of the photo
        });
    });
    auto brushSetting = [this](SliderRow* row, const char* key) {
        connect(row, &SliderRow::valueChanged, this, [this, key](double v) {
            QSettings().setValue(QLatin1String(key), v);
            emit brushChanged();
        });
    };
    brushSetting(m_brushSize, "photo/brushSize");
    brushSetting(m_brushHardness, "photo/brushHardness");
    brushSetting(m_brushFlow, "photo/brushFlow");
    connect(m_brushErase, &QCheckBox::toggled, this, &LayersPanel::brushChanged);
    connect(m_brushOverlay, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("photo/brushOverlay"), on);
        emit brushChanged();
    });
    connect(m_brushClear, &QPushButton::clicked, this, [this] {
        editCurrent(tr("Clear brush strokes"), {}, [](PhotoLayer& l) { l.mask.strokes.clear(); });
    });
    connect(m_maskInvert, &QCheckBox::toggled, this, [this](bool on) {
        editCurrent(tr("Invert mask"), {}, [&](PhotoLayer& l) { l.mask.invert = on; });
    });
    connect(m_color, &ColorPanel::gradeEdited, this, [this](const ColorGrade& g, const QString& text, const QString& field) {
        editCurrent(text, field.isEmpty() ? QString() : QStringLiteral("color:") + field,
                    [&](PhotoLayer& l) { l.grade = g; });
    });
    setLayers({}, false);
}

const PhotoLayer* LayersPanel::current() const {
    for (const auto& l : m_layers)
        if (l.id == m_current)
            return &l;
    return nullptr;
}

int LayersPanel::currentIndex() const {
    for (int i = 0; i < static_cast<int>(m_layers.size()); ++i)
        if (m_layers[i].id == m_current)
            return i;
    return -1;
}

void LayersPanel::setLayers(const std::vector<PhotoLayer>& layers, bool enabled) {
    const bool selectionLost = !m_current.isNull() &&
                               std::none_of(layers.begin(), layers.end(), [&](const PhotoLayer& l) { return l.id == m_current; });
    m_layers = layers;
    m_enabled = enabled;
    if (selectionLost)
        m_current = {};
    if (m_current.isNull() && !m_layers.empty())
        m_current = m_layers.back().id;
    rebuildList();
    syncEditors();
    if (selectionLost)
        emit currentLayerChanged();
}

void LayersPanel::rebuildList() {
    m_syncing = true;
    m_list->clear();
    // Top layer first, like every layered editor.
    for (auto it = m_layers.rbegin(); it != m_layers.rend(); ++it) {
        const PhotoLayer& l = *it;
        QString name = l.name;
        if (name.isEmpty())
            name = l.kind == LayerKind::Image ? QFileInfo(l.imagePath).fileName() : tr("Adjustment");
        QString detail = l.kind == LayerKind::Image ? tr("picture") : tr("adjustment");
        if (l.mask.shape != MaskShape::None)
            detail += tr(", masked");
        if (l.opacity < 1.0)
            detail += QStringLiteral(", %1 %").arg(std::lround(l.opacity * 100));
        auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(name, detail));
        item->setData(kIdRole, l.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(l.visible ? Qt::Checked : Qt::Unchecked);
        if (l.kind == LayerKind::Image)
            item->setToolTip(QDir::toNativeSeparators(l.imagePath));
        m_list->addItem(item);
        if (l.id == m_current)
            m_list->setCurrentItem(item);
    }
    m_syncing = false;
}

void LayersPanel::syncEditors() {
    m_syncing = true;
    const int i = currentIndex();
    const PhotoLayer* l = i >= 0 ? &m_layers[i] : nullptr;
    m_addAdjustment->setEnabled(m_enabled);
    m_addPicture->setEnabled(m_enabled);
    m_list->setEnabled(m_enabled);
    m_remove->setEnabled(m_enabled && l);
    m_up->setEnabled(m_enabled && l && i + 1 < static_cast<int>(m_layers.size()));
    m_down->setEnabled(m_enabled && l && i > 0);
    m_editors->setVisible(m_enabled && l);
    if (l) {
        m_name->setText(l->name);
        m_name->setPlaceholderText(l->kind == LayerKind::Image ? QFileInfo(l->imagePath).fileName() : tr("Adjustment"));
        m_blend->setCurrentIndex(m_blend->findData(static_cast<int>(l->blend)));
        m_opacity->setValue(l->opacity * 100);
        m_pictureBox->setVisible(l->kind == LayerKind::Image);
        m_picX->setValue(l->x * 100);
        m_picY->setValue(l->y * 100);
        m_picScale->setValue(l->scale * 100);
        m_maskShape->setCurrentIndex(m_maskShape->findData(static_cast<int>(l->mask.shape)));
        const bool masked = l->mask.shape != MaskShape::None;
        const bool brush = l->mask.shape == MaskShape::Brush;
        m_maskEditors->setVisible(masked && !brush);
        m_brushEditors->setVisible(brush);
        m_brushClear->setEnabled(!l->mask.strokes.empty());
        m_maskInvert->setVisible(masked);
        const bool linear = l->mask.shape == MaskShape::Linear;
        m_maskW->setVisible(!linear);
        m_maskH->setVisible(!linear);
        m_maskAngle->setVisible(linear);
        m_maskX->setValue(l->mask.rect.center().x() * 100);
        m_maskY->setValue(l->mask.rect.center().y() * 100);
        m_maskW->setValue(l->mask.rect.width() * 100);
        m_maskH->setValue(l->mask.rect.height() * 100);
        m_maskAngle->setValue(l->mask.angle);
        m_maskFeather->setValue(l->mask.feather * 100);
        m_maskInvert->setChecked(l->mask.invert);
        m_colorBox->setVisible(l->kind == LayerKind::Adjustment);
        m_color->setGrade(l->grade);
    }
    m_syncing = false;
}

BrushStroke LayersPanel::brush() const {
    BrushStroke b;
    b.radius = m_brushSize->value() / 200.0; // the size is the diameter, in % of the photo height
    b.hardness = m_brushHardness->value() / 100.0;
    b.flow = m_brushFlow->value() / 100.0;
    b.erase = m_brushErase->isChecked();
    return b;
}

bool LayersPanel::showMaskOverlay() const {
    return m_brushOverlay->isChecked();
}

void LayersPanel::commit(std::vector<PhotoLayer> layers, const QString& text, const QString& field) {
    if (layers == m_layers)
        return;
    m_layers = std::move(layers);
    emit layersEdited(m_layers, text, field);
}

void LayersPanel::editCurrent(const QString& text, const QString& field, const std::function<void(PhotoLayer&)>& change) {
    if (m_syncing)
        return;
    const int i = currentIndex();
    if (i < 0)
        return;
    auto layers = m_layers;
    change(layers[i]);
    commit(layers, text, field.isEmpty() ? QString() : m_current.toString() + QLatin1Char(':') + field);
}

void LayersPanel::addAdjustment() {
    PhotoLayer l;
    l.id = QUuid::createUuid();
    l.kind = LayerKind::Adjustment;
    // A useful start: a soft ellipse, ready to brighten or recolour a subject.
    l.mask.shape = MaskShape::Ellipse;
    l.mask.rect = QRectF(0.25, 0.2, 0.5, 0.6);
    l.mask.feather = 0.35;
    auto layers = m_layers;
    layers.push_back(l);
    m_current = l.id;
    commit(layers, tr("Add adjustment layer"));
    emit currentLayerChanged();
}

void LayersPanel::addPicture() {
    QSettings s;
    QStringList filters;
    for (const auto& ext : imageFileExtensions())
        filters << QStringLiteral("*.") + ext;
    const QString path = QFileDialog::getOpenFileName(this, tr("Add picture layer"),
                                                      s.value(QStringLiteral("dirs/photos")).toString(),
                                                      tr("Pictures (%1)").arg(filters.join(QLatin1Char(' '))));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/photos"), QFileInfo(path).absolutePath());
    PhotoLayer l;
    l.id = QUuid::createUuid();
    l.kind = LayerKind::Image;
    l.imagePath = path;
    l.scale = 0.5;
    auto layers = m_layers;
    layers.push_back(l);
    m_current = l.id;
    commit(layers, tr("Add picture layer"));
    emit currentLayerChanged();
}

void LayersPanel::removeCurrent() {
    const int i = currentIndex();
    if (i < 0)
        return;
    auto layers = m_layers;
    layers.erase(layers.begin() + i);
    m_current = layers.empty() ? QUuid() : layers[std::max(0, i - 1)].id;
    commit(layers, tr("Delete layer"));
    emit currentLayerChanged();
}

void LayersPanel::moveCurrent(int delta) {
    const int i = currentIndex();
    const int j = i + delta;
    if (i < 0 || j < 0 || j >= static_cast<int>(m_layers.size()))
        return;
    auto layers = m_layers;
    std::swap(layers[i], layers[j]);
    commit(layers, delta > 0 ? tr("Move layer up") : tr("Move layer down"));
}

} // namespace lumora

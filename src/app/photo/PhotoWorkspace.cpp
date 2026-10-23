#include "PhotoWorkspace.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include "Icons.h"
#include "MediaCache.h"
#include "Tasks.h"
#include "Theme.h"
#include "ai/Redaction.h"
#include "ai/Vision.h"
#include "color/ColorPanel.h"
#include "color/ScopesWidget.h"
#include "dialogs/PhotoExportDialog.h"
#include "image/PhotoLibrary.h"
#include "image/PhotoProcessor.h"
#include "media/MediaInfo.h"
#include "photo/LayersPanel.h"
#include "photo/PhotoView.h"
#include "photo/SmartToolsPanel.h"

namespace lumora {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kSmartTab = 2;
}

// ----------------------------------------------------------- PhotoWorkspace

PhotoWorkspace::PhotoWorkspace(PhotoLibrary* library, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), m_lib(library), m_undo(undo) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* split = new QSplitter;
    layout->addWidget(split);

    // Left: photo list.
    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(6, 6, 6, 6);
    auto* openBtn = new QPushButton(Icons::import(), tr("Open photos…"));
    ll->addWidget(openBtn);
    m_list = new QListWidget;
    m_list->setIconSize(QSize(96, 64));
    m_list->setSpacing(2);
    ll->addWidget(m_list, 1);
    makePanelSurface(left);
    split->addWidget(left);

    // Center: view.
    m_view = new PhotoView;
    m_view->setPlaceholder(tr("Open a photo (JPEG, PNG, HEIC, TIFF, WebP) with \"Open photos…\" or drag it here.\n\n"
                              "The original file is never changed: edits are stored in the project and applied "
                              "when you export."));
    split->addWidget(m_view);

    // Right: scope, then the photo's colour settings and its layers in tabs,
    // then crop and export.
    auto* right = new QWidget;
    right->setMinimumWidth(350);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 8, 8, 8);

    m_scope = new ScopesWidget(nullptr, scopes::Kind::Histogram, QStringLiteral("photo/scopeKind"));
    m_scope->setMinimumHeight(150);
    m_scope->setMaximumHeight(190);
    rl->addWidget(m_scope);

    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    auto scrolled = [](QWidget* w) {
        auto* sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setWidget(w);
        return sa;
    };
    m_color = new ColorPanel;
    m_layers = new LayersPanel;
    m_tabs->addTab(scrolled(m_color), tr("Adjust"));
    m_tabs->addTab(scrolled(m_layers), tr("Layers"));
    m_smart = new SmartToolsPanel;
    m_tabs->addTab(scrolled(m_smart), tr("Smart"));
    rl->addWidget(m_tabs, 1);

    auto* cropBox = new QGroupBox(tr("Crop"));
    auto* cropL = new QGridLayout(cropBox);
    m_cropBtn = new QToolButton;
    m_cropBtn->setIcon(Icons::crop());
    m_cropBtn->setText(tr("Crop"));
    m_cropBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_cropBtn->setCheckable(true);
    m_cropBtn->setToolTip(tr("Show the whole photo and drag the frame (C)"));
    m_aspect = new QComboBox;
    m_aspect->addItem(tr("Free"), 0.0);
    m_aspect->addItem(tr("Original"), -1.0);
    m_aspect->addItem(QStringLiteral("1:1"), 1.0);
    m_aspect->addItem(QStringLiteral("4:3"), 4.0 / 3);
    m_aspect->addItem(QStringLiteral("3:2"), 3.0 / 2);
    m_aspect->addItem(QStringLiteral("16:9"), 16.0 / 9);
    m_aspect->addItem(QStringLiteral("4:5"), 4.0 / 5);
    m_aspect->addItem(QStringLiteral("9:16"), 9.0 / 16);
    auto* resetCrop = new QToolButton;
    resetCrop->setIcon(Icons::reset());
    resetCrop->setToolTip(tr("Remove the crop"));
    cropL->addWidget(m_cropBtn, 0, 0);
    cropL->addWidget(m_aspect, 0, 1);
    cropL->addWidget(resetCrop, 0, 2);
    rl->addWidget(cropBox);

    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    rl->addWidget(m_info);
    auto* bottom = new QHBoxLayout;
    m_compareBtn = new QToolButton;
    m_compareBtn->setIcon(Icons::compare());
    m_compareBtn->setText(tr("Before"));
    m_compareBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_compareBtn->setCheckable(true);
    m_compareBtn->setToolTip(tr("Compare with the original (\\)"));
    auto* resetBtn = new QToolButton;
    resetBtn->setIcon(Icons::reset());
    resetBtn->setText(tr("Reset"));
    resetBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_fullScreenBtn = new QToolButton;
    m_fullScreenBtn->setIcon(Icons::fullScreen());
    m_fullScreenBtn->setToolTip(tr("Full screen (F or double-click the photo)"));
    m_bgBtn = new QToolButton;
    m_bgBtn->setIcon(Icons::removeBackground());
    m_bgBtn->setText(tr("Remove background"));
    m_bgBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_bgBtn->setCheckable(true);
    m_bgBtn->setToolTip(tr("Cut the person or object out and make the rest transparent, in one click (B)"));
    bottom->addWidget(m_bgBtn);
    bottom->addWidget(m_compareBtn);
    bottom->addWidget(resetBtn);
    bottom->addStretch();
    bottom->addWidget(m_fullScreenBtn);
    rl->addLayout(bottom);
    m_exportBtn = new QPushButton(Icons::exportFile(), tr("Export photo…"));
    m_exportBtn->setDefault(true);
    markPrimary(m_exportBtn);
    rl->addWidget(m_exportBtn);
    makePanelSurface(right);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    split->setSizes({180, 800, 370});

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(0);
    connect(m_renderTimer, &QTimer::timeout, this, &PhotoWorkspace::render);

    connect(openBtn, &QPushButton::clicked, this, &PhotoWorkspace::openDialog);
    connect(m_view, &PhotoView::filesDropped, this, &PhotoWorkspace::openFiles);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        if (it)
            showPhoto(it->data(kIdRole).toUuid());
    });
    connect(m_color, &ColorPanel::gradeEdited, this, [this](const ColorGrade& g, const QString& text, const QString& field) {
        if (m_syncing || m_current.isNull())
            return;
        PhotoAdjustments a = current();
        a.color = g;
        pushChange(a, text, field.isEmpty() ? PhotoAdjustCommand::Other : PhotoAdjustCommand::Color, field);
    });
    connect(m_layers, &LayersPanel::layersEdited, this,
            [this](const std::vector<PhotoLayer>& layers, const QString& text, const QString& field) {
                if (m_syncing || m_current.isNull())
                    return;
                PhotoAdjustments a = current();
                a.layers = layers;
                pushChange(a, text, field.isEmpty() ? PhotoAdjustCommand::Other : PhotoAdjustCommand::Layer, field);
            });
    connect(m_layers, &LayersPanel::currentLayerChanged, this, &PhotoWorkspace::updateMaskOverlay);
    connect(m_smart, &SmartToolsPanel::findRequested, this, &PhotoWorkspace::findRedactions);
    connect(m_smart, &SmartToolsPanel::eraseModeChanged, this, [this](bool on) {
        if (on && m_cropBtn->isChecked())
            m_cropBtn->setChecked(false);
        if (on && m_compareBtn->isChecked())
            m_compareBtn->setChecked(false);
        updateMaskOverlay();
    });
    connect(m_smart, &SmartToolsPanel::eraseBrushChanged, this, &PhotoWorkspace::updateMaskOverlay);
    connect(m_smart, &SmartToolsPanel::retouchEdited, this, [this](const PhotoRetouch& r, const QString& text, const QString& field) {
        if (m_syncing || m_current.isNull())
            return;
        if (r.faces > 0.0 && !ensureVisionModels(this, {QStringLiteral("face"), QStringLiteral("face-restore")}, tr("Restore faces"))) {
            syncControls();
            return;
        }
        PhotoAdjustments a = current();
        a.retouch = r;
        pushChange(a, text, PhotoAdjustCommand::Other, field);
    });
    auto* eraseKey = new QShortcut(QKeySequence(Qt::Key_E), this);
    eraseKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(eraseKey, &QShortcut::activated, this, [this] {
        if (m_working.isNull())
            return;
        m_tabs->setCurrentIndex(kSmartTab);
        m_smart->setEraseMode(!m_smart->eraseMode());
    });
    connect(m_smart, &SmartToolsPanel::redactionEdited, this, [this](const Redaction& r, const QString& text, const QString& field) {
        if (m_syncing || m_current.isNull())
            return;
        PhotoAdjustments a = current();
        a.redaction = r;
        pushChange(a, text, PhotoAdjustCommand::Other, field);
    });
    connect(m_smart, &SmartToolsPanel::backgroundEdited, this, [this](const BackgroundRemoval& b, const QString& text, const QString& field) {
        if (m_syncing || m_current.isNull())
            return;
        if (b.enabled && !ensureVisionModels(this, {ai::backgroundModelId(b.model)}, tr("Remove the background"))) {
            syncControls(); // back to how it was
            return;
        }
        PhotoAdjustments a = current();
        a.background = b;
        pushChange(a, text, PhotoAdjustCommand::Other, field);
    });
    m_tabs->setCurrentIndex(QSettings().value(QStringLiteral("photo/tab"), 0).toInt());
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        QSettings().setValue(QStringLiteral("photo/tab"), i);
        updateMaskOverlay();
    });
    connect(m_layers, &LayersPanel::brushChanged, this, &PhotoWorkspace::updateMaskOverlay);
    connect(m_view, &PhotoView::strokePainted, this, [this](const BrushStroke& stroke) {
        if (erasing()) {
            removeObject(stroke);
            return;
        }
        PhotoAdjustments a = current();
        for (auto& l : a.layers)
            if (l.id == m_layers->currentLayer())
                l.mask.strokes.push_back(stroke);
        pushChange(a, stroke.erase ? tr("Erase from mask") : tr("Paint mask"), PhotoAdjustCommand::Other);
    });
    connect(m_view, &PhotoView::pictureMoved, this, [this](const TransformGizmo::Box& b, bool finished) {
        PhotoAdjustments a = current();
        for (auto& l : a.layers)
            if (l.id == m_layers->currentLayer()) {
                l.x = b.x;
                l.y = b.y;
                l.scale = b.scale;
                l.rotation = b.rotation;
            }
        // One undo step per drag.
        pushChange(a, tr("Move sticker"), PhotoAdjustCommand::Layer, QStringLiteral("sticker:%1").arg(m_pictureDrags));
        if (finished)
            ++m_pictureDrags;
    });
    connect(m_view, &PhotoView::maskMoved, this, [this](const QRectF& r) {
        PhotoAdjustments a = current();
        for (auto& l : a.layers)
            if (l.id == m_layers->currentLayer())
                l.mask.rect = r;
        pushChange(a, tr("Move mask"), PhotoAdjustCommand::Other);
    });

    connect(m_cropBtn, &QToolButton::toggled, this, [this](bool on) {
        m_view->setCropMode(on);
        scheduleRender();
    });
    connect(m_aspect, &QComboBox::currentIndexChanged, this, [this] {
        double a = m_aspect->currentData().toDouble();
        if (a < 0 && !m_working.isNull())
            a = double(m_working.width()) / m_working.height();
        if (a != 0 && !m_cropBtn->isChecked())
            m_cropBtn->setChecked(true);
        m_view->setAspect(a);
    });
    connect(m_view, &PhotoView::cropEdited, this, [this](const QRectF& r) {
        if (m_current.isNull())
            return;
        PhotoAdjustments a = current();
        a.crop = (r == QRectF(0, 0, 1, 1)) ? QRectF() : r;
        pushChange(a, tr("Crop"), PhotoAdjustCommand::Crop);
    });
    connect(resetCrop, &QToolButton::clicked, this, [this] {
        if (m_current.isNull())
            return;
        PhotoAdjustments a = current();
        a.crop = QRectF();
        pushChange(a, tr("Remove crop"), PhotoAdjustCommand::Other);
    });
    connect(resetBtn, &QToolButton::clicked, this, &PhotoWorkspace::resetAll);
    connect(m_compareBtn, &QToolButton::toggled, this, [this](bool on) {
        m_compare = on;
        updateMaskOverlay();
        scheduleRender();
    });
    connect(m_exportBtn, &QPushButton::clicked, this, &PhotoWorkspace::exportCurrent);
    connect(m_fullScreenBtn, &QToolButton::clicked, this, &PhotoWorkspace::fullScreenRequested);
    connect(m_view, &PhotoView::doubleClicked, this, &PhotoWorkspace::fullScreenRequested);
    connect(m_bgBtn, &QToolButton::clicked, this, &PhotoWorkspace::toggleBackground);
    auto* bgKey = new QShortcut(QKeySequence(Qt::Key_B), this);
    bgKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(bgKey, &QShortcut::activated, this, &PhotoWorkspace::toggleBackground);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, &PhotoWorkspace::showViewMenu);
    auto* cropKey = new QShortcut(QKeySequence(Qt::Key_C), this);
    cropKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(cropKey, &QShortcut::activated, m_cropBtn, &QToolButton::toggle);
    auto* compareKey = new QShortcut(QKeySequence(Qt::Key_Backslash), this);
    compareKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(compareKey, &QShortcut::activated, m_compareBtn, &QToolButton::toggle);

    connect(m_lib, &PhotoLibrary::added, this, [this](const QUuid& id) {
        const PhotoEntry* e = m_lib->find(id);
        auto* it = new QListWidgetItem(QFileInfo(e->path).fileName());
        it->setData(kIdRole, id);
        it->setToolTip(e->path);
        it->setIcon(QPixmap::fromImage(MediaCache::makeImageThumbnail(e->path, QSize(96, 64))));
        m_list->addItem(it);
        if (m_current.isNull())
            m_list->setCurrentItem(it);
    });
    connect(m_lib, &PhotoLibrary::cleared, this, [this] {
        m_list->clear();
        m_current = {};
        m_original = m_working = m_proxy = QImage();
        m_view->setImage({});
        syncControls();
    });
    connect(m_lib, &PhotoLibrary::adjustmentsChanged, this, [this](const QUuid& id) {
        if (id == m_current) {
            syncControls();
            scheduleRender();
        }
    });
    syncControls();
}

PhotoAdjustments PhotoWorkspace::current() const {
    const PhotoEntry* e = m_lib->find(m_current);
    return e ? e->adjustments : PhotoAdjustments{};
}

void PhotoWorkspace::openDialog() {
    QSettings s;
    QStringList filters;
    for (const auto& ext : imageFileExtensions())
        filters << QStringLiteral("*.") + ext;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Open photos"), s.value(QStringLiteral("dirs/photos")).toString(),
        tr("Photos (%1)").arg(filters.join(QLatin1Char(' '))));
    if (files.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/photos"), QFileInfo(files.front()).absolutePath());
    openFiles(files);
}

void PhotoWorkspace::openFiles(const QStringList& paths) {
    QUuid last;
    for (const auto& p : paths) {
        if (!isImageFile(p))
            continue;
        last = m_lib->add(p);
    }
    if (!last.isNull())
        showPhoto(last);
}

void PhotoWorkspace::showPhoto(const QUuid& id) {
    if (id == m_current && !m_working.isNull())
        return;
    m_current = id;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(kIdRole).toUuid() == id) {
            const QSignalBlocker b(m_list);
            m_list->setCurrentRow(i);
        }
    loadCurrent();
}

void PhotoWorkspace::loadCurrent() {
    const PhotoEntry* e = m_lib->find(m_current);
    m_original = m_working = m_proxy = QImage();
    if (e) {
        QString err;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_working = PhotoProcessor::load(e->path, &m_original, &err);
        QApplication::restoreOverrideCursor();
        if (m_working.isNull())
            QMessageBox::warning(this, tr("Open photo"), tr("Cannot open %1: %2").arg(e->path, err));
        else
            m_proxy = PhotoProcessor::makeProxy(m_working, 2048);
    }
    m_color->setPreviewImage(m_proxy.isNull() ? QImage() : m_proxy.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    syncControls();
    scheduleRender();
}

void PhotoWorkspace::syncControls() {
    m_syncing = true;
    const PhotoAdjustments a = current();
    const bool has = !m_working.isNull();
    m_color->setGrade(a.color);
    m_color->setEditable(has);
    m_layers->setLayers(a.layers, has);
    m_smart->setState(a.retouch, a.redaction, a.background, has);
    m_view->setCrop(a.crop);
    for (QWidget* w : std::initializer_list<QWidget*>{m_cropBtn, m_aspect, m_exportBtn, m_compareBtn, m_fullScreenBtn, m_bgBtn})
        w->setEnabled(has);
    m_bgBtn->setChecked(a.background.enabled);
    m_bgBtn->setText(a.background.enabled ? tr("Background removed") : tr("Remove background"));
    if (has) {
        const QRect cr = a.cropRect(m_working.size());
        m_info->setText(tr("%1 × %2 px · %3-bit source · output %4 × %5 px")
                            .arg(m_working.width())
                            .arg(m_working.height())
                            .arg(PhotoExporter::sourceBitDepth(m_original))
                            .arg(cr.width())
                            .arg(cr.height()));
    } else {
        m_info->clear();
    }
    m_syncing = false;
    updateMaskOverlay();
}

bool PhotoWorkspace::erasing() const {
    return m_smart->eraseMode() && m_tabs->currentIndex() == kSmartTab && !m_compare &&
           !m_working.isNull();
}

void PhotoWorkspace::removeObject(const BrushStroke& stroke) {
    if (m_current.isNull())
        return;
    PhotoAdjustments a = current();
    if (stroke.erase && a.retouch.erase.empty())
        return; // nothing removed yet to paint back
    if (!stroke.erase && !ensureVisionModels(this, {QStringLiteral("inpaint")}, tr("Remove objects")))
        return;
    a.retouch.erase.push_back(stroke);
    pushChange(a, stroke.erase ? tr("Bring part back") : tr("Remove object"), PhotoAdjustCommand::Other);
}

void PhotoWorkspace::updateMaskOverlay() {
    if (erasing()) {
        // A brush canvas over the photo; what was removed is not tinted, so
        // the filled-in result shows.
        LayerMask m;
        m.shape = MaskShape::Brush;
        m.strokes = current().retouch.erase;
        m_view->setMaskOverlay(m);
        m_view->setBrush(m_smart->eraseBrush(), false);
        m_view->setPictureBox(std::nullopt);
        return;
    }
    m_view->setBrush(m_layers->brush(), m_layers->showMaskOverlay());
    const PhotoLayer* l = m_layers->current();
    const bool show = l && m_tabs->currentIndex() == 1 && l->mask.shape != MaskShape::None && !m_compare;
    m_view->setMaskOverlay(show ? std::optional<LayerMask>(l->mask) : std::nullopt);
    // Picture layers (stickers) get handles on the photo.
    std::optional<TransformGizmo::Box> box;
    if (l && l->kind == LayerKind::Image && l->visible && m_tabs->currentIndex() == 1 && !m_compare && !m_working.isNull()) {
        const QSizeF pic(PhotoProcessor::layerPictureSize(*l));
        const QSizeF photo(m_working.size());
        if (!pic.isEmpty()) {
            const QSizeF fit = pic.scaled(photo, Qt::KeepAspectRatio);
            box = TransformGizmo::Box{QSizeF(fit.width() / photo.width(), fit.height() / photo.height()), l->x, l->y,
                                      l->scale, l->rotation, {}};
        }
    }
    m_view->setPictureBox(box);
}

void PhotoWorkspace::pushChange(const PhotoAdjustments& after, const QString& text, int control, const QString& key) {
    const PhotoAdjustments before = current();
    if (before == after)
        return;
    m_undo->push(new PhotoAdjustCommand(m_lib, m_current, before, after, text,
                                        static_cast<PhotoAdjustCommand::Control>(control), key));
}

void PhotoWorkspace::resetAll() {
    if (m_current.isNull())
        return;
    pushChange(PhotoAdjustments{}, tr("Reset photo"), PhotoAdjustCommand::Reset);
}

void PhotoWorkspace::scheduleRender() { m_renderTimer->start(); }

void PhotoWorkspace::render() {
    if (m_proxy.isNull()) {
        m_view->setImage({});
        m_shown = {};
        emit previewChanged();
        return;
    }
    PhotoAdjustments a = current();
    if (m_compare) {
        PhotoAdjustments before;
        before.crop = a.crop;
        a = before;
    }
    if (a.retouch.isActive() && a.retouch == m_skippedRetouch) {
        a.retouch = {};
    } else if (a.retouch.isActive() && !PhotoProcessor::retouchReady(m_proxy, a.retouch)) {
        // Removing objects and restoring faces take a few seconds: done
        // once behind a progress dialog, then kept.
        if (m_preparing)
            return;
        m_preparing = true;
        const QImage proxy = m_proxy;
        const PhotoRetouch retouch = a.retouch;
        QString err;
        const auto ready = withProgress<bool>(this, tr("Repairing the photo…"), [proxy, retouch, &err](const TaskProgress& progress) {
            return !PhotoProcessor::retouched(proxy, retouch, progress, &err).isNull();
        });
        m_preparing = false;
        if (!ready || !*ready) {
            if (ready && !err.isEmpty())
                QMessageBox::warning(this, tr("Repair"), err);
            // Shown without the repairs (not asked again for the same ones);
            // undo takes the last one back.
            m_skippedRetouch = a.retouch;
            a.retouch = {};
            emit statusMessage(tr("The photo is shown without its repairs. Undo (Ctrl+Z) takes the last one back."));
        }
    }
    const QImage processed = PhotoProcessor::apply(m_proxy, a, false); // crop is drawn by the view
    QImage shown;
    if (processed.hasAlphaChannel() && a.background.enabled) {
        // Transparent parts over the usual grey checkerboard.
        shown = QImage(processed.size(), QImage::Format_RGBX8888);
        QPainter p(&shown);
        QImage tile(16, 16, QImage::Format_RGBX8888);
        tile.fill(QColor(200, 200, 200));
        QPainter(&tile).fillRect(0, 0, 8, 8, QColor(150, 150, 150));
        QPainter(&tile).fillRect(8, 8, 8, 8, QColor(150, 150, 150));
        p.fillRect(shown.rect(), QBrush(tile));
        p.drawImage(0, 0, processed);
    } else {
        shown = processed.convertToFormat(QImage::Format_RGBX8888);
    }
    m_view->setImage(shown);
    // The scope shows the result that will be exported, i.e. the cropped part
    // (without the checkerboard).
    const QImage measured = a.background.enabled ? processed.convertToFormat(QImage::Format_RGBX8888) : shown;
    const QRect crop = a.cropRect(measured.size());
    m_scope->setImage(crop == measured.rect() ? measured : measured.copy(crop));
    m_view->setBadge(m_compare ? tr("Before") : QString());
    m_shown = shown;
    m_shownCrop = a.cropRect(shown.size());
    emit previewChanged();
}

QImage PhotoWorkspace::previewImage() const {
    if (m_shown.isNull() || m_shownCrop == m_shown.rect())
        return m_shown;
    return m_shown.copy(m_shownCrop);
}

QString PhotoWorkspace::currentTitle() const {
    const PhotoEntry* e = m_lib->find(m_current);
    return e ? QFileInfo(e->path).fileName() : QString();
}

void PhotoWorkspace::stepPhoto(int delta) {
    const int n = m_list->count();
    if (n < 2)
        return;
    const int row = std::clamp(m_list->currentRow() + delta, 0, n - 1);
    if (row != m_list->currentRow())
        m_list->setCurrentRow(row);
}

void PhotoWorkspace::toggleBackground() {
    if (m_current.isNull() || m_working.isNull()) {
        syncControls();
        return;
    }
    PhotoAdjustments a = current();
    const bool on = !a.background.enabled;
    if (on && !ensureVisionModels(this, {ai::backgroundModelId(a.background.model)}, tr("Remove the background"))) {
        syncControls(); // the button goes back
        return;
    }
    a.background.enabled = on;
    pushChange(a, on ? tr("Remove the background") : tr("Keep the background"), PhotoAdjustCommand::Other);
    if (on)
        emit statusMessage(tr("Background removed. Export as PNG to keep the transparency; click again to bring it back."));
}

void PhotoWorkspace::showViewMenu(const QPoint& pos) {
    if (m_current.isNull() || m_working.isNull())
        return;
    const PhotoAdjustments a = current();
    QMenu menu(this);
    menu.addAction(Icons::removeBackground(), a.background.enabled ? tr("Bring the background back") : tr("Remove background"),
                   this, &PhotoWorkspace::toggleBackground);
    menu.addAction(Icons::eraser(), tr("Remove objects"), this, [this] {
        m_tabs->setCurrentIndex(kSmartTab);
        m_smart->setEraseMode(true);
    });
    menu.addAction(Icons::face(), a.retouch.faces > 0.0 ? tr("Original faces") : tr("Restore faces"), this, [this] {
        PhotoAdjustments a = current();
        const bool on = a.retouch.faces <= 0.0;
        if (on && !ensureVisionModels(this, {QStringLiteral("face"), QStringLiteral("face-restore")}, tr("Restore faces")))
            return;
        a.retouch.faces = on ? 0.8 : 0.0;
        pushChange(a, on ? tr("Restore faces") : tr("Original faces"), PhotoAdjustCommand::Other);
    });
    menu.addAction(tr("Hide faces and plates"), this, &PhotoWorkspace::findRedactions);
    menu.addSeparator();
    menu.addAction(Icons::exportFile(), tr("Export photo…"), this, &PhotoWorkspace::exportCurrent);
    menu.exec(m_view->mapToGlobal(pos));
}

void PhotoWorkspace::findRedactions() {
    if (m_current.isNull() || m_working.isNull())
        return;
    const QString title = tr("Hide faces and plates");
    if (!ensureVisionModels(this, {QStringLiteral("face"), QStringLiteral("plate")}, title))
        return;
    const QImage image = m_working;
    const auto result = withProgress<ai::RedactionResult>(this, tr("Looking for faces and number plates…"), [image](const TaskProgress&) {
        return ai::findRedactions(image, ai::RedactionOptions{});
    });
    if (!result || result->cancelled)
        return;
    if (!result->error.isEmpty()) {
        QMessageBox::warning(this, title, result->error);
        return;
    }
    PhotoAdjustments a = current();
    a.redaction.enabled = true;
    a.redaction.tracks = result->tracks;
    pushChange(a, title, PhotoAdjustCommand::Other);
    emit statusMessage(result->tracks.empty() ? tr("No faces or number plates were found.")
                                              : tr("Found %1 face(s) and %2 number plate(s).")
                                                    .arg(a.redaction.count(RedactionKind::Face))
                                                    .arg(a.redaction.count(RedactionKind::Plate)));
}

void PhotoWorkspace::exportCurrent() {
    const PhotoEntry* e = m_lib->find(m_current);
    if (!e || m_working.isNull())
        return;
    PhotoExportDialog dlg(e->path, m_original, m_working, e->adjustments, this);
    if (dlg.exec() == QDialog::Accepted)
        emit statusMessage(tr("Saved %1").arg(dlg.savedPath()));
}

} // namespace lumora

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
#include <QMessageBox>
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
#include "Theme.h"
#include "color/ColorPanel.h"
#include "color/ScopesWidget.h"
#include "dialogs/PhotoExportDialog.h"
#include "image/PhotoLibrary.h"
#include "image/PhotoProcessor.h"
#include "media/MediaInfo.h"
#include "photo/LayersPanel.h"
#include "photo/PhotoView.h"

namespace lumora {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
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
    m_view->setPlaceholder(tr("Open a photo (JPEG, PNG, TIFF, WebP) with \"Open photos…\" or drag it here.\n\n"
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
    bottom->addWidget(m_compareBtn);
    bottom->addWidget(resetBtn);
    bottom->addStretch();
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
    m_tabs->setCurrentIndex(QSettings().value(QStringLiteral("photo/tab"), 0).toInt());
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        QSettings().setValue(QStringLiteral("photo/tab"), i);
        updateMaskOverlay();
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
    m_view->setCrop(a.crop);
    for (QWidget* w : std::initializer_list<QWidget*>{m_cropBtn, m_aspect, m_exportBtn, m_compareBtn})
        w->setEnabled(has);
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

void PhotoWorkspace::updateMaskOverlay() {
    const PhotoLayer* l = m_layers->current();
    const bool show = l && m_tabs->currentIndex() == 1 && l->mask.shape != MaskShape::None && !m_compare;
    m_view->setMaskOverlay(show ? std::optional<LayerMask>(l->mask) : std::nullopt);
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
        return;
    }
    PhotoAdjustments a = current();
    if (m_compare)
        a = PhotoAdjustments{.color = {}, .crop = a.crop, .layers = {}};
    const QImage processed = PhotoProcessor::apply(m_proxy, a, false); // crop is drawn by the view
    const QImage shown = processed.convertToFormat(QImage::Format_RGBX8888);
    m_view->setImage(shown);
    // The scope shows the result that will be exported, i.e. the cropped part.
    const QRect crop = a.cropRect(shown.size());
    m_scope->setImage(crop == shown.rect() ? shown : shown.copy(crop));
    m_view->setBadge(m_compare ? tr("Before") : QString());
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

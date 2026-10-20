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
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include "Icons.h"
#include "MediaCache.h"
#include "Theme.h"
#include "dialogs/PhotoExportDialog.h"
#include "image/PhotoLibrary.h"
#include "image/PhotoProcessor.h"
#include "media/MediaInfo.h"
#include "photo/PhotoView.h"

namespace lumora {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

// ---------------------------------------------------------------- SliderRow

SliderRow::SliderRow(const QString& label, double min, double max, double step, int decimals, QWidget* parent)
    : QWidget(parent), m_step(step) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 2, 0, 2);
    auto* l = new QLabel(label);
    m_spin = new QDoubleSpinBox;
    m_spin->setRange(min, max);
    m_spin->setDecimals(decimals);
    m_spin->setSingleStep(step);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setFixedWidth(64);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(static_cast<int>(std::lround(min / step)), static_cast<int>(std::lround(max / step)));
    m_slider->installEventFilter(this);
    m_slider->setToolTip(tr("Double-click to reset"));
    grid->addWidget(l, 0, 0);
    grid->addWidget(m_spin, 0, 1);
    grid->addWidget(m_slider, 1, 0, 1, 2);
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_value = v * m_step;
        const QSignalBlocker b(m_spin);
        m_spin->setValue(m_value);
        emit valueChanged(m_value);
    });
    connect(m_spin, &QDoubleSpinBox::editingFinished, this, [this] {
        m_value = m_spin->value();
        const QSignalBlocker b(m_slider);
        m_slider->setValue(static_cast<int>(std::lround(m_value / m_step)));
        emit valueChanged(m_value);
    });
}

void SliderRow::setValue(double v) {
    m_value = v;
    const QSignalBlocker b1(m_slider), b2(m_spin);
    m_slider->setValue(static_cast<int>(std::lround(v / m_step)));
    m_spin->setValue(v);
}

bool SliderRow::eventFilter(QObject* o, QEvent* e) {
    if (o == m_slider && e->type() == QEvent::MouseButtonDblClick) {
        m_slider->setValue(0);
        return true;
    }
    return QWidget::eventFilter(o, e);
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

    // Right: adjustments.
    auto* right = new QWidget;
    right->setMinimumWidth(260);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 8, 8, 8);

    auto* light = new QGroupBox(tr("Light"));
    auto* lightL = new QVBoxLayout(light);
    m_exposure = new SliderRow(tr("Exposure (EV)"), -5, 5, 0.01, 2);
    m_contrast = new SliderRow(tr("Contrast"), -100, 100, 1, 0);
    lightL->addWidget(m_exposure);
    lightL->addWidget(m_contrast);
    rl->addWidget(light);

    auto* wb = new QGroupBox(tr("White balance"));
    auto* wbL = new QVBoxLayout(wb);
    m_temperature = new SliderRow(tr("Temperature"), -100, 100, 1, 0);
    m_tint = new SliderRow(tr("Tint"), -100, 100, 1, 0);
    wbL->addWidget(m_temperature);
    wbL->addWidget(m_tint);
    rl->addWidget(wb);

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

    rl->addStretch();
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
    split->setSizes({180, 800, 280});

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
    auto bindSlider = [this](SliderRow* s, double PhotoAdjustments::*field, const QString& text, int control) {
        connect(s, &SliderRow::valueChanged, this, [this, field, text, control](double v) {
            if (m_syncing || m_current.isNull())
                return;
            PhotoAdjustments a = current();
            a.*field = v;
            pushChange(a, text, control);
        });
    };
    bindSlider(m_exposure, &PhotoAdjustments::exposure, tr("Exposure"), PhotoAdjustCommand::Exposure);
    bindSlider(m_contrast, &PhotoAdjustments::contrast, tr("Contrast"), PhotoAdjustCommand::Contrast);
    bindSlider(m_temperature, &PhotoAdjustments::temperature, tr("Temperature"), PhotoAdjustCommand::Temperature);
    bindSlider(m_tint, &PhotoAdjustments::tint, tr("Tint"), PhotoAdjustCommand::Tint);

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
    m_exposure->setValue(a.exposure);
    m_contrast->setValue(a.contrast);
    m_temperature->setValue(a.temperature);
    m_tint->setValue(a.tint);
    m_view->setCrop(a.crop);
    const bool has = !m_working.isNull();
    for (QWidget* w : std::initializer_list<QWidget*>{m_exposure, m_contrast, m_temperature, m_tint, m_cropBtn, m_aspect,
                                                      m_exportBtn, m_compareBtn})
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
}

void PhotoWorkspace::pushChange(const PhotoAdjustments& after, const QString& text, int control) {
    const PhotoAdjustments before = current();
    if (before == after)
        return;
    m_undo->push(new PhotoAdjustCommand(m_lib, m_current, before, after, text,
                                        static_cast<PhotoAdjustCommand::Control>(control)));
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
        a = PhotoAdjustments{.crop = a.crop};
    const QImage processed = PhotoProcessor::apply(m_proxy, a, false); // crop is drawn by the view
    m_view->setImage(processed.convertToFormat(QImage::Format_RGBX8888));
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

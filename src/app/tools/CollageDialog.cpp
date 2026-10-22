#include "CollageDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "Theme.h"
#include "media/MediaInfo.h"
#include "project/Project.h"
#include "render/Collage.h"

namespace lumora {

namespace {

QIcon layoutIcon(const CollageLayout& l) {
    const QSize size(96, 54);
    QPixmap pm(size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor accent = themeColors().accent;
    for (const QRectF& c : l.cells) {
        const QRectF r(c.x() * size.width() + 1.5, c.y() * size.height() + 1.5, c.width() * size.width() - 3,
                       c.height() * size.height() - 3);
        QColor fill = accent;
        fill.setAlphaF(0.35f);
        p.setPen(QPen(accent, 1.2));
        p.setBrush(fill);
        p.drawRoundedRect(r, 3, 3);
    }
    return QIcon(pm);
}

bool usable(const MediaItem& m) { return m.info.kind == MediaKind::Image || m.info.video.has_value(); }

} // namespace

CollageDialog::CollageDialog(Project* project, std::function<QImage(const QUuid&)> thumbnail, QWidget* parent)
    : QDialog(parent), m_project(project), m_thumbnail(std::move(thumbnail)) {
    setWindowTitle(tr("Video collage"));
    resize(580, 640);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Choose a layout and the videos or pictures for it. Each one goes on its own track at the "
                                "playhead, cut to fit its cell; afterwards every clip can still be moved and changed."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    layout->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Layout"))));
    m_layouts = new QListWidget;
    m_layouts->setViewMode(QListView::IconMode);
    m_layouts->setIconSize(QSize(96, 54));
    m_layouts->setGridSize(QSize(124, 100));
    m_layouts->setTextElideMode(Qt::ElideNone);
    m_layouts->setResizeMode(QListView::Adjust);
    m_layouts->setMovement(QListView::Static);
    m_layouts->setWordWrap(true);
    m_layouts->setFixedHeight(220);
    for (const auto& l : collageLayouts()) {
        auto* item = new QListWidgetItem(layoutIcon(l), l.name);
        item->setData(Qt::UserRole, l.id);
        m_layouts->addItem(item);
    }
    layout->addWidget(m_layouts);

    auto* mediaHead = new QHBoxLayout;
    mediaHead->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Videos and pictures"))));
    mediaHead->addStretch(1);
    m_count = new QLabel;
    mediaHead->addWidget(m_count);
    layout->addLayout(mediaHead);
    m_media = new QListWidget;
    m_media->setIconSize(QSize(64, 36));
    m_media->setDragDropMode(QAbstractItemView::InternalMove);
    m_media->setToolTip(tr("Tick what goes in the collage; drag to change the order"));
    layout->addWidget(m_media, 1);
    auto* add = new QPushButton(tr("Add files…"));
    auto* addRow = new QHBoxLayout;
    addRow->addWidget(add);
    addRow->addStretch(1);
    layout->addLayout(addRow);

    auto* form = new QFormLayout;
    m_gap = new QSpinBox;
    m_gap->setRange(0, 10);
    m_gap->setSuffix(QStringLiteral(" %"));
    m_gap->setToolTip(tr("Space between the cells; what is on the tracks below shows through"));
    form->addRow(tr("Space between"), m_gap);
    m_seconds = new QDoubleSpinBox;
    m_seconds->setRange(0.5, 600);
    m_seconds->setDecimals(1);
    m_seconds->setSuffix(tr(" s"));
    m_seconds->setToolTip(tr("How long the collage lasts when it holds only pictures; with videos it lasts as long as the shortest one"));
    form->addRow(tr("Length with pictures only"), m_seconds);
    m_sound = new QComboBox;
    m_sound->addItem(tr("Sound of the first video"), int(Sound::First));
    m_sound->addItem(tr("Sound of every video"), int(Sound::All));
    m_sound->addItem(tr("No sound"), int(Sound::None));
    form->addRow(tr("Sound"), m_sound);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* make = buttons->addButton(tr("Make collage"), QDialogButtonBox::AcceptRole);
    make->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(make, &QPushButton::clicked, this, [this] {
        const auto* l = findCollageLayout(layoutId());
        if (!l || media().isEmpty()) {
            QMessageBox::information(this, windowTitle(), tr("Tick at least one video or picture."));
            return;
        }
        QSettings s;
        s.setValue(QStringLiteral("collage/layout"), layoutId());
        s.setValue(QStringLiteral("collage/gap"), m_gap->value());
        s.setValue(QStringLiteral("collage/seconds"), m_seconds->value());
        s.setValue(QStringLiteral("collage/sound"), m_sound->currentData());
        accept();
    });
    layout->addWidget(buttons);

    QSettings s;
    const QString last = s.value(QStringLiteral("collage/layout"), QStringLiteral("sideBySide")).toString();
    for (int i = 0; i < m_layouts->count(); ++i)
        if (m_layouts->item(i)->data(Qt::UserRole).toString() == last)
            m_layouts->setCurrentRow(i);
    if (!m_layouts->currentItem())
        m_layouts->setCurrentRow(0);
    m_gap->setValue(s.value(QStringLiteral("collage/gap"), 1).toInt());
    m_seconds->setValue(s.value(QStringLiteral("collage/seconds"), 5.0).toDouble());
    m_sound->setCurrentIndex(std::max(0, m_sound->findData(s.value(QStringLiteral("collage/sound"), 0))));

    fillMedia();
    connect(add, &QPushButton::clicked, this, &CollageDialog::addFiles);
    connect(m_layouts, &QListWidget::currentRowChanged, this, &CollageDialog::refreshCount);
    connect(m_media, &QListWidget::itemChanged, this, &CollageDialog::refreshCount);
    refreshCount();
}

void CollageDialog::fillMedia() {
    QList<QUuid> ticked = media();
    m_media->blockSignals(true);
    m_media->clear();
    for (const auto& m : m_project->media()->items()) {
        if (!usable(*m))
            continue;
        auto* item = new QListWidgetItem(QFileInfo(m->path).fileName());
        item->setData(Qt::UserRole, m->id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(ticked.contains(m->id) ? Qt::Checked : Qt::Unchecked);
        if (m_thumbnail) {
            const QImage thumb = m_thumbnail(m->id);
            if (!thumb.isNull())
                item->setIcon(QPixmap::fromImage(thumb.scaled(128, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }
        m_media->addItem(item);
    }
    m_media->blockSignals(false);
}

void CollageDialog::addFiles() {
    QSettings s;
    QStringList patterns;
    for (const auto& e : imageFileExtensions() + videoFileExtensions())
        patterns << QStringLiteral("*.") + e;
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add files"), s.value(QStringLiteral("dirs/media")).toString(),
                                                            tr("Pictures and videos (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (files.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/media"), QFileInfo(files.front()).absolutePath());
    QList<QUuid> added;
    QStringList failed;
    for (const QString& f : files) {
        QString err;
        if (auto item = m_project->media()->import(f, &err); item && usable(*item))
            added << item->id;
        else
            failed << QFileInfo(f).fileName();
    }
    fillMedia();
    for (int i = 0; i < m_media->count(); ++i)
        if (added.contains(m_media->item(i)->data(Qt::UserRole).toUuid()))
            m_media->item(i)->setCheckState(Qt::Checked);
    if (!failed.isEmpty())
        QMessageBox::warning(this, windowTitle(), tr("These files could not be opened:\n%1").arg(failed.join(QLatin1Char('\n'))));
    refreshCount();
}

void CollageDialog::refreshCount() {
    const auto* l = findCollageLayout(layoutId());
    const int cells = l ? int(l->cells.size()) : 0;
    const int n = int(media().size());
    m_count->setText(n > cells ? tr("%1 of %2 cells (the rest are left out)").arg(cells).arg(cells)
                               : tr("%1 of %2 cells").arg(n).arg(cells));
}

QString CollageDialog::layoutId() const {
    return m_layouts->currentItem() ? m_layouts->currentItem()->data(Qt::UserRole).toString() : QString();
}

QList<QUuid> CollageDialog::media() const {
    QList<QUuid> out;
    for (int i = 0; i < m_media->count(); ++i)
        if (m_media->item(i)->checkState() == Qt::Checked)
            out << m_media->item(i)->data(Qt::UserRole).toUuid();
    return out;
}

double CollageDialog::gap() const { return m_gap->value() / 100.0; }

double CollageDialog::pictureSeconds() const { return m_seconds->value(); }

CollageDialog::Sound CollageDialog::sound() const { return static_cast<Sound>(m_sound->currentData().toInt()); }

} // namespace lumora

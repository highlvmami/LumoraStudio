#include "MediaBinPanel.h"

#include <cmath>

#include <QDragEnterEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "MediaCache.h"
#include "media/MediaPool.h"
#include "panels/MimeTypes.h"

namespace lumora {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
const QSize kThumb(160, 90);

QPixmap decorate(const QImage& thumb, const MediaInfo& info) {
    QPixmap pm(kThumb);
    pm.fill(QColor(30, 30, 34));
    QPainter p(&pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    if (!thumb.isNull()) {
        const QSize s = thumb.size().scaled(kThumb, Qt::KeepAspectRatio);
        p.drawImage(QRect(QPoint((kThumb.width() - s.width()) / 2, (kThumb.height() - s.height()) / 2), s), thumb);
    } else if (info.kind == MediaKind::Audio) {
        // Sound files have no picture: a stylised waveform instead.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(70, 200, 150));
        const int mid = kThumb.height() / 2, bars = 28, w = kThumb.width() - 32;
        for (int i = 0; i < bars; ++i) {
            const double v = 0.25 + 0.75 * std::abs(std::sin(i * 0.7) * std::cos(i * 0.23));
            const int h = static_cast<int>(v * kThumb.height() * 0.32);
            p.drawRoundedRect(QRectF(16 + i * w / double(bars), mid - h, w / double(bars) * 0.6, 2 * h), 1.5, 1.5);
        }
    }
    QString badge;
    if (info.kind == MediaKind::Image)
        badge = QStringLiteral("%1×%2").arg(info.imageWidth).arg(info.imageHeight);
    else
        badge = formatSeconds(info.duration).section(QLatin1Char('.'), 0, 0);
    QFont f;
    f.setPointSize(8);
    f.setBold(true);
    p.setFont(f);
    const QRect tr = p.fontMetrics().boundingRect(badge).adjusted(-4, -2, 4, 2);
    const QRect box(QPoint(kThumb.width() - tr.width() - 4, kThumb.height() - tr.height() - 4), tr.size());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, badge);
    const QString kind = info.kind == MediaKind::Image ? QStringLiteral("IMG")
                         : info.kind == MediaKind::Audio ? QStringLiteral("♪")
                                                         : (info.video && info.video->isHdr() ? QStringLiteral("HDR") : QString());
    if (!kind.isEmpty()) {
        const QRect kr = p.fontMetrics().boundingRect(kind).adjusted(-4, -2, 4, 2);
        const QRect kbox(QPoint(4, 4), kr.size());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(236, 72, 153, 200));
        p.drawRoundedRect(kbox, 3, 3);
        p.setPen(Qt::white);
        p.drawText(kbox, Qt::AlignCenter, kind);
    }
    return pm;
}
} // namespace

QStringList MediaListWidget::mimeTypes() const { return {QString::fromLatin1(kMediaMimeType)}; }

QMimeData* MediaListWidget::mimeData(const QList<QListWidgetItem*>& items) const {
    QList<QUuid> ids;
    for (auto* it : items)
        ids << it->data(kIdRole).toUuid();
    return encodeMediaIds(ids);
}

void MediaListWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragEnterEvent(e);
}

void MediaListWidget::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        QListWidget::dragMoveEvent(e);
}

void MediaListWidget::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls())
        return;
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    emit filesDropped(paths);
    e->acceptProposedAction();
}

MediaBinPanel::MediaBinPanel(MediaPool* pool, MediaCache* cache, QWidget* parent)
    : QWidget(parent), m_pool(pool), m_cache(cache) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search files"));
    m_search->setClearButtonEnabled(true);
    auto* importBtn = new QToolButton;
    importBtn->setIcon(Icons::import());
    importBtn->setToolTip(tr("Import media (Ctrl+I)"));
    importBtn->setAutoRaise(true);
    auto* viewBtn = new QToolButton;
    viewBtn->setText(tr("List"));
    viewBtn->setCheckable(true);
    viewBtn->setAutoRaise(true);
    viewBtn->setToolTip(tr("Switch between thumbnails and a list"));
    top->addWidget(m_search, 1);
    top->addWidget(viewBtn);
    top->addWidget(importBtn);
    layout->addLayout(top);

    m_list = new MediaListWidget;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(kThumb);
    m_list->setGridSize(QSize(kThumb.width() + 16, kThumb.height() + 40));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDragDropMode(QAbstractItemView::DragDrop);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_list, 1);

    connect(importBtn, &QToolButton::clicked, this, &MediaBinPanel::importRequested);
    connect(viewBtn, &QToolButton::toggled, this, [this](bool list) {
        m_list->setViewMode(list ? QListView::ListMode : QListView::IconMode);
        m_list->setIconSize(list ? QSize(64, 36) : kThumb);
        m_list->setGridSize(list ? QSize() : QSize(kThumb.width() + 16, kThumb.height() + 40));
        m_list->setDragEnabled(true);
    });
    connect(m_search, &QLineEdit::textChanged, this, &MediaBinPanel::applyFilter);
    connect(m_list, &MediaListWidget::filesDropped, this, &MediaBinPanel::filesDropped);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) { emit activated(it->data(kIdRole).toUuid()); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it) {
        emit selectionChanged(it ? it->data(kIdRole).toUuid() : QUuid());
    });
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const auto ids = selectedIds();
        if (ids.isEmpty())
            return;
        QMenu menu;
        menu.addAction(tr("Open"), this, [this, ids] { emit activated(ids.front()); });
        menu.addAction(Icons::add(), tr("Add to timeline"), this, [this, ids] { emit addToTimeline(ids); });
        menu.addSeparator();
        menu.addAction(Icons::remove(), tr("Remove from project"), this, [this, ids] { emit removeRequested(ids); });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });

    connect(pool, &MediaPool::itemAdded, this, [this](const MediaItemPtr& item) { addItem(item->id); });
    connect(pool, &MediaPool::itemRemoved, this, [this](const QUuid& id) { delete itemFor(id); });
    connect(pool, &MediaPool::cleared, m_list, &QListWidget::clear);
    connect(cache, &MediaCache::thumbnailReady, this, &MediaBinPanel::refreshIcon);
}

QList<QUuid> MediaBinPanel::selectedIds() const {
    QList<QUuid> ids;
    for (auto* it : m_list->selectedItems())
        ids << it->data(kIdRole).toUuid();
    return ids;
}

QListWidgetItem* MediaBinPanel::itemFor(const QUuid& id) const {
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(kIdRole).toUuid() == id)
            return m_list->item(i);
    return nullptr;
}

void MediaBinPanel::addItem(const QUuid& id) {
    auto media = m_pool->find(id);
    if (!media)
        return;
    auto* it = new QListWidgetItem(QFileInfo(media->path).fileName());
    it->setData(kIdRole, id);
    it->setToolTip(QStringLiteral("%1\n%2").arg(QDir::toNativeSeparators(media->path), media->info.summary()));
    it->setIcon(decorate(m_cache->thumbnail(id), media->info));
    m_list->addItem(it);
    applyFilter();
}

void MediaBinPanel::refreshIcon(const QUuid& id) {
    auto media = m_pool->find(id);
    if (auto* it = itemFor(id); it && media)
        it->setIcon(decorate(m_cache->thumbnail(id), media->info));
}

void MediaBinPanel::applyFilter() {
    const QString q = m_search->text().trimmed();
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setHidden(!q.isEmpty() && !m_list->item(i)->text().contains(q, Qt::CaseInsensitive));
}

} // namespace lumora

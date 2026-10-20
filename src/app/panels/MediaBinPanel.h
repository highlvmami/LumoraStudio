#pragma once

#include <QListWidget>
#include <QUuid>
#include <QWidget>

class QLineEdit;

namespace lumora {

class MediaPool;
class MediaCache;

class MediaListWidget : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;

signals:
    void filesDropped(const QStringList& paths);

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

// "Files" panel: imported media with thumbnails, search and drag & drop.
class MediaBinPanel : public QWidget {
    Q_OBJECT
public:
    MediaBinPanel(MediaPool* pool, MediaCache* cache, QWidget* parent = nullptr);
    QList<QUuid> selectedIds() const;

signals:
    void importRequested();
    void filesDropped(const QStringList& paths);
    void activated(const QUuid& id);      // double click
    void addToTimeline(const QList<QUuid>& ids);
    void removeRequested(const QList<QUuid>& ids);
    void selectionChanged(const QUuid& current);

private:
    void addItem(const QUuid& id);
    QListWidgetItem* itemFor(const QUuid& id) const;
    void applyFilter();
    void refreshIcon(const QUuid& id);

    MediaPool* m_pool;
    MediaCache* m_cache;
    MediaListWidget* m_list;
    QLineEdit* m_search;
};

} // namespace lumora

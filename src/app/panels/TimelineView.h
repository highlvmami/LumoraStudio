#pragma once

#include <QSet>
#include <QUuid>
#include <QWidget>
#include <optional>

#include "timeline/Timeline.h"

class QUndoStack;

namespace lumora {

class MediaPool;
class MediaCache;

// Interactive drawing of the timeline: ruler, tracks, clips with thumbnails,
// waveforms and key frame marks, the playhead, selection, ripple move and
// trim by dragging, and drops from the media bin.
class TimelineView : public QWidget {
    Q_OBJECT
public:
    TimelineView(Timeline* timeline, MediaPool* pool, MediaCache* cache, QUndoStack* undo, QWidget* parent = nullptr);

    void setPlayhead(Ticks t);
    Ticks playhead() const { return m_playhead; }
    QList<QUuid> selection() const { return m_selection.values(); }
    void selectOnly(const QUuid& id);
    void clearSelection();

    double pixelsPerSecond() const { return m_pps; }
    void setPixelsPerSecond(double pps, int anchorX = -1);
    void zoomToFit();
    int scrollX() const { return m_scrollX; }
    void setScrollX(int x);
    int contentWidth() const;
    void setSnapping(bool on) { m_snap = on; }
    bool snapping() const { return m_snap; }
    void ensurePlayheadVisible();

    QSize sizeHint() const override { return {800, 200}; }
    QSize minimumSizeHint() const override { return {200, 150}; }

signals:
    void seekRequested(lumora::Ticks t);
    void selectionChanged();
    void clipDoubleClicked(const QUuid& clipId);
    void mediaDropped(const QList<QUuid>& ids, lumora::Ticks at);
    void filesDropped(const QStringList& paths, lumora::Ticks at);
    void scrollChanged();
    void contextMenuForClip(const QUuid& clipId, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    struct Hit {
        int track = -1;
        QUuid clip;
        enum Edge { None, In, Out } edge = None;
    };

    static constexpr int kHeader = 70;
    static constexpr int kRuler = 26;

    int trackTop(int track) const;
    int trackHeight(int track) const;
    double xOf(Ticks t) const;
    Ticks timeAt(double x) const;
    Ticks snapTime(Ticks t, const QUuid& ignoreClip = {}) const;
    Hit hitTest(QPoint pos) const;
    int insertionIndexAt(double x) const;
    void paintRuler(QPainter& p);
    void paintClip(QPainter& p, const Clip& c, int track, const QRect& r);

    Timeline* m_timeline;
    MediaPool* m_pool;
    MediaCache* m_cache;
    QUndoStack* m_undo;
    double m_pps = 60.0; // pixels per second
    int m_scrollX = 0;
    Ticks m_playhead = 0;
    QSet<QUuid> m_selection;
    bool m_snap = true;

    enum class Drag { None, Playhead, Move, TrimIn, TrimOut } m_drag = Drag::None;
    QUuid m_dragClip;
    QPoint m_pressPos;
    TimelineState m_dragBefore;
    Ticks m_dragSourceStart = 0;
    int m_dropIndex = -1; // insertion marker while moving or dropping
    double m_dropX = -1;
    bool m_dragStarted = false;
};

} // namespace lumora

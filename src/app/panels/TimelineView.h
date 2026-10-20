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

// Interactive drawing of the timeline: ruler, track headers (hide, mute,
// lock), every video track above the storyline and every audio track below
// it, clips with thumbnails, waveforms, fades and key frame marks, transition
// markers, the playhead, selection, moving clips within and between tracks,
// trimming, and drops from the media bin.
class TimelineView : public QWidget {
    Q_OBJECT
public:
    TimelineView(Timeline* timeline, MediaPool* pool, MediaCache* cache, QUndoStack* undo, QWidget* parent = nullptr);

    static constexpr int kHeader = 118;

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
    int scrollY() const { return m_scrollY; }
    void setScrollY(int y);
    int contentHeight() const;
    void setSnapping(bool on) { m_snap = on; }
    bool snapping() const { return m_snap; }
    void ensurePlayheadVisible();

    QSize sizeHint() const override { return {800, 220}; }
    QSize minimumSizeHint() const override { return {200, 150}; }

signals:
    void seekRequested(lumora::Ticks t);
    void selectionChanged();
    void clipDoubleClicked(const QUuid& clipId);
    // track < 0: insert into the storyline at the nearest edit.
    void mediaDropped(const QList<QUuid>& ids, lumora::Ticks at, int track);
    void filesDropped(const QStringList& paths, lumora::Ticks at, int track);
    void scrollChanged();
    void contextMenuForClip(const QUuid& clipId, const QPoint& globalPos);
    void addTitleRequested(int track, lumora::Ticks at);

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
    struct Row {
        int track = -1;
        int top = 0;
        int height = 0;
        int bottom() const { return top + height; }
    };
    struct Hit {
        int track = -1;
        QUuid clip;
        enum Edge { None, In, Out } edge = None;
    };
    enum class HeaderButton { None, Visibility, Lock };

    static constexpr int kRuler = 26;

    std::vector<Row> rows() const;
    std::optional<Row> rowOf(int track) const;
    int trackAtY(int y) const;
    double xOf(Ticks t) const;
    Ticks timeAt(double x) const;
    Ticks snapTime(Ticks t, const QUuid& ignoreClip = {}) const;
    Hit hitTest(QPoint pos) const;
    HeaderButton headerButtonAt(QPoint pos, int* track) const;
    QRect headerButtonRect(const Row& row, HeaderButton b) const;
    int insertionIndexAt(double x) const;
    Ticks storylineBoundary(int index) const;
    bool isSelected(const Clip& c) const;
    void paintRuler(QPainter& p);
    void paintHeader(QPainter& p, const Row& row);
    void paintClip(QPainter& p, const Clip& c, int track, const QRect& r);
    void paintTransitions(QPainter& p, const Row& row);
    void showTrackMenu(int track, Ticks at, const QPoint& globalPos);
    // Where a dragged clip / drop would land.
    void updateDropTarget(QPoint pos, TrackType type, Ticks length, Ticks grabOffset, const QUuid& ignore);

    Timeline* m_timeline;
    MediaPool* m_pool;
    MediaCache* m_cache;
    QUndoStack* m_undo;
    double m_pps = 60.0; // pixels per second
    int m_scrollX = 0;
    int m_scrollY = 0;
    Ticks m_playhead = 0;
    QSet<QUuid> m_selection;
    bool m_snap = true;

    enum class Drag { None, Playhead, Move, TrimIn, TrimOut } m_drag = Drag::None;
    QUuid m_dragClip;
    QPoint m_pressPos;
    TimelineState m_dragBefore;
    Ticks m_dragSourceStart = 0;
    Ticks m_grabOffset = 0;   // time between the clip start and the mouse
    bool m_dragStarted = false;

    // Drop / move target: either a storyline insertion index or a track + time.
    int m_dropIndex = -1;
    int m_dropTrack = -1;
    Ticks m_dropTime = 0;
    Ticks m_dropLength = 0;
};

} // namespace lumora

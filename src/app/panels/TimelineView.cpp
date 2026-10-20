#include "TimelineView.h"

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QUndoStack>
#include <cmath>

#include "MediaCache.h"
#include "media/MediaPool.h"
#include "panels/MimeTypes.h"
#include "undo/TimelineCommands.h"

namespace lumora {

namespace {
const QColor kVideoClip(58, 110, 196);
const QColor kAudioClip(46, 140, 104);
const QColor kPlayhead(255, 84, 84);
constexpr int kEdgeGrab = 6;
constexpr int kSnapPx = 8;
} // namespace

TimelineView::TimelineView(Timeline* timeline, MediaPool* pool, MediaCache* cache, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), m_timeline(timeline), m_pool(pool), m_cache(cache), m_undo(undo) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(m_timeline, &Timeline::changed, this, [this] {
        // Drop selections that no longer exist.
        QSet<QUuid> keep;
        for (const auto& id : m_selection)
            if (m_timeline->clip(id))
                keep.insert(id);
        if (keep != m_selection) {
            m_selection = keep;
            emit selectionChanged();
        }
        emit scrollChanged();
        update();
    });
    connect(m_cache, &MediaCache::thumbnailReady, this, qOverload<>(&QWidget::update));
    connect(m_cache, &MediaCache::peaksReady, this, qOverload<>(&QWidget::update));
    connect(m_cache, &MediaCache::keyframesReady, this, qOverload<>(&QWidget::update));
}

int TimelineView::trackTop(int track) const { return kRuler + 6 + (track == 0 ? 0 : 74); }
int TimelineView::trackHeight(int track) const { return track == 0 ? 68 : 52; }

double TimelineView::xOf(Ticks t) const { return kHeader + ticksToSeconds(t) * m_pps - m_scrollX; }

Ticks TimelineView::timeAt(double x) const {
    return std::max<Ticks>(0, ticksFromSeconds((x - kHeader + m_scrollX) / m_pps));
}

int TimelineView::contentWidth() const {
    return static_cast<int>(ticksToSeconds(m_timeline->duration()) * m_pps) + 200;
}

void TimelineView::setScrollX(int x) {
    x = std::clamp(x, 0, std::max(0, contentWidth() - (width() - kHeader)));
    if (x == m_scrollX)
        return;
    m_scrollX = x;
    emit scrollChanged();
    update();
}

void TimelineView::setPixelsPerSecond(double pps, int anchorX) {
    pps = std::clamp(pps, 2.0, 2000.0);
    if (anchorX < 0)
        anchorX = width() / 2;
    const Ticks anchorT = timeAt(anchorX);
    m_pps = pps;
    m_scrollX = std::max(0, static_cast<int>(ticksToSeconds(anchorT) * m_pps - (anchorX - kHeader)));
    emit scrollChanged();
    update();
}

void TimelineView::zoomToFit() {
    const double secs = std::max(1.0, ticksToSeconds(m_timeline->duration()));
    m_pps = std::clamp((width() - kHeader - 40) / secs, 2.0, 2000.0);
    m_scrollX = 0;
    emit scrollChanged();
    update();
}

void TimelineView::setPlayhead(Ticks t) {
    if (t == m_playhead)
        return;
    m_playhead = t;
    update();
}

void TimelineView::ensurePlayheadVisible() {
    const double x = xOf(m_playhead);
    if (x < kHeader || x > width() - 20)
        setScrollX(static_cast<int>(ticksToSeconds(m_playhead) * m_pps - (width() - kHeader) * 0.2));
}

void TimelineView::selectOnly(const QUuid& id) {
    m_selection = {id};
    emit selectionChanged();
    update();
}

void TimelineView::clearSelection() {
    if (m_selection.isEmpty())
        return;
    m_selection.clear();
    emit selectionChanged();
    update();
}

Ticks TimelineView::snapTime(Ticks t, const QUuid& ignoreClip) const {
    if (!m_snap)
        return snapToFrame(t, m_timeline->frameRate());
    const Ticks threshold = ticksFromSeconds(kSnapPx / m_pps);
    Ticks best = t;
    Ticks bestDist = threshold + 1;
    auto consider = [&](Ticks c) {
        const Ticks d = std::abs(c - t);
        if (d < bestDist) {
            bestDist = d;
            best = c;
        }
    };
    for (const auto& tr : m_timeline->tracks())
        for (const auto& c : tr.clips) {
            if (c.id == ignoreClip)
                continue;
            consider(c.timelineStart);
            consider(c.timelineEnd());
        }
    consider(0);
    if (bestDist <= threshold)
        return best;
    return snapToFrame(t, m_timeline->frameRate());
}

TimelineView::Hit TimelineView::hitTest(QPoint pos) const {
    Hit h;
    for (int ti = 0; ti < static_cast<int>(m_timeline->tracks().size()); ++ti) {
        if (pos.y() < trackTop(ti) || pos.y() >= trackTop(ti) + trackHeight(ti))
            continue;
        h.track = ti;
        for (const auto& c : m_timeline->tracks()[ti].clips) {
            const double x0 = xOf(c.timelineStart), x1 = xOf(c.timelineEnd());
            if (pos.x() >= x0 - 1 && pos.x() <= x1 + 1) {
                h.clip = c.id;
                if (pos.x() - x0 <= kEdgeGrab && x1 - x0 > 3 * kEdgeGrab)
                    h.edge = Hit::In;
                else if (x1 - pos.x() <= kEdgeGrab && x1 - x0 > 3 * kEdgeGrab)
                    h.edge = Hit::Out;
                else if (x1 - x0 <= 3 * kEdgeGrab)
                    h.edge = Hit::None;
                return h;
            }
        }
    }
    return h;
}

int TimelineView::insertionIndexAt(double x) const {
    const auto& clips = m_timeline->tracks()[0].clips;
    const Ticks t = timeAt(x);
    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
        if (t < clips[i].timelineStart + clips[i].duration() / 2)
            return i;
    return static_cast<int>(clips.size());
}

void TimelineView::paintRuler(QPainter& p) {
    const QPalette pal = palette();
    p.fillRect(QRect(0, 0, width(), kRuler), pal.color(QPalette::Window).darker(115));
    // Pick a tick step of at least ~80 px.
    static const double steps[] = {1 / 25.0, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double step = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * m_pps >= 80) {
            step = s;
            break;
        }
    const double firstSec = std::floor((m_scrollX / m_pps) / step) * step;
    p.setPen(pal.color(QPalette::WindowText));
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    for (double s = firstSec; ; s += step) {
        const double x = kHeader + s * m_pps - m_scrollX;
        if (x > width())
            break;
        if (x < kHeader)
            continue;
        p.drawLine(QPointF(x, kRuler - 10), QPointF(x, kRuler));
        const Ticks t = ticksFromSeconds(s);
        const QString label = step < 1 ? formatTimecode(t, m_timeline->frameRate()) : formatTimecode(t, m_timeline->frameRate()).left(8);
        p.drawText(QPointF(x + 4, kRuler - 12), label);
        for (int i = 1; i < 5; ++i) {
            const double sx = x + i * step * m_pps / 5;
            p.drawLine(QPointF(sx, kRuler - 4), QPointF(sx, kRuler));
        }
    }
}

void TimelineView::paintClip(QPainter& p, const Clip& c, int track, const QRect& r) {
    const bool video = m_timeline->tracks()[track].type == TrackType::Video;
    const bool selected = m_selection.contains(c.id) || (!c.linkId.isNull() && [&] {
        for (const auto& id : m_selection)
            if (const Clip* s = m_timeline->clip(id); s && s->linkId == c.linkId)
                return true;
        return false;
    }());
    QColor base = video ? kVideoClip : kAudioClip;
    auto media = m_pool->find(c.mediaId);
    QPainterPath path;
    path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
    p.save();
    p.setClipPath(path);
    p.fillRect(r, base.darker(140));
    if (video) {
        const QImage thumb = m_cache->thumbnail(c.mediaId);
        if (!thumb.isNull()) {
            const int th = r.height() - 18;
            const int tw = thumb.width() * th / std::max(1, thumb.height());
            for (int x = r.left(); x < r.right(); x += tw + 2) {
                p.drawImage(QRect(x, r.top() + 16, tw, th), thumb);
                if (tw * 3 > r.width())
                    break;
            }
        }
        // Key frame marks: where a lossless cut can start exactly.
        if (media && media->hasKeyframes()) {
            const auto kf = media->keyframesCopy();
            const double spacing = kf.size() > 1 ? ticksToSeconds(kf[1] - kf[0]) * m_pps : 100;
            if (spacing > 6) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 210, 90));
                for (Ticks k : kf) {
                    if (k < c.sourceIn || k >= c.sourceOut)
                        continue;
                    const double x = xOf(c.timelineStart + (k - c.sourceIn));
                    QPolygonF tri{QPointF(x - 3.5, r.bottom()), QPointF(x + 3.5, r.bottom()), QPointF(x, r.bottom() - 5)};
                    p.drawPolygon(tri);
                }
            }
        }
    } else if (auto peaks = m_cache->peaks(c.mediaId)) {
        p.setPen(Qt::NoPen);
        p.setBrush(base.lighter(150));
        const int mid = r.top() + r.height() / 2 + 6;
        const double halfH = (r.height() - 20) / 2.0;
        const int x0 = std::max(r.left(), 0), x1 = std::min(r.right(), width());
        QPainterPath wave;
        wave.moveTo(x0, mid);
        std::vector<QPointF> lower;
        for (int x = x0; x <= x1; x += 2) {
            const Ticks t0 = c.toSource(timeAt(x)), t1 = c.toSource(timeAt(x + 2));
            const size_t a = static_cast<size_t>(std::max<Ticks>(0, t0) * MediaCache::kPeaksPerSecond / kTicksPerSecond);
            const size_t b = std::max(a + 1, static_cast<size_t>(std::max<Ticks>(0, t1) * MediaCache::kPeaksPerSecond / kTicksPerSecond));
            float peak = 0;
            for (size_t i = a; i < b && i < peaks->size(); ++i)
                peak = std::max(peak, (*peaks)[i]);
            const double h = std::sqrt(peak) * halfH;
            wave.lineTo(x, mid - h);
            lower.emplace_back(x, mid + h);
        }
        for (auto it = lower.rbegin(); it != lower.rend(); ++it)
            wave.lineTo(*it);
        wave.closeSubpath();
        p.drawPath(wave);
    }
    // Title bar.
    p.fillRect(QRect(r.left(), r.top(), r.width(), 16), base);
    p.setPen(Qt::white);
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    const QString name = media ? QFileInfo(media->path).fileName() : tr("Missing media");
    p.drawText(QRect(r.left() + 5, r.top(), r.width() - 10, 16), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(name + QStringLiteral("  ·  ") + formatSeconds(c.duration()), Qt::ElideRight,
                                          r.width() - 10));
    p.restore();
    p.setPen(QPen(selected ? QColor(255, 200, 60) : base.lighter(130), selected ? 2.5 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));
    paintRuler(p);

    const auto& tracks = m_timeline->tracks();
    for (int ti = 0; ti < static_cast<int>(tracks.size()); ++ti) {
        const QRect lane(kHeader, trackTop(ti), width() - kHeader, trackHeight(ti));
        p.fillRect(lane, pal.color(QPalette::AlternateBase));
        p.save();
        p.setClipRect(lane.adjusted(0, -2, 0, 2));
        for (const auto& c : tracks[ti].clips) {
            QRect r(QPoint(static_cast<int>(xOf(c.timelineStart)), lane.top() + 2),
                    QPoint(static_cast<int>(xOf(c.timelineEnd())) - 1, lane.bottom() - 2));
            if (r.right() < kHeader || r.left() > width())
                continue;
            paintClip(p, c, ti, r);
        }
        p.restore();
        // Header.
        const QRect header(0, trackTop(ti), kHeader - 4, trackHeight(ti));
        p.fillRect(header, pal.color(QPalette::Window));
        p.setPen(pal.color(QPalette::WindowText));
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.drawText(header.adjusted(8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tracks[ti].name);
        p.setFont(font());
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(header.adjusted(8, 0, -4, -6), Qt::AlignBottom | Qt::AlignLeft,
                   tracks[ti].type == TrackType::Video ? tr("Video") : tr("Audio"));
    }
    p.fillRect(QRect(0, 0, kHeader - 4, kRuler), pal.color(QPalette::Window));

    if (m_timeline->isEmpty()) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRect(kHeader, trackTop(0), width() - kHeader, trackTop(1) + trackHeight(1) - trackTop(0)),
                   Qt::AlignCenter, tr("Drag files here from the Files panel, or double-click a file and press \"Add to timeline\"."));
    }

    // Insertion marker for move / drop.
    if (m_dropIndex >= 0) {
        const auto& clips = tracks[0].clips;
        const Ticks t = m_dropIndex < static_cast<int>(clips.size()) ? clips[m_dropIndex].timelineStart
                                                                      : m_timeline->duration();
        const double x = xOf(t);
        p.setPen(QPen(QColor(255, 200, 60), 3));
        p.drawLine(QPointF(x, trackTop(0) - 2), QPointF(x, trackTop(1) + trackHeight(1) + 2));
    }

    // Playhead.
    const double px = xOf(m_playhead);
    if (px >= kHeader) {
        p.setPen(QPen(kPlayhead, 1.5));
        p.drawLine(QPointF(px, kRuler - 6), QPointF(px, height()));
        QPolygonF head{QPointF(px - 6, 2), QPointF(px + 6, 2), QPointF(px + 6, kRuler - 10), QPointF(px, kRuler - 4),
                       QPointF(px - 6, kRuler - 10)};
        p.setBrush(kPlayhead);
        p.setPen(Qt::NoPen);
        p.drawPolygon(head);
    }
}

void TimelineView::mousePressEvent(QMouseEvent* e) {
    setFocus();
    const QPoint pos = e->position().toPoint();
    m_pressPos = pos;
    m_dragStarted = false;
    if (e->button() != Qt::LeftButton)
        return;
    if (pos.y() < kRuler || pos.x() < kHeader) {
        m_drag = Drag::Playhead;
        emit seekRequested(snapTime(timeAt(std::max(pos.x(), kHeader))));
        return;
    }
    const Hit h = hitTest(pos);
    if (h.clip.isNull()) {
        clearSelection();
        m_drag = Drag::Playhead;
        emit seekRequested(snapTime(timeAt(pos.x())));
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {
        if (m_selection.contains(h.clip))
            m_selection.remove(h.clip);
        else
            m_selection.insert(h.clip);
        emit selectionChanged();
    } else if (!m_selection.contains(h.clip)) {
        m_selection = {h.clip};
        emit selectionChanged();
    }
    update();
    m_dragClip = h.clip;
    m_dragBefore = m_timeline->state();
    const Clip* c = m_timeline->clip(h.clip);
    if (h.edge == Hit::In) {
        m_drag = Drag::TrimIn;
        m_dragSourceStart = c->sourceIn;
    } else if (h.edge == Hit::Out) {
        m_drag = Drag::TrimOut;
        m_dragSourceStart = c->sourceOut;
    } else {
        m_drag = h.track == 0 ? Drag::Move : Drag::None;
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    if (m_drag == Drag::None) {
        const Hit h = hitTest(pos);
        setCursor(h.edge != Hit::None ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    if (!m_dragStarted && (pos - m_pressPos).manhattanLength() < 4 && m_drag != Drag::Playhead)
        return;
    m_dragStarted = true;
    switch (m_drag) {
    case Drag::Playhead:
        emit seekRequested(snapTime(timeAt(std::max(pos.x(), kHeader))));
        break;
    case Drag::Move:
        m_dropIndex = insertionIndexAt(pos.x());
        update();
        break;
    case Drag::TrimIn:
    case Drag::TrimOut: {
        const Clip* c = m_timeline->clip(m_dragClip);
        auto media = c ? m_pool->find(c->mediaId) : nullptr;
        if (!c || !media)
            break;
        const Ticks delta = snapToFrame(ticksFromSeconds((pos.x() - m_pressPos.x()) / m_pps), m_timeline->frameRate());
        if (m_drag == Drag::TrimIn)
            m_timeline->trimIn(m_dragClip, m_dragSourceStart + delta);
        else
            m_timeline->trimOut(m_dragClip, m_dragSourceStart + delta, media->info.duration);
        break;
    }
    default:
        break;
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) {
    if (m_drag == Drag::Move && m_dragStarted && m_dropIndex >= 0) {
        if (auto loc = m_timeline->locate(m_dragClip)) {
            int target = m_dropIndex > loc->index ? m_dropIndex - 1 : m_dropIndex;
            runTimelineEdit(m_undo, m_timeline, tr("Move clip"), [&] { return m_timeline->moveClip(m_dragClip, target); });
        }
    } else if ((m_drag == Drag::TrimIn || m_drag == Drag::TrimOut) && m_dragStarted) {
        pushTimelineSnapshot(m_undo, m_timeline, m_dragBefore, tr("Trim clip"));
    }
    m_drag = Drag::None;
    m_dropIndex = -1;
    update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent* e) {
    const Hit h = hitTest(e->position().toPoint());
    if (!h.clip.isNull())
        emit clipDoubleClicked(h.clip);
}

void TimelineView::wheelEvent(QWheelEvent* e) {
    const int dy = e->angleDelta().y();
    const int dx = e->angleDelta().x();
    if (e->modifiers() & Qt::ControlModifier) {
        const double factor = std::pow(1.0015, dy);
        setPixelsPerSecond(m_pps * factor, static_cast<int>(e->position().x()));
    } else {
        setScrollX(m_scrollX - (dx != 0 ? dx : dy));
    }
    e->accept();
}

void TimelineView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kMediaMimeType) || e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void TimelineView::dragMoveEvent(QDragMoveEvent* e) {
    m_dropIndex = insertionIndexAt(e->position().x());
    update();
    e->acceptProposedAction();
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent*) {
    m_dropIndex = -1;
    update();
}

void TimelineView::dropEvent(QDropEvent* e) {
    const auto& clips = m_timeline->tracks()[0].clips;
    const int idx = insertionIndexAt(e->position().x());
    // Insert at the boundary itself (Timeline::insertMedia snaps to the
    // nearest boundary).
    const Ticks at = idx < static_cast<int>(clips.size()) ? clips[idx].timelineStart : m_timeline->duration();
    m_dropIndex = -1;
    update();
    if (e->mimeData()->hasFormat(kMediaMimeType)) {
        emit mediaDropped(decodeMediaIds(e->mimeData()), at);
    } else {
        QStringList paths;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile())
                paths << u.toLocalFile();
        emit filesDropped(paths, at);
    }
    e->acceptProposedAction();
}

void TimelineView::contextMenuEvent(QContextMenuEvent* e) {
    const Hit h = hitTest(e->pos());
    if (h.clip.isNull())
        return;
    if (!m_selection.contains(h.clip))
        selectOnly(h.clip);
    emit contextMenuForClip(h.clip, e->globalPos());
}

void TimelineView::resizeEvent(QResizeEvent*) { emit scrollChanged(); }

} // namespace lumora

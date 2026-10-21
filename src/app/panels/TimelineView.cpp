#include "TimelineView.h"

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QUndoStack>
#include <cmath>

#include "MediaCache.h"
#include "Theme.h"
#include "media/MediaPool.h"
#include "panels/MimeTypes.h"
#include "undo/TimelineCommands.h"

namespace lumora {

namespace {
QColor withAlpha(QColor c, double a) {
    c.setAlphaF(a);
    return c;
}
constexpr int kEdgeGrab = 6;
constexpr int kSnapPx = 8;
constexpr int kButton = 20;
constexpr Ticks kNoMediaLimit = std::numeric_limits<Ticks>::max() / 4;

// Small vector glyphs for the track header buttons.
void drawGlyph(QPainter& p, const QRectF& r, bool video, bool lock, bool active, const QColor& fg) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = r.center();
    QPen pen(fg, 1.4);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (lock) {
        const QRectF body(c.x() - 5, c.y() - 1, 10, 7);
        p.setBrush(active ? fg : Qt::transparent);
        p.drawRoundedRect(body, 1.5, 1.5);
        p.setBrush(Qt::NoBrush);
        QPainterPath shackle;
        const double lift = active ? 0 : 2.5;
        shackle.moveTo(c.x() - 3.2, c.y() - 1 - lift);
        shackle.arcTo(QRectF(c.x() - 3.2, c.y() - 7 - lift, 6.4, 8), 180, -180);
        if (active)
            shackle.lineTo(c.x() + 3.2, c.y() - 1);
        p.drawPath(shackle);
    } else if (video) {
        QPainterPath eye;
        eye.moveTo(c.x() - 7, c.y());
        eye.quadTo(c.x(), c.y() - 7, c.x() + 7, c.y());
        eye.quadTo(c.x(), c.y() + 7, c.x() - 7, c.y());
        p.drawPath(eye);
        p.setBrush(fg);
        p.drawEllipse(c, 2.2, 2.2);
        if (!active) // hidden
            p.drawLine(QPointF(c.x() - 7, c.y() + 6), QPointF(c.x() + 7, c.y() - 6));
    } else {
        QPolygonF speaker{QPointF(c.x() - 7, c.y() - 2.5), QPointF(c.x() - 4, c.y() - 2.5), QPointF(c.x(), c.y() - 6),
                          QPointF(c.x(), c.y() + 6), QPointF(c.x() - 4, c.y() + 2.5), QPointF(c.x() - 7, c.y() + 2.5)};
        p.setBrush(fg);
        p.drawPolygon(speaker);
        p.setBrush(Qt::NoBrush);
        if (active) {
            p.drawArc(QRectF(c.x() - 2, c.y() - 4, 6, 8), -60 * 16, 120 * 16);
            p.drawArc(QRectF(c.x() - 2, c.y() - 7, 10, 14), -55 * 16, 110 * 16);
        } else { // muted
            p.drawLine(QPointF(c.x() + 2, c.y() - 3), QPointF(c.x() + 7, c.y() + 3));
            p.drawLine(QPointF(c.x() + 2, c.y() + 3), QPointF(c.x() + 7, c.y() - 3));
        }
    }
    p.restore();
}
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
        setScrollY(m_scrollY);
        emit scrollChanged();
        update();
    });
    connect(m_cache, &MediaCache::thumbnailReady, this, qOverload<>(&QWidget::update));
    connect(m_cache, &MediaCache::peaksReady, this, qOverload<>(&QWidget::update));
    connect(m_cache, &MediaCache::keyframesReady, this, qOverload<>(&QWidget::update));
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

std::vector<TimelineView::Row> TimelineView::rows() const {
    std::vector<Row> out;
    const TimelineState& s = m_timeline->state();
    int y = kRuler + 4 - m_scrollY;
    const auto subs = s.trackIndices(TrackType::Subtitle);
    for (auto it = subs.rbegin(); it != subs.rend(); ++it) {
        out.push_back({*it, y, 36});
        y += 38;
    }
    if (!subs.empty())
        y += 4;
    const auto video = s.trackIndices(TrackType::Video);
    for (auto it = video.rbegin(); it != video.rend(); ++it) {
        const int h = *it == 0 ? 66 : 48;
        out.push_back({*it, y, h});
        y += h + 2;
    }
    y += 6; // gap between picture and sound
    for (int ti : s.trackIndices(TrackType::Audio)) {
        out.push_back({ti, y, 48});
        y += 50;
    }
    return out;
}

std::optional<TimelineView::Row> TimelineView::rowOf(int track) const {
    for (const auto& r : rows())
        if (r.track == track)
            return r;
    return std::nullopt;
}

int TimelineView::trackAtY(int y) const {
    for (const auto& r : rows())
        if (y >= r.top - 1 && y < r.bottom() + 1)
            return r.track;
    return -1;
}

int TimelineView::contentHeight() const {
    const auto r = rows();
    return r.empty() ? 0 : r.back().bottom() + m_scrollY + 10;
}

void TimelineView::setScrollY(int y) {
    y = std::clamp(y, 0, std::max(0, contentHeight() - height()));
    if (y == m_scrollY)
        return;
    m_scrollY = y;
    update();
    emit scrollChanged();
}

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
    std::vector<QUuid> ignore{ignoreClip};
    if (!ignoreClip.isNull())
        ignore = m_timeline->linkedClips(ignoreClip);
    for (const auto& tr : m_timeline->tracks())
        for (const auto& c : tr.clips) {
            if (std::find(ignore.begin(), ignore.end(), c.id) != ignore.end())
                continue;
            consider(c.timelineStart);
            consider(c.timelineEnd());
        }
    consider(0);
    consider(m_playhead);
    if (bestDist <= threshold)
        return best;
    return snapToFrame(t, m_timeline->frameRate());
}

TimelineView::Hit TimelineView::hitTest(QPoint pos) const {
    Hit h;
    if (pos.x() < kHeader)
        return h;
    for (const Row& row : rows()) {
        if (pos.y() < row.top || pos.y() >= row.bottom())
            continue;
        h.track = row.track;
        for (const auto& c : m_timeline->tracks()[row.track].clips) {
            const double x0 = xOf(c.timelineStart), x1 = xOf(c.timelineEnd());
            if (pos.x() >= x0 - 1 && pos.x() <= x1 + 1) {
                h.clip = c.id;
                if (pos.x() - x0 <= kEdgeGrab && x1 - x0 > 3 * kEdgeGrab)
                    h.edge = Hit::In;
                else if (x1 - pos.x() <= kEdgeGrab && x1 - x0 > 3 * kEdgeGrab)
                    h.edge = Hit::Out;
                return h;
            }
        }
        return h;
    }
    return h;
}

QRect TimelineView::headerButtonRect(const Row& row, HeaderButton b) const {
    const int y = row.top + (row.height - kButton) / 2 + 4;
    const int x = kHeader - 8 - (b == HeaderButton::Lock ? kButton : 2 * kButton + 4);
    return QRect(x, y, kButton, kButton);
}

TimelineView::HeaderButton TimelineView::headerButtonAt(QPoint pos, int* track) const {
    for (const Row& row : rows())
        for (HeaderButton b : {HeaderButton::Visibility, HeaderButton::Lock})
            if (headerButtonRect(row, b).contains(pos)) {
                *track = row.track;
                return b;
            }
    return HeaderButton::None;
}

int TimelineView::insertionIndexAt(double x) const {
    const auto& clips = m_timeline->tracks()[0].clips;
    const Ticks t = timeAt(x);
    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
        if (t < clips[i].timelineStart + clips[i].duration() / 2)
            return i;
    return static_cast<int>(clips.size());
}

Ticks TimelineView::storylineBoundary(int index) const {
    const auto& clips = m_timeline->tracks()[0].clips;
    if (index >= 0 && index < static_cast<int>(clips.size()))
        return clips[index].timelineStart;
    return clips.empty() ? 0 : clips.back().timelineEnd();
}

bool TimelineView::isSelected(const Clip& c) const {
    if (m_selection.contains(c.id))
        return true;
    if (c.linkId.isNull())
        return false;
    for (const auto& id : m_selection)
        if (const Clip* s = m_timeline->clip(id); s && s->linkId == c.linkId)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void TimelineView::paintRuler(QPainter& p) {
    const ThemeColors& tc = themeColors();
    p.fillRect(QRect(0, 0, width(), kRuler), tc.panel);
    p.fillRect(QRect(kHeader, kRuler - 1, width() - kHeader, 1), tc.border);
    // Pick a tick step of at least ~80 px.
    static const double steps[] = {1 / 25.0, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    double step = steps[std::size(steps) - 1];
    for (double s : steps)
        if (s * m_pps >= 80) {
            step = s;
            break;
        }
    const double firstSec = std::floor((m_scrollX / m_pps) / step) * step;
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.82);
    p.setFont(f);
    for (double s = firstSec;; s += step) {
        const double x = kHeader + s * m_pps - m_scrollX;
        if (x > width())
            break;
        if (x < kHeader)
            continue;
        p.setPen(tc.textFaint);
        p.drawLine(QPointF(x, kRuler - 9), QPointF(x, kRuler - 1));
        const Ticks t = ticksFromSeconds(s);
        const QString label = step < 1 ? formatTimecode(t, m_timeline->frameRate())
                                       : formatTimecode(t, m_timeline->frameRate()).left(8);
        p.setPen(tc.textDim);
        p.drawText(QPointF(x + 4, kRuler - 11), label);
        p.setPen(withAlpha(tc.textFaint, 0.6));
        for (int i = 1; i < 5; ++i) {
            const double sx = x + i * step * m_pps / 5;
            p.drawLine(QPointF(sx, kRuler - 4), QPointF(sx, kRuler - 1));
        }
    }
}

void TimelineView::paintHeader(QPainter& p, const Row& row) {
    const QPalette pal = palette();
    const ThemeColors& tc = themeColors();
    const Track& t = m_timeline->tracks()[row.track];
    const QRectF header(4, row.top + 2, kHeader - 12, row.height - 4);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(tc.raised);
    p.drawRoundedRect(header, 7, 7);
    // Colour pill: indigo for picture, teal for sound.
    p.setBrush(t.type == TrackType::Video ? tc.videoClip : t.type == TrackType::Subtitle ? tc.accentAlt : tc.audioClip);
    p.drawRoundedRect(QRectF(header.left() + 5, header.top() + 6, 3, header.height() - 12), 1.5, 1.5);
    p.restore();
    p.setPen(pal.color(QPalette::WindowText));
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    const bool compact = row.height < 44;
    p.drawText(QRect(16, row.top + (compact ? 2 : 6), kHeader - 76, compact ? 16 : 18), Qt::AlignVCenter | Qt::AlignLeft, t.name);
    p.setFont(font());
    p.setPen(pal.color(QPalette::PlaceholderText));
    QString kind = t.type == TrackType::Video ? (row.track == 0 ? tr("Main") : tr("Video"))
                   : t.type == TrackType::Subtitle
                       ? (t.language.isEmpty() ? tr("Subtitles") : t.language.toUpper())
                       : tr("Audio");
    p.drawText(QRect(16, row.top + (compact ? 17 : 23), kHeader - 76, compact ? 16 : 18), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(kind, Qt::ElideRight, kHeader - 76));
    const QColor fg = pal.color(QPalette::WindowText);
    const QColor off = pal.color(QPalette::PlaceholderText);
    const bool video = t.type != TrackType::Audio; // picture and subtitles are shown / hidden
    const bool visible = video ? !t.hidden : !t.muted;
    const QRect vis = headerButtonRect(row, HeaderButton::Visibility);
    const QRect lock = headerButtonRect(row, HeaderButton::Lock);
    p.setPen(Qt::NoPen);
    p.setBrush(visible ? Qt::transparent : withAlpha(tc.danger, 0.22));
    p.drawRoundedRect(vis, 6, 6);
    p.setBrush(t.locked ? withAlpha(tc.accent, 0.22) : Qt::transparent);
    p.drawRoundedRect(lock, 6, 6);
    drawGlyph(p, vis, video, false, visible, visible ? fg : tc.danger);
    drawGlyph(p, lock, video, true, t.locked, t.locked ? tc.accent : off);
}

void TimelineView::paintClip(QPainter& p, const Clip& c, int track, const QRect& r) {
    const Track& t = m_timeline->tracks()[track];
    const bool video = t.type == TrackType::Video;
    const bool title = c.kind == ClipKind::Title;
    const bool subtitle = c.kind == ClipKind::Subtitle;
    const bool freeze = c.kind == ClipKind::Freeze;
    const bool selected = isSelected(c);
    const ThemeColors& tc = themeColors();
    const QColor base = subtitle ? tc.accentAlt : title ? tc.titleClip : freeze ? tc.accentCool : video ? tc.videoClip : tc.audioClip;
    auto media = c.hasMedia() ? m_pool->find(c.mediaId) : nullptr;
    QPainterPath path;
    path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    p.save();
    if (t.hidden || t.muted)
        p.setOpacity(0.45);
    p.setClipPath(path);
    QLinearGradient body(r.topLeft(), r.bottomLeft());
    const bool dark = currentTheme() == ThemeKind::Dark;
    body.setColorAt(0, base.darker(dark ? 170 : 115));
    body.setColorAt(1, base.darker(dark ? 230 : 140));
    p.fillRect(r, body);
    if (subtitle) {
        // Just the words: subtitle rows are short.
        p.setPen(base.lighter(170));
        const QString text = c.text.simplified();
        p.drawText(r.adjusted(7, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(text.isEmpty() ? tr("(empty subtitle)") : text, Qt::ElideRight, r.width() - 12));
        p.restore();
        p.setBrush(Qt::NoBrush);
        if (selected) {
            p.setPen(QPen(withAlpha(tc.accent, 0.35), 5));
            p.drawPath(path);
            p.setPen(QPen(tc.accent, 2));
        } else {
            p.setPen(QPen(withAlpha(base.lighter(140), 0.7), 1));
        }
        p.drawPath(path);
        return;
    }
    if (title) {
        QFont f = font();
        f.setBold(c.title.bold);
        f.setItalic(c.title.italic);
        p.setFont(f);
        p.setPen(base.lighter(160));
        const QString text = c.title.text.simplified();
        p.drawText(r.adjusted(8, 17, -6, -2), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(text, Qt::ElideRight, r.width() - 14));
    } else if (video) {
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
        if (track == 0 && !freeze && media && media->hasKeyframes()) {
            const auto kf = media->keyframesCopy();
            const double spacing = kf.size() > 1 ? ticksToSeconds(kf[1] - kf[0]) * m_pps : 100;
            if (spacing > 6) {
                p.setPen(Qt::NoPen);
                p.setBrush(tc.accent);
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
        p.setBrush(base.lighter(135));
        const int mid = r.top() + r.height() / 2 + 6;
        const double gain = std::pow(10.0, c.gainDb / 20.0);
        const double halfH = (r.height() - 20) / 2.0;
        const int x0 = std::max(r.left(), 0), x1 = std::min(r.right(), width());
        QPainterPath wave;
        wave.moveTo(x0, mid);
        std::vector<QPointF> lower;
        for (int x = x0; x <= x1; x += 2) {
            const Ticks t0 = c.toSource(timeAt(x)), t1 = c.toSource(timeAt(x + 2));
            const size_t a = static_cast<size_t>(std::max<Ticks>(0, t0) * MediaCache::kPeaksPerSecond / kTicksPerSecond);
            const size_t b = std::max(
                a + 1, static_cast<size_t>(std::max<Ticks>(0, t1) * MediaCache::kPeaksPerSecond / kTicksPerSecond));
            float peak = 0;
            for (size_t i = a; i < b && i < peaks->size(); ++i)
                peak = std::max(peak, (*peaks)[i]);
            const double h = std::min(1.0, std::sqrt(peak * gain)) * halfH;
            wave.lineTo(x, mid - h);
            lower.emplace_back(x, mid + h);
        }
        for (auto it = lower.rbegin(); it != lower.rend(); ++it)
            wave.lineTo(*it);
        wave.closeSubpath();
        p.drawPath(wave);
    }
    // Fades: a shaded ramp at each end.
    auto fade = [&](Ticks len, bool atStart) {
        if (len <= 0)
            return;
        const double w = std::min<double>(r.width(), ticksToSeconds(len) * m_pps);
        const double top = r.top() + 16, bottom = r.bottom();
        const double x0 = atStart ? r.left() : r.right() - w, x1 = atStart ? r.left() + w : r.right();
        QPolygonF shade = atStart ? QPolygonF{QPointF(x0, top), QPointF(x1, top), QPointF(x0, bottom)}
                                  : QPolygonF{QPointF(x0, top), QPointF(x1, top), QPointF(x1, bottom)};
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        p.drawPolygon(shade);
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawLine(atStart ? QPointF(x0, bottom) : QPointF(x0, top), atStart ? QPointF(x1, top) : QPointF(x1, bottom));
    };
    fade(c.fadeIn, true);
    fade(c.fadeOut, false);
    // Name strip.
    QLinearGradient strip(r.topLeft(), r.topRight());
    strip.setColorAt(0, base);
    strip.setColorAt(1, base.darker(125));
    p.fillRect(QRect(r.left(), r.top(), r.width(), 16), strip);
    p.setPen(Qt::white);
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.82);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    QString name = title ? tr("Title") : media ? QFileInfo(media->path).fileName() : tr("Missing media");
    if (freeze)
        name = tr("Freeze frame") + QStringLiteral("  ·  ") + name;
    name += QStringLiteral("  ·  ") + formatSeconds(c.duration());
    if (!video && c.gainDb != 0.0)
        name += QStringLiteral("  ·  %1%2 dB").arg(c.gainDb > 0 ? QStringLiteral("+") : QString()).arg(c.gainDb, 0, 'f', 1);
    if (!c.transform.isIdentity())
        name += QStringLiteral("  ·  ⤢");
    p.drawText(QRect(r.left() + 5, r.top(), r.width() - 10, 16), Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(name, Qt::ElideRight, r.width() - 10));
    p.restore();
    p.setBrush(Qt::NoBrush);
    if (selected) {
        // A soft glow under a crisp amber outline.
        p.setPen(QPen(withAlpha(tc.accent, 0.35), 5));
        p.drawPath(path);
        p.setPen(QPen(tc.accent, 2));
    } else {
        p.setPen(QPen(withAlpha(base.lighter(140), 0.7), 1));
    }
    p.drawPath(path);
}

void TimelineView::paintTransitions(QPainter& p, const Row& row) {
    const auto& clips = m_timeline->tracks()[0].clips;
    auto dur = [this](const QUuid& id) {
        auto m = m_pool->find(id);
        return m ? m->info.duration : Ticks(0);
    };
    for (int k = 1; k < static_cast<int>(clips.size()); ++k) {
        const Clip& c = clips[k];
        if (c.transitionIn.isNone())
            continue;
        const Ticks d = m_timeline->state().transitionDuration(k, dur);
        const double cx = xOf(c.timelineStart);
        const double half = std::max(6.0, ticksToSeconds(d) * m_pps / 2);
        const QRectF box(cx - half, row.top + row.height / 2.0 - 7, 2 * half, 18);
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        const bool selected = m_selection.contains(c.id);
        const ThemeColors& tc = themeColors();
        p.setBrush(d > 0 ? tc.accentCool : tc.danger);
        p.setPen(QPen(selected ? tc.accent : tc.canvas, selected ? 2 : 1));
        p.drawRoundedRect(box, 6, 6);
        // Bow tie: one picture fading out while the next fades in.
        p.setPen(QPen(withAlpha(Qt::white, 0.85), 1.2));
        p.drawLine(box.topLeft() + QPointF(3, 3), box.bottomRight() - QPointF(3, 3));
        p.drawLine(box.bottomLeft() + QPointF(3, -3), box.topRight() + QPointF(-3, 3));
        p.restore();
    }
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const ThemeColors& tc = themeColors();
    p.fillRect(rect(), tc.panel);

    const auto& tracks = m_timeline->tracks();
    const auto allRows = rows();
    const QRect lanes(kHeader, kRuler, width() - kHeader, height() - kRuler);
    for (const Row& row : allRows) {
        const int ti = row.track;
        const QRect lane(kHeader, row.top, width() - kHeader, row.height);
        if (lane.bottom() < kRuler || lane.top() > height())
            continue;
        p.save();
        p.setClipRect(lanes);
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(tc.raised, 0.55));
        p.drawRoundedRect(QRectF(lane).adjusted(-8, 1, 0, -1), 8, 8);
        if (tracks[ti].locked) {
            p.fillRect(lane, QBrush(withAlpha(tc.text, 0.06), Qt::BDiagPattern));
        }
        p.setClipRect(lane.adjusted(0, -2, 0, 2).intersected(lanes));
        for (const auto& c : tracks[ti].clips) {
            QRect r(QPoint(static_cast<int>(xOf(c.timelineStart)), lane.top() + 2),
                    QPoint(static_cast<int>(xOf(c.timelineEnd())) - 1, lane.bottom() - 2));
            if (r.right() < kHeader || r.left() > width())
                continue;
            paintClip(p, c, ti, r);
        }
        if (ti == 0)
            paintTransitions(p, row);
        p.restore();
    }

    // Connections of upper video clips to the storyline.
    if (auto v1 = rowOf(0)) {
        const int v1Top = v1->top;
        p.save();
        p.setClipRect(lanes);
        p.setPen(QPen(withAlpha(tc.text, 0.3), 1, Qt::DotLine));
        for (const Row& row : allRows) {
            if (row.track == 0 || tracks[row.track].type != TrackType::Video)
                continue;
            for (const auto& c : tracks[row.track].clips)
                if (!c.anchorId.isNull()) {
                    const double x = xOf(c.timelineStart) + 3;
                    p.drawLine(QPointF(x, row.bottom() - 2), QPointF(x, v1Top + 2));
                }
        }
        p.restore();
    }

    if (m_timeline->isEmpty()) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        if (auto v1 = rowOf(0))
            p.drawText(QRect(kHeader, v1->top, width() - kHeader, v1->height + 52), Qt::AlignCenter,
                       tr("Drag files here from the Files panel, or double-click a file and press \"Add to timeline\"."));
    }

    // Insertion marker / drop ghost.
    if (m_dropIndex >= 0) {
        if (auto v1 = rowOf(0)) {
            const double x = xOf(storylineBoundary(m_dropIndex));
            auto a1 = rowOf(1);
            p.setPen(QPen(tc.accent, 3));
            p.drawLine(QPointF(x, v1->top - 2), QPointF(x, (a1 ? a1->bottom() : v1->bottom()) + 2));
        }
    } else if (m_dropTrack >= 0) {
        if (auto row = rowOf(m_dropTrack)) {
            const QRectF ghost(QPointF(xOf(m_dropTime), row->top + 2), QPointF(xOf(m_dropTime + m_dropLength), row->bottom() - 2));
            p.setPen(QPen(tc.accent, 1.5, Qt::DashLine));
            p.setBrush(withAlpha(tc.accent, 0.2));
            p.drawRoundedRect(ghost, 7, 7);
        }
    }

    // Headers on top of everything that scrolls horizontally.
    p.save();
    p.setClipRect(QRect(0, kRuler, kHeader, height() - kRuler));
    p.fillRect(QRect(0, kRuler, kHeader - 4, height() - kRuler), tc.panel);
    for (const Row& row : allRows)
        paintHeader(p, row);
    p.restore();

    paintRuler(p);
    p.fillRect(QRect(0, 0, kHeader - 4, kRuler), tc.panel);

    // Playhead.
    const double px = xOf(m_playhead);
    if (px >= kHeader) {
        // A beam of light: a soft glow behind a thin line, with a rounded cap.
        p.setPen(QPen(withAlpha(tc.playhead, 0.18), 7));
        p.drawLine(QPointF(px, kRuler), QPointF(px, height()));
        p.setPen(QPen(tc.playhead, 1.5));
        p.drawLine(QPointF(px, kRuler - 6), QPointF(px, height()));
        QPainterPath head;
        head.addRoundedRect(QRectF(px - 6, 3, 12, kRuler - 12), 4, 4);
        head.addPolygon(QPolygonF{QPointF(px - 4, kRuler - 10), QPointF(px + 4, kRuler - 10), QPointF(px, kRuler - 4)});
        p.setPen(Qt::NoPen);
        p.fillPath(head.simplified(), tc.playhead);
    }
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void TimelineView::mousePressEvent(QMouseEvent* e) {
    setFocus();
    const QPoint pos = e->position().toPoint();
    m_pressPos = pos;
    m_dragStarted = false;
    if (e->button() != Qt::LeftButton)
        return;
    if (pos.y() < kRuler) {
        m_drag = Drag::Playhead;
        emit seekRequested(snapTime(timeAt(std::max(pos.x(), kHeader))));
        return;
    }
    if (pos.x() < kHeader) {
        int track = -1;
        const HeaderButton b = headerButtonAt(pos, &track);
        if (b != HeaderButton::None) {
            const Track& t = m_timeline->tracks()[track];
            const bool video = t.type != TrackType::Audio;
            bool locked = t.locked, muted = t.muted, hidden = t.hidden;
            if (b == HeaderButton::Lock)
                locked = !locked;
            else if (video)
                hidden = !hidden;
            else
                muted = !muted;
            const QString text = b == HeaderButton::Lock ? (locked ? tr("Lock track") : tr("Unlock track"))
                                 : video                 ? (hidden ? tr("Hide track") : tr("Show track"))
                                                         : (muted ? tr("Mute track") : tr("Unmute track"));
            runTimelineEdit(m_undo, m_timeline, text,
                            [&] { return m_timeline->setTrackFlags(track, locked, muted, hidden); });
        }
        return;
    }
    // Transition markers select the clip they lead into.
    if (auto v1 = rowOf(0); v1 && pos.y() >= v1->top && pos.y() < v1->bottom()) {
        const auto& clips = m_timeline->tracks()[0].clips;
        for (int k = 1; k < static_cast<int>(clips.size()); ++k) {
            if (clips[k].transitionIn.isNone())
                continue;
            const double cx = xOf(clips[k].timelineStart);
            const double half = std::max(6.0, ticksToSeconds(clips[k].transitionIn.duration) * m_pps / 2);
            if (std::abs(pos.x() - cx) <= half && std::abs(pos.y() - (v1->top + v1->height / 2.0 + 2)) <= 10) {
                selectOnly(clips[k].id);
                m_drag = Drag::None;
                return;
            }
        }
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
    m_drag = Drag::None;
    if (m_timeline->tracks()[h.track].locked)
        return;
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
        // Linked sound moves with its picture: drag the video clip.
        if (m_timeline->tracks()[h.track].type == TrackType::Audio && !c->linkId.isNull())
            for (const auto& id : m_timeline->linkedClips(h.clip))
                if (auto loc = m_timeline->locate(id); loc && m_timeline->tracks()[loc->track].type == TrackType::Video) {
                    m_dragClip = id;
                    c = m_timeline->clip(id);
                }
        m_drag = Drag::Move;
        m_grabOffset = timeAt(pos.x()) - c->timelineStart;
    }
}

void TimelineView::updateDropTarget(QPoint pos, TrackType type, Ticks length, Ticks grabOffset, const QUuid& ignore) {
    int track = trackAtY(pos.y());
    const bool anyType = ignore.isNull(); // drops from the bin go anywhere
    if (track >= 0 && !anyType && m_timeline->tracks()[track].type != type)
        track = -1;
    if (track < 0) {
        if (m_dropTrack < 0 && m_dropIndex < 0) {
            if (auto loc = m_timeline->locate(ignore))
                track = loc->track;
            else
                track = 0;
        } else {
            track = m_dropIndex >= 0 ? 0 : m_dropTrack;
        }
    }
    m_dropLength = length;
    if (track == 0) {
        m_dropIndex = insertionIndexAt(pos.x());
        m_dropTrack = 0;
    } else {
        m_dropIndex = -1;
        m_dropTrack = track;
        const Ticks raw = std::max<Ticks>(0, timeAt(pos.x()) - grabOffset);
        const Ticks byStart = snapTime(raw, ignore);
        const Ticks byEnd = snapTime(raw + length, ignore) - length;
        m_dropTime = std::max<Ticks>(0, std::abs(byEnd - raw) < std::abs(byStart - raw) ? byEnd : byStart);
    }
    update();
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
    case Drag::Move: {
        const Clip* c = m_timeline->clip(m_dragClip);
        auto loc = m_timeline->locate(m_dragClip);
        if (c && loc)
            updateDropTarget(pos, m_timeline->tracks()[loc->track].type, c->duration(), m_grabOffset, m_dragClip);
        break;
    }
    case Drag::TrimIn:
    case Drag::TrimOut: {
        const Clip* c = m_timeline->clip(m_dragClip);
        if (!c)
            break;
        auto media = m_pool->find(c->mediaId);
        const Ticks limit = c->kind != ClipKind::Media ? kNoMediaLimit : media ? media->info.duration : c->sourceOut;
        const Ticks delta = snapToFrame(ticksFromSeconds((pos.x() - m_pressPos.x()) / m_pps), m_timeline->frameRate());
        if (m_drag == Drag::TrimIn)
            m_timeline->trimIn(m_dragClip, m_dragSourceStart + delta);
        else
            m_timeline->trimOut(m_dragClip, m_dragSourceStart + delta, limit);
        break;
    }
    default:
        break;
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) {
    if (m_drag == Drag::Move && m_dragStarted && (m_dropIndex >= 0 || m_dropTrack >= 0)) {
        auto loc = m_timeline->locate(m_dragClip);
        const bool targetLocked = m_dropTrack >= 0 && m_timeline->tracks()[m_dropTrack].locked;
        if (loc && !targetLocked) {
            if (m_dropIndex >= 0 && loc->track == 0) {
                const int target = m_dropIndex > loc->index ? m_dropIndex - 1 : m_dropIndex;
                runTimelineEdit(m_undo, m_timeline, tr("Move clip"), [&] { return m_timeline->moveClip(m_dragClip, target); });
            } else if (m_dropIndex >= 0) {
                const Ticks at = storylineBoundary(m_dropIndex);
                runTimelineEdit(m_undo, m_timeline, tr("Move clip to the storyline"),
                                [&] { return m_timeline->moveClipTo(m_dragClip, 0, at); });
            } else {
                runTimelineEdit(m_undo, m_timeline, tr("Move clip"),
                                [&] { return m_timeline->moveClipTo(m_dragClip, m_dropTrack, m_dropTime); });
            }
        }
    } else if ((m_drag == Drag::TrimIn || m_drag == Drag::TrimOut) && m_dragStarted) {
        pushTimelineSnapshot(m_undo, m_timeline, m_dragBefore, tr("Trim clip"));
    }
    m_drag = Drag::None;
    m_dropIndex = -1;
    m_dropTrack = -1;
    update();
}

void TimelineView::mouseDoubleClickEvent(QMouseEvent* e) {
    const Hit h = hitTest(e->position().toPoint());
    if (!h.clip.isNull())
        emit clipDoubleClicked(h.clip);
    else if (h.track >= 0 && m_timeline->tracks()[h.track].type == TrackType::Video && h.track != 0)
        emit addTitleRequested(h.track, snapTime(timeAt(e->position().x())));
    else if (h.track >= 0 && m_timeline->tracks()[h.track].isSubtitle())
        emit addSubtitleRequested(h.track, snapTime(timeAt(e->position().x())));
}

void TimelineView::wheelEvent(QWheelEvent* e) {
    const int dy = e->angleDelta().y();
    const int dx = e->angleDelta().x();
    if (e->modifiers() & Qt::ControlModifier) {
        const double factor = std::pow(1.0015, dy);
        setPixelsPerSecond(m_pps * factor, static_cast<int>(e->position().x()));
    } else if (dx == 0 && !(e->modifiers() & Qt::ShiftModifier) && contentHeight() > height()) {
        setScrollY(m_scrollY - dy / 2);
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
    Ticks length = ticksFromSeconds(5);
    if (e->mimeData()->hasFormat(kMediaMimeType)) {
        const auto ids = decodeMediaIds(e->mimeData());
        if (!ids.isEmpty())
            if (auto m = m_pool->find(ids.front()))
                length = m->info.duration;
    }
    updateDropTarget(e->position().toPoint(), TrackType::Video, length, 0, {});
    e->acceptProposedAction();
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent*) {
    m_dropIndex = -1;
    m_dropTrack = -1;
    update();
}

void TimelineView::dropEvent(QDropEvent* e) {
    updateDropTarget(e->position().toPoint(), TrackType::Video, m_dropLength, 0, {});
    const bool storyline = m_dropIndex >= 0 || m_dropTrack <= 0;
    // On the storyline the drop goes to the boundary itself
    // (Timeline::insertMedia snaps to the nearest boundary).
    const Ticks at = storyline ? storylineBoundary(std::max(0, m_dropIndex)) : m_dropTime;
    const int track = storyline ? -1 : m_dropTrack;
    m_dropIndex = -1;
    m_dropTrack = -1;
    update();
    if (e->mimeData()->hasFormat(kMediaMimeType)) {
        emit mediaDropped(decodeMediaIds(e->mimeData()), at, track);
    } else {
        QStringList paths;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile())
                paths << u.toLocalFile();
        emit filesDropped(paths, at, track);
    }
    e->acceptProposedAction();
}

void TimelineView::showTrackMenu(int track, Ticks at, const QPoint& globalPos) {
    QMenu menu;
    const auto& tracks = m_timeline->tracks();
    if (track >= 0 && tracks[track].type == TrackType::Video)
        menu.addAction(tr("Add title here"), this, [this, track, at] { emit addTitleRequested(track, at); });
    if (track >= 0 && tracks[track].isSubtitle())
        menu.addAction(tr("Add subtitle here"), this, [this, track, at] { emit addSubtitleRequested(track, at); });
    menu.addAction(tr("Add video track"), this, [this] {
        runTimelineEdit(m_undo, m_timeline, tr("Add video track"), [&] { return m_timeline->addTrack(TrackType::Video) >= 0; });
    });
    menu.addAction(tr("Add audio track"), this, [this] {
        runTimelineEdit(m_undo, m_timeline, tr("Add audio track"), [&] { return m_timeline->addTrack(TrackType::Audio) >= 0; });
    });
    menu.addAction(tr("Add subtitle track"), this, [this] {
        runTimelineEdit(m_undo, m_timeline, tr("Add subtitle track"),
                        [&] { return m_timeline->addTrack(TrackType::Subtitle) >= 0; });
    });
    if (track >= 2) {
        menu.addSeparator();
        const QString name = tracks[track].name;
        menu.addAction(tr("Remove track %1").arg(name), this, [this, track, name] {
            runTimelineEdit(m_undo, m_timeline, tr("Remove track %1").arg(name), [&] { return m_timeline->removeTrack(track); });
        });
    }
    menu.exec(globalPos);
}

void TimelineView::contextMenuEvent(QContextMenuEvent* e) {
    if (e->pos().y() < kRuler)
        return;
    const Hit h = hitTest(e->pos());
    if (h.clip.isNull()) {
        showTrackMenu(trackAtY(e->pos().y()), snapTime(timeAt(std::max(e->pos().x(), kHeader))), e->globalPos());
        return;
    }
    if (!m_selection.contains(h.clip))
        selectOnly(h.clip);
    emit contextMenuForClip(h.clip, e->globalPos());
}

void TimelineView::resizeEvent(QResizeEvent*) {
    setScrollY(m_scrollY);
    emit scrollChanged();
}

} // namespace lumora

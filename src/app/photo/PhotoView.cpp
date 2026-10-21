#include "PhotoView.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <functional>

#include "Theme.h"

namespace lumora {

PhotoView::PhotoView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAcceptDrops(true);
    setMinimumSize(320, 240);
}

void PhotoView::setImage(const QImage& img) {
    const bool resized = img.size() != m_image.size();
    m_image = img;
    if (resized)
        rebuildTint();
    update();
}

void PhotoView::setCrop(const QRectF& r) {
    m_crop = r.isNull() ? QRectF(0, 0, 1, 1) : r;
    update();
}

void PhotoView::setCropMode(bool on) {
    m_cropMode = on;
    update();
}

void PhotoView::setAspect(double aspect) {
    m_aspect = aspect;
    if (m_aspect > 0 && !m_image.isNull()) {
        QRectF r = m_crop;
        applyAspect(r, BottomRight);
        if (r != m_crop) {
            m_crop = r;
            emit cropEdited(m_crop);
        }
    }
    update();
}

void PhotoView::setPlaceholder(const QString& text) {
    m_placeholder = text;
    update();
}

void PhotoView::setBadge(const QString& text) {
    m_badge = text;
    update();
}

void PhotoView::setMaskOverlay(const std::optional<LayerMask>& mask) {
    if (mask == m_mask)
        return;
    m_mask = mask;
    if (!m_maskDrag)
        unsetCursor();
    rebuildTint();
    update();
}

void PhotoView::setBrush(const BrushStroke& brush, bool tint) {
    m_brush = brush;
    if (tint != m_tint) {
        m_tint = tint;
        rebuildTint();
    }
    update();
}

bool PhotoView::brushMode() const {
    return !m_cropMode && m_mask && m_mask->shape == MaskShape::Brush && !m_image.isNull();
}

void PhotoView::rebuildTint() {
    m_tintImage = {};
    if (!m_tint || !m_mask || m_mask->shape != MaskShape::Brush || m_image.isNull())
        return;
    // A reduced copy is plenty for an overlay.
    const QSize size = m_image.size().scaled(QSize(720, 720), Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    const std::vector<float> m = m_mask->render(size.width(), size.height());
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    const QColor c = themeColors().accentAlt;
    for (int y = 0; y < size.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const int a = static_cast<int>(m[size_t(y) * size.width() + x] * 120.0f);
            line[x] = qPremultiply(qRgba(c.red(), c.green(), c.blue(), a));
        }
    }
    m_tintImage = img;
}

QPointF PhotoView::toImage(QPointF pos) const {
    const QRectF ir = imageRect();
    return {(pos.x() - ir.x()) / ir.width(), (pos.y() - ir.y()) / ir.height()};
}

void PhotoView::leaveEvent(QEvent*) {
    m_hover.reset();
    update();
}

QRectF PhotoView::maskWidgetRect() const {
    const QRectF ir = imageRect();
    const QRectF& m = m_mask->rect;
    return QRectF(ir.x() + m.x() * ir.width(), ir.y() + m.y() * ir.height(), m.width() * ir.width(),
                  m.height() * ir.height());
}

void PhotoView::paintMask(QPainter& p, const QRectF& ir) const {
    if (!m_mask || m_mask->shape == MaskShape::None || m_cropMode)
        return;
    p.save();
    p.setClipRect(cropWidgetRect());
    if (m_mask->shape == MaskShape::Brush) {
        if (!m_tintImage.isNull())
            p.drawImage(ir, m_tintImage);
        const QColor accent = themeColors().accentAlt;
        if (m_stroke && !m_stroke->points.empty()) {
            QPainterPath path;
            path.moveTo(ir.x() + m_stroke->points[0].x() * ir.width(), ir.y() + m_stroke->points[0].y() * ir.height());
            for (const QPointF& pt : m_stroke->points)
                path.lineTo(ir.x() + pt.x() * ir.width(), ir.y() + pt.y() * ir.height());
            QColor c = m_stroke->erase ? QColor(255, 255, 255) : accent;
            c.setAlpha(m_stroke->erase ? 70 : static_cast<int>(40 + 80 * m_stroke->flow));
            p.setPen(QPen(c, 2 * m_stroke->radius * ir.height(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
        if (m_hover) {
            // The brush outline: the hard core and the full reach.
            const double r = m_brush.radius * ir.height();
            p.setBrush(Qt::NoBrush);
            for (double rr : {r, r * std::max(0.05, m_brush.hardness)}) {
                p.setPen(QPen(QColor(0, 0, 0, 150), 3));
                p.drawEllipse(*m_hover, rr, rr);
                p.setPen(QPen(Qt::white, rr == r ? 1.5 : 1, rr == r ? Qt::SolidLine : Qt::DotLine));
                p.drawEllipse(*m_hover, rr, rr);
            }
        }
        p.restore();
        return;
    }
    const QRectF r = maskWidgetRect();
    const QColor accent = themeColors().accent;
    QPen outline(accent, 1.5, Qt::DashLine);
    QPen shadow(QColor(0, 0, 0, 140), 3);
    auto stroke = [&](const std::function<void()>& draw) {
        p.setPen(shadow);
        draw();
        p.setPen(outline);
        draw();
    };
    p.setBrush(Qt::NoBrush);
    switch (m_mask->shape) {
    case MaskShape::Rectangle: stroke([&] { p.drawRect(r); }); break;
    case MaskShape::Ellipse: stroke([&] { p.drawEllipse(r); }); break;
    case MaskShape::Linear: {
        // The line through the centre at the mask angle, plus the edges of
        // the soft band.
        const double a = m_mask->angle * M_PI / 180.0;
        const QPointF dir(std::cos(a), std::sin(a));
        const QPointF normal(-dir.y(), dir.x());
        const double reach = std::hypot(ir.width(), ir.height());
        const double band = m_mask->feather * reach / 2;
        const QPointF c = r.center();
        stroke([&] { p.drawLine(c - dir * reach, c + dir * reach); });
        outline.setWidthF(1.0);
        shadow.setWidthF(2.0);
        stroke([&] {
            p.drawLine(c - normal * band - dir * reach, c - normal * band + dir * reach);
            p.drawLine(c + normal * band - dir * reach, c + normal * band + dir * reach);
        });
        break;
    }
    case MaskShape::None:
    case MaskShape::Brush: break;
    }
    // Centre handle.
    p.setPen(QPen(QColor(0, 0, 0, 160), 3));
    p.drawEllipse(r.center(), 5, 5);
    p.setPen(QPen(accent, 2));
    p.drawEllipse(r.center(), 5, 5);
    if (m_mask->invert) {
        p.setPen(accent);
        p.drawText(r.center() + QPointF(9, -6), tr("inverted"));
    }
    p.restore();
}

QRectF PhotoView::imageRect() const {
    if (m_image.isNull())
        return {};
    // In normal mode the visible image is only the crop, so fit the crop.
    const QSizeF full = m_image.size();
    const QSizeF shown = m_cropMode ? full : QSizeF(full.width() * m_crop.width(), full.height() * m_crop.height());
    const QSizeF avail = QSizeF(size()) - QSizeF(40, 40);
    const QSizeF fitted = shown.scaled(avail, Qt::KeepAspectRatio);
    const QPointF topLeft((width() - fitted.width()) / 2, (height() - fitted.height()) / 2);
    if (m_cropMode)
        return QRectF(topLeft, fitted);
    // Rectangle of the *full* image such that the crop lands on `fitted`.
    const double sx = fitted.width() / m_crop.width();
    const double sy = fitted.height() / m_crop.height();
    return QRectF(topLeft.x() - m_crop.x() * sx, topLeft.y() - m_crop.y() * sy, sx, sy);
}

QRectF PhotoView::cropWidgetRect() const {
    const QRectF ir = imageRect();
    return QRectF(ir.x() + m_crop.x() * ir.width(), ir.y() + m_crop.y() * ir.height(), m_crop.width() * ir.width(),
                  m_crop.height() * ir.height());
}

void PhotoView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QPainterPath frame;
    frame.addRoundedRect(QRectF(rect()), 9, 9);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(frame);
    p.fillRect(rect(), themeColors().viewer);
    if (m_image.isNull()) {
        p.setPen(themeColors().textFaint);
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF ir = imageRect();
    if (m_cropMode) {
        p.drawImage(ir, m_image);
        const QRectF cr = cropWidgetRect();
        QPainterPath outside;
        outside.addRect(ir);
        QPainterPath inside;
        inside.addRect(cr);
        p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 150));
        p.setPen(QPen(QColor(255, 255, 255, 110), 1));
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(cr.left() + cr.width() * i / 3, cr.top()), QPointF(cr.left() + cr.width() * i / 3, cr.bottom()));
            p.drawLine(QPointF(cr.left(), cr.top() + cr.height() * i / 3), QPointF(cr.right(), cr.top() + cr.height() * i / 3));
        }
        p.setPen(QPen(Qt::white, 1.5));
        p.drawRect(cr);
        p.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::SquareCap));
        const double L = 16;
        for (QPointF c : {cr.topLeft(), cr.topRight(), cr.bottomLeft(), cr.bottomRight()}) {
            const double dx = c.x() == cr.left() ? L : -L;
            const double dy = c.y() == cr.top() ? L : -L;
            p.drawLine(c, c + QPointF(dx, 0));
            p.drawLine(c, c + QPointF(0, dy));
        }
    } else {
        p.save();
        p.setClipRect(cropWidgetRect());
        p.drawImage(ir, m_image);
        p.restore();
        paintMask(p, ir);
        m_gizmo.paint(p, ir, themeColors().accent);
    }
    if (!m_badge.isEmpty()) {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRect r = p.fontMetrics().boundingRect(m_badge).adjusted(-8, -4, 8, 4);
        const QRect box(QPoint(12, 12), r.size());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, m_badge);
    }
}

PhotoView::Handle PhotoView::handleAt(QPointF pt) const {
    const QRectF cr = cropWidgetRect();
    const double g = 10;
    const bool l = std::abs(pt.x() - cr.left()) < g, r = std::abs(pt.x() - cr.right()) < g;
    const bool t = std::abs(pt.y() - cr.top()) < g, b = std::abs(pt.y() - cr.bottom()) < g;
    const bool inX = pt.x() > cr.left() - g && pt.x() < cr.right() + g;
    const bool inY = pt.y() > cr.top() - g && pt.y() < cr.bottom() + g;
    if (l && t) return TopLeft;
    if (r && t) return TopRight;
    if (l && b) return BottomLeft;
    if (r && b) return BottomRight;
    if (l && inY) return Left;
    if (r && inY) return Right;
    if (t && inX) return Top;
    if (b && inX) return Bottom;
    if (cr.contains(pt)) return Move;
    return None;
}

void PhotoView::applyAspect(QRectF& r, Handle h) const {
    if (m_aspect <= 0 || m_image.isNull())
        return;
    // Aspect in normalised units: (w * W) / (h * H) = aspect.
    const double k = m_aspect * m_image.height() / m_image.width();
    double w = r.width(), hgt = r.height();
    if (h == Top || h == Bottom)
        w = hgt * k;
    else
        hgt = w / k;
    if (w > 1) { w = 1; hgt = w / k; }
    if (hgt > 1) { hgt = 1; w = hgt * k; }
    QRectF out(r.topLeft(), QSizeF(w, hgt));
    if (h == TopLeft || h == Left || h == BottomLeft)
        out.moveRight(r.right());
    if (h == TopLeft || h == Top || h == TopRight)
        out.moveBottom(r.bottom());
    if (out.left() < 0) out.moveLeft(0);
    if (out.top() < 0) out.moveTop(0);
    if (out.right() > 1) out.moveRight(1);
    if (out.bottom() > 1) out.moveBottom(1);
    r = out;
}

void PhotoView::setPictureBox(const std::optional<TransformGizmo::Box>& box) {
    m_gizmo.setBox(box);
    update();
}

void PhotoView::mousePressEvent(QMouseEvent* e) {
    if (!m_cropMode && !brushMode() && e->button() == Qt::LeftButton && !m_image.isNull() &&
        m_gizmo.press(e->position(), imageRect()))
        return;
    if (brushMode()) {
        if (e->button() != Qt::LeftButton)
            return;
        BrushStroke s = m_brush;
        if (e->modifiers() & Qt::AltModifier)
            s.erase = !s.erase;
        s.points = {toImage(e->position())};
        m_stroke = s;
        update();
        return;
    }
    if (!m_cropMode && m_mask && m_mask->shape != MaskShape::None && !m_image.isNull() &&
        e->button() == Qt::LeftButton) {
        m_maskDrag = true;
        m_pressPos = e->position();
        m_pressMask = m_mask->rect;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (!m_cropMode || m_image.isNull())
        return;
    m_drag = handleAt(e->position());
    m_pressPos = e->position();
    m_pressCrop = m_crop;
}

void PhotoView::mouseMoveEvent(QMouseEvent* e) {
    if (m_gizmo.move(e->position(), imageRect(), e->modifiers())) {
        update();
        emit pictureMoved(*m_gizmo.box(), false);
        return;
    }
    if (!m_cropMode && !brushMode() && !m_maskDrag) {
        if (auto c = m_gizmo.cursorAt(e->position(), imageRect())) {
            setCursor(*c);
            return;
        }
    }
    if (brushMode()) {
        setCursor(Qt::CrossCursor);
        m_hover = e->position();
        if (m_stroke) {
            const QPointF pt = toImage(e->position());
            const QPointF last = m_stroke->points.back();
            const double aspect = double(m_image.width()) / m_image.height();
            // Points closer than a quarter of the radius add nothing.
            if (std::hypot((pt.x() - last.x()) * aspect, pt.y() - last.y()) >= m_stroke->radius * 0.25)
                m_stroke->points.push_back(pt);
        }
        update();
        return;
    }
    m_hover.reset();
    if (m_maskDrag && m_mask) {
        const QRectF ir = imageRect();
        const QPointF d((e->position().x() - m_pressPos.x()) / ir.width(), (e->position().y() - m_pressPos.y()) / ir.height());
        QRectF r = m_pressMask.translated(d);
        // Keep the centre on the picture.
        r.moveCenter(QPointF(std::clamp(r.center().x(), 0.0, 1.0), std::clamp(r.center().y(), 0.0, 1.0)));
        m_mask->rect = r;
        update();
        return;
    }
    if (!m_cropMode && m_mask && m_mask->shape != MaskShape::None) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (!m_cropMode || m_image.isNull()) {
        unsetCursor();
        return;
    }
    if (m_drag == None) {
        switch (handleAt(e->position())) {
        case Left: case Right: setCursor(Qt::SizeHorCursor); break;
        case Top: case Bottom: setCursor(Qt::SizeVerCursor); break;
        case TopLeft: case BottomRight: setCursor(Qt::SizeFDiagCursor); break;
        case TopRight: case BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
        case Move: setCursor(Qt::SizeAllCursor); break;
        default: unsetCursor();
        }
        return;
    }
    const QRectF ir = imageRect();
    const QPointF d((e->position().x() - m_pressPos.x()) / ir.width(), (e->position().y() - m_pressPos.y()) / ir.height());
    QRectF r = m_pressCrop;
    const double minSize = 0.02;
    switch (m_drag) {
    case Move:
        r.translate(d);
        r.moveLeft(std::clamp(r.left(), 0.0, 1.0 - r.width()));
        r.moveTop(std::clamp(r.top(), 0.0, 1.0 - r.height()));
        break;
    default:
        if (m_drag == Left || m_drag == TopLeft || m_drag == BottomLeft)
            r.setLeft(std::clamp(r.left() + d.x(), 0.0, r.right() - minSize));
        if (m_drag == Right || m_drag == TopRight || m_drag == BottomRight)
            r.setRight(std::clamp(r.right() + d.x(), r.left() + minSize, 1.0));
        if (m_drag == Top || m_drag == TopLeft || m_drag == TopRight)
            r.setTop(std::clamp(r.top() + d.y(), 0.0, r.bottom() - minSize));
        if (m_drag == Bottom || m_drag == BottomLeft || m_drag == BottomRight)
            r.setBottom(std::clamp(r.bottom() + d.y(), r.top() + minSize, 1.0));
        applyAspect(r, m_drag);
    }
    m_crop = r;
    update();
}

void PhotoView::mouseReleaseEvent(QMouseEvent*) {
    if (m_gizmo.release()) {
        emit pictureMoved(*m_gizmo.box(), true);
        return;
    }
    if (m_stroke) {
        const BrushStroke s = *m_stroke;
        m_stroke.reset();
        update();
        emit strokePainted(s);
        return;
    }
    if (m_maskDrag) {
        m_maskDrag = false;
        setCursor(Qt::OpenHandCursor);
        if (m_mask && m_mask->rect != m_pressMask)
            emit maskMoved(m_mask->rect);
        return;
    }
    if (m_drag != None && m_crop != m_pressCrop)
        emit cropEdited(m_crop);
    m_drag = None;
}

void PhotoView::mouseDoubleClickEvent(QMouseEvent* e) {
    // Only on the plain picture: crop and mask editing use the mouse.
    if (e->button() == Qt::LeftButton && !m_cropMode && !m_mask && !m_image.isNull())
        emit doubleClicked();
}

void PhotoView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void PhotoView::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile())
            paths << u.toLocalFile();
    emit filesDropped(paths);
}

} // namespace lumora

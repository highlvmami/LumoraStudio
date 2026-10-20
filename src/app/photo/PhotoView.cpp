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
    m_image = img;
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
    case MaskShape::None: break;
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

void PhotoView::mousePressEvent(QMouseEvent* e) {
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

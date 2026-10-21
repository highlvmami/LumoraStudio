#include "VideoView.h"

#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QUuid>

#include "Theme.h"
#include "panels/MimeTypes.h"

namespace lumora {

VideoView::VideoView(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void VideoView::setImage(const QImage& img) {
    m_image = img;
    update();
}

void VideoView::setOverlayText(const QString& text) {
    m_overlay = text;
    update();
}

void VideoView::setPlaceholder(const QString& text) {
    m_placeholder = text;
    update();
}

QRect VideoView::imageRect() const {
    if (m_image.isNull())
        return {};
    const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

void VideoView::setTransformBox(const std::optional<ClipTransform>& t, QSizeF fit, QPointF follow) {
    if (m_gizmo.dragging())
        return; // the drag owns the box until it ends
    if (!t) {
        m_gizmo.setBox(std::nullopt);
    } else {
        m_transform = *t;
        m_gizmo.setBox(TransformGizmo::Box{fit, t->x, t->y, t->scale, t->rotation, follow});
    }
    update();
}

void VideoView::startRegionPick(const QString& hint) {
    m_picking = true;
    m_pickHint = hint;
    m_pickRect = {};
    setCursor(Qt::CrossCursor);
    setFocus(Qt::OtherFocusReason);
    update();
}

void VideoView::cancelRegionPick() {
    if (!m_picking)
        return;
    m_picking = false;
    m_pickRect = {};
    unsetCursor();
    update();
    emit regionPickCancelled();
}

void VideoView::mousePressEvent(QMouseEvent* e) {
    if (!m_picking && e->button() == Qt::LeftButton && m_gizmo.press(e->position(), imageRect()))
        return;
    if (!m_picking || e->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(e);
    m_pickStart = e->position().toPoint();
    m_pickRect = QRect(m_pickStart, QSize(1, 1));
    update();
}

void VideoView::mouseMoveEvent(QMouseEvent* e) {
    if (!m_picking && m_gizmo.box()) {
        if (m_gizmo.move(e->position(), imageRect(), e->modifiers())) {
            const auto& b = *m_gizmo.box();
            m_transform.x = b.x;
            m_transform.y = b.y;
            m_transform.scale = b.scale;
            m_transform.rotation = b.rotation;
            update();
            emit transformEdited(m_transform, false);
            return;
        }
        if (auto c = m_gizmo.cursorAt(e->position(), imageRect()))
            setCursor(*c);
        else
            unsetCursor();
    }
    if (!m_picking || !(e->buttons() & Qt::LeftButton))
        return QWidget::mouseMoveEvent(e);
    m_pickRect = QRect(m_pickStart, e->position().toPoint()).normalized().intersected(imageRect());
    update();
}

void VideoView::mouseReleaseEvent(QMouseEvent* e) {
    if (!m_picking && e->button() == Qt::LeftButton && m_gizmo.release()) {
        emit transformEdited(m_transform, true);
        return;
    }
    if (!m_picking || e->button() != Qt::LeftButton)
        return QWidget::mouseReleaseEvent(e);
    const QRect img = imageRect();
    if (m_pickRect.width() < 6 || m_pickRect.height() < 6 || img.isEmpty()) {
        m_pickRect = {};
        update();
        return; // a click: keep waiting for a rectangle
    }
    const QRectF region((m_pickRect.left() - img.left()) / double(img.width()), (m_pickRect.top() - img.top()) / double(img.height()),
                        m_pickRect.width() / double(img.width()), m_pickRect.height() / double(img.height()));
    m_picking = false;
    m_pickRect = {};
    unsetCursor();
    update();
    emit regionPicked(region);
}

void VideoView::keyPressEvent(QKeyEvent* e) {
    if (m_picking && e->key() == Qt::Key_Escape) {
        cancelRegionPick();
        return;
    }
    QWidget::keyPressEvent(e);
}

void VideoView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QPainterPath frame;
    frame.addRoundedRect(QRectF(rect()), 9, 9);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(frame);
    p.fillRect(rect(), themeColors().viewer);
    if (!m_image.isNull()) {
        const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, m_image);
    } else if (!m_placeholder.isEmpty()) {
        p.setPen(themeColors().textFaint);
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
    }
    if (!m_picking && !m_image.isNull())
        m_gizmo.paint(p, imageRect(), themeColors().accent);
    if (m_picking) {
        const ThemeColors& tc = themeColors();
        if (!m_pickRect.isEmpty()) {
            p.setPen(QPen(tc.accent, 2));
            p.setBrush(QColor(tc.accent.red(), tc.accent.green(), tc.accent.blue(), 40));
            p.drawRect(m_pickRect);
        }
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRect area = rect().adjusted(16, 0, -16, -12);
        const QRect text = p.fontMetrics().boundingRect(area, Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap, m_pickHint);
        const QRect box = text.adjusted(-10, -6, 10, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 170));
        p.drawRoundedRect(box, 6, 6);
        p.setPen(Qt::white);
        p.drawText(text, Qt::AlignCenter | Qt::TextWordWrap, m_pickHint);
    }
    if (!m_overlay.isEmpty()) {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRect r = p.fontMetrics().boundingRect(m_overlay).adjusted(-8, -4, 8, 4);
        const QRect box(QPoint(10, 10), r.size());
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0, 0, 0, 150));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, m_overlay);
    }
}

void VideoView::resizeEvent(QResizeEvent*) { emit sizeChanged(size() * devicePixelRatioF()); }

void VideoView::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() || e->mimeData()->hasFormat(kMediaMimeType))
        e->acceptProposedAction();
}

void VideoView::dropEvent(QDropEvent* e) {
    if (e->mimeData()->hasFormat(kMediaMimeType)) {
        emit mediaDropped(decodeMediaIds(e->mimeData()));
    } else {
        QStringList paths;
        for (const QUrl& u : e->mimeData()->urls())
            if (u.isLocalFile())
                paths << u.toLocalFile();
        emit filesDropped(paths);
    }
    e->acceptProposedAction();
}

} // namespace lumora

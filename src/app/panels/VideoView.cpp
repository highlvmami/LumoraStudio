#include "VideoView.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QPainter>
#include <QUuid>

#include "panels/MimeTypes.h"

namespace lumora {

VideoView::VideoView(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
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

void VideoView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(12, 12, 14));
    if (!m_image.isNull()) {
        const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, m_image);
    } else if (!m_placeholder.isEmpty()) {
        p.setPen(QColor(140, 140, 150));
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
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

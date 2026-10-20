#include "Icons.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <functional>

namespace lumora::Icons {

namespace {

QIcon draw(const std::function<void(QPainter&, QColor)>& fn) {
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64}) {
        for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
            QPixmap pm(size, size);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(size / 24.0, size / 24.0);
            QColor c = QApplication::palette().color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                                     QPalette::ButtonText);
            fn(p, c);
            icon.addPixmap(pm, mode);
        }
    }
    return icon;
}

QPen pen(QColor c, double w = 2.0) { return QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

void triangle(QPainter& p, QColor c, QPointF a, QPointF b, QPointF d) {
    QPainterPath path;
    path.moveTo(a);
    path.lineTo(b);
    path.lineTo(d);
    path.closeSubpath();
    p.fillPath(path, c);
}

} // namespace

QIcon play() { return draw([](QPainter& p, QColor c) { triangle(p, c, {7, 4}, {20, 12}, {7, 20}); }); }

QIcon pause() {
    return draw([](QPainter& p, QColor c) {
        p.fillRect(QRectF(6, 5, 4, 14), c);
        p.fillRect(QRectF(14, 5, 4, 14), c);
    });
}

QIcon stepBack() {
    return draw([](QPainter& p, QColor c) {
        p.fillRect(QRectF(5, 6, 2.5, 12), c);
        triangle(p, c, {18, 6}, {9, 12}, {18, 18});
    });
}

QIcon stepForward() {
    return draw([](QPainter& p, QColor c) {
        p.fillRect(QRectF(16.5, 6, 2.5, 12), c);
        triangle(p, c, {6, 6}, {15, 12}, {6, 18});
    });
}

QIcon toStart() {
    return draw([](QPainter& p, QColor c) {
        p.fillRect(QRectF(4, 6, 2.5, 12), c);
        triangle(p, c, {13, 6}, {7, 12}, {13, 18});
        triangle(p, c, {20, 6}, {14, 12}, {20, 18});
    });
}

QIcon toEnd() {
    return draw([](QPainter& p, QColor c) {
        p.fillRect(QRectF(17.5, 6, 2.5, 12), c);
        triangle(p, c, {4, 6}, {10, 12}, {4, 18});
        triangle(p, c, {11, 6}, {17, 12}, {11, 18});
    });
}

QIcon markIn() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawLine(QPointF(8, 4), QPointF(8, 20));
        p.drawLine(QPointF(8, 4), QPointF(14, 4));
        p.drawLine(QPointF(8, 20), QPointF(14, 20));
    });
}

QIcon markOut() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawLine(QPointF(16, 4), QPointF(16, 20));
        p.drawLine(QPointF(10, 4), QPointF(16, 4));
        p.drawLine(QPointF(10, 20), QPointF(16, 20));
    });
}

QIcon add() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c, 2.4));
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
    });
}

QIcon split() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawRoundedRect(QRectF(3, 8, 7, 8), 1.5, 1.5);
        p.drawRoundedRect(QRectF(14, 8, 7, 8), 1.5, 1.5);
        p.setPen(pen(QColor(230, 80, 80), 2));
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
    });
}

QIcon remove() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawLine(QPointF(5, 7), QPointF(19, 7));
        p.drawLine(QPointF(10, 4), QPointF(14, 4));
        p.drawRoundedRect(QRectF(7, 7, 10, 13), 1.5, 1.5);
        p.drawLine(QPointF(10.5, 10.5), QPointF(10.5, 16.5));
        p.drawLine(QPointF(13.5, 10.5), QPointF(13.5, 16.5));
    });
}

QIcon join() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawRoundedRect(QRectF(3, 8, 18, 8), 1.5, 1.5);
        p.setPen(pen(c, 1.2));
        p.drawLine(QPointF(12, 9.5), QPointF(12, 14.5));
    });
}

QIcon undo() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        QPainterPath path;
        path.moveTo(6, 10);
        path.cubicTo(10, 5, 19, 6, 19, 13);
        path.cubicTo(19, 17, 15, 19, 12, 19);
        p.drawPath(path);
        triangle(p, c, {3, 10}, {9, 6}, {9, 13});
    });
}

QIcon redo() {
    return draw([](QPainter& p, QColor c) {
        p.translate(24, 0);
        p.scale(-1, 1);
        p.setPen(pen(c));
        QPainterPath path;
        path.moveTo(6, 10);
        path.cubicTo(10, 5, 19, 6, 19, 13);
        path.cubicTo(19, 17, 15, 19, 12, 19);
        p.drawPath(path);
        triangle(p, c, {3, 10}, {9, 6}, {9, 13});
    });
}

QIcon import() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawLine(QPointF(12, 3), QPointF(12, 14));
        p.drawLine(QPointF(8, 10), QPointF(12, 14));
        p.drawLine(QPointF(16, 10), QPointF(12, 14));
        p.drawLine(QPointF(4, 15), QPointF(4, 20));
        p.drawLine(QPointF(4, 20), QPointF(20, 20));
        p.drawLine(QPointF(20, 20), QPointF(20, 15));
    });
}

QIcon exportFile() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawLine(QPointF(12, 14), QPointF(12, 3));
        p.drawLine(QPointF(8, 7), QPointF(12, 3));
        p.drawLine(QPointF(16, 7), QPointF(12, 3));
        p.drawLine(QPointF(4, 15), QPointF(4, 20));
        p.drawLine(QPointF(4, 20), QPointF(20, 20));
        p.drawLine(QPointF(20, 20), QPointF(20, 15));
    });
}

QIcon crop() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawLine(QPointF(7, 3), QPointF(7, 17));
        p.drawLine(QPointF(7, 17), QPointF(21, 17));
        p.drawLine(QPointF(3, 7), QPointF(17, 7));
        p.drawLine(QPointF(17, 7), QPointF(17, 21));
    });
}

QIcon magnet() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c, 3));
        QPainterPath path;
        path.moveTo(6, 4);
        path.lineTo(6, 13);
        path.arcTo(QRectF(6, 7, 12, 12), 180, 180);
        path.lineTo(18, 4);
        p.drawPath(path);
        p.setPen(pen(QColor(230, 80, 80), 3));
        p.drawLine(QPointF(6, 4), QPointF(6, 7));
        p.drawLine(QPointF(18, 4), QPointF(18, 7));
    });
}

QIcon zoomIn() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawEllipse(QPointF(10, 10), 6, 6);
        p.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
        p.drawLine(QPointF(10, 7), QPointF(10, 13));
    });
}

QIcon zoomOut() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawEllipse(QPointF(10, 10), 6, 6);
        p.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
        p.drawLine(QPointF(7, 10), QPointF(13, 10));
    });
}

QIcon reset() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawArc(QRectF(5, 5, 14, 14), 90 * 16, 300 * 16);
        triangle(p, c, {12, 2}, {16, 5}, {12, 8});
    });
}

QIcon compare() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c));
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
        p.fillRect(QRectF(3, 5, 9, 14), c);
    });
}

QIcon keyframe() {
    return draw([](QPainter& p, QColor c) {
        QPainterPath path;
        path.moveTo(12, 4);
        path.lineTo(20, 12);
        path.lineTo(12, 20);
        path.lineTo(4, 12);
        path.closeSubpath();
        p.fillPath(path, c);
    });
}

QIcon title() {
    return draw([](QPainter& p, QColor c) {
        p.setPen(pen(c, 2.2));
        p.drawLine(QPointF(5, 6), QPointF(19, 6));
        p.drawLine(QPointF(12, 6), QPointF(12, 19));
        p.setPen(pen(c, 1.6));
        p.drawLine(QPointF(5, 6), QPointF(5, 8.5));
        p.drawLine(QPointF(19, 6), QPointF(19, 8.5));
        p.drawLine(QPointF(9.5, 19), QPointF(14.5, 19));
    });
}

QIcon transition() {
    return draw([](QPainter& p, QColor c) {
        // Two overlapping frames, one fading into the other.
        p.setPen(pen(c, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(3, 5, 11, 10), 1.5, 1.5);
        QColor fill = c;
        fill.setAlphaF(0.55);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(10, 9, 11, 10), 1.5, 1.5);
    });
}

QIcon appIcon() {
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 64.0, size / 64.0);
        QLinearGradient g(0, 0, 64, 64);
        g.setColorAt(0, QColor(255, 179, 71));
        g.setColorAt(0.55, QColor(236, 72, 153));
        g.setColorAt(1, QColor(99, 102, 241));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(2, 2, 60, 60), 14, 14);
        // A lens-like ring with a play triangle: video + photo.
        p.setPen(QPen(QColor(255, 255, 255, 235), 5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(32, 32), 18, 18);
        QPainterPath tri;
        tri.moveTo(27, 22);
        tri.lineTo(43, 32);
        tri.lineTo(27, 42);
        tri.closeSubpath();
        p.fillPath(tri, QColor(255, 255, 255, 235));
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace lumora::Icons

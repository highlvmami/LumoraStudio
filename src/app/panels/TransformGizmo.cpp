#include "TransformGizmo.h"

#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace lumora {

namespace {
constexpr double kGrip = 7.0;         // half size of a corner handle
constexpr double kRotateReach = 26.0; // distance of the turn knob above the box
} // namespace

void TransformGizmo::setBox(const std::optional<Box>& box) {
    if (dragging() && box)
        return; // the drag owns the box until it ends
    m_box = box;
    if (!m_box)
        m_grip = Grip::None;
}

QPointF TransformGizmo::centre(const QRectF& frame) const {
    return frame.center() + QPointF((m_box->x + m_box->follow.x()) * frame.width(), (m_box->y + m_box->follow.y()) * frame.height());
}

QSizeF TransformGizmo::size(const QRectF& frame) const {
    const double s = std::max(0.0, m_box->scale);
    return QSizeF(m_box->fit.width() * frame.width() * s, m_box->fit.height() * frame.height() * s);
}

QPointF TransformGizmo::local(QPointF pos, const QRectF& frame) const {
    const QPointF d = pos - centre(frame);
    const double a = qDegreesToRadians(-m_box->rotation);
    return QPointF(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
}

TransformGizmo::Grip TransformGizmo::gripAt(QPointF pos, const QRectF& frame) const {
    if (!m_box || frame.isEmpty())
        return Grip::None;
    const QPointF l = local(pos, frame);
    const QSizeF s = size(frame);
    const double hw = s.width() / 2, hh = s.height() / 2;
    if (std::hypot(l.x(), l.y() + hh + kRotateReach) <= kGrip + 3)
        return Grip::Rotate;
    for (const QPointF c : {QPointF(-hw, -hh), QPointF(hw, -hh), QPointF(-hw, hh), QPointF(hw, hh)})
        if (std::abs(l.x() - c.x()) <= kGrip + 2 && std::abs(l.y() - c.y()) <= kGrip + 2)
            return Grip::Scale;
    if (std::abs(l.x()) <= hw && std::abs(l.y()) <= hh)
        return Grip::Move;
    return Grip::None;
}

bool TransformGizmo::press(QPointF pos, const QRectF& frame) {
    m_grip = gripAt(pos, frame);
    if (m_grip == Grip::None)
        return false;
    m_pressPos = pos;
    m_pressBox = *m_box;
    const QPointF d = pos - centre(frame);
    m_pressAngle = qRadiansToDegrees(std::atan2(d.y(), d.x()));
    m_pressDistance = std::max(1.0, std::hypot(d.x(), d.y()));
    return true;
}

bool TransformGizmo::move(QPointF pos, const QRectF& frame, Qt::KeyboardModifiers mods) {
    if (!dragging() || !m_box || frame.isEmpty())
        return false;
    Box b = m_pressBox;
    const QPointF d = pos - centre(frame);
    if (m_grip == Grip::Move) {
        const QPointF delta = pos - m_pressPos;
        b.x = std::clamp(m_pressBox.x + delta.x() / frame.width(), -1.5, 1.5);
        b.y = std::clamp(m_pressBox.y + delta.y() / frame.height(), -1.5, 1.5);
    } else if (m_grip == Grip::Scale) {
        // Corners keep the picture's shape; the centre stays put.
        b.scale = std::clamp(m_pressBox.scale * std::hypot(d.x(), d.y()) / m_pressDistance, 0.02, 10.0);
    } else {
        double angle = std::remainder(m_pressBox.rotation + qRadiansToDegrees(std::atan2(d.y(), d.x())) - m_pressAngle, 360.0);
        if (mods & Qt::ShiftModifier)
            angle = std::round(angle / 15.0) * 15.0;
        b.rotation = angle == 0.0 ? 0.0 : angle; // no negative zero
    }
    m_box = b;
    return true;
}

bool TransformGizmo::release() {
    if (!dragging())
        return false;
    m_grip = Grip::None;
    return true;
}

std::optional<Qt::CursorShape> TransformGizmo::cursorAt(QPointF pos, const QRectF& frame) const {
    switch (gripAt(pos, frame)) {
    case Grip::Move: return Qt::SizeAllCursor;
    case Grip::Scale: return Qt::SizeFDiagCursor;
    case Grip::Rotate: return Qt::PointingHandCursor;
    case Grip::None: break;
    }
    return std::nullopt;
}

void TransformGizmo::paint(QPainter& p, const QRectF& frame, const QColor& accent) const {
    if (!m_box || frame.isEmpty())
        return;
    const QSizeF s = size(frame);
    const double hw = s.width() / 2, hh = s.height() / 2;
    const QRectF r(-hw, -hh, s.width(), s.height());
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(centre(frame));
    p.rotate(m_box->rotation);
    // A dark line under a light one stays visible on any picture.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 140), 3));
    p.drawRect(r);
    p.drawLine(QPointF(0, -hh), QPointF(0, -hh - kRotateReach));
    p.setPen(QPen(accent, 1.5));
    p.drawRect(r);
    p.drawLine(QPointF(0, -hh), QPointF(0, -hh - kRotateReach));
    p.setBrush(Qt::white);
    for (const QPointF c : {QPointF(-hw, -hh), QPointF(hw, -hh), QPointF(-hw, hh), QPointF(hw, hh)})
        p.drawRect(QRectF(c.x() - kGrip / 1.4, c.y() - kGrip / 1.4, kGrip * 1.4, kGrip * 1.4));
    p.drawEllipse(QPointF(0, -hh - kRotateReach), kGrip / 1.2, kGrip / 1.2);
    p.restore();
}

} // namespace lumora

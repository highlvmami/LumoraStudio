#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <Qt>
#include <optional>

class QPainter;

namespace lumora {

// On-screen handles for placing a picture over a frame (video viewer, photo
// view): drag inside to move it, a corner to resize, the knob above it to
// turn it (Shift snaps to 15°). Geometry follows ClipTransform / PhotoLayer:
// centre offset and size relative to the frame, rotation in degrees
// clockwise. `frame` is where the frame is drawn, in widget pixels.
class TransformGizmo {
public:
    struct Box {
        QSizeF fit;           // untransformed picture size, fraction of the frame
        double x = 0.0, y = 0.0;
        double scale = 1.0;
        double rotation = 0.0;
        QPointF follow;       // extra offset that is not edited (motion tracking)
    };

    void setBox(const std::optional<Box>& box);
    const std::optional<Box>& box() const { return m_box; }
    bool dragging() const { return m_grip != Grip::None; }

    // Returns true when the press grabbed a handle.
    bool press(QPointF pos, const QRectF& frame);
    // Returns true when the box changed.
    bool move(QPointF pos, const QRectF& frame, Qt::KeyboardModifiers mods);
    // Returns true when a drag ended.
    bool release();
    std::optional<Qt::CursorShape> cursorAt(QPointF pos, const QRectF& frame) const;
    void paint(QPainter& p, const QRectF& frame, const QColor& accent) const;

private:
    enum class Grip { None, Move, Scale, Rotate };
    QPointF centre(const QRectF& frame) const;
    QSizeF size(const QRectF& frame) const;
    QPointF local(QPointF pos, const QRectF& frame) const; // unrotated, relative to the centre
    Grip gripAt(QPointF pos, const QRectF& frame) const;

    std::optional<Box> m_box;
    Grip m_grip = Grip::None;
    QPointF m_pressPos;
    Box m_pressBox;
    double m_pressAngle = 0.0;
    double m_pressDistance = 1.0;
};

} // namespace lumora

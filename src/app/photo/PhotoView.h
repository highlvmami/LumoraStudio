#pragma once

#include <QImage>
#include <QWidget>
#include <optional>

#include "image/PhotoAdjustments.h"
#include "panels/TransformGizmo.h"

namespace lumora {

// Displays the processed photo; in crop mode shows the full frame with an
// editable crop rectangle (normalised coordinates). With a brush mask
// selected it is a canvas for painting the mask.
class PhotoView : public QWidget {
    Q_OBJECT
public:
    explicit PhotoView(QWidget* parent = nullptr);

    void setImage(const QImage& img); // full (uncropped) processed proxy
    void setCrop(const QRectF& normalized);
    QRectF crop() const { return m_crop; }
    void setCropMode(bool on);
    bool cropMode() const { return m_cropMode; }
    // Width / height of the crop in source pixels; 0 = free.
    void setAspect(double aspect);
    void setPlaceholder(const QString& text);
    void setBadge(const QString& text);
    // Outline of the selected layer's mask; dragging inside it moves it.
    void setMaskOverlay(const std::optional<LayerMask>& mask);
    // With a brush mask selected, dragging paints a stroke with this brush;
    // `tint` shows the mask as a coloured overlay.
    void setBrush(const BrushStroke& brush, bool tint);
    // Move / resize / turn handles for the selected picture layer.
    void setPictureBox(const std::optional<TransformGizmo::Box>& box);

signals:
    void cropEdited(const QRectF& normalized); // emitted on mouse release
    void maskMoved(const QRectF& normalized);  // emitted on mouse release
    void strokePainted(const lumora::BrushStroke& stroke); // emitted on mouse release
    void filesDropped(const QStringList& paths);
    // While dragging the picture handles (finished = false) and on release.
    void pictureMoved(const lumora::TransformGizmo::Box& box, bool finished);
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    enum Handle { None, Move, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
    QRectF imageRect() const;
    QRectF cropWidgetRect() const;
    Handle handleAt(QPointF p) const;
    void applyAspect(QRectF& r, Handle h) const;
    void paintMask(QPainter& p, const QRectF& imageRect) const;
    QRectF maskWidgetRect() const;
    bool brushMode() const;
    void rebuildTint();
    QPointF toImage(QPointF widgetPos) const;

    QImage m_image;
    QRectF m_crop{0, 0, 1, 1};
    bool m_cropMode = false;
    double m_aspect = 0;
    QString m_placeholder;
    QString m_badge;
    Handle m_drag = None;
    QPointF m_pressPos;
    QRectF m_pressCrop;
    std::optional<LayerMask> m_mask;
    bool m_maskDrag = false;
    QRectF m_pressMask;
    BrushStroke m_brush;
    bool m_tint = true;
    QImage m_tintImage;
    std::optional<BrushStroke> m_stroke; // being painted
    std::optional<QPointF> m_hover;
    TransformGizmo m_gizmo;
};

} // namespace lumora

#pragma once

#include <QImage>
#include <QWidget>
#include <optional>

#include "image/PhotoAdjustments.h"

namespace lumora {

// Displays the processed photo; in crop mode shows the full frame with an
// editable crop rectangle (normalised coordinates).
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

signals:
    void cropEdited(const QRectF& normalized); // emitted on mouse release
    void maskMoved(const QRectF& normalized);  // emitted on mouse release
    void filesDropped(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
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
};

} // namespace lumora

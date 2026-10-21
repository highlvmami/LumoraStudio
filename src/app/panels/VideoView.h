#pragma once

#include <QImage>
#include <QWidget>

namespace lumora {

// Paints the current preview frame letterboxed on black.
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);
    void setImage(const QImage& img);
    const QImage& image() const { return m_image; }
    void setOverlayText(const QString& text);
    void setPlaceholder(const QString& text);
    // Lets the user drag a rectangle over the picture; regionPicked() gets it
    // as a fraction of the picture. Esc cancels.
    void startRegionPick(const QString& hint);
    void cancelRegionPick();
    bool isPickingRegion() const { return m_picking; }

signals:
    void sizeChanged(QSize physicalSize);
    void filesDropped(const QStringList& paths);
    void mediaDropped(const QList<QUuid>& ids);
    void regionPicked(const QRectF& region);
    void regionPickCancelled();
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QRect imageRect() const;

    QImage m_image;
    QString m_overlay;
    QString m_placeholder;
    bool m_picking = false;
    QString m_pickHint;
    QPoint m_pickStart;
    QRect m_pickRect;
};

} // namespace lumora

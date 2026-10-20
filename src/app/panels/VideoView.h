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
    void setOverlayText(const QString& text);
    void setPlaceholder(const QString& text);

signals:
    void sizeChanged(QSize physicalSize);
    void filesDropped(const QStringList& paths);
    void mediaDropped(const QList<QUuid>& ids);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QImage m_image;
    QString m_overlay;
    QString m_placeholder;
};

} // namespace lumora

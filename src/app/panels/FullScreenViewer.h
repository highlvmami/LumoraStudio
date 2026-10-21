#pragma once

#include <QImage>
#include <QWidget>

#include "TimeBase.h"

class QLabel;
class QTimer;
class QToolButton;

namespace lumora {

class PlaybackEngine;
class ScrubBar;

// The preview on a whole screen: the video monitor with its own play
// controls, or the edited photo. The controls fade out while the mouse rests
// and come back when it moves. Esc, F or a double-click leaves.
class FullScreenViewer : public QWidget {
    Q_OBJECT
public:
    explicit FullScreenViewer(PlaybackEngine* engine, QWidget* parent = nullptr);

    // Opens on the screen that shows `over` (usually the main window).
    void showVideo(const QImage& current, const QString& title, QWidget* over);
    void showPhoto(const QImage& image, const QString& title, QWidget* over);
    // Replaces the photo while open (e.g. after stepping to the next one).
    void setPhoto(const QImage& image, const QString& title);
    bool isVideo() const { return m_video; }

signals:
    void closed();
    void photoStepRequested(int delta);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    void open(QWidget* over);
    void showControls();
    void hideControls();
    void layoutControls();
    void updateTimecode();

    PlaybackEngine* m_engine;
    bool m_video = true;
    QImage m_image;
    QString m_title;

    QWidget* m_top;
    QLabel* m_titleLabel;
    QWidget* m_bar;
    QToolButton* m_play;
    QToolButton* m_prev;
    QToolButton* m_next;
    ScrubBar* m_scrub;
    QLabel* m_timecode;
    QTimer* m_hideTimer;
    QTimer* m_clickTimer;
};

} // namespace lumora

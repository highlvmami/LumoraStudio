#include "FullScreenViewer.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include "Icons.h"
#include "Theme.h"
#include "panels/MonitorPanel.h"
#include "playback/PlaybackEngine.h"

namespace lumora {

namespace {

constexpr int kHideDelayMs = 2500;

// A translucent dark strip; the controls on it stay readable over any picture
// and in both themes.
class OverlayStrip : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(12, 12, 16, 190));
        p.drawRoundedRect(QRectF(rect()), 12, 12);
    }
};

QToolButton* overlayButton(const QIcon& icon, const QString& tip, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setIcon(icon);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setIconSize(QSize(24, 24));
    b->setFocusPolicy(Qt::NoFocus);
    b->setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: none; border-radius: 8px; padding: 6px; }"
                                    "QToolButton:hover { background: rgba(255, 255, 255, 40); }"
                                    "QToolButton:pressed { background: rgba(255, 255, 255, 70); }"));
    return b;
}

} // namespace

FullScreenViewer::FullScreenViewer(PlaybackEngine* engine, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_engine(engine) {
    setWindowTitle(QStringLiteral("LumoraStudio"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    const QColor white(Qt::white);
    m_top = new OverlayStrip(this);
    auto* tl = new QHBoxLayout(m_top);
    tl->setContentsMargins(16, 6, 6, 6);
    m_titleLabel = new QLabel(m_top);
    m_titleLabel->setStyleSheet(QStringLiteral("color: white; font-weight: 600; background: transparent;"));
    tl->addWidget(m_titleLabel, 1);
    auto* exit = overlayButton(Icons::exitFullScreen(white), tr("Leave full screen (Esc)"), m_top);
    tl->addWidget(exit);

    m_bar = new OverlayStrip(this);
    auto* bl = new QHBoxLayout(m_bar);
    bl->setContentsMargins(10, 6, 16, 6);
    bl->setSpacing(4);
    m_prev = overlayButton(Icons::stepBack(white), QString(), m_bar);
    m_play = overlayButton(Icons::play(white), tr("Play / pause (Space)"), m_bar);
    m_next = overlayButton(Icons::stepForward(white), QString(), m_bar);
    m_play->setIconSize(QSize(30, 30));
    bl->addWidget(m_prev);
    bl->addWidget(m_play);
    bl->addWidget(m_next);
    m_scrub = new ScrubBar(m_bar);
    m_scrub->setFocusPolicy(Qt::NoFocus);
    bl->addWidget(m_scrub, 1);
    m_timecode = new QLabel(m_bar);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_timecode->setFont(mono);
    m_timecode->setStyleSheet(QStringLiteral("color: white; background: transparent;"));
    bl->addWidget(m_timecode);

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(kHideDelayMs);
    connect(m_hideTimer, &QTimer::timeout, this, &FullScreenViewer::hideControls);
    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    m_clickTimer->setInterval(QApplication::doubleClickInterval());
    connect(m_clickTimer, &QTimer::timeout, m_engine, &PlaybackEngine::togglePlay);

    connect(exit, &QToolButton::clicked, this, &QWidget::close);
    connect(m_play, &QToolButton::clicked, m_engine, &PlaybackEngine::togglePlay);
    connect(m_prev, &QToolButton::clicked, this, [this] {
        if (m_video)
            m_engine->stepFrames(-1);
        else
            emit photoStepRequested(-1);
    });
    connect(m_next, &QToolButton::clicked, this, [this] {
        if (m_video)
            m_engine->stepFrames(1);
        else
            emit photoStepRequested(1);
    });
    connect(m_scrub, &ScrubBar::seekRequested, m_engine, &PlaybackEngine::seek);

    connect(m_engine, &PlaybackEngine::frameReady, this, [this](const QImage& img) {
        if (m_video && isVisible()) {
            m_image = img;
            update();
        }
    });
    connect(m_engine, &PlaybackEngine::positionChanged, this, [this](Ticks t) {
        if (!m_video || !isVisible())
            return;
        m_scrub->setPosition(t);
        updateTimecode();
    });
    connect(m_engine, &PlaybackEngine::playingChanged, this, [this](bool playing) {
        m_play->setIcon(playing ? Icons::pause(Qt::white) : Icons::play(Qt::white));
        if (m_video && isVisible())
            showControls();
    });
}

void FullScreenViewer::showVideo(const QImage& current, const QString& title, QWidget* over) {
    m_video = true;
    m_image = current;
    m_title = title;
    m_prev->setToolTip(tr("Previous frame (Left)"));
    m_next->setToolTip(tr("Next frame (Right)"));
    m_play->setVisible(true);
    m_scrub->setVisible(true);
    m_timecode->setVisible(true);
    m_scrub->setDuration(m_engine->duration());
    m_scrub->setPosition(m_engine->position());
    m_play->setIcon(m_engine->isPlaying() ? Icons::pause(Qt::white) : Icons::play(Qt::white));
    updateTimecode();
    open(over);
}

void FullScreenViewer::showPhoto(const QImage& image, const QString& title, QWidget* over) {
    m_video = false;
    m_prev->setToolTip(tr("Previous photo (Left)"));
    m_next->setToolTip(tr("Next photo (Right)"));
    m_play->setVisible(false);
    m_scrub->setVisible(false);
    m_timecode->setVisible(false);
    setPhoto(image, title);
    open(over);
}

void FullScreenViewer::setPhoto(const QImage& image, const QString& title) {
    m_image = image;
    m_title = title;
    m_titleLabel->setText(title);
    update();
}

void FullScreenViewer::open(QWidget* over) {
    m_titleLabel->setText(m_title);
    if (over && over->screen()) {
        setScreen(over->screen());
        setGeometry(over->screen()->geometry());
    }
    showFullScreen();
    layoutControls();
    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    showControls();
}

void FullScreenViewer::showControls() {
    m_top->show();
    m_bar->show();
    unsetCursor();
    // A paused video keeps its controls, like most players.
    if (!m_video || m_engine->isPlaying())
        m_hideTimer->start();
    else
        m_hideTimer->stop();
}

void FullScreenViewer::hideControls() {
    // Not while the pointer is on the controls.
    if (m_bar->underMouse() || m_top->underMouse()) {
        m_hideTimer->start();
        return;
    }
    m_top->hide();
    m_bar->hide();
    setCursor(Qt::BlankCursor);
}

void FullScreenViewer::layoutControls() {
    const int margin = 24;
    // Photos only have the previous / next buttons: keep their strip small.
    const int w = m_video ? std::min(width() - 2 * margin, 1100) : m_bar->sizeHint().width();
    const int x = (width() - w) / 2;
    const int barH = m_bar->sizeHint().height();
    m_bar->setGeometry(x, height() - margin - barH, w, barH);
    const int topH = m_top->sizeHint().height();
    m_top->setGeometry(margin, margin, width() - 2 * margin, topH);
}

void FullScreenViewer::updateTimecode() {
    const AVRational rate = m_engine->sequence().frameRate;
    m_timecode->setText(QStringLiteral("%1 / %2").arg(formatTimecode(m_engine->position(), rate),
                                                     formatTimecode(m_engine->duration(), rate)));
}

void FullScreenViewer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_image.isNull())
        return;
    const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(target, m_image);
}

void FullScreenViewer::resizeEvent(QResizeEvent*) {
    layoutControls();
    if (m_video && isVisible()) {
        // Decode at the screen's size so the full-screen picture is sharp.
        m_engine->setPreviewSize(size() * devicePixelRatioF());
        if (!m_engine->isPlaying())
            m_engine->seek(m_engine->position());
    }
}

void FullScreenViewer::keyPressEvent(QKeyEvent* e) {
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    switch (e->key()) {
    case Qt::Key_Escape:
    case Qt::Key_F:
    case Qt::Key_F11:
        close();
        return;
    case Qt::Key_Space:
    case Qt::Key_K:
        if (m_video)
            m_engine->togglePlay();
        break;
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const int dir = e->key() == Qt::Key_Left ? -1 : 1;
        if (!m_video)
            emit photoStepRequested(dir);
        else if (shift) // a second at a time
            m_engine->seek(std::clamp<Ticks>(m_engine->position() + dir * kTicksPerSecond, 0, m_engine->duration()));
        else
            m_engine->stepFrames(dir);
        break;
    }
    case Qt::Key_Home:
        if (m_video)
            m_engine->seek(0);
        break;
    case Qt::Key_End:
        if (m_video)
            m_engine->seek(m_engine->duration());
        break;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    showControls();
}

void FullScreenViewer::mouseMoveEvent(QMouseEvent* e) {
    showControls();
    QWidget::mouseMoveEvent(e);
}

void FullScreenViewer::mousePressEvent(QMouseEvent* e) {
    // A click on the picture plays or pauses, as in most players; it waits
    // for the double-click time so that a double-click only leaves.
    if (e->button() == Qt::LeftButton && m_video)
        m_clickTimer->start();
    showControls();
}

void FullScreenViewer::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    m_clickTimer->stop();
    close();
}

void FullScreenViewer::wheelEvent(QWheelEvent* e) {
    const int dir = e->angleDelta().y() > 0 ? -1 : e->angleDelta().y() < 0 ? 1 : 0;
    if (dir == 0)
        return;
    if (m_video)
        m_engine->stepFrames(dir);
    else
        emit photoStepRequested(dir);
    showControls();
}

void FullScreenViewer::closeEvent(QCloseEvent* e) {
    m_hideTimer->stop();
    m_clickTimer->stop();
    unsetCursor();
    e->accept();
    emit closed();
}

} // namespace lumora

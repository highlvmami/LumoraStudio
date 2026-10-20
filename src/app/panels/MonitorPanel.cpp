#include "MonitorPanel.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "panels/VideoView.h"
#include "playback/PlaybackEngine.h"

namespace lumora {

// ----------------------------------------------------------------- ScrubBar

ScrubBar::ScrubBar(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(22);
    setCursor(Qt::PointingHandCursor);
}

void ScrubBar::setDuration(Ticks d) {
    m_duration = d;
    update();
}
void ScrubBar::setPosition(Ticks p) {
    m_pos = p;
    update();
}
void ScrubBar::setRange(Ticks in, Ticks out) {
    m_in = in;
    m_out = out;
    update();
}

Ticks ScrubBar::posAt(int x) const {
    if (m_duration <= 0 || width() <= 12)
        return 0;
    const double f = std::clamp((x - 6.0) / (width() - 12.0), 0.0, 1.0);
    return static_cast<Ticks>(f * m_duration);
}

void ScrubBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track(6, height() / 2.0 - 3, width() - 12, 6);
    const QPalette pal = palette();
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::Mid));
    p.drawRoundedRect(track, 3, 3);
    auto xOf = [&](Ticks t) { return track.left() + (m_duration > 0 ? track.width() * t / double(m_duration) : 0); };
    if (m_in != kInvalidTicks || m_out != kInvalidTicks) {
        const double a = xOf(m_in == kInvalidTicks ? 0 : m_in);
        const double b = xOf(m_out == kInvalidTicks ? m_duration : m_out);
        p.setBrush(pal.color(QPalette::Highlight).lighter(110));
        p.drawRoundedRect(QRectF(a, track.top(), std::max(2.0, b - a), track.height()), 3, 3);
    }
    const double x = xOf(m_pos);
    p.setBrush(pal.color(QPalette::BrightText));
    p.drawEllipse(QPointF(x, height() / 2.0), 6, 6);
}

void ScrubBar::mousePressEvent(QMouseEvent* e) { emit seekRequested(posAt(e->position().toPoint().x())); }
void ScrubBar::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton)
        emit seekRequested(posAt(e->position().toPoint().x()));
}

// ------------------------------------------------------------- MonitorPanel

MonitorPanel::MonitorPanel(PlaybackEngine* engine, QWidget* parent) : QWidget(parent), m_engine(engine) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto* top = new QHBoxLayout;
    m_sourceBtn = new QToolButton;
    m_sourceBtn->setText(tr("Source"));
    m_sourceBtn->setToolTip(tr("Show the selected file. Double-click a file to open it here."));
    m_programBtn = new QToolButton;
    m_programBtn->setText(tr("Timeline"));
    m_programBtn->setToolTip(tr("Show the edited timeline"));
    for (auto* b : {m_sourceBtn, m_programBtn}) {
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    auto* group = new QButtonGroup(this);
    group->addButton(m_sourceBtn);
    group->addButton(m_programBtn);
    m_programBtn->setChecked(true);
    top->addWidget(m_sourceBtn);
    top->addWidget(m_programBtn);
    top->addStretch();
    layout->addLayout(top);

    m_view = new VideoView;
    m_view->setPlaceholder(tr("Import videos with File → Import media, or drag them here."));
    layout->addWidget(m_view, 1);

    m_scrub = new ScrubBar;
    layout->addWidget(m_scrub);

    auto* controls = new QHBoxLayout;
    controls->setSpacing(2);
    auto makeBtn = [&](const QIcon& icon, const QString& tip) {
        auto* b = new QToolButton;
        b->setIcon(icon);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(20, 20));
        controls->addWidget(b);
        return b;
    };
    auto* toStart = makeBtn(Icons::toStart(), tr("Go to start (Home)"));
    auto* back = makeBtn(Icons::stepBack(), tr("Previous frame (Left)"));
    m_play = makeBtn(Icons::play(), tr("Play / pause (Space)"));
    auto* fwd = makeBtn(Icons::stepForward(), tr("Next frame (Right)"));
    auto* toEnd = makeBtn(Icons::toEnd(), tr("Go to end (End)"));
    controls->addSpacing(12);
    m_timecode = new QLabel(QStringLiteral("00:00:00:00 / 00:00:00:00"));
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_timecode->setFont(mono);
    controls->addWidget(m_timecode);
    controls->addStretch();

    m_sourceTools = new QWidget;
    auto* st = new QHBoxLayout(m_sourceTools);
    st->setContentsMargins(0, 0, 0, 0);
    auto* inBtn = new QToolButton;
    inBtn->setText(tr("In"));
    inBtn->setIcon(Icons::markIn());
    inBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    inBtn->setToolTip(tr("Set in point (I)"));
    auto* outBtn = new QToolButton;
    outBtn->setText(tr("Out"));
    outBtn->setIcon(Icons::markOut());
    outBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    outBtn->setToolTip(tr("Set out point (O)"));
    m_addBtn = new QPushButton(Icons::add(), tr("Add to timeline"));
    m_addBtn->setToolTip(tr("Add the range between the in and out points to the end of the timeline"));
    st->addWidget(inBtn);
    st->addWidget(outBtn);
    st->addWidget(m_addBtn);
    controls->addWidget(m_sourceTools);
    m_sourceTools->setVisible(false);
    layout->addLayout(controls);

    connect(m_sourceBtn, &QToolButton::clicked, this, [this] {
        if (m_source)
            showSource(m_source);
        else
            setMode(Mode::Program);
    });
    connect(m_programBtn, &QToolButton::clicked, this, [this] {
        if (m_program)
            showProgram(*m_program, true);
    });
    connect(toStart, &QToolButton::clicked, this, [this] { m_engine->seek(0); });
    connect(toEnd, &QToolButton::clicked, this,
            [this] { m_engine->seek(m_engine->duration()); });
    connect(back, &QToolButton::clicked, this, [this] { m_engine->stepFrames(-1); });
    connect(fwd, &QToolButton::clicked, this, [this] { m_engine->stepFrames(1); });
    connect(m_play, &QToolButton::clicked, m_engine, &PlaybackEngine::togglePlay);
    connect(inBtn, &QToolButton::clicked, this, &MonitorPanel::setMarkIn);
    connect(outBtn, &QToolButton::clicked, this, &MonitorPanel::setMarkOut);
    connect(m_addBtn, &QPushButton::clicked, this, &MonitorPanel::addSourceToTimeline);
    connect(m_scrub, &ScrubBar::seekRequested, m_engine, &PlaybackEngine::seek);

    connect(m_engine, &PlaybackEngine::frameReady, m_view, [this](const QImage& img) { m_view->setImage(img); });
    connect(m_engine, &PlaybackEngine::positionChanged, this, [this](Ticks t) {
        m_scrub->setPosition(t);
        if (m_mode == Mode::Program)
            m_programPosition = t;
        updateTimecode();
    });
    connect(m_engine, &PlaybackEngine::playingChanged, this,
            [this](bool playing) { m_play->setIcon(playing ? Icons::pause() : Icons::play()); });
    connect(m_view, &VideoView::sizeChanged, m_engine, &PlaybackEngine::setPreviewSize);
    connect(m_view, &VideoView::filesDropped, this, &MonitorPanel::filesDropped);
    connect(m_view, &VideoView::mediaDropped, this, &MonitorPanel::mediaDropped);
}

void MonitorPanel::setMode(Mode m) {
    m_mode = m;
    m_sourceBtn->setChecked(m == Mode::Source);
    m_programBtn->setChecked(m == Mode::Program);
    m_sourceTools->setVisible(m == Mode::Source);
    m_view->setOverlayText(m == Mode::Source && m_source ? m_source->info.path.section(QLatin1Char('/'), -1)
                                                         : QString());
    updateMarksUi();
    emit modeChanged(m);
}

void MonitorPanel::showSource(const MediaItemPtr& item) {
    if (!item)
        return;
    m_engine->pause();
    if (m_mode == Mode::Program)
        m_programPosition = m_engine->position();
    m_source = item;
    m_engine->setSequence(PlaybackSequence::fromMedia(*item), false);
    const Marks mk = m_marks.value(item->id);
    m_engine->seek(mk.in != kInvalidTicks ? mk.in : 0);
    m_scrub->setDuration(m_engine->duration());
    m_view->setPlaceholder(QString());
    setMode(Mode::Source);
    updateTimecode();
}

void MonitorPanel::showProgram(const PlaybackSequence& seq, bool switchMode) {
    m_program = std::make_unique<PlaybackSequence>(seq);
    if (m_mode == Mode::Source && !switchMode)
        return;
    const bool wasSource = m_mode == Mode::Source;
    if (wasSource)
        m_engine->pause();
    m_engine->setSequence(seq, true);
    if (wasSource)
        m_engine->seek(m_programPosition);
    m_scrub->setDuration(seq.duration);
    m_view->setPlaceholder(seq.segments.empty()
                               ? tr("Import videos with File → Import media, or drag them here.")
                               : QString());
    if (seq.segments.empty())
        m_view->setImage({});
    if (m_mode != Mode::Program)
        setMode(Mode::Program);
    updateTimecode();
}

Ticks MonitorPanel::markIn() const {
    if (!m_source)
        return kInvalidTicks;
    return m_marks.value(m_source->id).in;
}

Ticks MonitorPanel::markOut() const {
    if (!m_source)
        return kInvalidTicks;
    return m_marks.value(m_source->id).out;
}

void MonitorPanel::setMarkIn() {
    if (m_mode != Mode::Source || !m_source)
        return;
    Marks& mk = m_marks[m_source->id];
    mk.in = snapToFrame(m_engine->position(), m_engine->sequence().frameRate);
    if (mk.out != kInvalidTicks && mk.out <= mk.in)
        mk.out = kInvalidTicks;
    updateMarksUi();
}

void MonitorPanel::setMarkOut() {
    if (m_mode != Mode::Source || !m_source)
        return;
    Marks& mk = m_marks[m_source->id];
    // The out point is exclusive: include the frame on screen.
    const Ticks fd = frameDuration(m_engine->sequence().frameRate);
    mk.out = std::min(snapToFrame(m_engine->position(), m_engine->sequence().frameRate) + fd, m_engine->duration());
    if (mk.in != kInvalidTicks && mk.in >= mk.out)
        mk.in = kInvalidTicks;
    updateMarksUi();
}

void MonitorPanel::clearMarks() {
    if (!m_source)
        return;
    m_marks.remove(m_source->id);
    updateMarksUi();
}

void MonitorPanel::addSourceToTimeline() {
    if (!m_source)
        return;
    const Marks mk = m_marks.value(m_source->id);
    const Ticks in = mk.in == kInvalidTicks ? 0 : mk.in;
    const Ticks out = mk.out == kInvalidTicks ? m_source->info.duration : mk.out;
    emit addToTimelineRequested(m_source->id, in, out);
}

void MonitorPanel::updateMarksUi() {
    if (m_mode == Mode::Source && m_source) {
        const Marks mk = m_marks.value(m_source->id);
        m_scrub->setRange(mk.in, mk.out);
        const Ticks in = mk.in == kInvalidTicks ? 0 : mk.in;
        const Ticks out = mk.out == kInvalidTicks ? m_source->info.duration : mk.out;
        m_addBtn->setText(tr("Add to timeline (%1)").arg(formatSeconds(out - in)));
    } else {
        m_scrub->setRange(kInvalidTicks, kInvalidTicks);
    }
}

void MonitorPanel::updateTimecode() {
    const AVRational rate = m_engine->sequence().frameRate;
    m_timecode->setText(QStringLiteral("%1 / %2").arg(formatTimecode(m_engine->position(), rate),
                                                     formatTimecode(m_engine->duration(), rate)));
}

} // namespace lumora

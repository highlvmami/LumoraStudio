#pragma once

#include <QHash>
#include <QUuid>
#include <QWidget>

#include "TimeBase.h"
#include "media/MediaPool.h"

class QLabel;
class QToolButton;
class QPushButton;

namespace lumora {

class PlaybackEngine;
class VideoView;
struct PlaybackSequence;

// Thin bar under the preview: playhead, in/out range, click/drag to seek.
class ScrubBar : public QWidget {
    Q_OBJECT
public:
    explicit ScrubBar(QWidget* parent = nullptr);
    void setDuration(Ticks d);
    void setPosition(Ticks p);
    void setRange(Ticks in, Ticks out); // kInvalidTicks hides a mark
    QSize sizeHint() const override { return {200, 22}; }

signals:
    void seekRequested(lumora::Ticks t);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    Ticks posAt(int x) const;
    Ticks m_duration = 0, m_pos = 0, m_in = kInvalidTicks, m_out = kInvalidTicks;
};

// Preview monitor with a source mode (one file, set I/O, add to timeline)
// and a program mode (the timeline).
class MonitorPanel : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Source, Program };

    MonitorPanel(PlaybackEngine* engine, QWidget* parent = nullptr);

    Mode mode() const { return m_mode; }
    void showSource(const MediaItemPtr& item);
    void showProgram(const PlaybackSequence& seq, bool switchMode);
    MediaItemPtr sourceItem() const { return m_source; }

    // Source marks (media time).
    Ticks markIn() const;
    Ticks markOut() const;

public slots:
    void setMarkIn();
    void setMarkOut();
    void clearMarks();
    void addSourceToTimeline();

signals:
    void modeChanged(lumora::MonitorPanel::Mode mode);
    void addToTimelineRequested(const QUuid& media, lumora::Ticks in, lumora::Ticks out);
    void filesDropped(const QStringList& paths);
    void mediaDropped(const QList<QUuid>& ids);

private:
    void setMode(Mode m);
    void updateTimecode();
    void updateMarksUi();

    PlaybackEngine* m_engine;
    VideoView* m_view;
    ScrubBar* m_scrub;
    QLabel* m_timecode;
    QToolButton* m_play;
    QToolButton* m_sourceBtn;
    QToolButton* m_programBtn;
    QWidget* m_sourceTools;
    QPushButton* m_addBtn;
    Mode m_mode = Mode::Program;
    MediaItemPtr m_source;
    struct Marks {
        Ticks in = kInvalidTicks;
        Ticks out = kInvalidTicks;
    };
    QHash<QUuid, Marks> m_marks;
    Ticks m_programPosition = 0;
    std::unique_ptr<PlaybackSequence> m_program;
};

} // namespace lumora

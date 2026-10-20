#pragma once

#include <QAudioFormat>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "playback/PlaybackSequence.h"

class QAudioSink;

namespace lumora {

class AudioRingDevice;

// Preview playback. A video thread composites the frame that belongs to the
// current clock position with FrameRenderer at preview size (dropping frames
// when it falls behind) and an audio thread mixes ahead with AudioMixer into
// a ring buffer consumed by QAudioSink. It uses the same compositor and mixer
// as the export, so the preview shows what will be written.
class PlaybackEngine : public QObject {
    Q_OBJECT
public:
    explicit PlaybackEngine(QObject* parent = nullptr);
    ~PlaybackEngine() override;

    void setSequence(const PlaybackSequence& seq, bool keepPosition = true);
    const PlaybackSequence& sequence() const { return m_sequence; }

    void play();
    void pause();
    void togglePlay() { m_playing ? pause() : play(); }
    bool isPlaying() const { return m_playing; }
    void seek(Ticks t);
    void stepFrames(int n);
    Ticks position() const { return m_position; }
    Ticks duration() const { return m_sequence.duration; }

    void setPreviewSize(QSize size);
    bool audioAvailable() const { return m_audioOk; }

signals:
    void frameReady(const QImage& image, lumora::Ticks time);
    void positionChanged(lumora::Ticks time);
    void playingChanged(bool playing);

private:
    struct Command {
        enum Type { None, Render, Play, Pause, Quit } type = None;
        Ticks time = 0;
        quint64 generation = 0;
    };

    void videoLoop();
    void audioLoop();
    void post(Command::Type type, Ticks t);
    void startAudio(Ticks from);
    void stopAudio();

    PlaybackSequence m_sequence; // GUI thread copy
    Ticks m_position = 0;
    bool m_playing = false;

    // Shared with the threads.
    std::mutex m_mutex;
    std::condition_variable m_cv;
    Command m_pending;
    PlaybackSequence m_threadSequence;
    QSize m_previewSize{1280, 720};
    std::atomic<quint64> m_generation{0};
    std::thread m_videoThread;
    std::thread m_audioThread;
    std::atomic_bool m_audioRun{false};
    Ticks m_audioFrom = 0;

    quint64 m_sequenceVersion = 0; // bumped on every setSequence (guarded by m_mutex)

    QAudioFormat m_format;
    QAudioSink* m_sink = nullptr;
    AudioRingDevice* m_ring = nullptr;
    bool m_audioOk = false;
};

} // namespace lumora

#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>

#include "TimeBase.h"

class QAudioSource;
class QCheckBox;
class QComboBox;
class QIODevice;
class QLabel;
class QProgressBar;
class QPushButton;
class QScreen;
class QSpinBox;

namespace lumora {

class LiveWriter;

// The microphone as 16-bit samples, handed to a callback as they arrive,
// with the loudness for a level meter.
class MicCapture : public QObject {
    Q_OBJECT
public:
    using Sink = std::function<void(const std::int16_t* samples, int frames)>;

    explicit MicCapture(QObject* parent = nullptr);
    ~MicCapture() override;

    // Picks a 16-bit format the device can record. False when it cannot.
    bool prepare(const QAudioDevice& device, QString* error);
    const QAudioFormat& format() const { return m_format; }
    bool start(Sink sink, QString* error);
    void stop();

signals:
    void level(double peak); // 0 .. 1, a few times a second

private:
    void read();

    QAudioDevice m_device;
    QAudioFormat m_format;
    QAudioSource* m_source = nullptr;
    QIODevice* m_io = nullptr;
    Sink m_sink;
    QByteArray m_carry;
    double m_peak = 0;
    QElapsedTimer m_levelClock;
};

// Writes 16-bit samples to a WAV file (lossless, opens everywhere).
class WavWriter {
public:
    bool open(const QString& path, int sampleRate, int channels);
    void write(const std::int16_t* samples, int frames);
    bool close();
    qint64 frames() const { return m_frames; }

private:
    QFile m_file;
    int m_rate = 48000, m_channels = 1;
    qint64 m_frames = 0;
};

// Records the microphone while the timeline plays, and puts the recording on
// an audio track where the recording started.
class VoiceoverDialog : public QDialog {
    Q_OBJECT
public:
    VoiceoverDialog(Ticks at, QWidget* parent = nullptr);
    ~VoiceoverDialog() override;

signals:
    // The timeline should play from `at` (muted when asked) / stop.
    void playRequested(lumora::Ticks at, bool muted);
    void stopRequested();
    // A finished recording that starts at `at` on the timeline.
    void recorded(const QString& path, lumora::Ticks at);

private:
    void toggle();
    void begin();
    void end(bool keep);
    void tick();

    Ticks m_at;
    QComboBox* m_device;
    QCheckBox* m_play;
    QCheckBox* m_countdown;
    QProgressBar* m_meter;
    QLabel* m_status;
    QPushButton* m_record;
    std::unique_ptr<MicCapture> m_mic;
    std::unique_ptr<WavWriter> m_wav;
    QString m_path;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_count = 0; // countdown seconds left
    bool m_recording = false;
};

// Records the screen (and the microphone, if wanted) to an MP4 file. While it
// records, the main window steps aside and a small bar with a Stop button
// stays on top.
class ScreenRecordDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScreenRecordDialog(QWidget* parent = nullptr);
    ~ScreenRecordDialog() override;

    static bool available();

signals:
    void recorded(const QString& path);

private:
    void start();
    void grab();
    void stop();

    QComboBox* m_screen;
    QComboBox* m_fps;
    QCheckBox* m_cursor;
    QCheckBox* m_micOn;
    QComboBox* m_mic;
    QCheckBox* m_hide;
    QWidget* m_bar = nullptr;
    QLabel* m_barTime = nullptr;
    QPointer<QScreen> m_target;
    std::unique_ptr<LiveWriter> m_writer;
    std::unique_ptr<MicCapture> m_capture;
    QTimer m_grabTimer;
    QElapsedTimer m_clock;
    QString m_path;
    int m_dropped = 0;
    QWidget* m_hidden = nullptr; // the main window while it is minimised
};

} // namespace lumora

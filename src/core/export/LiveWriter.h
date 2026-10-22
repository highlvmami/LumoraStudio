#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QString>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lumora {

// Writes pictures and sound that arrive while something is being recorded
// (the screen, the microphone) to an MP4 file: H.264 at high quality and
// AAC. Pictures may come at an uneven rate; each carries its own time.
// Encoding runs on a thread of its own so the caller never waits for it.
class LiveWriter {
    Q_DECLARE_TR_FUNCTIONS(lumora::LiveWriter)

public:
    struct Options {
        QSize size;              // picture size (made even)
        int frameRate = 30;      // nominal rate, for the encoder's planning
        int sampleRate = 48000;  // 0 = no sound
        int channels = 2;
        int quality = 18;        // x264 CRF: 18 looks like the source
    };

    LiveWriter();
    ~LiveWriter();
    LiveWriter(const LiveWriter&) = delete;
    LiveWriter& operator=(const LiveWriter&) = delete;

    bool open(const QString& path, const Options& options, QString* error);
    // `ms` since the recording started. Pictures are scaled to the size.
    void addPicture(const QImage& image, std::int64_t ms);
    // Interleaved 16-bit samples, in order and without gaps.
    void addSound(const std::int16_t* samples, int frames);
    // Writes what is queued and closes the file.
    bool finish(QString* error);

    // Pictures waiting to be encoded (the caller may drop frames when this grows).
    int backlog() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace lumora

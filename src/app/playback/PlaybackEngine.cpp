#include "PlaybackEngine.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QSettings>
#include <QTransform>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

#include "media/MediaPool.h"
#include "render/AudioMixer.h"
#include "render/FrameRenderer.h"

namespace lumora {

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
// How far ahead of the playhead upcoming clips get their decoders ready.
constexpr Ticks kPrefetchHorizon = 2 * kTicksPerSecond;

const char* const kHardwareKey = "preview/hardwareDecoding";

const char* const kProxyKey = "preview/proxies";

std::atomic_bool& proxyFlag() {
    static std::atomic_bool on{QSettings().value(QLatin1String(kProxyKey), true).toBool()};
    return on;
}

std::atomic_bool& hardwareFlag() {
    static std::atomic_bool on{QSettings().value(QLatin1String(kHardwareKey), true).toBool()};
    return on;
}

// Thread-safe float ring buffer exposed as a QIODevice for QAudioSink (pull
// mode). The decoder thread blocks in write() while the buffer is full.
class AudioRingDevice : public QIODevice {
public:
    explicit AudioRingDevice(size_t capacityFrames) : m_buffer(capacityFrames * kAudioChannels) {}

    void clear() {
        std::lock_guard lock(m_mutex);
        m_read = m_write = m_size = 0;
        m_abort = false;
    }
    void abort() {
        {
            std::lock_guard lock(m_mutex);
            m_abort = true;
        }
        m_cv.notify_all();
    }
    // Returns false when aborted.
    bool writeSamples(const float* data, size_t count) {
        while (count > 0) {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_abort || m_size < m_buffer.size(); });
            if (m_abort)
                return false;
            const size_t n = std::min(count, m_buffer.size() - m_size);
            for (size_t i = 0; i < n; ++i) {
                m_buffer[m_write] = data[i];
                m_write = (m_write + 1) % m_buffer.size();
            }
            m_size += n;
            data += n;
            count -= n;
        }
        return true;
    }

    // Devices that do not take float samples get 16 bit ones.
    void setInt16(bool on) { m_int16 = on; }

protected:
    qint64 readData(char* out, qint64 maxlen) override {
        const size_t bytes = m_int16 ? sizeof(qint16) : sizeof(float);
        const size_t want = static_cast<size_t>(maxlen) / bytes;
        size_t n;
        {
            std::lock_guard lock(m_mutex);
            n = std::min(want, m_size);
            for (size_t i = 0; i < n; ++i) {
                const float v = m_buffer[m_read];
                if (m_int16)
                    reinterpret_cast<qint16*>(out)[i] = static_cast<qint16>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
                else
                    reinterpret_cast<float*>(out)[i] = v;
                m_read = (m_read + 1) % m_buffer.size();
            }
            m_size -= n;
        }
        m_cv.notify_all();
        // Underrun: play silence instead of stalling the device.
        std::memset(out + n * bytes, 0, (want - n) * bytes);
        return static_cast<qint64>(want * bytes);
    }
    qint64 writeData(const char*, qint64) override { return -1; }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 4096 + QIODevice::bytesAvailable(); }

private:
    std::vector<float> m_buffer;
    size_t m_read = 0, m_write = 0, m_size = 0;
    bool m_abort = false;
    bool m_int16 = false;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

PlaybackEngine::PlaybackEngine(QObject* parent) : QObject(parent) {
    m_format.setSampleRate(kAudioRate);
    m_format.setChannelCount(kAudioChannels);
    m_format.setSampleFormat(QAudioFormat::Float);
    m_ring = new AudioRingDevice(kAudioRate / 2);
    m_ring->open(QIODevice::ReadOnly);
    m_devices = new QMediaDevices(this);
    openAudioOutput();
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &PlaybackEngine::audioOutputChanged);
    m_videoThread = std::thread([this] { videoLoop(); });
}

PlaybackEngine::~PlaybackEngine() {
    stopAudio();
    post(Command::Quit, 0);
    if (m_videoThread.joinable())
        m_videoThread.join();
    delete m_ring;
}

void PlaybackEngine::post(Command::Type type, Ticks t) {
    {
        std::lock_guard lock(m_mutex);
        // Quit always wins; otherwise the newest command replaces older ones
        // (fast scrubbing only renders the latest position).
        if (m_pending.type != Command::Quit)
            m_pending = Command{type, t, m_generation.load()};
    }
    m_cv.notify_all();
}

void PlaybackEngine::setSequence(const PlaybackSequence& seq, bool keepPosition) {
    m_sequence = seq;
    {
        std::lock_guard lock(m_mutex);
        m_threadSequence = seq;
        ++m_sequenceVersion;
    }
    if (!keepPosition)
        m_position = 0;
    m_position = std::clamp<Ticks>(m_position, 0, std::max<Ticks>(0, seq.duration - frameDuration(seq.frameRate)));
    if (m_playing) {
        startAudio(m_position);
        post(Command::Render, m_position);
    } else {
        post(Command::Render, m_position);
    }
    emit positionChanged(m_position);
}

void PlaybackEngine::setPreviewSize(QSize size) {
    std::lock_guard lock(m_mutex);
    m_previewSize = size;
}

void PlaybackEngine::play() {
    if (m_playing || m_sequence.isEmpty())
        return;
    if (m_position >= m_sequence.duration - frameDuration(m_sequence.frameRate))
        m_position = 0;
    m_playing = true;
    m_reopened = false;
    startAudio(m_position);
    post(Command::Play, m_position);
    emit playingChanged(true);
}

void PlaybackEngine::pause() {
    if (!m_playing)
        return;
    m_playing = false;
    stopAudio();
    post(Command::Pause, m_position);
    emit playingChanged(false);
}

void PlaybackEngine::seek(Ticks t) {
    const Ticks maxPos = std::max<Ticks>(0, m_sequence.duration - frameDuration(m_sequence.frameRate));
    m_position = std::clamp<Ticks>(t, 0, maxPos);
    if (m_playing) {
        startAudio(m_position);
        post(Command::Play, m_position);
    } else {
        post(Command::Render, m_position);
    }
    emit positionChanged(m_position);
}

void PlaybackEngine::stepFrames(int n) {
    pause();
    const Ticks fd = frameDuration(m_sequence.frameRate);
    seek(snapToFrame(m_position, m_sequence.frameRate) + n * fd);
}

bool PlaybackEngine::openAudioOutput() {
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (m_sink && dev.id() == m_deviceId && (m_sink->error() == QAudio::NoError || m_reopened))
        return false;
    // A device that keeps failing is reopened once per play.
    m_reopened = m_sink && dev.id() == m_deviceId;
    if (m_sink)
        m_sink->disconnect(this);
    delete m_sink;
    m_sink = nullptr;
    m_audioOk = false;
    m_deviceId = dev.id();
    if (dev.isNull())
        return true;
    QAudioFormat format = m_format;
    if (!dev.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        if (!dev.isFormatSupported(format))
            return true;
    }
    const bool int16 = format.sampleFormat() == QAudioFormat::Int16;
    m_ring->setInt16(int16);
    m_sink = new QAudioSink(dev, format, this);
    m_sink->setBufferSize(kAudioRate * kAudioChannels * (int16 ? sizeof(qint16) : sizeof(float)) / 10);
    // A device that goes away while playing stops the sink with an error.
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State st) {
        if (st == QAudio::StoppedState && m_sink && m_sink->error() != QAudio::NoError && !m_reopened)
            QMetaObject::invokeMethod(this, &PlaybackEngine::audioOutputChanged, Qt::QueuedConnection);
    });
    m_audioOk = true;
    return true;
}

void PlaybackEngine::audioOutputChanged() {
    stopAudio();
    openAudioOutput();
    if (m_playing)
        startAudio(m_position);
}

void PlaybackEngine::startAudio(Ticks from) {
    stopAudio();
    openAudioOutput();
    if (!m_audioOk)
        return;
    m_ring->clear();
    m_audioFrom = from;
    const quint64 gen = ++m_generation;
    PlaybackSequence seq = m_sequence;
    m_audioRun = true;
    m_audioThread = std::thread([this, seq, from, gen] {
        (void)gen;
        if (!seq.media)
            return;
        AudioMixer mixer(seq.media.get(), kAudioRate, kAudioChannels);
        mixer.setTimeline(seq.timeline);
        constexpr int chunk = 1024;
        std::vector<float> buf(chunk * kAudioChannels);
        std::int64_t sample = av_rescale(from, kAudioRate, kTicksPerSecond);
        const std::int64_t end = av_rescale(seq.duration, kAudioRate, kTicksPerSecond);
        while (m_audioRun && sample < end) {
            const int n = static_cast<int>(std::min<std::int64_t>(chunk, end - sample));
            mixer.mix(sample, n, buf.data());
            if (!m_ring->writeSamples(buf.data(), static_cast<size_t>(n) * kAudioChannels))
                return;
            sample += n;
        }
    });
    m_sink->setVolume(m_muted ? 0.0 : 1.0);
    m_sink->start(m_ring);
}

void PlaybackEngine::stopAudio() {
    m_audioRun = false;
    m_ring->abort();
    if (m_audioThread.joinable())
        m_audioThread.join();
    if (m_sink)
        m_sink->stop();
}

void PlaybackEngine::videoLoop() {
    using Clock = std::chrono::steady_clock;
    bool playing = false;
    Ticks playOrigin = 0;
    Clock::time_point wallOrigin;
    std::unique_ptr<FrameRenderer> renderer;
    std::shared_ptr<MediaPool> media; // keeps the renderer's media alive
    quint64 version = ~quint64(0);
    QSize outputSize;

    auto toWall = [&](Ticks t) {
        return wallOrigin + std::chrono::nanoseconds(av_rescale(t - playOrigin, 1'000'000'000, kTicksPerSecond));
    };

    while (true) {
        Command cmd;
        PlaybackSequence seq;
        QSize size;
        bool changed = false;
        {
            std::unique_lock lock(m_mutex);
            if (!playing)
                m_cv.wait(lock, [&] { return m_pending.type != Command::None; });
            cmd = m_pending;
            m_pending = {};
            if (version != m_sequenceVersion) {
                version = m_sequenceVersion;
                changed = true;
            }
            seq = m_threadSequence;
            size = m_previewSize;
        }
        if (cmd.type == Command::Quit)
            return;

        if (changed || !renderer || media != seq.media) {
            if (!renderer || media != seq.media) {
                renderer = std::make_unique<FrameRenderer>(seq.media.get());
                renderer->setDrawSubtitles(true); // the preview shows them; export decides for itself
                media = seq.media;
            }
            renderer->setTimeline(seq.timeline);
        }
        renderer->setHardwareDecoding(hardwareFlag());
        renderer->setUseProxies(proxyFlag());

        Ticks renderAt = kInvalidTicks;
        switch (cmd.type) {
        case Command::Play:
            playing = true;
            playOrigin = cmd.time;
            wallOrigin = Clock::now();
            break;
        case Command::Pause:
            playing = false;
            break;
        case Command::Render:
            if (playing) {
                playOrigin = cmd.time;
                wallOrigin = Clock::now();
            } else {
                renderAt = cmd.time;
            }
            break;
        default:
            break;
        }

        Ticks pos = renderAt;
        if (playing) {
            pos = playOrigin + av_rescale(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - wallOrigin).count(),
                                          kTicksPerSecond, 1'000'000'000);
            if (pos >= seq.duration) {
                playing = false;
                QMetaObject::invokeMethod(this, [this] {
                    if (!m_playing)
                        return;
                    m_playing = false;
                    stopAudio();
                    emit playingChanged(false);
                }, Qt::QueuedConnection);
                continue;
            }
            pos = snapToFrame(pos, seq.frameRate);
        }
        if (pos == kInvalidTicks)
            continue;

        // Preview resolution: the composition scaled to fit the view.
        QSize target = seq.canvas.scaled(size, Qt::KeepAspectRatio);
        if (target.width() > seq.canvas.width())
            target = seq.canvas;
        target = target.expandedTo(QSize(16, 9));
        if (target != outputSize) {
            outputSize = target;
            renderer->setOutputSize(target);
        }

        const QImage image = seq.media ? renderer->render(pos) : QImage();
        if (!image.isNull())
            QMetaObject::invokeMethod(this, [this, image, pos] { emit frameReady(image, pos); }, Qt::QueuedConnection);
        if (playing) {
            // Get the clips after the next cuts ready while this one plays.
            if (seq.media)
                renderer->prefetch(pos, kPrefetchHorizon);
            QMetaObject::invokeMethod(this, [this, pos] {
                if (!m_playing)
                    return;
                m_position = pos;
                emit positionChanged(pos);
            }, Qt::QueuedConnection);
            std::unique_lock lock(m_mutex);
            m_cv.wait_until(lock, toWall(pos + frameDuration(seq.frameRate)),
                            [&] { return m_pending.type != Command::None; });
        }
    }
}

bool PlaybackEngine::hardwareDecoding() { return hardwareFlag(); }

void PlaybackEngine::setHardwareDecoding(bool on) {
    QSettings().setValue(QLatin1String(kHardwareKey), on);
    hardwareFlag() = on; // the video thread picks it up with the next frame
}

bool PlaybackEngine::useProxies() { return proxyFlag(); }

void PlaybackEngine::setUseProxies(bool on) {
    QSettings().setValue(QLatin1String(kProxyKey), on);
    proxyFlag() = on;
}

void PlaybackEngine::setMuted(bool muted) {
    m_muted = muted;
    if (m_sink)
        m_sink->setVolume(muted ? 0.0 : 1.0);
}

} // namespace lumora

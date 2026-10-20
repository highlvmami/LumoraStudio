#include "PlaybackEngine.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QTransform>
#include <chrono>
#include <cstring>
#include <vector>

#include "media/AudioDecoder.h"
#include "media/VideoDecoder.h"

namespace lumora {

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;

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

protected:
    qint64 readData(char* out, qint64 maxlen) override {
        const size_t want = static_cast<size_t>(maxlen) / sizeof(float);
        auto* dst = reinterpret_cast<float*>(out);
        size_t n;
        {
            std::lock_guard lock(m_mutex);
            n = std::min(want, m_size);
            for (size_t i = 0; i < n; ++i) {
                dst[i] = m_buffer[m_read];
                m_read = (m_read + 1) % m_buffer.size();
            }
            m_size -= n;
        }
        m_cv.notify_all();
        // Underrun: play silence instead of stalling the device.
        std::memset(dst + n, 0, (want - n) * sizeof(float));
        return static_cast<qint64>(want * sizeof(float));
    }
    qint64 writeData(const char*, qint64) override { return -1; }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 4096 + QIODevice::bytesAvailable(); }

private:
    std::vector<float> m_buffer;
    size_t m_read = 0, m_write = 0, m_size = 0;
    bool m_abort = false;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

PlaybackEngine::PlaybackEngine(QObject* parent) : QObject(parent) {
    m_format.setSampleRate(kAudioRate);
    m_format.setChannelCount(kAudioChannels);
    m_format.setSampleFormat(QAudioFormat::Float);
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (!dev.isNull() && dev.isFormatSupported(m_format)) {
        m_ring = new AudioRingDevice(kAudioRate / 2);
        m_ring->open(QIODevice::ReadOnly);
        m_sink = new QAudioSink(dev, m_format, this);
        m_sink->setBufferSize(kAudioRate * kAudioChannels * sizeof(float) / 10);
        m_audioOk = true;
    }
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
    if (m_playing || m_sequence.segments.empty())
        return;
    if (m_position >= m_sequence.duration - frameDuration(m_sequence.frameRate))
        m_position = 0;
    m_playing = true;
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

void PlaybackEngine::startAudio(Ticks from) {
    stopAudio();
    if (!m_audioOk)
        return;
    m_ring->clear();
    m_audioFrom = from;
    const quint64 gen = ++m_generation;
    PlaybackSequence seq = m_sequence;
    m_audioRun = true;
    m_audioThread = std::thread([this, seq, from, gen] {
        (void)gen;
        std::map<QString, std::unique_ptr<AudioDecoder>> decoders;
        std::vector<float> silence(4096 * kAudioChannels, 0.0f);
        std::int64_t written = 0; // frames written since `from`
        auto targetFrames = [&](Ticks t) { return av_rescale(t - from, kAudioRate, kTicksPerSecond); };
        auto writeSilenceUntil = [&](std::int64_t frame) {
            while (written < frame && m_audioRun) {
                const std::int64_t n = std::min<std::int64_t>(4096, frame - written);
                if (!m_ring->writeSamples(silence.data(), static_cast<size_t>(n) * kAudioChannels))
                    return false;
                written += n;
            }
            return m_audioRun.load();
        };
        for (const auto& seg : seq.segments) {
            if (!m_audioRun)
                return;
            if (seg.end() <= from)
                continue;
            if (!writeSilenceUntil(targetFrames(std::max(seg.start, from))))
                return;
            const std::int64_t segEndFrame = targetFrames(seg.end());
            if (!seg.audio) {
                if (!writeSilenceUntil(segEndFrame))
                    return;
                continue;
            }
            auto& dec = decoders[seg.path];
            if (!dec) {
                dec = std::make_unique<AudioDecoder>();
                if (!dec->open(seg.path, kAudioRate, kAudioChannels))
                    dec.reset();
            }
            if (!dec) {
                if (!writeSilenceUntil(segEndFrame))
                    return;
                continue;
            }
            const Ticks segFrom = std::max(seg.start, from);
            dec->seek(seg.sourceIn + (segFrom - seg.start));
            std::vector<float> samples;
            Ticks chunkStart = 0;
            while (m_audioRun && written < segEndFrame && dec->next(samples, chunkStart)) {
                // Map the chunk's source time to output frames.
                const Ticks seqTime = seg.start + (chunkStart - seg.sourceIn);
                std::int64_t pos = targetFrames(seqTime);
                std::int64_t count = static_cast<std::int64_t>(samples.size()) / kAudioChannels;
                std::int64_t skip = 0;
                if (pos < written) {
                    skip = std::min(count, written - pos);
                } else if (pos > written) {
                    if (!writeSilenceUntil(std::min(pos, segEndFrame)))
                        return;
                }
                count = std::min(count - skip, segEndFrame - written);
                if (count > 0) {
                    if (!m_ring->writeSamples(samples.data() + skip * kAudioChannels,
                                              static_cast<size_t>(count) * kAudioChannels))
                        return;
                    written += count;
                }
            }
            if (!writeSilenceUntil(segEndFrame))
                return;
        }
    });
    m_sink->start(m_ring);
}

void PlaybackEngine::stopAudio() {
    if (!m_audioOk)
        return;
    m_audioRun = false;
    m_ring->abort();
    if (m_audioThread.joinable())
        m_audioThread.join();
    m_sink->stop();
}

VideoDecoder* PlaybackEngine::decoderFor(const QString& path) {
    auto it = m_decoders.find(path);
    if (it != m_decoders.end())
        return it->second.get();
    auto dec = std::make_unique<VideoDecoder>();
    if (!dec->open(path))
        return nullptr;
    if (m_decoderOrder.size() >= 6) {
        m_decoders.erase(m_decoderOrder.front());
        m_decoderOrder.pop_front();
    }
    m_decoderOrder.push_back(path);
    return (m_decoders[path] = std::move(dec)).get();
}

void PlaybackEngine::videoLoop() {
    using Clock = std::chrono::steady_clock;
    FrameConverter converter;
    bool playing = false;
    Ticks playOrigin = 0;
    Clock::time_point wallOrigin;
    QString lastKey;

    auto toWall = [&](Ticks t) {
        return wallOrigin + std::chrono::nanoseconds(av_rescale(t - playOrigin, 1'000'000'000, kTicksPerSecond));
    };

    while (true) {
        Command cmd;
        PlaybackSequence seq;
        QSize size;
        {
            std::unique_lock lock(m_mutex);
            if (!playing)
                m_cv.wait(lock, [&] { return m_pending.type != Command::None; });
            cmd = m_pending;
            m_pending = {};
            seq = m_threadSequence;
            size = m_previewSize;
        }
        if (cmd.type == Command::Quit)
            return;

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
        }
        if (pos == kInvalidTicks)
            continue;

        Ticks next = pos + frameDuration(seq.frameRate);
        const PlaybackSegment* seg = seq.segmentAt(pos);
        QString key;
        QImage image;
        if (seg && seg->video) {
            if (VideoDecoder* dec = decoderFor(seg->path)) {
                Ticks fpts = 0, fdur = 0;
                const AVFrame* f = dec->frameAt(seg->sourceIn + (pos - seg->start), &fpts, &fdur);
                key = seg->path + QLatin1Char('@') + QString::number(fpts);
                const Ticks frameEnd = seg->start + (fpts + std::max<Ticks>(fdur, 1) - seg->sourceIn);
                next = std::clamp(frameEnd, pos + 1, seg->end());
                if (f && (key != lastKey || !playing)) {
                    QSize max = size;
                    if (seg->rotation == 90 || seg->rotation == 270)
                        max.transpose();
                    image = converter.convert(f, max);
                    if (seg->rotation != 0)
                        image = image.transformed(QTransform().rotate(seg->rotation));
                }
            }
        } else {
            key = QStringLiteral("black");
            if (const PlaybackSegment* nextSeg = [&]() -> const PlaybackSegment* {
                    for (const auto& s : seq.segments)
                        if (s.start > pos)
                            return &s;
                    return nullptr;
                }())
                next = nextSeg->start;
            if (key != lastKey || !playing) {
                image = QImage(16, 9, QImage::Format_RGBX8888);
                image.fill(Qt::black);
            }
        }
        if (!image.isNull()) {
            lastKey = key;
            QMetaObject::invokeMethod(this, [this, image, pos] { emit frameReady(image, pos); }, Qt::QueuedConnection);
        }
        if (playing) {
            QMetaObject::invokeMethod(this, [this, pos] {
                if (!m_playing)
                    return;
                m_position = pos;
                emit positionChanged(pos);
            }, Qt::QueuedConnection);
            std::unique_lock lock(m_mutex);
            m_cv.wait_until(lock, toWall(next), [&] { return m_pending.type != Command::None; });
        }
    }
}

} // namespace lumora

#include "MediaCache.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QTransform>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

#include "image/HeifDecoder.h"
#include "image/RawDecoder.h"
#include "media/AudioDecoder.h"
#include "media/Proxy.h"
#include "media/VideoDecoder.h"
#include "playback/PlaybackEngine.h"

namespace lumora {

MediaCache::MediaCache(MediaPool* pool, QObject* parent) : QObject(parent), m_pool(pool) {
    m_proxyPool.setMaxThreadCount(1);
    connect(pool, &MediaPool::itemAdded, this, &MediaCache::analyze);
    connect(pool, &MediaPool::cleared, this, [this] {
        m_thumbs.clear();
        m_peaks.clear();
    });
}

MediaCache::~MediaCache() {
    *m_quit = true; // stops a copy in progress (its file is left out)
    m_proxyPool.clear();
    m_proxyPool.waitForDone();
}

void MediaCache::makeProxy(const MediaItemPtr& item) {
    if (!PlaybackEngine::useProxies() || !proxy::wanted(item->info) || !item->proxy().isEmpty())
        return;
    const QString dest = proxy::pathFor(item->path);
    if (QFile::exists(dest)) {
        // Made in an earlier session.
        item->setProxy(dest);
        emit proxyFinished(item->id, true);
        return;
    }
    QPointer<MediaCache> self(this);
    auto quit = m_quit;
    const bool hardware = PlaybackEngine::hardwareDecoding();
    m_proxyPool.start([self, quit, item, dest, hardware] {
        if (*quit)
            return;
        const QString name = QFileInfo(item->path).fileName();
        int shown = -1;
        auto progress = [&](double p) {
            const int percent = static_cast<int>(p * 100);
            if (percent != shown) {
                shown = percent;
                QMetaObject::invokeMethod(self, [self, name, percent] {
                    if (self)
                        emit self->proxyProgress(name, percent);
                }, Qt::QueuedConnection);
            }
            return !*quit;
        };
        const bool ok = proxy::make(item->path, dest, progress, hardware);
        if (*quit)
            return;
        if (ok)
            item->setProxy(dest);
        const QUuid id = item->id;
        QMetaObject::invokeMethod(self, [self, id, ok] {
            if (self)
                emit self->proxyFinished(id, ok);
        }, Qt::QueuedConnection);
    });
}

QImage MediaCache::makeVideoThumbnail(const QString& path, Ticks at, QSize size) {
    VideoDecoder dec;
    if (!dec.open(path, nullptr, 2))
        return {};
    const AVFrame* f = dec.frameAt(at);
    if (!f)
        return {};
    FrameConverter conv;
    QImage img = conv.convert(f, size * 2);
    if (dec.info().rotation)
        img = img.transformed(QTransform().rotate(dec.info().rotation));
    return img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage MediaCache::makeImageThumbnail(const QString& path, QSize size) {
    if (raw::isRawFile(path))
        return raw::thumbnail(path, size);
    if (heif::isHeifFile(path))
        return heif::thumbnail(path, size);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid())
        reader.setScaledSize(full.scaled(size, Qt::KeepAspectRatio));
    return reader.read();
}

void MediaCache::analyze(const MediaItemPtr& item) {
    QPointer<MediaCache> self(this);
    const QUuid id = item->id;
    const MediaInfo info = item->info;

    // Thumbnail.
    (void)QtConcurrent::run([self, id, info] {
        QImage img;
        if (info.kind == MediaKind::Image)
            img = makeImageThumbnail(info.path, QSize(320, 180));
        else if (info.kind == MediaKind::Video)
            img = makeVideoThumbnail(info.path, info.duration / 10, QSize(320, 180));
        if (img.isNull())
            return;
        QMetaObject::invokeMethod(self, [self, id, img] {
            if (!self)
                return;
            self->m_thumbs.insert(id, img);
            emit self->thumbnailReady(id);
        }, Qt::QueuedConnection);
    });

    // Preview copy of a heavy video, so it plays smoothly.
    makeProxy(item);

    // Key frames (needed for lossless cut planning).
    if (info.video && !item->hasKeyframes()) {
        (void)QtConcurrent::run([self, item, id] {
            MediaPool::scanKeyframes(item);
            QMetaObject::invokeMethod(self, [self, id] {
                if (self)
                    emit self->keyframesReady(id);
            }, Qt::QueuedConnection);
        });
    }

    // Waveform peaks.
    if (info.audio) {
        (void)QtConcurrent::run([self, id, info] {
            AudioDecoder dec;
            constexpr int rate = 8000;
            if (!dec.open(info.path, rate, 1))
                return;
            auto peaks = std::make_shared<std::vector<float>>();
            const int perPeak = rate / kPeaksPerSecond;
            std::vector<float> samples;
            Ticks start = 0;
            std::int64_t index = 0;
            while (dec.next(samples, start)) {
                if (!self)
                    return;
                for (float s : samples) {
                    const size_t bucket = static_cast<size_t>(index++ / perPeak);
                    if (bucket >= peaks->size())
                        peaks->resize(bucket + 1, 0.0f);
                    (*peaks)[bucket] = std::max((*peaks)[bucket], std::abs(s));
                }
            }
            QMetaObject::invokeMethod(self, [self, id, peaks] {
                if (!self)
                    return;
                self->m_peaks.insert(id, peaks);
                emit self->peaksReady(id);
            }, Qt::QueuedConnection);
        });
    }
}

} // namespace lumora

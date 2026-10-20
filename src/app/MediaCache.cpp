#include "MediaCache.h"

#include <QImageReader>
#include <QPointer>
#include <QTransform>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

#include "media/AudioDecoder.h"
#include "media/VideoDecoder.h"

namespace lumora {

MediaCache::MediaCache(MediaPool* pool, QObject* parent) : QObject(parent), m_pool(pool) {
    connect(pool, &MediaPool::itemAdded, this, &MediaCache::analyze);
    connect(pool, &MediaPool::cleared, this, [this] {
        m_thumbs.clear();
        m_peaks.clear();
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

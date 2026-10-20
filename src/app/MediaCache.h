#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QUuid>
#include <memory>
#include <vector>

#include "media/MediaPool.h"

namespace lumora {

// Background analysis of imported media: thumbnails, audio waveform peaks
// and key frame indexes. Results are cached per media id.
class MediaCache : public QObject {
    Q_OBJECT
public:
    static constexpr int kPeaksPerSecond = 100;

    explicit MediaCache(MediaPool* pool, QObject* parent = nullptr);

    QImage thumbnail(const QUuid& id) const { return m_thumbs.value(id); }
    // Peak amplitude (0..1) per 1/kPeaksPerSecond s, or nullptr if not ready.
    std::shared_ptr<const std::vector<float>> peaks(const QUuid& id) const { return m_peaks.value(id); }

    void analyze(const MediaItemPtr& item);

    static QImage makeVideoThumbnail(const QString& path, Ticks at, QSize size);
    static QImage makeImageThumbnail(const QString& path, QSize size);

signals:
    void thumbnailReady(const QUuid& id);
    void peaksReady(const QUuid& id);
    void keyframesReady(const QUuid& id);

private:
    MediaPool* m_pool;
    QHash<QUuid, QImage> m_thumbs;
    QHash<QUuid, std::shared_ptr<const std::vector<float>>> m_peaks;
};

} // namespace lumora

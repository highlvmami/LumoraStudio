#pragma once

#include <QObject>
#include <QUuid>
#include <memory>
#include <mutex>
#include <vector>

#include "media/MediaInfo.h"

namespace lumora {

// One imported file. Shared between the media bin, the timeline and the
// exporters; the file itself is never modified.
struct MediaItem {
    QUuid id;
    QString path;
    MediaInfo info;

    // Key frame times of the primary video stream (media time), filled in the
    // background after import. Guarded by `mutex`.
    mutable std::mutex mutex;
    std::vector<Ticks> keyframes;
    bool keyframesReady = false;

    // Preview copy of a heavy video (media/Proxy.h), empty until it is
    // ready. Guarded by `mutex`.
    QString proxyPath;

    QString proxy() const {
        std::lock_guard lock(mutex);
        return proxyPath;
    }
    void setProxy(const QString& path) {
        std::lock_guard lock(mutex);
        proxyPath = path;
    }

    std::vector<Ticks> keyframesCopy() const {
        std::lock_guard lock(mutex);
        return keyframes;
    }
    bool hasKeyframes() const {
        std::lock_guard lock(mutex);
        return keyframesReady;
    }
};

using MediaItemPtr = std::shared_ptr<MediaItem>;

class MediaPool : public QObject {
    Q_OBJECT
public:
    explicit MediaPool(QObject* parent = nullptr) : QObject(parent) {}

    // Probes and adds a file. Returns the existing item if the path was
    // already imported.
    MediaItemPtr import(const QString& path, QString* error = nullptr);
    void add(MediaItemPtr item);
    void remove(const QUuid& id);
    void clear();

    MediaItemPtr find(const QUuid& id) const;
    MediaItemPtr findByPath(const QString& path) const;
    const std::vector<MediaItemPtr>& items() const { return m_items; }

    // Scans key frames synchronously (call from a worker thread).
    static void scanKeyframes(const MediaItemPtr& item);

signals:
    void itemAdded(const lumora::MediaItemPtr& item);
    void itemRemoved(const QUuid& id);
    void itemUpdated(const QUuid& id);
    void cleared();

private:
    std::vector<MediaItemPtr> m_items;
};

} // namespace lumora

Q_DECLARE_METATYPE(lumora::MediaItemPtr)

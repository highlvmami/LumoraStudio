#include "MediaPool.h"

#include <QFileInfo>
#include <algorithm>

#include "ffmpeg/FFmpeg.h"

namespace lumora {

MediaItemPtr MediaPool::import(const QString& path, QString* error) {
    const QString canonical = QFileInfo(path).absoluteFilePath();
    if (auto existing = findByPath(canonical))
        return existing;
    auto info = probeMedia(canonical, error);
    if (!info)
        return nullptr;
    auto item = std::make_shared<MediaItem>();
    item->id = QUuid::createUuid();
    item->path = canonical;
    item->info = *info;
    if (!info->video) {
        std::lock_guard lock(item->mutex);
        item->keyframesReady = true;
    }
    add(item);
    return item;
}

void MediaPool::add(MediaItemPtr item) {
    m_items.push_back(item);
    emit itemAdded(item);
}

void MediaPool::remove(const QUuid& id) {
    auto it = std::find_if(m_items.begin(), m_items.end(), [&](const auto& m) { return m->id == id; });
    if (it == m_items.end())
        return;
    m_items.erase(it);
    emit itemRemoved(id);
}

void MediaPool::clear() {
    m_items.clear();
    emit cleared();
}

MediaItemPtr MediaPool::find(const QUuid& id) const {
    for (const auto& m : m_items)
        if (m->id == id)
            return m;
    return nullptr;
}

MediaItemPtr MediaPool::findByPath(const QString& path) const {
    for (const auto& m : m_items)
        if (QFileInfo(m->path) == QFileInfo(path))
            return m;
    return nullptr;
}

void MediaPool::scanKeyframes(const MediaItemPtr& item) {
    if (!item || !item->info.video)
        return;
    auto kf = lumora::scanKeyframes(item->path, item->info.video->index);
    // Store as media time (relative to container start) like every other time.
    QString err;
    auto fmt = av::openInput(item->path, &err);
    Ticks origin = 0;
    if (fmt && fmt->start_time != AV_NOPTS_VALUE)
        origin = ticksFromStream(fmt->start_time, AVRational{1, AV_TIME_BASE});
    for (auto& t : kf)
        t -= origin;
    std::lock_guard lock(item->mutex);
    item->keyframes = std::move(kf);
    item->keyframesReady = true;
}

} // namespace lumora

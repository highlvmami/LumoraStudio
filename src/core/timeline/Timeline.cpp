#include "Timeline.h"

#include <algorithm>
#include <map>
#include <set>

namespace lumora {

Timeline::Timeline(QObject* parent) : QObject(parent) { ensureDefaultTracks(); }

void Timeline::ensureDefaultTracks() {
    if (m_state.tracks.size() >= 2)
        return;
    m_state.tracks.clear();
    Track v;
    v.id = QUuid::createUuid();
    v.type = TrackType::Video;
    v.name = QStringLiteral("V1");
    v.magnetic = true;
    Track a;
    a.id = QUuid::createUuid();
    a.type = TrackType::Audio;
    a.name = QStringLiteral("A1");
    m_state.tracks = {v, a};
}

void Timeline::setState(const TimelineState& s) {
    m_state = s;
    ensureDefaultTracks();
    normalize();
    emit changed();
}

void Timeline::reset() {
    m_state = {};
    ensureDefaultTracks();
    emit changed();
}

AVRational Timeline::frameRate() const {
    if (m_state.frameRate.num > 0 && m_state.frameRate.den > 0)
        return m_state.frameRate;
    return AVRational{25, 1};
}

void Timeline::setFrameRate(AVRational rate) {
    m_state.frameRate = rate;
    emit changed();
}

void Timeline::setResolution(QSize size) {
    m_state.resolution = size;
    emit changed();
}

Ticks Timeline::duration() const {
    Ticks end = 0;
    for (const auto& t : m_state.tracks)
        for (const auto& c : t.clips)
            end = std::max(end, c.timelineEnd());
    return end;
}

bool Timeline::isEmpty() const {
    for (const auto& t : m_state.tracks)
        if (!t.clips.empty())
            return false;
    return true;
}

std::optional<ClipLocation> Timeline::locate(const QUuid& clipId) const {
    for (int ti = 0; ti < static_cast<int>(m_state.tracks.size()); ++ti) {
        const auto& clips = m_state.tracks[ti].clips;
        for (int ci = 0; ci < static_cast<int>(clips.size()); ++ci)
            if (clips[ci].id == clipId)
                return ClipLocation{ti, ci};
    }
    return std::nullopt;
}

const Clip* Timeline::clip(const QUuid& clipId) const {
    auto loc = locate(clipId);
    return loc ? &m_state.tracks[loc->track].clips[loc->index] : nullptr;
}

const Clip* Timeline::clipAt(int track, Ticks t) const {
    if (track < 0 || track >= static_cast<int>(m_state.tracks.size()))
        return nullptr;
    for (const auto& c : m_state.tracks[track].clips)
        if (c.contains(t))
            return &c;
    return nullptr;
}

std::vector<QUuid> Timeline::linkedClips(const QUuid& clipId) const {
    std::vector<QUuid> out;
    const Clip* c = clip(clipId);
    if (!c)
        return out;
    if (c->linkId.isNull())
        return {clipId};
    for (const auto& t : m_state.tracks)
        for (const auto& other : t.clips)
            if (other.linkId == c->linkId)
                out.push_back(other.id);
    return out;
}

void Timeline::normalize() {
    // Drop empty clips, sort, pack magnetic tracks, then align linked clips
    // on other tracks to their partner on the magnetic track.
    std::map<QUuid, Ticks> linkStart;
    for (auto& t : m_state.tracks) {
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(), [](const Clip& c) { return c.duration() <= 0; }),
                      t.clips.end());
        if (t.magnetic) {
            Ticks cursor = 0;
            for (auto& c : t.clips) {
                c.timelineStart = cursor;
                cursor += c.duration();
                if (!c.linkId.isNull())
                    linkStart[c.linkId] = c.timelineStart;
            }
        }
    }
    for (auto& t : m_state.tracks) {
        if (t.magnetic)
            continue;
        for (auto& c : t.clips) {
            auto it = linkStart.find(c.linkId);
            if (!c.linkId.isNull() && it != linkStart.end())
                c.timelineStart = it->second;
        }
        std::stable_sort(t.clips.begin(), t.clips.end(),
                         [](const Clip& a, const Clip& b) { return a.timelineStart < b.timelineStart; });
    }
}

QUuid Timeline::insertMedia(const QUuid& mediaId, Ticks sourceIn, Ticks sourceOut, bool hasVideo, bool hasAudio,
                            Ticks at) {
    if (sourceOut <= sourceIn || (!hasVideo && !hasAudio))
        return {};
    const QUuid link = (hasVideo && hasAudio) ? QUuid::createUuid() : QUuid();
    Clip base;
    base.mediaId = mediaId;
    base.sourceIn = sourceIn;
    base.sourceOut = sourceOut;
    base.linkId = link;

    QUuid primaryId;
    if (hasVideo) {
        Track& v = m_state.tracks[primaryVideoTrack()];
        int index = static_cast<int>(v.clips.size());
        if (at != kInvalidTicks) {
            for (int i = 0; i < static_cast<int>(v.clips.size()); ++i) {
                const Clip& c = v.clips[i];
                if (at < c.timelineStart + c.duration() / 2) {
                    index = i;
                    break;
                }
            }
        }
        Clip vc = base;
        vc.id = QUuid::createUuid();
        v.clips.insert(v.clips.begin() + index, vc);
        primaryId = vc.id;
    }
    if (hasAudio) {
        Track& a = m_state.tracks[primaryAudioTrack()];
        Clip ac = base;
        ac.id = QUuid::createUuid();
        if (!hasVideo) {
            Ticks end = 0;
            for (const auto& c : a.clips)
                end = std::max(end, c.timelineEnd());
            ac.timelineStart = end;
            primaryId = ac.id;
        }
        a.clips.push_back(ac);
    }
    normalize();
    emit changed();
    return primaryId;
}

bool Timeline::splitAt(Ticks t) {
    bool any = false;
    std::map<QUuid, QUuid> newLinks;
    for (auto& tr : m_state.tracks) {
        if (tr.locked)
            continue;
        for (size_t i = 0; i < tr.clips.size(); ++i) {
            Clip& c = tr.clips[i];
            if (t <= c.timelineStart || t >= c.timelineEnd())
                continue;
            Clip right = c;
            right.id = QUuid::createUuid();
            const Ticks splitSource = c.toSource(t);
            right.sourceIn = splitSource;
            right.timelineStart = t;
            c.sourceOut = splitSource;
            if (!c.linkId.isNull()) {
                auto [it, inserted] = newLinks.try_emplace(c.linkId, QUuid());
                if (inserted)
                    it->second = QUuid::createUuid();
                right.linkId = it->second;
            }
            tr.clips.insert(tr.clips.begin() + static_cast<long>(i) + 1, right);
            any = true;
            break; // clips don't overlap within a track
        }
    }
    if (any) {
        normalize();
        emit changed();
    }
    return any;
}

bool Timeline::removeClips(const std::vector<QUuid>& ids) {
    std::set<QUuid> all;
    for (const auto& id : ids)
        for (const auto& l : linkedClips(id))
            all.insert(l);
    if (all.empty())
        return false;
    bool any = false;
    for (auto& tr : m_state.tracks) {
        if (tr.locked)
            continue;
        const auto before = tr.clips.size();
        tr.clips.erase(std::remove_if(tr.clips.begin(), tr.clips.end(), [&](const Clip& c) { return all.count(c.id); }),
                       tr.clips.end());
        any |= before != tr.clips.size();
    }
    if (any) {
        normalize();
        emit changed();
    }
    return any;
}

bool Timeline::removeRange(Ticks from, Ticks to) {
    if (to <= from)
        return false;
    const TimelineState backup = m_state;
    {
        const QSignalBlocker block(this);
        splitAt(from);
        splitAt(to);
        std::vector<QUuid> doomed;
        for (const auto& tr : m_state.tracks) {
            if (tr.locked)
                continue;
            for (const auto& c : tr.clips)
                if (c.timelineStart >= from && c.timelineEnd() <= to)
                    doomed.push_back(c.id);
        }
        removeClips(doomed);
        // Close the gap for unlinked clips on non-magnetic tracks.
        const Ticks gap = to - from;
        for (auto& tr : m_state.tracks) {
            if (tr.magnetic || tr.locked)
                continue;
            for (auto& c : tr.clips)
                if (c.linkId.isNull() && c.timelineStart >= to)
                    c.timelineStart -= gap;
        }
        normalize();
    }
    if (m_state == backup)
        return false;
    emit changed();
    return true;
}

bool Timeline::trimIn(const QUuid& clipId, Ticks newSourceIn) {
    const Clip* c = clip(clipId);
    if (!c)
        return false;
    const Ticks minDur = frameDuration();
    newSourceIn = std::clamp<Ticks>(newSourceIn, 0, c->sourceOut - minDur);
    const Ticks delta = newSourceIn - c->sourceIn;
    if (delta == 0)
        return false;
    for (const auto& id : linkedClips(clipId)) {
        auto loc = locate(id);
        Clip& lc = m_state.tracks[loc->track].clips[loc->index];
        if (m_state.tracks[loc->track].locked)
            continue;
        lc.sourceIn = std::clamp<Ticks>(lc.sourceIn + delta, 0, lc.sourceOut - minDur);
        if (!m_state.tracks[loc->track].magnetic && lc.linkId.isNull())
            lc.timelineStart += delta;
    }
    normalize();
    emit changed();
    return true;
}

bool Timeline::trimOut(const QUuid& clipId, Ticks newSourceOut, Ticks mediaDuration) {
    const Clip* c = clip(clipId);
    if (!c)
        return false;
    const Ticks minDur = frameDuration();
    newSourceOut = std::clamp<Ticks>(newSourceOut, c->sourceIn + minDur, mediaDuration > 0 ? mediaDuration : INT64_MAX);
    const Ticks delta = newSourceOut - c->sourceOut;
    if (delta == 0)
        return false;
    for (const auto& id : linkedClips(clipId)) {
        auto loc = locate(id);
        if (m_state.tracks[loc->track].locked)
            continue;
        Clip& lc = m_state.tracks[loc->track].clips[loc->index];
        lc.sourceOut = std::max(lc.sourceIn + minDur, lc.sourceOut + delta);
        if (mediaDuration > 0)
            lc.sourceOut = std::min(lc.sourceOut, mediaDuration);
    }
    normalize();
    emit changed();
    return true;
}

bool Timeline::moveClip(const QUuid& clipId, int newIndex) {
    auto loc = locate(clipId);
    if (!loc)
        return false;
    Track& tr = m_state.tracks[loc->track];
    if (!tr.magnetic || tr.locked)
        return false;
    newIndex = std::clamp(newIndex, 0, static_cast<int>(tr.clips.size()) - 1);
    if (newIndex == loc->index)
        return false;
    Clip c = tr.clips[loc->index];
    tr.clips.erase(tr.clips.begin() + loc->index);
    tr.clips.insert(tr.clips.begin() + newIndex, c);
    normalize();
    emit changed();
    return true;
}

bool Timeline::canJoinWithNext(const QUuid& clipId) const {
    auto loc = locate(clipId);
    if (!loc)
        return false;
    const auto& clips = m_state.tracks[loc->track].clips;
    if (loc->index + 1 >= static_cast<int>(clips.size()))
        return false;
    const Clip& a = clips[loc->index];
    const Clip& b = clips[loc->index + 1];
    return a.mediaId == b.mediaId && a.sourceOut == b.sourceIn && a.timelineEnd() == b.timelineStart;
}

bool Timeline::joinWithNext(const QUuid& clipId) {
    if (!canJoinWithNext(clipId))
        return false;
    auto loc = locate(clipId);
    const Clip a = m_state.tracks[loc->track].clips[loc->index];
    const Clip b = m_state.tracks[loc->track].clips[loc->index + 1];
    // Join the clip pair on every track that holds a linked pair.
    for (auto& tr : m_state.tracks) {
        if (tr.locked)
            continue;
        for (size_t i = 0; i + 1 < tr.clips.size(); ++i) {
            Clip& x = tr.clips[i];
            Clip& y = tr.clips[i + 1];
            const bool isPair = (x.id == a.id && y.id == b.id) ||
                                (!a.linkId.isNull() && !b.linkId.isNull() && x.linkId == a.linkId && y.linkId == b.linkId);
            if (isPair && x.mediaId == y.mediaId && x.sourceOut == y.sourceIn) {
                x.sourceOut = y.sourceOut;
                tr.clips.erase(tr.clips.begin() + static_cast<long>(i) + 1);
                break;
            }
        }
    }
    normalize();
    emit changed();
    return true;
}

} // namespace lumora

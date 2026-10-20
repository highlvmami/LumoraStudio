#include "Timeline.h"

#include <algorithm>
#include <map>
#include <set>

namespace lumora {

// ---------------------------------------------------------------------------
// TimelineState
// ---------------------------------------------------------------------------

std::vector<int> TimelineState::trackIndices(TrackType type) const {
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
        if (tracks[i].type == type)
            out.push_back(i);
    return out;
}

int TimelineState::trackNumber(int index) const {
    if (index < 0 || index >= static_cast<int>(tracks.size()))
        return 0;
    int n = 0;
    for (int i = 0; i <= index; ++i)
        if (tracks[i].type == tracks[index].type)
            ++n;
    return n;
}

Ticks TimelineState::duration() const {
    Ticks end = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            end = std::max(end, c.timelineEnd());
    return end;
}

const Clip* TimelineState::findClip(const QUuid& id, int* trackIndex) const {
    for (int ti = 0; ti < static_cast<int>(tracks.size()); ++ti)
        for (const auto& c : tracks[ti].clips)
            if (c.id == id) {
                if (trackIndex)
                    *trackIndex = ti;
                return &c;
            }
    return nullptr;
}

Ticks TimelineState::transitionDuration(int index, const std::function<Ticks(const QUuid&)>& mediaDuration) const {
    if (tracks.empty())
        return 0;
    const auto& v = tracks[0].clips;
    if (index <= 0 || index >= static_cast<int>(v.size()))
        return 0;
    const Clip& a = v[index - 1];
    const Clip& b = v[index];
    if (b.transitionIn.isNone())
        return 0;
    constexpr Ticks unlimited = INT64_MAX / 4;
    // Media available after the out point of the outgoing clip and before the
    // in point of the incoming clip. Titles can be extended freely.
    Ticks handleA = unlimited;
    if (a.kind == ClipKind::Media) {
        const Ticks len = mediaDuration ? mediaDuration(a.mediaId) : 0;
        handleA = len > 0 ? std::max<Ticks>(0, len - a.sourceOut) : 0;
    }
    const Ticks handleB = b.kind == ClipKind::Media ? b.sourceIn : unlimited;
    Ticks d = b.transitionIn.duration;
    d = std::min(d, 2 * std::min(handleA, handleB));
    d = std::min(d, std::min(a.duration(), b.duration()));
    return std::max<Ticks>(0, d);
}

bool TimelineState::hasEffects() const {
    for (int ti = 0; ti < static_cast<int>(tracks.size()); ++ti) {
        const Track& t = tracks[ti];
        if (ti >= 2 && !t.clips.empty())
            return true;
        for (const auto& c : t.clips) {
            if (c.kind != ClipKind::Media || !c.transform.isIdentity() || !c.transitionIn.isNone() || c.fadeIn > 0 ||
                c.fadeOut > 0 || c.gainDb != 0.0)
                return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Timeline
// ---------------------------------------------------------------------------

Timeline::Timeline(QObject* parent) : QObject(parent) { ensureDefaultTracks(); }

void Timeline::ensureDefaultTracks() {
    const bool ok = m_state.tracks.size() >= 2 && m_state.tracks[0].type == TrackType::Video &&
                    m_state.tracks[1].type == TrackType::Audio;
    if (ok) {
        m_state.tracks[0].magnetic = true;
        renameTracks();
        return;
    }
    m_state.tracks.clear();
    Track v;
    v.id = QUuid::createUuid();
    v.type = TrackType::Video;
    v.magnetic = true;
    Track a;
    a.id = QUuid::createUuid();
    a.type = TrackType::Audio;
    m_state.tracks = {v, a};
    renameTracks();
}

void Timeline::renameTracks() {
    for (int i = 0; i < static_cast<int>(m_state.tracks.size()); ++i) {
        Track& t = m_state.tracks[i];
        t.name = (t.isVideo() ? QStringLiteral("V%1") : QStringLiteral("A%1")).arg(m_state.trackNumber(i));
    }
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

Ticks Timeline::duration() const { return m_state.duration(); }

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

Clip* Timeline::mutableClip(const QUuid& clipId) {
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

int Timeline::audioTrackFor(int videoTrack) {
    if (videoTrack <= 0)
        return primaryAudioTrack();
    const int n = std::max(1, m_state.trackNumber(videoTrack));
    auto audio = m_state.trackIndices(TrackType::Audio);
    while (static_cast<int>(audio.size()) < n) {
        Track t;
        t.id = QUuid::createUuid();
        t.type = TrackType::Audio;
        m_state.tracks.push_back(t);
        audio = m_state.trackIndices(TrackType::Audio);
    }
    renameTracks();
    return audio[n - 1];
}

void Timeline::anchor(Clip& c) {
    c.anchorId = QUuid();
    c.anchorSource = 0;
    for (const auto& v : m_state.tracks[primaryVideoTrack()].clips)
        if (c.timelineStart >= v.timelineStart && c.timelineStart < v.timelineEnd()) {
            c.anchorId = v.id;
            c.anchorSource = v.toSource(c.timelineStart);
            return;
        }
}

void Timeline::normalize() {
    // 1. Drop empty clips; pack magnetic tracks.
    std::map<QUuid, const Clip*> storyline;
    for (auto& t : m_state.tracks) {
        t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(), [](const Clip& c) { return c.duration() <= 0; }),
                      t.clips.end());
        if (t.magnetic) {
            Ticks cursor = 0;
            for (auto& c : t.clips) {
                c.timelineStart = cursor;
                cursor += c.duration();
                c.anchorId = QUuid();
            }
        }
    }
    for (const auto& c : m_state.tracks[primaryVideoTrack()].clips)
        storyline[c.id] = &c;

    // 2. Connected clips follow their anchor's picture.
    for (auto& t : m_state.tracks) {
        if (t.magnetic)
            continue;
        for (auto& c : t.clips) {
            if (c.anchorId.isNull())
                continue;
            auto it = storyline.find(c.anchorId);
            if (it != storyline.end())
                c.timelineStart = std::max<Ticks>(0, it->second->timelineStart + (c.anchorSource - it->second->sourceIn));
        }
    }

    // 3. Linked clips: the clip on the storyline (or on a video track) leads,
    //    the others take its position. Links without partners are dropped.
    struct Leader {
        Ticks start = 0;
        int rank = 3; // 0 magnetic, 1 video, 2 audio
        int count = 0;
        QUuid anchorId;
        Ticks anchorSource = 0;
    };
    std::map<QUuid, Leader> links;
    for (const auto& t : m_state.tracks)
        for (const auto& c : t.clips) {
            if (c.linkId.isNull())
                continue;
            Leader& l = links[c.linkId];
            ++l.count;
            const int rank = t.magnetic ? 0 : (t.isVideo() ? 1 : 2);
            if (rank < l.rank) {
                l.rank = rank;
                l.start = c.timelineStart;
                l.anchorId = c.anchorId;
                l.anchorSource = c.anchorSource;
            }
        }
    for (auto& t : m_state.tracks)
        for (auto& c : t.clips) {
            if (c.linkId.isNull())
                continue;
            const Leader& l = links[c.linkId];
            if (l.count < 2) {
                c.linkId = QUuid();
                continue;
            }
            if (!t.magnetic) {
                c.timelineStart = l.start;
                c.anchorId = l.anchorId;
                c.anchorSource = l.anchorSource;
            }
        }

    // 4. Sort, then re-anchor so connections always describe the storyline
    //    clip that is actually under each connected clip.
    for (auto& t : m_state.tracks) {
        if (t.magnetic)
            continue;
        std::stable_sort(t.clips.begin(), t.clips.end(),
                         [](const Clip& a, const Clip& b) { return a.timelineStart < b.timelineStart; });
        for (auto& c : t.clips)
            anchor(c);
    }
    // Storyline transitions only exist between clips.
    if (!m_state.tracks[primaryVideoTrack()].clips.empty())
        m_state.tracks[primaryVideoTrack()].clips.front().transitionIn = {};
    for (auto& t : m_state.tracks)
        if (!t.magnetic)
            for (auto& c : t.clips)
                c.transitionIn = {};
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
            ac.timelineStart = at != kInvalidTicks ? at : end;
            primaryId = ac.id;
            clearRange(primaryAudioTrack(), ac.timelineStart, ac.timelineStart + ac.duration());
            anchor(ac);
        }
        m_state.tracks[primaryAudioTrack()].clips.push_back(ac);
    }
    normalize();
    emit changed();
    return primaryId;
}

void Timeline::clearRange(int trackIndex, Ticks from, Ticks to, const std::vector<QUuid>& keep) {
    Track* t = track(trackIndex);
    if (!t || t->magnetic || to <= from)
        return;
    std::set<QUuid> unlinked; // link ids broken because only this track changes
    std::vector<Clip> result;
    for (const Clip& c : t->clips) {
        if (std::find(keep.begin(), keep.end(), c.id) != keep.end() || c.timelineEnd() <= from ||
            c.timelineStart >= to) {
            result.push_back(c);
            continue;
        }
        if (!c.linkId.isNull())
            unlinked.insert(c.linkId);
        if (c.timelineStart < from) { // left part survives
            Clip left = c;
            left.sourceOut = c.toSource(from);
            left.fadeOut = 0;
            left.linkId = QUuid();
            result.push_back(left);
        }
        if (c.timelineEnd() > to) { // right part survives
            Clip right = c;
            if (c.timelineStart < from)
                right.id = QUuid::createUuid();
            right.sourceIn = c.toSource(to);
            right.timelineStart = to;
            right.fadeIn = 0;
            right.linkId = QUuid();
            anchor(right);
            result.push_back(right);
        }
    }
    t->clips = std::move(result);
    if (!unlinked.empty())
        for (auto& other : m_state.tracks)
            for (auto& c : other.clips)
                if (unlinked.count(c.linkId))
                    c.linkId = QUuid();
}

QUuid Timeline::placeMedia(const QUuid& mediaId, Ticks sourceIn, Ticks sourceOut, bool hasVideo, bool hasAudio,
                           int trackIndex, Ticks at) {
    Track* t = track(trackIndex);
    if (!t || sourceOut <= sourceIn || (!hasVideo && !hasAudio))
        return {};
    if (t->magnetic)
        return insertMedia(mediaId, sourceIn, sourceOut, hasVideo, hasAudio, at);
    if (t->locked)
        return {};
    at = std::max<Ticks>(0, at == kInvalidTicks ? 0 : at);
    const Ticks dur = sourceOut - sourceIn;
    Clip base;
    base.mediaId = mediaId;
    base.sourceIn = sourceIn;
    base.sourceOut = sourceOut;
    base.timelineStart = at;
    anchor(base);

    int videoTrack = -1, audioTrack = -1;
    if (hasVideo) {
        if (!t->isVideo())
            return {};
        videoTrack = trackIndex;
        if (hasAudio)
            audioTrack = audioTrackFor(trackIndex);
    } else {
        audioTrack = t->isAudio() ? trackIndex : audioTrackFor(trackIndex);
    }
    if (audioTrack >= 0 && m_state.tracks[audioTrack].locked)
        audioTrack = -1;
    if (videoTrack < 0 && audioTrack < 0)
        return {};
    if (videoTrack >= 0 && audioTrack >= 0)
        base.linkId = QUuid::createUuid();

    QUuid mainId;
    if (videoTrack >= 0) {
        clearRange(videoTrack, at, at + dur);
        Clip vc = base;
        vc.id = QUuid::createUuid();
        m_state.tracks[videoTrack].clips.push_back(vc);
        mainId = vc.id;
    }
    if (audioTrack >= 0) {
        clearRange(audioTrack, at, at + dur);
        Clip ac = base;
        ac.id = QUuid::createUuid();
        m_state.tracks[audioTrack].clips.push_back(ac);
        if (mainId.isNull())
            mainId = ac.id;
    }
    normalize();
    emit changed();
    return mainId;
}

QUuid Timeline::addTitle(const TitleData& title, Ticks duration, int trackIndex, Ticks at) {
    Track* t = track(trackIndex);
    if (!t || !t->isVideo() || t->locked || duration <= 0)
        return {};
    Clip c;
    c.id = QUuid::createUuid();
    c.kind = ClipKind::Title;
    c.sourceIn = 0;
    c.sourceOut = duration;
    c.title = title;
    if (t->magnetic) {
        int index = static_cast<int>(t->clips.size());
        if (at != kInvalidTicks)
            for (int i = 0; i < static_cast<int>(t->clips.size()); ++i)
                if (at < t->clips[i].timelineStart + t->clips[i].duration() / 2) {
                    index = i;
                    break;
                }
        t->clips.insert(t->clips.begin() + index, c);
    } else {
        c.timelineStart = std::max<Ticks>(0, at == kInvalidTicks ? 0 : at);
        clearRange(trackIndex, c.timelineStart, c.timelineEnd());
        anchor(c);
        m_state.tracks[trackIndex].clips.push_back(c);
    }
    normalize();
    emit changed();
    return c.id;
}

bool Timeline::moveClipTo(const QUuid& clipId, int targetTrack, Ticks start) {
    auto loc = locate(clipId);
    if (!loc || !track(targetTrack))
        return false;
    // Work with indices only: creating a paired audio track may reallocate
    // the track vector.
    const int fromTrack = loc->track;
    const bool fromMagnetic = m_state.tracks[fromTrack].magnetic;
    const bool toMagnetic = m_state.tracks[targetTrack].magnetic;
    const bool toVideo = m_state.tracks[targetTrack].isVideo();
    if (m_state.tracks[fromTrack].type != m_state.tracks[targetTrack].type || m_state.tracks[fromTrack].locked ||
        m_state.tracks[targetTrack].locked)
        return false;
    start = std::max<Ticks>(0, start);
    const Clip c = m_state.tracks[fromTrack].clips[loc->index];

    if (fromMagnetic && toMagnetic) {
        // Reorder on the storyline: insert at the nearest edit.
        const auto& clips = m_state.tracks[fromTrack].clips;
        int index = static_cast<int>(clips.size()) - 1;
        for (int i = 0; i < static_cast<int>(clips.size()); ++i)
            if (start < clips[i].timelineStart + clips[i].duration() / 2) {
                index = i > loc->index ? i - 1 : i;
                break;
            }
        return moveClip(clipId, index);
    }

    // The clip and its linked partners, with source and destination tracks.
    struct Moving {
        Clip clip;
        int fromTrack;
        int toTrack;
    };
    std::vector<Moving> moving;
    for (const auto& id : linkedClips(clipId)) {
        auto l = locate(id);
        if (l)
            moving.push_back({m_state.tracks[l->track].clips[l->index], l->track, l->track});
    }
    for (auto& m : moving) {
        if (m.clip.id == clipId)
            m.toTrack = targetTrack;
        else if (m_state.tracks[m.fromTrack].isAudio() && toVideo)
            m.toTrack = toMagnetic ? primaryAudioTrack() : audioTrackFor(targetTrack);
    }
    for (const auto& m : moving)
        if (m_state.tracks[m.toTrack].locked || m_state.tracks[m.fromTrack].locked)
            return false;

    const Ticks delta = start - c.timelineStart;
    std::vector<QUuid> keep;
    for (const auto& m : moving)
        keep.push_back(m.clip.id);
    for (const auto& m : moving) {
        auto& clips = m_state.tracks[m.fromTrack].clips;
        clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const Clip& x) { return x.id == m.clip.id; }),
                    clips.end());
    }
    if (fromMagnetic) {
        // Leaving the storyline: close the gap first so the destination time
        // refers to the rippled timeline the user sees afterwards.
        normalize();
    }
    if (toMagnetic) {
        // Joining the storyline: insert at the nearest edit.
        auto& v = m_state.tracks[primaryVideoTrack()].clips;
        int index = static_cast<int>(v.size());
        for (int i = 0; i < static_cast<int>(v.size()); ++i)
            if (start < v[i].timelineStart + v[i].duration() / 2) {
                index = i;
                break;
            }
        for (const auto& m : moving) {
            Clip moved = m.clip;
            moved.anchorId = QUuid();
            if (m.toTrack == primaryVideoTrack())
                v.insert(v.begin() + index, moved);
            else
                m_state.tracks[m.toTrack].clips.push_back(moved);
        }
    } else {
        for (const auto& m : moving) {
            Clip moved = m.clip;
            moved.timelineStart = fromMagnetic ? start : std::max<Ticks>(0, m.clip.timelineStart + delta);
            moved.transitionIn = {};
            clearRange(m.toTrack, moved.timelineStart, moved.timelineEnd(), keep);
            anchor(moved);
            m_state.tracks[m.toTrack].clips.push_back(moved);
        }
    }
    normalize();
    emit changed();
    return true;
}

int Timeline::addTrack(TrackType type) {
    Track t;
    t.id = QUuid::createUuid();
    t.type = type;
    m_state.tracks.push_back(t);
    renameTracks();
    emit changed();
    return static_cast<int>(m_state.tracks.size()) - 1;
}

bool Timeline::removeTrack(int index) {
    if (index < 2 || index >= static_cast<int>(m_state.tracks.size()))
        return false;
    m_state.tracks.erase(m_state.tracks.begin() + index);
    renameTracks();
    normalize();
    emit changed();
    return true;
}

bool Timeline::setTrackFlags(int index, bool locked, bool muted, bool hidden) {
    Track* t = track(index);
    if (!t || (t->locked == locked && t->muted == muted && t->hidden == hidden))
        return false;
    t->locked = locked;
    t->muted = muted;
    t->hidden = hidden;
    emit changed();
    return true;
}

bool Timeline::setTransform(const QUuid& clipId, const ClipTransform& tr) {
    Clip* c = mutableClip(clipId);
    if (!c || c->transform == tr)
        return false;
    c->transform = tr;
    emit changed();
    return true;
}

bool Timeline::setTransition(const QUuid& clipId, const Transition& tr) {
    auto loc = locate(clipId);
    if (!loc || loc->track != primaryVideoTrack() || loc->index == 0)
        return false;
    Clip& c = m_state.tracks[loc->track].clips[loc->index];
    Transition t = tr;
    if (t.type == TransitionType::None)
        t.duration = 0;
    if (c.transitionIn == t)
        return false;
    c.transitionIn = t;
    emit changed();
    return true;
}

bool Timeline::setFades(const QUuid& clipId, Ticks fadeIn, Ticks fadeOut) {
    Clip* c = mutableClip(clipId);
    if (!c)
        return false;
    fadeIn = std::clamp<Ticks>(fadeIn, 0, c->duration());
    fadeOut = std::clamp<Ticks>(fadeOut, 0, c->duration() - fadeIn);
    if (c->fadeIn == fadeIn && c->fadeOut == fadeOut)
        return false;
    c->fadeIn = fadeIn;
    c->fadeOut = fadeOut;
    emit changed();
    return true;
}

bool Timeline::setGain(const QUuid& clipId, double gainDb) {
    Clip* c = mutableClip(clipId);
    gainDb = std::clamp(gainDb, -60.0, 24.0);
    if (!c || c->gainDb == gainDb)
        return false;
    c->gainDb = gainDb;
    emit changed();
    return true;
}

bool Timeline::setTitle(const QUuid& clipId, const TitleData& title) {
    Clip* c = mutableClip(clipId);
    if (!c || c->kind != ClipKind::Title || c->title == title)
        return false;
    c->title = title;
    emit changed();
    return true;
}

bool Timeline::setDuration(const QUuid& clipId, Ticks duration) {
    auto loc = locate(clipId);
    if (!loc)
        return false;
    Track& t = m_state.tracks[loc->track];
    Clip& c = t.clips[loc->index];
    if (c.kind != ClipKind::Title || t.locked)
        return false;
    duration = std::max(duration, frameDuration());
    if (duration == c.duration())
        return false;
    const Ticks oldEnd = c.timelineEnd();
    c.sourceOut = c.sourceIn + duration;
    c.fadeOut = std::min(c.fadeOut, duration);
    if (!t.magnetic && c.timelineEnd() > oldEnd)
        clearRange(loc->track, oldEnd, c.timelineEnd(), {c.id});
    normalize();
    emit changed();
    return true;
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
            right.transitionIn = {};
            right.fadeIn = 0;
            c.sourceOut = splitSource;
            c.fadeOut = 0;
            c.fadeIn = std::min(c.fadeIn, c.duration());
            right.fadeOut = std::min(right.fadeOut, right.duration());
            if (!c.linkId.isNull()) {
                auto [it, inserted] = newLinks.try_emplace(c.linkId, QUuid());
                if (inserted)
                    it->second = QUuid::createUuid();
                right.linkId = it->second;
            }
            tr.clips.insert(tr.clips.begin() + static_cast<long>(i) + 1, right);
            any = true;
            if (!tr.magnetic)
                continue; // overlapping clips on free tracks are split too
            break;        // clips don't overlap on the storyline
        }
    }
    if (any) {
        // Connected clips whose start lies in the right half of a split
        // storyline clip move their anchor to the new right piece.
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
    // Connected clips keep their current place when their anchor goes away.
    for (auto& tr : m_state.tracks)
        for (auto& c : tr.clips)
            if (all.count(c.anchorId))
                c.anchorId = QUuid();
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
        // Close the gap for clips that are not connected to the storyline
        // (connected ones follow their anchor).
        const Ticks gap = to - from;
        for (auto& tr : m_state.tracks) {
            if (tr.magnetic || tr.locked)
                continue;
            for (auto& c : tr.clips)
                if (c.anchorId.isNull() && c.linkId.isNull() && c.timelineStart >= to) {
                    c.timelineStart -= gap;
                    anchor(c);
                }
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
    const Ticks lower = c->kind == ClipKind::Title ? INT64_MIN / 4 : 0;
    newSourceIn = std::clamp<Ticks>(newSourceIn, lower, c->sourceOut - minDur);
    const Ticks delta = newSourceIn - c->sourceIn;
    if (delta == 0)
        return false;
    const auto group = linkedClips(clipId);
    bool onStoryline = false;
    for (const auto& id : group)
        onStoryline |= m_state.tracks[locate(id)->track].magnetic;
    for (const auto& id : group) {
        auto loc = locate(id);
        if (m_state.tracks[loc->track].locked)
            continue;
        Clip& lc = m_state.tracks[loc->track].clips[loc->index];
        lc.sourceIn = std::clamp<Ticks>(lc.sourceIn + delta, lower, lc.sourceOut - minDur);
        if (!onStoryline) {
            lc.timelineStart += delta;
            anchor(lc);
        }
    }
    normalize();
    emit changed();
    return true;
}

bool Timeline::trimOut(const QUuid& clipId, Ticks newSourceOut, Ticks mediaDuration) {
    const Clip* c = clip(clipId);
    if (!c)
        return false;
    if (c->kind == ClipKind::Title)
        mediaDuration = 0;
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
        lc.fadeOut = std::min(lc.fadeOut, lc.duration());
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
    return a.kind == ClipKind::Media && b.kind == ClipKind::Media && a.mediaId == b.mediaId &&
           a.sourceOut == b.sourceIn && a.timelineEnd() == b.timelineStart;
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
                x.fadeOut = y.fadeOut;
                const QUuid gone = y.id;
                tr.clips.erase(tr.clips.begin() + static_cast<long>(i) + 1);
                for (auto& other : m_state.tracks)
                    for (auto& c : other.clips)
                        if (c.anchorId == gone)
                            c.anchorId = x.id;
                break;
            }
        }
    }
    normalize();
    emit changed();
    return true;
}

} // namespace lumora

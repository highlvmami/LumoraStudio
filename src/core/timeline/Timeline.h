#pragma once

#include <QObject>
#include <QSize>
#include <QString>
#include <QUuid>
#include <optional>
#include <vector>

#include "TimeBase.h"

namespace lumora {

enum class TrackType { Video, Audio };

// A clip references a range [sourceIn, sourceOut) of a media item and is
// placed at timelineStart. Clips never own pixels; they are pure edit
// decisions, so the source file is never touched.
struct Clip {
    QUuid id;
    QUuid mediaId;
    Ticks sourceIn = 0;
    Ticks sourceOut = 0;
    Ticks timelineStart = 0;
    // Clips that share a non-null link id (e.g. the video and audio of one
    // file) are edited together.
    QUuid linkId;

    Ticks duration() const { return sourceOut - sourceIn; }
    Ticks timelineEnd() const { return timelineStart + duration(); }
    // Source time shown at timeline time `t` (no speed changes in 0.1).
    Ticks toSource(Ticks t) const { return sourceIn + (t - timelineStart); }
    bool contains(Ticks t) const { return t >= timelineStart && t < timelineEnd(); }

    bool operator==(const Clip&) const = default;
};

struct Track {
    QUuid id;
    TrackType type = TrackType::Video;
    QString name;
    bool locked = false;
    bool muted = false;
    bool hidden = false;
    // The primary storyline is magnetic: clips are always packed without
    // gaps (ripple behaviour). Other tracks keep absolute positions.
    bool magnetic = false;
    std::vector<Clip> clips; // sorted by timelineStart

    bool operator==(const Track&) const = default;
};

// Value type holding the full edit state. Copyable so undo can snapshot it.
struct TimelineState {
    std::vector<Track> tracks;
    AVRational frameRate{0, 1}; // {0,1} = not set yet (taken from the first clip)
    QSize resolution;

    bool operator==(const TimelineState& o) const {
        return tracks == o.tracks && frameRate.num == o.frameRate.num && frameRate.den == o.frameRate.den &&
               resolution == o.resolution;
    }
};

struct ClipLocation {
    int track = -1;
    int index = -1;
    bool valid() const { return track >= 0 && index >= 0; }
};

// The edit model. All mutations go through methods that keep the invariants
// (sorted clips, packed magnetic track, linked clips in sync) and emit
// changed(). Undo is implemented on top by snapshotting TimelineState.
class Timeline : public QObject {
    Q_OBJECT
public:
    explicit Timeline(QObject* parent = nullptr);

    const TimelineState& state() const { return m_state; }
    void setState(const TimelineState& s);
    void reset();

    const std::vector<Track>& tracks() const { return m_state.tracks; }
    AVRational frameRate() const;
    Ticks frameDuration() const { return lumora::frameDuration(frameRate()); }
    Ticks duration() const;
    bool isEmpty() const;

    int primaryVideoTrack() const { return 0; }
    int primaryAudioTrack() const { return 1; }

    std::optional<ClipLocation> locate(const QUuid& clipId) const;
    const Clip* clip(const QUuid& clipId) const;
    const Clip* clipAt(int track, Ticks t) const;
    std::vector<QUuid> linkedClips(const QUuid& clipId) const; // includes clipId

    // --- edit operations (return false if nothing changed) ---

    // Appends or inserts a media range on the primary storyline. If the media
    // has audio a linked audio clip is created as well. `at` is a timeline
    // time; the clip is inserted at the nearest clip boundary (ripple insert).
    QUuid insertMedia(const QUuid& mediaId, Ticks sourceIn, Ticks sourceOut, bool hasVideo, bool hasAudio,
                      Ticks at = kInvalidTicks);
    // Splits every clip under `t` on unlocked tracks.
    bool splitAt(Ticks t);
    // Removes the clips (and linked clips). Magnetic tracks close the gap.
    bool removeClips(const std::vector<QUuid>& ids);
    // Removes the range [from, to) from all unlocked tracks (ripple delete).
    bool removeRange(Ticks from, Ticks to);
    // Moves the edge of a clip. newSourceTime is clamped to the media bounds
    // [0, mediaDuration] and to at least one frame.
    bool trimIn(const QUuid& clipId, Ticks newSourceIn);
    bool trimOut(const QUuid& clipId, Ticks newSourceOut, Ticks mediaDuration);
    // Reorders a clip on the magnetic track to position `newIndex`.
    bool moveClip(const QUuid& clipId, int newIndex);
    // Joins a clip with the next one when they are contiguous pieces of the
    // same media (undoes a split).
    bool joinWithNext(const QUuid& clipId);
    bool canJoinWithNext(const QUuid& clipId) const;

    void setFrameRate(AVRational rate);
    void setResolution(QSize size);

signals:
    void changed();

private:
    void normalize();
    void ensureDefaultTracks();
    Track* track(int i) { return i >= 0 && i < static_cast<int>(m_state.tracks.size()) ? &m_state.tracks[i] : nullptr; }

    TimelineState m_state;
};

} // namespace lumora

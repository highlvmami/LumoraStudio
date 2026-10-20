#pragma once

#include <QObject>
#include <QSize>
#include <QString>
#include <QUuid>
#include <functional>
#include <optional>
#include <vector>

#include "TimeBase.h"
#include "timeline/ClipEffects.h"

namespace lumora {

enum class TrackType { Video, Audio };

enum class ClipKind { Media, Title };

// A clip references a range [sourceIn, sourceOut) of a media item and is
// placed at timelineStart. Clips never own pixels; they are pure edit
// decisions, so the source file is never touched. Title clips have no media;
// their "source" range is simply [0, duration).
struct Clip {
    QUuid id;
    ClipKind kind = ClipKind::Media;
    QUuid mediaId;
    Ticks sourceIn = 0;
    Ticks sourceOut = 0;
    Ticks timelineStart = 0;
    // Clips that share a non-null link id (e.g. the video and audio of one
    // file) are edited together.
    QUuid linkId;
    // Clips on the other tracks are connected to the primary storyline clip
    // under their start: they keep their place relative to that clip's
    // picture when the storyline is rippled. anchorSource is the source time
    // of the anchor clip at which this clip starts.
    QUuid anchorId;
    Ticks anchorSource = 0;

    ClipTransform transform;     // video and title clips
    Transition transitionIn;     // primary storyline: transition from the previous clip
    Ticks fadeIn = 0;            // opacity (video) / volume (audio) ramps
    Ticks fadeOut = 0;
    double gainDb = 0.0;         // audio clips
    TitleData title;             // ClipKind::Title

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

    bool isVideo() const { return type == TrackType::Video; }
    bool isAudio() const { return type == TrackType::Audio; }
    bool operator==(const Track&) const = default;
};

// Value type holding the full edit state. Copyable so undo can snapshot it.
//
// Track order: tracks[0] is V1 (the magnetic primary storyline), tracks[1] is
// A1. Further tracks are appended; among tracks of one type the vector order
// is their number (V1, V2, ... composited bottom to top; A1, A2, ...).
struct TimelineState {
    std::vector<Track> tracks;
    AVRational frameRate{0, 1}; // {0,1} = not set yet (taken from the first clip)
    QSize resolution;

    bool operator==(const TimelineState& o) const {
        return tracks == o.tracks && frameRate.num == o.frameRate.num && frameRate.den == o.frameRate.den &&
               resolution == o.resolution;
    }

    // Indices into `tracks` of each type, in numbering order.
    std::vector<int> trackIndices(TrackType type) const;
    // 1-based number of a track among tracks of its type (V2 -> 2).
    int trackNumber(int index) const;
    Ticks duration() const;
    const Clip* findClip(const QUuid& id, int* trackIndex = nullptr) const;
    // Effective duration of the transition into storyline clip `index`,
    // limited by the media available beyond the cut on both sides and by
    // the clip lengths. `mediaDuration` returns the length of a media item.
    Ticks transitionDuration(int index, const std::function<Ticks(const QUuid&)>& mediaDuration) const;
    // True when anything needs compositing (more than plain cuts on V1/A1).
    bool hasEffects() const;
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
    // Audio track paired with a video track (V2 -> A2), created when missing.
    int audioTrackFor(int videoTrack);

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
    // Places a media range on a track other than V1 at time `at`, replacing
    // whatever is under it (overwrite). Video goes to `track` (a video track)
    // and linked audio to the paired audio track; audio-only media goes to
    // `track` if it is an audio track. Returns the id of the main clip.
    QUuid placeMedia(const QUuid& mediaId, Ticks sourceIn, Ticks sourceOut, bool hasVideo, bool hasAudio, int track,
                     Ticks at);
    // Adds a title clip. On V1 it is inserted like media (ripple); on other
    // video tracks it is placed at `at`, overwriting.
    QUuid addTitle(const TitleData& title, Ticks duration, int track, Ticks at);
    // Moves a clip (with its linked clips) to another track and/or time.
    // Storyline clips that move to another track leave the storyline (the
    // gap closes); clips that move onto V1 are inserted at the nearest edit.
    bool moveClipTo(const QUuid& clipId, int track, Ticks start);

    // Track management. V1 and A1 always exist and cannot be removed.
    int addTrack(TrackType type);
    bool removeTrack(int index);
    bool setTrackFlags(int index, bool locked, bool muted, bool hidden);

    // Per-clip settings.
    bool setTransform(const QUuid& clipId, const ClipTransform& t);
    bool setTransition(const QUuid& clipId, const Transition& t);
    bool setFades(const QUuid& clipId, Ticks fadeIn, Ticks fadeOut);
    bool setGain(const QUuid& clipId, double gainDb);
    bool setTitle(const QUuid& clipId, const TitleData& title);
    // Length of a clip on a non-magnetic track or a title (any track).
    bool setDuration(const QUuid& clipId, Ticks duration);
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
    void renameTracks();
    Track* track(int i) { return i >= 0 && i < static_cast<int>(m_state.tracks.size()) ? &m_state.tracks[i] : nullptr; }
    Clip* mutableClip(const QUuid& id);
    // Clears [from, to) on a non-magnetic track (splitting and trimming the
    // clips that overlap it), ignoring the clips in `keep`.
    void clearRange(int track, Ticks from, Ticks to, const std::vector<QUuid>& keep = {});
    // Connects a clip on a non-magnetic track to the storyline clip under it.
    void anchor(Clip& c);

    TimelineState m_state;
};

} // namespace lumora

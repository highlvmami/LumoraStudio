#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <vector>

#include "TimeBase.h"

namespace lumora {

// Placement of a clip's picture in the output frame. The identity transform
// shows the picture scaled to fit the frame (letter/pillar box), centred.
struct ClipTransform {
    double x = 0.0;        // centre offset as a fraction of the frame width (-1 .. 1)
    double y = 0.0;        // centre offset as a fraction of the frame height
    double scale = 1.0;    // 1 = fit the frame
    double rotation = 0.0; // degrees, clockwise
    double opacity = 1.0;  // 0 .. 1
    // Part of the picture that is shown, in fractions of the picture; the
    // rest is cut away and the shown part stays where it was (collage cells).
    // A null rectangle shows everything.
    QRectF crop;

    bool hasCrop() const { return !crop.isNull() && crop != QRectF(0, 0, 1, 1); }
    bool isIdentity() const { return x == 0.0 && y == 0.0 && scale == 1.0 && rotation == 0.0 && opacity == 1.0 && !hasCrop(); }
    bool operator==(const ClipTransform&) const = default;

    QJsonObject toJson() const;
    static ClipTransform fromJson(const QJsonObject& o);
};

// A transition is stored on the clip it leads into and is centred on the cut:
// it starts duration/2 before the cut and ends duration/2 after it, using the
// media beyond the out point of the outgoing clip and before the in point of
// the incoming clip. The timeline length never changes.
enum class TransitionType {
    None,
    CrossDissolve,
    DipToBlack,
    DipToWhite,
    WipeLeft,  // the new clip is revealed from the right edge towards the left
    WipeRight,
    WipeUp,
    WipeDown,
    SlideLeft, // the new clip pushes the old one out to the left
    SlideRight,
    Zoom,
    Blur,
    // Transition pack (0.7)
    PushUp,    // the new clip pushes the old one out upwards
    PushDown,
    Iris,      // a growing circle reveals the new clip
    ClockWipe, // a hand sweeping round from twelve o'clock
    BarnDoor,  // the old clip opens like two doors from the middle
    DiagonalWipe,
    Flash,     // a white flash at the cut
    Pixelate,  // the picture breaks into blocks and comes back
    Spin,      // the old clip spins away, the new one spins in
    Glitch,    // colour channels and stripes jump about
    ZoomBlur,  // zooms into the cut with motion blur
};

// All transition types, for menus (None first).
std::vector<TransitionType> allTransitionTypes();

struct Transition {
    TransitionType type = TransitionType::None;
    Ticks duration = 0;

    bool isNone() const { return type == TransitionType::None || duration <= 0; }
    bool operator==(const Transition&) const = default;

    QJsonObject toJson() const;
    static Transition fromJson(const QJsonObject& o);
};

QString transitionTypeId(TransitionType t);
TransitionType transitionTypeFromId(const QString& id);

enum class TitleAnimation { None, Fade, SlideUp, Pop, Typewriter, SlideLeft, Zoom, Blur };

// Shapes drawn with a title (title templates).
enum class TitleDecoration {
    None,
    Bar,       // an accent bar on the left of the text (lower thirds)
    Underline, // an accent line under the text
    Frame,     // a thin rectangle around the text
    Band,      // a full-width band behind the text (news ticker)
    Brackets,  // corner brackets
};

QString titleDecorationId(TitleDecoration d);
TitleDecoration titleDecorationFromId(const QString& id);

QString titleAnimationId(TitleAnimation a);
TitleAnimation titleAnimationFromId(const QString& id);

// Text of a title clip. Sizes are relative to the frame height so a title
// looks the same at every output resolution.
struct TitleData {
    QString text;
    QString fontFamily;         // empty = default sans serif
    double size = 0.075;        // font size as a fraction of the frame height
    bool bold = true;
    bool italic = false;
    QColor color = QColor(255, 255, 255);
    QColor outlineColor = QColor(0, 0, 0);
    double outlineWidth = 0.0;  // fraction of the font size
    bool shadow = true;
    QColor backgroundColor = QColor(0, 0, 0);
    double backgroundOpacity = 0.0;
    int align = 1;              // 0 left, 1 centre, 2 right
    double x = 0.5;             // centre of the text block, fraction of the frame width
    double y = 0.82;            // ... and height
    TitleAnimation animationIn = TitleAnimation::Fade;
    TitleAnimation animationOut = TitleAnimation::Fade;
    Ticks animationDuration = kTicksPerSecond / 2;
    TitleDecoration decoration = TitleDecoration::None;
    QColor accentColor = QColor(255, 176, 32);
    double letterSpacing = 0.0; // extra space between letters, fraction of the font size
    bool uppercase = false;

    bool operator==(const TitleData&) const = default;

    QJsonObject toJson() const;
    static TitleData fromJson(const QJsonObject& o);
};

// Camera path of a video clip measured by the stabiliser: for every analysed
// source frame its media time and how far the picture content has moved since
// the first frame (shift as a fraction of the picture size, rotation in
// degrees). Rendering shifts each frame from this path onto a smoothed one.
struct CameraSample {
    Ticks time = 0;
    double x = 0.0, y = 0.0, angle = 0.0;
    bool operator==(const CameraSample&) const = default;
};

struct Stabilization {
    bool enabled = false;
    double smoothing = 1.0; // seconds of shake averaged away (0.2 .. 5)
    double zoom = 1.0;      // enlarges the picture so the moving edges stay outside the frame
    std::vector<CameraSample> path;

    struct Correction {
        double x = 0.0, y = 0.0, angle = 0.0; // fraction of the picture size, degrees
    };
    bool isActive() const { return enabled && path.size() > 1; }
    // Shift and rotation that move the frame at media time `t` onto the
    // smoothed camera path.
    Correction correctionAt(Ticks t) const;
    // Smallest zoom that keeps the picture edges out of view for the current
    // smoothing (pictures with `aspect` = width / height), at most 1.5.
    double requiredZoom(double aspect) const;

    bool operator==(const Stabilization&) const = default;
    QJsonObject toJson() const;
    static Stabilization fromJson(const QJsonObject& o);
};

// A clip that follows a tracked object: its offset from where it is placed,
// as a fraction of the frame, at times relative to the clip start.
struct MotionPoint {
    Ticks time = 0;
    double x = 0.0, y = 0.0;
    bool operator==(const MotionPoint&) const = default;
};

struct MotionPath {
    std::vector<MotionPoint> points; // sorted by time

    bool isEmpty() const { return points.empty(); }
    // Linear between the points, held before the first and after the last.
    QPointF offsetAt(Ticks local) const;
    bool operator==(const MotionPath&) const = default;
    QJsonArray toJson() const;
    static MotionPath fromJson(const QJsonArray& a);
};

// Faces and number plates hidden in the picture. The regions are found once
// by the detectors (see ai/Redaction.h) and stored here, so rendering needs
// no neural network. Each found face or plate is one track: its box at the
// analysed source frames (media time, fraction of the decoded picture),
// linear in between.
enum class RedactionKind { Face, Plate, Manual };
enum class RedactionStyle { Blur, Pixelate, Fill };

QString redactionStyleId(RedactionStyle s);
RedactionStyle redactionStyleFromId(const QString& id);

struct RedactionBox {
    Ticks time = 0;
    QRectF rect;
    bool operator==(const RedactionBox&) const = default;
};

struct RedactionTrack {
    RedactionKind kind = RedactionKind::Face;
    bool enabled = true;
    std::vector<RedactionBox> boxes; // sorted by time

    Ticks start() const { return boxes.empty() ? 0 : boxes.front().time; }
    Ticks end() const { return boxes.empty() ? 0 : boxes.back().time; }
    // The box at media time `t`; empty outside [start - hold, end + hold].
    QRectF rectAt(Ticks t, Ticks hold) const;
    bool operator==(const RedactionTrack&) const = default;
};

struct Redaction {
    bool enabled = false;
    bool faces = true;   // hide the face tracks
    bool plates = true;  // hide the number plate tracks
    RedactionStyle style = RedactionStyle::Blur;
    double strength = 0.6; // 0 .. 1: how little of the region stays recognisable
    std::vector<RedactionTrack> tracks;
    // Media range that was searched; media outside it (after the clip is
    // lengthened) has not been checked.
    Ticks analyzedFrom = 0, analyzedTo = 0;

    bool isActive() const;
    bool shows(const RedactionTrack& t) const;
    struct Region {
        QRectF rect; // fraction of the picture
        bool ellipse = false;
    };
    // What to hide at media time `t`.
    std::vector<Region> regionsAt(Ticks t) const;
    int count(RedactionKind kind) const;

    bool operator==(const Redaction&) const = default;
    QJsonObject toJson() const;
    static Redaction fromJson(const QJsonObject& o);
};

// Cuts the subject out of the picture with a segmentation model; the rest
// becomes transparent so the tracks below show through.
struct BackgroundRemoval {
    bool enabled = false;
    QString model = QStringLiteral("fast"); // ai::vision background model id
    double softness = 0.5;                  // 0 = hard edge .. 1 = soft edge
    bool operator==(const BackgroundRemoval&) const = default;
    QJsonObject toJson() const;
    static BackgroundRemoval fromJson(const QJsonObject& o);
};


// Speed changes of a media clip: faster, slower, backwards, or a speed ramp
// that gets gradually faster and slower. The clip's source range is then
// measured in "remapped" time, [0, length), which runs at timeline speed;
// map() turns it into media time. Splitting, trimming and moving keep working
// on the remapped time, so the clip is cut like any other.
struct SpeedPoint {
    double pos = 0.0;  // 0 .. 1 along the remapped range
    double rate = 1.0; // relative speed there (linear in between)
    bool operator==(const SpeedPoint&) const = default;
};

// How new pictures are made between source frames when a clip plays slower
// than it was filmed.
enum class FrameBlending {
    None,        // each frame is held (steps)
    Blend,       // neighbouring frames are mixed
    OpticalFlow, // in-between frames are drawn from the motion (smooth slow motion)
};

struct TimeRemap {
    double speed = 1.0;            // average speed: 2 = twice as fast, 0.5 = half (0.05 .. 20)
    bool reverse = false;          // plays backwards
    std::vector<SpeedPoint> ramp;  // speed ramp shape; empty = constant speed
    FrameBlending blending = FrameBlending::OpticalFlow;
    bool keepPitch = true;         // sound keeps its pitch (constant speed)
    Ticks length = 0;              // remapped length of the whole range
    Ticks mediaIn = 0;             // media range that the remapped range covers
    Ticks mediaOut = 0;

    bool isActive() const { return length > 0 && (speed != 1.0 || reverse || !ramp.empty()); }
    // Relative speed at `pos` (0 .. 1) of a ramp.
    double rateAt(double pos) const;
    // Media time at remapped time `v` (clamped to the media range).
    Ticks map(Ticks v) const;
    // Media seconds per timeline second at remapped time `v` (negative when
    // playing backwards).
    double mediaRate(Ticks v) const;
    // Remapped time at which media time `m` is shown (inverse of map).
    Ticks unmap(Ticks m) const;
    // Average relative speed of the ramp (1 without a ramp).
    double rampAverage() const;

    bool operator==(const TimeRemap&) const = default;
    QJsonObject toJson() const;
    static TimeRemap fromJson(const QJsonObject& o);
};

// Ready-made speed ramps.
enum class SpeedRampPreset { None, Montage, Hero, Bullet, FlashIn, FlashOut, SlowIn, SlowOut };
std::vector<SpeedPoint> speedRampPoints(SpeedRampPreset preset);
QString speedRampId(SpeedRampPreset preset);
SpeedRampPreset speedRampFromId(const QString& id);
// The preset a ramp was made from (None when it was changed by hand).
SpeedRampPreset speedRampPresetOf(const std::vector<SpeedPoint>& ramp);

// Green or blue screen: the key colour becomes transparent so the tracks below
// show through.
struct ChromaKey {
    bool enabled = false;
    QColor color = QColor(0, 177, 64); // the screen colour
    double tolerance = 0.35;           // 0 .. 1: how far from the key colour still goes away
    double softness = 0.15;            // 0 .. 1: width of the soft edge
    double spill = 0.5;                // 0 .. 1: removes the green / blue glow on the subject
    bool operator==(const ChromaKey&) const = default;
    QJsonObject toJson() const;
    static ChromaKey fromJson(const QJsonObject& o);
};

// Picture effects of a video clip, applied after the colour settings.
struct PictureEffects {
    ChromaKey chromaKey;
    double denoise = 0.0;  // 0 .. 1: removes grain / noise ("karıncalanma")
    double sharpen = 0.0;  // 0 .. 1
    double blur = 0.0;     // 0 .. 1
    double vignette = 0.0; // 0 .. 1
    double grain = 0.0;    // 0 .. 1 film grain

    bool isIdentity() const {
        return !chromaKey.enabled && denoise == 0.0 && sharpen == 0.0 && blur == 0.0 && vignette == 0.0 && grain == 0.0;
    }
    bool operator==(const PictureEffects&) const = default;
    QJsonObject toJson() const;
    static PictureEffects fromJson(const QJsonObject& o);
};

} // namespace lumora

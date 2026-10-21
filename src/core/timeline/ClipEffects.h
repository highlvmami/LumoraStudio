#pragma once

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
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

    bool isIdentity() const { return x == 0.0 && y == 0.0 && scale == 1.0 && rotation == 0.0 && opacity == 1.0; }
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
};

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

enum class TitleAnimation { None, Fade, SlideUp, Pop, Typewriter };

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

} // namespace lumora

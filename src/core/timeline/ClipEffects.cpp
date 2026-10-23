#include "ClipEffects.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace lumora {

namespace {

QJsonValue ticksToJson(Ticks t) { return QString::number(t); }
Ticks ticksFromJson(const QJsonValue& v, Ticks fallback) {
    bool ok = false;
    const Ticks t = v.toString().toLongLong(&ok);
    return ok ? t : fallback;
}

QString colorToJson(const QColor& c) { return c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb); }
QColor colorFromJson(const QJsonValue& v, const QColor& fallback) {
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}

struct TransitionName {
    TransitionType type;
    const char* id;
};
constexpr TransitionName kTransitionNames[] = {
    {TransitionType::None, "none"},          {TransitionType::CrossDissolve, "crossDissolve"},
    {TransitionType::DipToBlack, "dipToBlack"}, {TransitionType::DipToWhite, "dipToWhite"},
    {TransitionType::WipeLeft, "wipeLeft"},  {TransitionType::WipeRight, "wipeRight"},
    {TransitionType::WipeUp, "wipeUp"},      {TransitionType::WipeDown, "wipeDown"},
    {TransitionType::SlideLeft, "slideLeft"}, {TransitionType::SlideRight, "slideRight"},
    {TransitionType::Zoom, "zoom"},          {TransitionType::Blur, "blur"},
    {TransitionType::PushUp, "pushUp"},      {TransitionType::PushDown, "pushDown"},
    {TransitionType::Iris, "iris"},          {TransitionType::ClockWipe, "clockWipe"},
    {TransitionType::BarnDoor, "barnDoor"},  {TransitionType::DiagonalWipe, "diagonalWipe"},
    {TransitionType::Flash, "flash"},        {TransitionType::Pixelate, "pixelate"},
    {TransitionType::Spin, "spin"},          {TransitionType::Glitch, "glitch"},
    {TransitionType::ZoomBlur, "zoomBlur"},
};

struct AnimationName {
    TitleAnimation anim;
    const char* id;
};
constexpr AnimationName kAnimationNames[] = {
    {TitleAnimation::None, "none"}, {TitleAnimation::Fade, "fade"},   {TitleAnimation::SlideUp, "slideUp"},
    {TitleAnimation::Pop, "pop"},   {TitleAnimation::Typewriter, "typewriter"},
    {TitleAnimation::SlideLeft, "slideLeft"}, {TitleAnimation::Zoom, "zoom"}, {TitleAnimation::Blur, "blur"},
};

struct DecorationName {
    TitleDecoration decoration;
    const char* id;
};
constexpr DecorationName kDecorationNames[] = {
    {TitleDecoration::None, "none"},           {TitleDecoration::Bar, "bar"},   {TitleDecoration::Underline, "underline"},
    {TitleDecoration::Frame, "frame"},         {TitleDecoration::Band, "band"}, {TitleDecoration::Brackets, "brackets"},
};

struct RampName {
    SpeedRampPreset preset;
    const char* id;
};
constexpr RampName kRampNames[] = {
    {SpeedRampPreset::None, "none"},       {SpeedRampPreset::Montage, "montage"}, {SpeedRampPreset::Hero, "hero"},
    {SpeedRampPreset::Bullet, "bullet"},   {SpeedRampPreset::FlashIn, "flashIn"}, {SpeedRampPreset::FlashOut, "flashOut"},
    {SpeedRampPreset::SlowIn, "slowIn"},   {SpeedRampPreset::SlowOut, "slowOut"},
};

} // namespace

QString transitionTypeId(TransitionType t) {
    for (const auto& n : kTransitionNames)
        if (n.type == t)
            return QString::fromLatin1(n.id);
    return QStringLiteral("none");
}

TransitionType transitionTypeFromId(const QString& id) {
    for (const auto& n : kTransitionNames)
        if (id == QLatin1String(n.id))
            return n.type;
    return TransitionType::None;
}

std::vector<TransitionType> allTransitionTypes() {
    std::vector<TransitionType> out;
    for (const auto& n : kTransitionNames)
        out.push_back(n.type);
    return out;
}

QString titleDecorationId(TitleDecoration d) {
    for (const auto& n : kDecorationNames)
        if (n.decoration == d)
            return QString::fromLatin1(n.id);
    return QStringLiteral("none");
}

TitleDecoration titleDecorationFromId(const QString& id) {
    for (const auto& n : kDecorationNames)
        if (id == QLatin1String(n.id))
            return n.decoration;
    return TitleDecoration::None;
}

QString titleAnimationId(TitleAnimation a) {
    for (const auto& n : kAnimationNames)
        if (n.anim == a)
            return QString::fromLatin1(n.id);
    return QStringLiteral("none");
}

TitleAnimation titleAnimationFromId(const QString& id) {
    for (const auto& n : kAnimationNames)
        if (id == QLatin1String(n.id))
            return n.anim;
    return TitleAnimation::None;
}

QJsonObject ClipTransform::toJson() const {
    QJsonObject o{{"x", x}, {"y", y}, {"scale", scale}, {"rotation", rotation}, {"opacity", opacity}};
    if (hasCrop())
        o["crop"] = QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()};
    return o;
}

ClipTransform ClipTransform::fromJson(const QJsonObject& o) {
    ClipTransform t;
    t.x = o.value("x").toDouble(0.0);
    t.y = o.value("y").toDouble(0.0);
    t.scale = o.value("scale").toDouble(1.0);
    t.rotation = o.value("rotation").toDouble(0.0);
    t.opacity = o.value("opacity").toDouble(1.0);
    const QJsonArray c = o.value("crop").toArray();
    if (c.size() == 4)
        t.crop = QRectF(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(), c[3].toDouble());
    return t;
}

QJsonObject Transition::toJson() const {
    return QJsonObject{{"type", transitionTypeId(type)}, {"duration", ticksToJson(duration)}};
}

Transition Transition::fromJson(const QJsonObject& o) {
    Transition t;
    t.type = transitionTypeFromId(o.value("type").toString());
    t.duration = ticksFromJson(o.value("duration"), 0);
    return t;
}

QJsonObject TitleData::toJson() const {
    QJsonObject o;
    o["text"] = text;
    if (!fontFamily.isEmpty())
        o["font"] = fontFamily;
    o["size"] = size;
    o["bold"] = bold;
    o["italic"] = italic;
    o["color"] = colorToJson(color);
    o["outlineColor"] = colorToJson(outlineColor);
    o["outlineWidth"] = outlineWidth;
    o["shadow"] = shadow;
    o["backgroundColor"] = colorToJson(backgroundColor);
    o["backgroundOpacity"] = backgroundOpacity;
    o["align"] = align;
    o["x"] = x;
    o["y"] = y;
    o["animationIn"] = titleAnimationId(animationIn);
    o["animationOut"] = titleAnimationId(animationOut);
    o["animationDuration"] = ticksToJson(animationDuration);
    if (decoration != TitleDecoration::None) {
        o["decoration"] = titleDecorationId(decoration);
        o["accentColor"] = colorToJson(accentColor);
    }
    if (letterSpacing != 0.0)
        o["letterSpacing"] = letterSpacing;
    if (uppercase)
        o["uppercase"] = true;
    return o;
}

TitleData TitleData::fromJson(const QJsonObject& o) {
    TitleData d;
    d.text = o.value("text").toString();
    d.fontFamily = o.value("font").toString();
    d.size = o.value("size").toDouble(d.size);
    d.bold = o.value("bold").toBool(d.bold);
    d.italic = o.value("italic").toBool(d.italic);
    d.color = colorFromJson(o.value("color"), d.color);
    d.outlineColor = colorFromJson(o.value("outlineColor"), d.outlineColor);
    d.outlineWidth = o.value("outlineWidth").toDouble(d.outlineWidth);
    d.shadow = o.value("shadow").toBool(d.shadow);
    d.backgroundColor = colorFromJson(o.value("backgroundColor"), d.backgroundColor);
    d.backgroundOpacity = o.value("backgroundOpacity").toDouble(d.backgroundOpacity);
    d.align = o.value("align").toInt(d.align);
    d.x = o.value("x").toDouble(d.x);
    d.y = o.value("y").toDouble(d.y);
    d.animationIn = o.contains("animationIn") ? titleAnimationFromId(o.value("animationIn").toString()) : d.animationIn;
    d.animationOut =
        o.contains("animationOut") ? titleAnimationFromId(o.value("animationOut").toString()) : d.animationOut;
    d.animationDuration = ticksFromJson(o.value("animationDuration"), d.animationDuration);
    d.decoration = titleDecorationFromId(o.value("decoration").toString());
    d.accentColor = colorFromJson(o.value("accentColor"), d.accentColor);
    d.letterSpacing = o.value("letterSpacing").toDouble(0.0);
    d.uppercase = o.value("uppercase").toBool(false);
    return d;
}

// ---------------------------------------------------------------------------
// Stabilization
// ---------------------------------------------------------------------------

Stabilization::Correction Stabilization::correctionAt(Ticks t) const {
    Correction c;
    if (path.size() < 2)
        return c;
    auto it = std::upper_bound(path.begin(), path.end(), t, [](Ticks v, const CameraSample& s) { return v < s.time; });
    const long i = std::clamp<long>(static_cast<long>(it - path.begin()) - 1, 0, static_cast<long>(path.size()) - 1);
    // Gaussian average of the path around frame i (sigma = smoothing / 2).
    const double sigma = std::max(0.05, smoothing / 2.0);
    const double step = std::max(1e-6, ticksToSeconds(path.back().time - path.front().time) / (path.size() - 1));
    // A window that stays centred near the ends, so a steady pan is not
    // mistaken for shake there.
    const long last = static_cast<long>(path.size()) - 1;
    const long radius = std::min({std::max<long>(1, static_cast<long>(std::ceil(2.5 * sigma / step))), i, last - i});
    double wsum = 0, sx = 0, sy = 0, sa = 0;
    for (long k = i - radius; k <= i + radius; ++k) {
        const double d = (k - i) * step;
        const double w = std::exp(-0.5 * d * d / (sigma * sigma));
        wsum += w;
        sx += w * path[k].x;
        sy += w * path[k].y;
        sa += w * path[k].angle;
    }
    c.x = sx / wsum - path[i].x;
    c.y = sy / wsum - path[i].y;
    c.angle = sa / wsum - path[i].angle;
    return c;
}

double Stabilization::requiredZoom(double aspect) const {
    double z = 1.0;
    aspect = std::max(0.1, aspect);
    const double longSide = std::max(aspect, 1.0 / aspect);
    for (const auto& s : path) {
        const Correction c = correctionAt(s.time);
        const double r = std::abs(c.angle) * 3.14159265358979 / 180.0;
        const double need = std::cos(r) + std::sin(r) * longSide + 2 * std::max(std::abs(c.x), std::abs(c.y));
        z = std::max(z, need);
    }
    return std::min(z, 1.5);
}

QJsonObject Stabilization::toJson() const {
    QJsonObject o;
    o["enabled"] = enabled;
    o["smoothing"] = smoothing;
    o["zoom"] = zoom;
    // One short array per sample: [time, x, y, angle].
    QJsonArray a;
    for (const auto& s : path)
        a.append(QJsonArray{ticksToJson(s.time), s.x, s.y, s.angle});
    o["path"] = a;
    return o;
}

Stabilization Stabilization::fromJson(const QJsonObject& o) {
    Stabilization s;
    s.enabled = o.value("enabled").toBool();
    s.smoothing = o.value("smoothing").toDouble(1.0);
    s.zoom = o.value("zoom").toDouble(1.0);
    for (const auto& v : o.value("path").toArray()) {
        const QJsonArray e = v.toArray();
        if (e.size() < 4)
            continue;
        s.path.push_back({ticksFromJson(e[0], 0), e[1].toDouble(), e[2].toDouble(), e[3].toDouble()});
    }
    return s;
}

QPointF MotionPath::offsetAt(Ticks local) const {
    if (points.empty())
        return {};
    if (local <= points.front().time)
        return {points.front().x, points.front().y};
    if (local >= points.back().time)
        return {points.back().x, points.back().y};
    auto it = std::upper_bound(points.begin(), points.end(), local, [](Ticks v, const MotionPoint& p) { return v < p.time; });
    const MotionPoint& b = *it;
    const MotionPoint& a = *(it - 1);
    const double f = static_cast<double>(local - a.time) / std::max<Ticks>(1, b.time - a.time);
    return {a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
}

QJsonArray MotionPath::toJson() const {
    QJsonArray a;
    for (const auto& p : points)
        a.append(QJsonArray{ticksToJson(p.time), p.x, p.y});
    return a;
}

MotionPath MotionPath::fromJson(const QJsonArray& a) {
    MotionPath m;
    for (const auto& v : a) {
        const QJsonArray e = v.toArray();
        if (e.size() >= 3)
            m.points.push_back({ticksFromJson(e[0], 0), e[1].toDouble(), e[2].toDouble()});
    }
    return m;
}

QString redactionStyleId(RedactionStyle s) {
    switch (s) {
    case RedactionStyle::Blur: return QStringLiteral("blur");
    case RedactionStyle::Pixelate: return QStringLiteral("pixelate");
    case RedactionStyle::Fill: return QStringLiteral("fill");
    }
    return QStringLiteral("blur");
}

RedactionStyle redactionStyleFromId(const QString& id) {
    if (id == QLatin1String("pixelate"))
        return RedactionStyle::Pixelate;
    if (id == QLatin1String("fill"))
        return RedactionStyle::Fill;
    return RedactionStyle::Blur;
}

namespace {

QString redactionKindId(RedactionKind k) {
    switch (k) {
    case RedactionKind::Face: return QStringLiteral("face");
    case RedactionKind::Plate: return QStringLiteral("plate");
    case RedactionKind::Manual: return QStringLiteral("manual");
    }
    return QStringLiteral("face");
}

RedactionKind redactionKindFromId(const QString& id) {
    if (id == QLatin1String("plate"))
        return RedactionKind::Plate;
    if (id == QLatin1String("manual"))
        return RedactionKind::Manual;
    return RedactionKind::Face;
}

// Four decimals of a fraction of the picture are well under a pixel at 8K.
double round4(double v) { return std::round(v * 10000.0) / 10000.0; }

} // namespace

QRectF RedactionTrack::rectAt(Ticks t, Ticks hold) const {
    if (boxes.empty() || t < boxes.front().time - hold || t > boxes.back().time + hold)
        return {};
    if (t <= boxes.front().time)
        return boxes.front().rect;
    if (t >= boxes.back().time)
        return boxes.back().rect;
    auto it = std::upper_bound(boxes.begin(), boxes.end(), t, [](Ticks v, const RedactionBox& b) { return v < b.time; });
    const RedactionBox& b = *it;
    const RedactionBox& a = *(it - 1);
    const double f = b.time > a.time ? static_cast<double>(t - a.time) / (b.time - a.time) : 0.0;
    auto lerp = [f](double x, double y) { return x + (y - x) * f; };
    return {lerp(a.rect.x(), b.rect.x()), lerp(a.rect.y(), b.rect.y()), lerp(a.rect.width(), b.rect.width()),
            lerp(a.rect.height(), b.rect.height())};
}

bool Redaction::shows(const RedactionTrack& t) const {
    if (!t.enabled)
        return false;
    switch (t.kind) {
    case RedactionKind::Face: return faces;
    case RedactionKind::Plate: return plates;
    case RedactionKind::Manual: return true;
    }
    return true;
}

bool Redaction::isActive() const {
    return enabled && std::any_of(tracks.begin(), tracks.end(), [this](const RedactionTrack& t) { return shows(t); });
}

std::vector<Redaction::Region> Redaction::regionsAt(Ticks t) const {
    std::vector<Region> out;
    if (!enabled)
        return out;
    // A detection is shown a little before and after the frames it was found
    // on, so a face is not uncovered for a frame at the start of a track.
    const Ticks hold = kTicksPerSecond / 8;
    for (const auto& tr : tracks) {
        if (!shows(tr))
            continue;
        const QRectF r = tr.rectAt(t, hold);
        if (!r.isEmpty())
            out.push_back({r, tr.kind == RedactionKind::Face});
    }
    return out;
}

int Redaction::count(RedactionKind kind) const {
    return static_cast<int>(std::count_if(tracks.begin(), tracks.end(), [kind](const RedactionTrack& t) { return t.kind == kind; }));
}

QJsonObject Redaction::toJson() const {
    QJsonObject o;
    o["enabled"] = enabled;
    o["faces"] = faces;
    o["plates"] = plates;
    o["style"] = redactionStyleId(style);
    o["strength"] = strength;
    o["from"] = ticksToJson(analyzedFrom);
    o["to"] = ticksToJson(analyzedTo);
    QJsonArray ta;
    for (const auto& t : tracks) {
        QJsonObject to;
        to["kind"] = redactionKindId(t.kind);
        if (!t.enabled)
            to["enabled"] = false;
        QJsonArray ba;
        for (const auto& b : t.boxes)
            ba.append(QJsonArray{ticksToJson(b.time), round4(b.rect.x()), round4(b.rect.y()), round4(b.rect.width()),
                                 round4(b.rect.height())});
        to["boxes"] = ba;
        ta.append(to);
    }
    o["tracks"] = ta;
    return o;
}

Redaction Redaction::fromJson(const QJsonObject& o) {
    Redaction r;
    r.enabled = o.value("enabled").toBool();
    r.faces = o.value("faces").toBool(true);
    r.plates = o.value("plates").toBool(true);
    r.style = redactionStyleFromId(o.value("style").toString());
    r.strength = std::clamp(o.value("strength").toDouble(0.6), 0.0, 1.0);
    r.analyzedFrom = ticksFromJson(o.value("from"), 0);
    r.analyzedTo = ticksFromJson(o.value("to"), 0);
    for (const auto& tv : o.value("tracks").toArray()) {
        const QJsonObject to = tv.toObject();
        RedactionTrack t;
        t.kind = redactionKindFromId(to.value("kind").toString());
        t.enabled = to.value("enabled").toBool(true);
        for (const auto& bv : to.value("boxes").toArray()) {
            const QJsonArray e = bv.toArray();
            if (e.size() < 5)
                continue;
            t.boxes.push_back({ticksFromJson(e[0], 0), QRectF(e[1].toDouble(), e[2].toDouble(), e[3].toDouble(), e[4].toDouble())});
        }
        std::sort(t.boxes.begin(), t.boxes.end(), [](const RedactionBox& a, const RedactionBox& b) { return a.time < b.time; });
        if (!t.boxes.empty())
            r.tracks.push_back(std::move(t));
    }
    return r;
}

QJsonObject BackgroundRemoval::toJson() const {
    QJsonObject o;
    o["enabled"] = enabled;
    o["model"] = model;
    o["softness"] = softness;
    if (!object.boxes.empty()) {
        QJsonArray ba;
        for (const auto& b : object.boxes)
            ba.append(QJsonArray{ticksToJson(b.time), round4(b.rect.x()), round4(b.rect.y()), round4(b.rect.width()),
                                 round4(b.rect.height())});
        o["object"] = ba;
    }
    return o;
}

BackgroundRemoval BackgroundRemoval::fromJson(const QJsonObject& o) {
    BackgroundRemoval b;
    b.enabled = o.value("enabled").toBool();
    b.model = o.value("model").toString(QStringLiteral("fast"));
    b.softness = std::clamp(o.value("softness").toDouble(0.5), 0.0, 1.0);
    b.object.kind = RedactionKind::Manual;
    for (const auto& v : o.value("object").toArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() >= 5)
            b.object.boxes.push_back({ticksFromJson(a[0], 0), QRectF(a[1].toDouble(), a[2].toDouble(), a[3].toDouble(), a[4].toDouble())});
    }
    return b;
}


// ---------------------------------------------------------------------------
// Speed
// ---------------------------------------------------------------------------

namespace {

// Integral of the ramp's relative speed from 0 to u (u in 0 .. 1).
double rampIntegral(const std::vector<SpeedPoint>& ramp, double u) {
    if (ramp.empty())
        return u;
    double sum = 0.0;
    double prevPos = 0.0, prevRate = ramp.front().rate;
    auto segment = [&](double pos, double rate) {
        // Linear from (prevPos, prevRate) to (pos, rate), integrated up to u.
        const double end = std::min(pos, u);
        if (end > prevPos) {
            const double span = pos - prevPos;
            const double rEnd = span > 0 ? prevRate + (rate - prevRate) * (end - prevPos) / span : rate;
            sum += (end - prevPos) * (prevRate + rEnd) / 2;
        }
        prevPos = pos;
        prevRate = rate;
    };
    for (const auto& p : ramp)
        segment(std::clamp(p.pos, 0.0, 1.0), std::max(0.01, p.rate));
    segment(1.0, prevRate);
    return sum;
}

// Inverse of rampIntegral (bisection; the integral grows monotonically).
double rampInverse(const std::vector<SpeedPoint>& ramp, double value) {
    if (ramp.empty())
        return value;
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 48; ++i) {
        const double mid = (lo + hi) / 2;
        (rampIntegral(ramp, mid) < value ? lo : hi) = mid;
    }
    return (lo + hi) / 2;
}

} // namespace

double TimeRemap::rateAt(double pos) const {
    if (ramp.empty())
        return 1.0;
    pos = std::clamp(pos, 0.0, 1.0);
    if (pos <= ramp.front().pos)
        return ramp.front().rate;
    for (size_t i = 1; i < ramp.size(); ++i)
        if (pos <= ramp[i].pos) {
            const SpeedPoint& a = ramp[i - 1];
            const SpeedPoint& b = ramp[i];
            const double f = b.pos > a.pos ? (pos - a.pos) / (b.pos - a.pos) : 1.0;
            return a.rate + (b.rate - a.rate) * f;
        }
    return ramp.back().rate;
}

Ticks TimeRemap::map(Ticks v) const {
    if (length <= 0)
        return v;
    const double u = std::clamp(static_cast<double>(v) / length, 0.0, 1.0);
    const double total = rampIntegral(ramp, 1.0);
    const double f = total > 0 ? rampIntegral(ramp, u) / total : u;
    const double span = static_cast<double>(mediaOut - mediaIn);
    // Backwards, a moment shows the frame just before it in the media: the
    // range [in, out) then plays from the last frame to the first.
    return reverse ? std::max(mediaIn, mediaOut - 1 - static_cast<Ticks>(std::llround(f * span)))
                   : mediaIn + static_cast<Ticks>(std::llround(f * span));
}

double TimeRemap::mediaRate(Ticks v) const {
    if (length <= 0)
        return 1.0;
    const double total = rampIntegral(ramp, 1.0);
    const double u = std::clamp(static_cast<double>(v) / length, 0.0, 1.0);
    const double rate = (total > 0 ? rateAt(u) / total : 1.0) * static_cast<double>(mediaOut - mediaIn) / length;
    return reverse ? -rate : rate;
}

Ticks TimeRemap::unmap(Ticks m) const {
    if (length <= 0 || mediaOut <= mediaIn)
        return m;
    double f = static_cast<double>(m - mediaIn) / (mediaOut - mediaIn);
    if (reverse)
        f = 1.0 - f;
    f = std::clamp(f, 0.0, 1.0);
    const double u = rampInverse(ramp, f * rampIntegral(ramp, 1.0));
    return static_cast<Ticks>(std::llround(u * length));
}

double TimeRemap::rampAverage() const { return rampIntegral(ramp, 1.0); }

QJsonObject TimeRemap::toJson() const {
    QJsonObject o;
    o["speed"] = speed;
    if (reverse)
        o["reverse"] = true;
    if (!ramp.empty()) {
        QJsonArray a;
        for (const auto& p : ramp)
            a.append(QJsonArray{p.pos, p.rate});
        o["ramp"] = a;
    }
    o["blending"] = blending == FrameBlending::None    ? "none"
                    : blending == FrameBlending::Blend ? "blend"
                    : blending == FrameBlending::AiFlow ? "ai-flow"
                                                       : "flow";
    o["keepPitch"] = keepPitch;
    o["length"] = ticksToJson(length);
    o["mediaIn"] = ticksToJson(mediaIn);
    o["mediaOut"] = ticksToJson(mediaOut);
    return o;
}

TimeRemap TimeRemap::fromJson(const QJsonObject& o) {
    TimeRemap r;
    r.speed = std::clamp(o.value("speed").toDouble(1.0), 0.01, 100.0);
    r.reverse = o.value("reverse").toBool(false);
    for (const auto& v : o.value("ramp").toArray()) {
        const QJsonArray e = v.toArray();
        if (e.size() >= 2)
            r.ramp.push_back({std::clamp(e[0].toDouble(), 0.0, 1.0), std::max(0.01, e[1].toDouble(1.0))});
    }
    const QString b = o.value("blending").toString();
    r.blending = b == QLatin1String("none")      ? FrameBlending::None
                 : b == QLatin1String("blend")   ? FrameBlending::Blend
                 : b == QLatin1String("ai-flow") ? FrameBlending::AiFlow
                                                 : FrameBlending::OpticalFlow;
    r.keepPitch = o.value("keepPitch").toBool(true);
    r.length = ticksFromJson(o.value("length"), 0);
    r.mediaIn = ticksFromJson(o.value("mediaIn"), 0);
    r.mediaOut = ticksFromJson(o.value("mediaOut"), 0);
    return r;
}

std::vector<SpeedPoint> speedRampPoints(SpeedRampPreset preset) {
    switch (preset) {
    case SpeedRampPreset::None: return {};
    // Fast, a slow moment in the middle, fast again.
    case SpeedRampPreset::Montage: return {{0.0, 3.0}, {0.35, 3.0}, {0.45, 0.4}, {0.6, 0.4}, {0.7, 3.0}, {1.0, 3.0}};
    // Normal, slowing right down for the big moment, back to normal.
    case SpeedRampPreset::Hero: return {{0.0, 1.0}, {0.3, 1.0}, {0.42, 0.25}, {0.62, 0.25}, {0.75, 1.0}, {1.0, 1.0}};
    // Very slow in the middle, fast around it.
    case SpeedRampPreset::Bullet: return {{0.0, 2.0}, {0.38, 2.0}, {0.45, 0.15}, {0.58, 0.15}, {0.65, 2.0}, {1.0, 2.0}};
    // Starts fast and settles to normal speed.
    case SpeedRampPreset::FlashIn: return {{0.0, 5.0}, {0.25, 5.0}, {0.4, 1.0}, {1.0, 1.0}};
    // Normal speed that rushes away at the end.
    case SpeedRampPreset::FlashOut: return {{0.0, 1.0}, {0.6, 1.0}, {0.75, 5.0}, {1.0, 5.0}};
    // Starts slow and speeds up.
    case SpeedRampPreset::SlowIn: return {{0.0, 0.3}, {0.3, 0.3}, {0.6, 1.5}, {1.0, 1.5}};
    // Slows down towards the end.
    case SpeedRampPreset::SlowOut: return {{0.0, 1.5}, {0.4, 1.5}, {0.7, 0.3}, {1.0, 0.3}};
    }
    return {};
}

QString speedRampId(SpeedRampPreset preset) {
    for (const auto& n : kRampNames)
        if (n.preset == preset)
            return QString::fromLatin1(n.id);
    return QStringLiteral("none");
}

SpeedRampPreset speedRampFromId(const QString& id) {
    for (const auto& n : kRampNames)
        if (id == QLatin1String(n.id))
            return n.preset;
    return SpeedRampPreset::None;
}

SpeedRampPreset speedRampPresetOf(const std::vector<SpeedPoint>& ramp) {
    for (const auto& n : kRampNames)
        if (speedRampPoints(n.preset) == ramp)
            return n.preset;
    return SpeedRampPreset::None;
}

// ---------------------------------------------------------------------------
// Picture effects
// ---------------------------------------------------------------------------

QJsonObject ChromaKey::toJson() const {
    return QJsonObject{{"enabled", enabled}, {"color", colorToJson(color)}, {"tolerance", tolerance}, {"softness", softness}, {"spill", spill}};
}

ChromaKey ChromaKey::fromJson(const QJsonObject& o) {
    ChromaKey k;
    k.enabled = o.value("enabled").toBool();
    k.color = colorFromJson(o.value("color"), k.color);
    k.tolerance = std::clamp(o.value("tolerance").toDouble(k.tolerance), 0.0, 1.0);
    k.softness = std::clamp(o.value("softness").toDouble(k.softness), 0.0, 1.0);
    k.spill = std::clamp(o.value("spill").toDouble(k.spill), 0.0, 1.0);
    return k;
}

QJsonObject PictureEffects::toJson() const {
    QJsonObject o;
    if (chromaKey.enabled)
        o["chromaKey"] = chromaKey.toJson();
    auto put = [&](const char* key, double v) {
        if (v != 0.0)
            o[QLatin1String(key)] = v;
    };
    put("denoise", denoise);
    put("sharpen", sharpen);
    put("blur", blur);
    put("vignette", vignette);
    put("grain", grain);
    return o;
}

PictureEffects PictureEffects::fromJson(const QJsonObject& o) {
    PictureEffects e;
    if (o.contains("chromaKey"))
        e.chromaKey = ChromaKey::fromJson(o.value("chromaKey").toObject());
    auto get = [&](const char* key) { return std::clamp(o.value(QLatin1String(key)).toDouble(0.0), 0.0, 1.0); };
    e.denoise = get("denoise");
    e.sharpen = get("sharpen");
    e.blur = get("blur");
    e.vignette = get("vignette");
    e.grain = get("grain");
    return e;
}

} // namespace lumora

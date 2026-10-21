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
};

struct AnimationName {
    TitleAnimation anim;
    const char* id;
};
constexpr AnimationName kAnimationNames[] = {
    {TitleAnimation::None, "none"}, {TitleAnimation::Fade, "fade"},   {TitleAnimation::SlideUp, "slideUp"},
    {TitleAnimation::Pop, "pop"},   {TitleAnimation::Typewriter, "typewriter"},
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
    return QJsonObject{{"x", x}, {"y", y}, {"scale", scale}, {"rotation", rotation}, {"opacity", opacity}};
}

ClipTransform ClipTransform::fromJson(const QJsonObject& o) {
    ClipTransform t;
    t.x = o.value("x").toDouble(0.0);
    t.y = o.value("y").toDouble(0.0);
    t.scale = o.value("scale").toDouble(1.0);
    t.rotation = o.value("rotation").toDouble(0.0);
    t.opacity = o.value("opacity").toDouble(1.0);
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
    const long radius = std::max<long>(1, static_cast<long>(std::ceil(2.5 * sigma / step)));
    double wsum = 0, sx = 0, sy = 0, sa = 0;
    for (long k = std::max<long>(0, i - radius); k <= std::min<long>(static_cast<long>(path.size()) - 1, i + radius); ++k) {
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

} // namespace lumora

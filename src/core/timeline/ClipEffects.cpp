#include "ClipEffects.h"

#include <QJsonArray>

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

} // namespace lumora

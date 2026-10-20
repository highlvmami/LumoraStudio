#include "ColorGrade.h"

#include <cmath>

namespace lumora {

void ColorWheel::channelOffsets(double out[3]) const {
    const double r = std::hypot(x, y);
    if (r <= 0.0) {
        out[0] = out[1] = out[2] = 0.0;
        return;
    }
    const double angle = std::atan2(y, x);
    for (int c = 0; c < 3; ++c)
        out[c] = r * std::cos(angle - c * 2.0 * M_PI / 3.0);
}

QJsonObject ColorWheel::toJson() const { return QJsonObject{{"x", x}, {"y", y}, {"master", master}}; }

ColorWheel ColorWheel::fromJson(const QJsonObject& o) {
    ColorWheel w;
    w.x = o.value("x").toDouble();
    w.y = o.value("y").toDouble();
    w.master = o.value("master").toDouble();
    return w;
}

namespace {

struct Field {
    const char* key;
    double ColorGrade::*member;
};
constexpr Field kFields[] = {
    {"exposure", &ColorGrade::exposure},     {"contrast", &ColorGrade::contrast},
    {"highlights", &ColorGrade::highlights}, {"shadows", &ColorGrade::shadows},
    {"whites", &ColorGrade::whites},         {"blacks", &ColorGrade::blacks},
    {"temperature", &ColorGrade::temperature}, {"tint", &ColorGrade::tint},
    {"saturation", &ColorGrade::saturation}, {"vibrance", &ColorGrade::vibrance},
};

} // namespace

QJsonObject ColorGrade::toJson() const {
    QJsonObject o;
    for (const auto& f : kFields)
        if (this->*f.member != 0.0)
            o[QLatin1String(f.key)] = this->*f.member;
    if (!lift.isIdentity())
        o["lift"] = lift.toJson();
    if (!gamma.isIdentity())
        o["gamma"] = gamma.toJson();
    if (!gain.isIdentity())
        o["gain"] = gain.toJson();
    if (!lutPath.isEmpty()) {
        o["lut"] = lutPath;
        o["lutAmount"] = lutAmount;
    }
    return o;
}

ColorGrade ColorGrade::fromJson(const QJsonObject& o) {
    ColorGrade g;
    for (const auto& f : kFields)
        g.*f.member = o.value(QLatin1String(f.key)).toDouble();
    g.lift = ColorWheel::fromJson(o.value("lift").toObject());
    g.gamma = ColorWheel::fromJson(o.value("gamma").toObject());
    g.gain = ColorWheel::fromJson(o.value("gain").toObject());
    g.lutPath = o.value("lut").toString();
    g.lutAmount = o.value("lutAmount").toDouble(1.0);
    return g;
}

} // namespace lumora

#include "ColorGrade.h"

#include <algorithm>
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

// Monotone cubic (Fritsch-Carlson) tangents for sorted points.
std::vector<double> tangents(const std::vector<QPointF>& p) {
    const size_t n = p.size();
    std::vector<double> m(n, 0.0);
    if (n < 2)
        return m;
    std::vector<double> d(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        const double dx = p[i + 1].x() - p[i].x();
        d[i] = dx > 1e-9 ? (p[i + 1].y() - p[i].y()) / dx : 0.0;
    }
    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (size_t i = 1; i + 1 < n; ++i)
        m[i] = d[i - 1] * d[i] <= 0.0 ? 0.0 : (d[i - 1] + d[i]) / 2;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (d[i] == 0.0) {
            m[i] = m[i + 1] = 0.0;
            continue;
        }
        const double a = m[i] / d[i], b = m[i + 1] / d[i];
        const double h = a * a + b * b;
        if (h > 9.0) {
            const double t = 3.0 / std::sqrt(h);
            m[i] = t * a * d[i];
            m[i + 1] = t * b * d[i];
        }
    }
    return m;
}

double evalCurve(const std::vector<QPointF>& p, const std::vector<double>& m, double x) {
    x = std::clamp(x, 0.0, 1.0);
    if (p.empty())
        return x;
    if (p.size() == 1)
        return std::clamp(p[0].y(), 0.0, 1.0);
    if (x <= p.front().x())
        return std::clamp(p.front().y(), 0.0, 1.0);
    if (x >= p.back().x())
        return std::clamp(p.back().y(), 0.0, 1.0);
    size_t i = 0;
    while (i + 2 < p.size() && x > p[i + 1].x())
        ++i;
    const double h = p[i + 1].x() - p[i].x();
    if (h <= 1e-9)
        return std::clamp(p[i + 1].y(), 0.0, 1.0);
    const double t = (x - p[i].x()) / h;
    const double t2 = t * t, t3 = t2 * t;
    const double y = (2 * t3 - 3 * t2 + 1) * p[i].y() + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * p[i + 1].y() +
                     (t3 - t2) * h * m[i + 1];
    return std::clamp(y, 0.0, 1.0);
}

constexpr double kHslHues[kHslBands] = {0, 30, 60, 120, 180, 240, 270, 300};
constexpr const char* kHslIds[kHslBands] = {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"};

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

bool ToneCurve::isIdentity() const {
    for (const auto& p : points)
        if (std::abs(p.x() - p.y()) > 1e-9)
            return false;
    // Points on the diagonal only: still the identity whatever their count.
    return true;
}

double ToneCurve::eval(double x) const { return evalCurve(points, tangents(points), x); }

void ToneCurve::sample(std::vector<float>& table, int n) const {
    table.resize(n);
    const auto m = tangents(points);
    for (int i = 0; i < n; ++i)
        table[i] = static_cast<float>(evalCurve(points, m, n > 1 ? double(i) / (n - 1) : 0.0));
}

QJsonArray ToneCurve::toJson() const {
    QJsonArray a;
    for (const auto& p : points)
        a.append(QJsonArray{p.x(), p.y()});
    return a;
}

ToneCurve ToneCurve::fromJson(const QJsonArray& a) {
    ToneCurve c;
    for (const auto& v : a) {
        const QJsonArray xy = v.toArray();
        if (xy.size() == 2)
            c.points.emplace_back(std::clamp(xy[0].toDouble(), 0.0, 1.0), std::clamp(xy[1].toDouble(), 0.0, 1.0));
    }
    std::sort(c.points.begin(), c.points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    return c;
}

double hslBandHue(int band) { return kHslHues[std::clamp(band, 0, kHslBands - 1)]; }
const char* hslBandId(int band) { return kHslIds[std::clamp(band, 0, kHslBands - 1)]; }

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
    if (hasHsl()) {
        QJsonObject h;
        for (int i = 0; i < kHslBands; ++i)
            if (!hsl[i].isIdentity())
                h[QLatin1String(kHslIds[i])] =
                    QJsonObject{{"h", hsl[i].hue}, {"s", hsl[i].saturation}, {"l", hsl[i].luminance}};
        o["hsl"] = h;
    }
    if (hasCurves()) {
        QJsonObject c;
        if (!curveMaster.isIdentity())
            c["master"] = curveMaster.toJson();
        if (!curveRed.isIdentity())
            c["red"] = curveRed.toJson();
        if (!curveGreen.isIdentity())
            c["green"] = curveGreen.toJson();
        if (!curveBlue.isIdentity())
            c["blue"] = curveBlue.toJson();
        o["curves"] = c;
    }
    if (!lutPath.isEmpty()) {
        o["lut"] = lutPath;
        o["lutAmount"] = lutAmount;
    }
    if (!look.isEmpty()) {
        o["look"] = look;
        o["lookAmount"] = lookAmount;
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
    const QJsonObject h = o.value("hsl").toObject();
    for (int i = 0; i < kHslBands; ++i) {
        const QJsonObject b = h.value(QLatin1String(kHslIds[i])).toObject();
        g.hsl[i] = {b.value("h").toDouble(), b.value("s").toDouble(), b.value("l").toDouble()};
    }
    const QJsonObject c = o.value("curves").toObject();
    g.curveMaster = ToneCurve::fromJson(c.value("master").toArray());
    g.curveRed = ToneCurve::fromJson(c.value("red").toArray());
    g.curveGreen = ToneCurve::fromJson(c.value("green").toArray());
    g.curveBlue = ToneCurve::fromJson(c.value("blue").toArray());
    g.lutPath = o.value("lut").toString();
    g.lutAmount = o.value("lutAmount").toDouble(1.0);
    g.look = o.value("look").toString();
    g.lookAmount = o.value("lookAmount").toDouble(1.0);
    return g;
}

} // namespace lumora

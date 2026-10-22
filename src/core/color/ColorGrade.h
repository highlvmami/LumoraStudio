#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <array>
#include <vector>

namespace lumora {

// One of the three colour wheels (lift / gamma / gain). The puck position is
// a point in the unit disc: its angle is the hue that is pushed, its distance
// from the centre the strength. `master` moves all three channels together
// (the slider under the wheel), -1 .. +1.
struct ColorWheel {
    double x = 0.0;
    double y = 0.0;
    double master = 0.0;

    bool isIdentity() const { return x == 0.0 && y == 0.0 && master == 0.0; }
    bool operator==(const ColorWheel&) const = default;

    // Zero-mean per channel offset of the puck (red at 0°, green at 120°,
    // blue at 240°, counter-clockwise), scaled to the puck distance.
    void channelOffsets(double out[3]) const;

    QJsonObject toJson() const;
    static ColorWheel fromJson(const QJsonObject& o);
};

// A tone curve through control points in 0..1 (x = input, y = output),
// interpolated with a monotone cubic so it never overshoots between points.
// No points (or only the two neutral end points) = identity.
struct ToneCurve {
    std::vector<QPointF> points; // sorted by x

    bool isIdentity() const;
    bool operator==(const ToneCurve&) const = default;
    // Value of the curve at x (clamped to 0..1).
    double eval(double x) const;
    // Fills `table` with n samples of the curve over 0..1.
    void sample(std::vector<float>& table, int n) const;

    QJsonArray toJson() const;
    static ToneCurve fromJson(const QJsonArray& a);
};

// One colour range of the HSL mixer. -100 .. +100 each; hue moves the range
// towards its neighbours (up to 30 degrees), saturation and luminance work
// only on coloured pixels, so greys and skin of other hues stay put.
struct HslBand {
    double hue = 0.0;
    double saturation = 0.0;
    double luminance = 0.0;

    bool isIdentity() const { return hue == 0.0 && saturation == 0.0 && luminance == 0.0; }
    bool operator==(const HslBand&) const = default;
};

// Red, orange, yellow, green, aqua, blue, purple, magenta.
constexpr int kHslBands = 8;
// Centre hue of each band in degrees.
double hslBandHue(int band);
const char* hslBandId(int band);

// The colour settings shared by video clips, photos and photo adjustment
// layers. All values are neutral at zero (LUT: no path), so a default
// constructed grade leaves pixels untouched.
//
// Order of operations (see ColorProcessor): white balance, exposure and
// contrast in linear light; highlights / shadows / whites / blacks on
// luminance; saturation and vibrance; lift / gamma / gain on the display
// encoded values; the HSL mixer and the curves (master, then red / green /
// blue) on the display encoded values; finally the LUT (which expects display encoded input, as
// almost every .cube file in the wild does).
struct ColorGrade {
    double exposure = 0.0;    // stops (EV), -5 .. +5
    double contrast = 0.0;    // -100 .. +100
    double highlights = 0.0;  // -100 .. +100
    double shadows = 0.0;     // -100 .. +100
    double whites = 0.0;      // -100 .. +100
    double blacks = 0.0;      // -100 .. +100
    double temperature = 0.0; // -100 (cooler) .. +100 (warmer)
    double tint = 0.0;        // -100 (green) .. +100 (magenta)
    double saturation = 0.0;  // -100 (grey) .. +100
    double vibrance = 0.0;    // -100 .. +100, protects already saturated colours
    ColorWheel lift;
    ColorWheel gamma;
    ColorWheel gain;
    std::array<HslBand, kHslBands> hsl{};
    ToneCurve curveMaster;    // applied to all three channels
    ToneCurve curveRed;
    ToneCurve curveGreen;
    ToneCurve curveBlue;
    QString lutPath;          // .cube file; empty = none
    double lutAmount = 1.0;   // 0 .. 1 mix of the LUT result
    QString look;             // ready-made colour filter (color/Looks.h), added to the settings above
    double lookAmount = 1.0;  // 0 .. 1 strength of the filter

    bool hasLut() const { return !lutPath.isEmpty() && lutAmount > 0.0; }
    bool hasLook() const { return !look.isEmpty() && lookAmount > 0.0; }
    bool hasWheels() const { return !lift.isIdentity() || !gamma.isIdentity() || !gain.isIdentity(); }
    bool hasHsl() const {
        for (const auto& b : hsl)
            if (!b.isIdentity())
                return true;
        return false;
    }
    bool hasCurves() const {
        return !curveMaster.isIdentity() || !curveRed.isIdentity() || !curveGreen.isIdentity() || !curveBlue.isIdentity();
    }
    bool isIdentity() const {
        return exposure == 0.0 && contrast == 0.0 && highlights == 0.0 && shadows == 0.0 && whites == 0.0 &&
               blacks == 0.0 && temperature == 0.0 && tint == 0.0 && saturation == 0.0 && vibrance == 0.0 &&
               !hasWheels() && !hasHsl() && !hasCurves() && !hasLut() && !hasLook();
    }
    bool operator==(const ColorGrade&) const = default;

    // Only the fields that differ from neutral are written.
    QJsonObject toJson() const;
    static ColorGrade fromJson(const QJsonObject& o);
};

} // namespace lumora

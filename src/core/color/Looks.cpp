#include "Looks.h"

#include <QCoreApplication>
#include <cmath>

namespace lumora {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("lumora::looks", text); }

// A colour wheel push towards `hueDeg` (0 = red, 120 = green, 240 = blue).
ColorWheel wheel(double hueDeg, double strength, double master = 0.0) {
    ColorWheel w;
    const double a = hueDeg * M_PI / 180.0;
    w.x = std::cos(a) * strength;
    w.y = std::sin(a) * strength;
    w.master = master;
    return w;
}

ToneCurve curve(std::initializer_list<QPointF> points) { return ToneCurve{std::vector<QPointF>(points)}; }

std::vector<Look> makeLooks() {
    std::vector<Look> out;
    auto add = [&](const char* id, const char* name, auto setup) {
        Look l;
        l.id = QString::fromLatin1(id);
        l.name = tr(name);
        setup(l.grade);
        out.push_back(l);
    };
    add("vivid", QT_TRANSLATE_NOOP("lumora::looks", "Vivid"), [](ColorGrade& g) {
        g.contrast = 15;
        g.saturation = 20;
        g.vibrance = 30;
    });
    add("warm", QT_TRANSLATE_NOOP("lumora::looks", "Warm"), [](ColorGrade& g) {
        g.temperature = 30;
        g.tint = 4;
        g.vibrance = 10;
    });
    add("cool", QT_TRANSLATE_NOOP("lumora::looks", "Cool"), [](ColorGrade& g) {
        g.temperature = -30;
        g.tint = -3;
    });
    add("cinematic", QT_TRANSLATE_NOOP("lumora::looks", "Cinematic"), [](ColorGrade& g) {
        // Teal shadows, warm highlights.
        g.contrast = 20;
        g.saturation = -5;
        g.lift = wheel(190, 0.28);
        g.gain = wheel(30, 0.22);
        g.highlights = -15;
    });
    add("goldenHour", QT_TRANSLATE_NOOP("lumora::looks", "Golden hour"), [](ColorGrade& g) {
        g.temperature = 40;
        g.tint = 8;
        g.gain = wheel(35, 0.15);
        g.highlights = -10;
        g.vibrance = 15;
    });
    add("fresh", QT_TRANSLATE_NOOP("lumora::looks", "Fresh"), [](ColorGrade& g) {
        g.exposure = 0.15;
        g.vibrance = 25;
        g.temperature = -8;
        g.highlights = -15;
        g.shadows = 15;
    });
    add("dramatic", QT_TRANSLATE_NOOP("lumora::looks", "Dramatic"), [](ColorGrade& g) {
        g.contrast = 35;
        g.highlights = -30;
        g.shadows = 20;
        g.saturation = -10;
        g.vibrance = 10;
        g.blacks = -10;
    });
    add("faded", QT_TRANSLATE_NOOP("lumora::looks", "Faded"), [](ColorGrade& g) {
        g.contrast = -15;
        g.saturation = -25;
        g.curveMaster = curve({{0, 0.1}, {0.5, 0.52}, {1, 0.95}});
    });
    add("matte", QT_TRANSLATE_NOOP("lumora::looks", "Matte"), [](ColorGrade& g) {
        g.saturation = -10;
        g.curveMaster = curve({{0, 0.09}, {0.25, 0.27}, {0.75, 0.76}, {1, 0.92}});
    });
    add("vintage", QT_TRANSLATE_NOOP("lumora::looks", "Vintage"), [](ColorGrade& g) {
        g.saturation = -20;
        g.temperature = 15;
        g.gain = wheel(55, 0.12);
        g.lift = wheel(200, 0.1);
        g.curveMaster = curve({{0, 0.08}, {0.5, 0.5}, {1, 0.94}});
    });
    add("pastel", QT_TRANSLATE_NOOP("lumora::looks", "Pastel"), [](ColorGrade& g) {
        g.exposure = 0.2;
        g.contrast = -20;
        g.saturation = -30;
        g.highlights = -10;
        g.lift = wheel(330, 0.08);
    });
    add("night", QT_TRANSLATE_NOOP("lumora::looks", "Night"), [](ColorGrade& g) {
        g.temperature = -40;
        g.exposure = -0.5;
        g.saturation = -20;
        g.lift = wheel(230, 0.15);
    });
    add("sepia", QT_TRANSLATE_NOOP("lumora::looks", "Sepia"), [](ColorGrade& g) {
        g.saturation = -100;
        g.contrast = 10;
        g.gain = wheel(35, 0.25);
        g.gamma = wheel(30, 0.12);
    });
    add("bw", QT_TRANSLATE_NOOP("lumora::looks", "Black and white"), [](ColorGrade& g) {
        g.saturation = -100;
        g.contrast = 20;
    });
    add("noir", QT_TRANSLATE_NOOP("lumora::looks", "Noir"), [](ColorGrade& g) {
        g.saturation = -100;
        g.contrast = 45;
        g.blacks = -20;
        g.curveMaster = curve({{0, 0}, {0.25, 0.18}, {0.75, 0.84}, {1, 1}});
    });
    return out;
}

} // namespace

const std::vector<Look>& looks() {
    // Built on first use, after the translator is installed.
    static const std::vector<Look> all = makeLooks();
    return all;
}

const Look* findLook(const QString& id) {
    for (const auto& l : looks())
        if (l.id == id)
            return &l;
    return nullptr;
}

ColorGrade applyLook(const ColorGrade& base, const Look& look, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    const ColorGrade& l = look.grade;
    ColorGrade g = base;
    auto mix = [amount](double a, double b) { return a + b * amount; };
    // Look values are added to what is there (the look is relative), except
    // the curves and wheels, which it sets.
    g.exposure = mix(base.exposure, l.exposure);
    g.contrast = std::clamp(mix(base.contrast, l.contrast), -100.0, 100.0);
    g.highlights = std::clamp(mix(base.highlights, l.highlights), -100.0, 100.0);
    g.shadows = std::clamp(mix(base.shadows, l.shadows), -100.0, 100.0);
    g.whites = std::clamp(mix(base.whites, l.whites), -100.0, 100.0);
    g.blacks = std::clamp(mix(base.blacks, l.blacks), -100.0, 100.0);
    g.temperature = std::clamp(mix(base.temperature, l.temperature), -100.0, 100.0);
    g.tint = std::clamp(mix(base.tint, l.tint), -100.0, 100.0);
    g.saturation = std::clamp(mix(base.saturation, l.saturation), -100.0, 100.0);
    g.vibrance = std::clamp(mix(base.vibrance, l.vibrance), -100.0, 100.0);
    auto scaleWheel = [amount](const ColorWheel& w) {
        ColorWheel out = w;
        out.x *= amount;
        out.y *= amount;
        out.master *= amount;
        return out;
    };
    if (!l.lift.isIdentity())
        g.lift = scaleWheel(l.lift);
    if (!l.gamma.isIdentity())
        g.gamma = scaleWheel(l.gamma);
    if (!l.gain.isIdentity())
        g.gain = scaleWheel(l.gain);
    if (!l.curveMaster.isIdentity()) {
        ToneCurve c = l.curveMaster;
        for (auto& p : c.points)
            p.setY(p.x() + (p.y() - p.x()) * amount);
        g.curveMaster = c;
    }
    return g;
}

} // namespace lumora

#include "ColorProcessor.h"

#include "color/Looks.h"

#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "color/Lut.h"

namespace lumora {

namespace {

inline float srgbToLinearExact(float v) {
    if (v <= 0.0f)
        return 0.0f;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgbExact(float v) {
    if (v <= 0.0f)
        return 0.0f;
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// The transfer curves are smooth, so dense tables with linear interpolation
// stay within 1e-6 of the exact curve (far below a 16-bit step) at a
// fraction of the cost of std::pow. Values above 1 use the exact formula.
constexpr int kCurveSteps = 16384;

struct CurveTables {
    std::vector<float> decode16; // exact for every 16-bit code value
    std::vector<float> decode;   // 0..1, kCurveSteps + 1 entries
    std::vector<float> encode;   // 0..1, kCurveSteps + 1 entries
    CurveTables() : decode16(65536), decode(kCurveSteps + 2), encode(kCurveSteps + 2) {
        for (int i = 0; i < 65536; ++i)
            decode16[i] = srgbToLinearExact(i / 65535.0f);
        for (int i = 0; i <= kCurveSteps + 1; ++i) {
            decode[i] = srgbToLinearExact(static_cast<float>(i) / kCurveSteps);
            encode[i] = linearToSrgbExact(static_cast<float>(i) / kCurveSteps);
        }
    }
};

const CurveTables& curves() {
    static const CurveTables t;
    return t;
}

inline float lookup(const std::vector<float>& table, float v) {
    const float p = v * kCurveSteps;
    const int i = static_cast<int>(p);
    const float f = p - i;
    return table[i] + (table[i + 1] - table[i]) * f;
}

// x^e for x >= 0. Between 1/256 and 1 a 64k table with linear
// interpolation (error < 1e-7); outside it, or very close to zero where the
// curve can be steep, the exact std::pow.
class PowCurve {
public:
    void init(float e) {
        m_e = e;
        m_identity = e == 1.0f;
        if (m_identity)
            return;
        m_table.resize(kSteps + 2);
        for (int i = 0; i <= kSteps + 1; ++i)
            m_table[i] = std::pow(static_cast<float>(i) / kSteps, e);
    }
    float operator()(float x) const {
        if (m_identity)
            return x;
        if (x <= 0.0f)
            return 0.0f;
        if (x < 1.0f / 256 || x >= 1.0f)
            return std::pow(x, m_e);
        return lookup(x);
    }

private:
    static constexpr int kSteps = 65536;
    float lookup(float x) const {
        const float p = x * kSteps;
        const int i = static_cast<int>(p);
        return m_table[i] + (m_table[i + 1] - m_table[i]) * (p - i);
    }
    float m_e = 1.0f;
    bool m_identity = true;
    std::vector<float> m_table;
};

inline float srgbToLinear(float v) {
    if (v <= 0.0f)
        return 0.0f;
    return v < 1.0f ? lookup(curves().decode, v) : srgbToLinearExact(v);
}
inline float linearToSrgb(float v) {
    if (v <= 0.0f)
        return 0.0f;
    return v < 1.0f ? lookup(curves().encode, v) : linearToSrgbExact(v);
}

inline float luminance(const float rgb[3]) { return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]; }

constexpr float kMiddleGrey = 0.18f;

bool is16Bit(QImage::Format f) {
    return f == QImage::Format_RGBA64 || f == QImage::Format_RGBX64 || f == QImage::Format_RGBA64_Premultiplied;
}
bool is8Bit(QImage::Format f) {
    switch (f) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBX8888:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied: return true;
    default: return false;
    }
}

} // namespace

// Tone curves are sampled into one table per channel (master curve, then
// the channel's own curve) and read with linear interpolation.
constexpr int kToneSteps = 8192;

struct ColorCurves {
    PowCurve contrast; // l^p, scaled by kMiddleGrey^(1-p)
    float contrastScale = 1.0f;
    PowCurve gamma[3];
    std::vector<float> tone[3]; // kToneSteps + 1 entries each
};

namespace {

inline float toneLookup(const std::vector<float>& table, float v) {
    const float p = std::clamp(v, 0.0f, 1.0f) * kToneSteps;
    const int i = std::min(static_cast<int>(p), kToneSteps - 1);
    return table[i] + (table[i + 1] - table[i]) * (p - i);
}

// HSL mixer on display encoded values. Each pixel's hue falls between two
// neighbouring band centres and takes a smooth mix of both bands' settings.
void applyHsl(float rgb[3], const float hue[kHslBands], const float sat[kHslBands], const float lum[kHslBands]) {
    float r = std::clamp(rgb[0], 0.0f, 1.0f), g = std::clamp(rgb[1], 0.0f, 1.0f), b = std::clamp(rgb[2], 0.0f, 1.0f);
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const float chroma = mx - mn;
    if (chroma < 1e-5f)
        return;
    const float l = (mx + mn) / 2;
    const float s = chroma / std::max(1e-6f, 1.0f - std::abs(2 * l - 1));
    float h;
    if (mx == r)
        h = 60.0f * std::fmod((g - b) / chroma + 6.0f, 6.0f);
    else if (mx == g)
        h = 60.0f * ((b - r) / chroma + 2.0f);
    else
        h = 60.0f * ((r - g) / chroma + 4.0f);
    int i = kHslBands - 1;
    for (int k = 0; k + 1 < kHslBands; ++k)
        if (h < hslBandHue(k + 1)) {
            i = k;
            break;
        }
    const int j = (i + 1) % kHslBands;
    const float c0 = static_cast<float>(hslBandHue(i));
    const float c1 = j == 0 ? 360.0f : static_cast<float>(hslBandHue(j));
    const float t = std::clamp((h - c0) / (c1 - c0), 0.0f, 1.0f);
    const float wj = 0.5f - 0.5f * std::cos(t * static_cast<float>(M_PI));
    const float wi = 1.0f - wj;
    const float dh = wi * hue[i] + wj * hue[j];
    const float ds = wi * sat[i] + wj * sat[j];
    const float dl = wi * lum[i] + wj * lum[j];
    // Strength of the effect follows how coloured the pixel is.
    const float strength = std::min(1.0f, s);
    float h2 = std::fmod(h + dh * 30.0f * strength + 360.0f, 360.0f);
    float s2 = std::clamp(s * (1.0f + ds), 0.0f, 1.0f);
    float l2 = dl >= 0 ? l + (1 - l) * dl * 0.5f * strength : l * (1 + dl * 0.5f * strength);
    l2 = std::clamp(l2, 0.0f, 1.0f);
    const float c2 = (1 - std::abs(2 * l2 - 1)) * s2;
    const float hp = h2 / 60.0f;
    const float x = c2 * (1 - std::abs(std::fmod(hp, 2.0f) - 1));
    float r1 = 0, g1 = 0, b1 = 0;
    switch (static_cast<int>(hp) % 6) {
    case 0: r1 = c2, g1 = x; break;
    case 1: r1 = x, g1 = c2; break;
    case 2: g1 = c2, b1 = x; break;
    case 3: g1 = x, b1 = c2; break;
    case 4: r1 = x, b1 = c2; break;
    default: r1 = c2, b1 = x; break;
    }
    const float m = l2 - c2 / 2;
    rgb[0] = r1 + m;
    rgb[1] = g1 + m;
    rgb[2] = b1 + m;
}

} // namespace

ColorProcessor::ColorProcessor(const ColorGrade& input) : m_grade(input) {
    // A colour filter is resolved into ordinary settings first.
    ColorGrade g = input;
    g.look.clear();
    if (const Look* look = input.hasLook() ? findLook(input.look) : nullptr)
        g = applyLook(g, *look, input.lookAmount);
    m_identity = g.isIdentity();
    if (m_identity)
        return;

    // White balance as per-channel gains in linear light, normalised so that
    // overall luminance is unchanged (exposure stays separate).
    const float t = static_cast<float>(g.temperature) / 100.0f;
    const float m = static_cast<float>(g.tint) / 100.0f;
    const float r = std::exp2(0.45f * t + 0.10f * m);
    const float gr = std::exp2(-0.30f * m);
    const float b = std::exp2(-0.45f * t + 0.10f * m);
    const float lum = 0.2126f * r + 0.7152f * gr + 0.0722f * b;
    m_wb[0] = r / lum;
    m_wb[1] = gr / lum;
    m_wb[2] = b / lum;
    m_exposureGain = std::exp2(static_cast<float>(g.exposure));
    m_contrast = g.contrast != 0.0;
    m_contrastPower = std::exp2(static_cast<float>(g.contrast) / 100.0f * 0.75f);

    m_tone = g.highlights != 0.0 || g.shadows != 0.0 || g.whites != 0.0 || g.blacks != 0.0;
    m_highlights = static_cast<float>(g.highlights) / 100.0f;
    m_shadows = static_cast<float>(g.shadows) / 100.0f;
    m_whites = static_cast<float>(g.whites) / 100.0f;
    m_blacks = static_cast<float>(g.blacks) / 100.0f;

    m_sat = g.saturation != 0.0 || g.vibrance != 0.0;
    m_saturation = std::max(0.0f, 1.0f + static_cast<float>(g.saturation) / 100.0f);
    m_vibrance = static_cast<float>(g.vibrance) / 100.0f;

    m_wheels = g.hasWheels();
    double lift[3], gamma[3], gain[3];
    float gammaExp[3];
    g.lift.channelOffsets(lift);
    g.gamma.channelOffsets(gamma);
    g.gain.channelOffsets(gain);
    for (int c = 0; c < 3; ++c) {
        m_lift[c] = static_cast<float>(0.2 * g.lift.master + 0.15 * lift[c]);
        m_gain[c] = static_cast<float>(std::max(0.0, 1.0 + 0.5 * g.gain.master + 0.3 * gain[c]));
        gammaExp[c] = static_cast<float>(std::exp2(-(0.8 * g.gamma.master + 0.5 * gamma[c])));
    }

    if (g.hasLut()) {
        m_lut = luts::get(g.lutPath, &m_lutError);
        m_lutAmount = static_cast<float>(std::clamp(g.lutAmount, 0.0, 1.0));
    }

    m_hsl = g.hasHsl();
    for (int i = 0; i < kHslBands; ++i) {
        m_hslHue[i] = static_cast<float>(g.hsl[i].hue / 100.0);
        m_hslSat[i] = static_cast<float>(g.hsl[i].saturation / 100.0);
        m_hslLum[i] = static_cast<float>(g.hsl[i].luminance / 100.0);
    }

    m_curves = std::make_unique<ColorCurves>();
    m_toneCurves = g.hasCurves();
    if (m_toneCurves) {
        std::vector<float> master;
        g.curveMaster.sample(master, kToneSteps + 1);
        const ToneCurve* channel[3] = {&g.curveRed, &g.curveGreen, &g.curveBlue};
        for (int c = 0; c < 3; ++c) {
            auto& t = m_curves->tone[c];
            t.resize(kToneSteps + 1);
            for (int i = 0; i <= kToneSteps; ++i)
                t[i] = channel[c]->isIdentity() ? master[i] : static_cast<float>(channel[c]->eval(master[i]));
        }
    }
    if (m_contrast) {
        m_curves->contrast.init(m_contrastPower);
        m_curves->contrastScale = std::pow(kMiddleGrey, 1.0f - m_contrastPower);
    }
    for (int c = 0; c < 3; ++c)
        m_curves->gamma[c].init(gammaExp[c]);
}

ColorProcessor::~ColorProcessor() = default;

void ColorProcessor::process(float rgb[3]) const {
    if (m_identity)
        return;
    float lin[3];
    for (int c = 0; c < 3; ++c)
        lin[c] = srgbToLinear(rgb[c]) * m_wb[c] * m_exposureGain;

    if (m_contrast) {
        // Contrast on luminance in log space around middle grey; scaling RGB
        // by the luminance ratio keeps hue and saturation stable.
        const float l = luminance(lin);
        if (l > 1e-6f) {
            const float k = m_curves->contrastScale * m_curves->contrast(l) / l;
            for (float& v : lin)
                v *= k;
        }
    }

    if (m_tone) {
        // Tone ranges work on perceptual luminance: shadows and highlights are
        // smooth bumps centred at 1/4 and 3/4, blacks and whites move the ends.
        const float l = luminance(lin);
        if (l > 1e-6f) {
            const float e = linearToSrgb(l);
            const float x = std::clamp(e, 0.0f, 1.0f);
            const float wSh = 9.48f * x * (1 - x) * (1 - x) * (1 - x);
            const float wHi = 9.48f * x * x * x * (1 - x);
            const float wBl = (1 - x) * (1 - x) * (1 - x);
            const float wWh = x * x * x;
            const float e2 = e + 0.3f * (m_shadows * wSh + m_highlights * wHi) + 0.2f * (m_blacks * wBl + m_whites * wWh);
            const float k = srgbToLinear(std::max(e2, 0.0f)) / l;
            for (float& v : lin)
                v *= k;
        }
    }

    if (m_sat) {
        const float l = luminance(lin);
        float s = m_saturation;
        if (m_vibrance != 0.0f) {
            const float mx = std::max({lin[0], lin[1], lin[2]});
            const float mn = std::min({lin[0], lin[1], lin[2]});
            const float current = mx > 1e-6f ? (mx - mn) / mx : 0.0f;
            s *= std::max(0.0f, 1.0f + m_vibrance * (1.0f - current));
        }
        for (float& v : lin)
            v = std::max(0.0f, l + (v - l) * s);
    }

    for (int c = 0; c < 3; ++c)
        rgb[c] = linearToSrgb(lin[c]);

    if (m_wheels) {
        for (int c = 0; c < 3; ++c) {
            const float v = std::clamp(rgb[c], 0.0f, 1.0f);
            const float o = v * m_gain[c] + m_lift[c] * (1.0f - v);
            rgb[c] = m_curves->gamma[c](o);
        }
    }

    if (m_hsl)
        applyHsl(rgb, m_hslHue, m_hslSat, m_hslLum);

    if (m_toneCurves)
        for (int c = 0; c < 3; ++c)
            rgb[c] = toneLookup(m_curves->tone[c], rgb[c]);

    if (m_lut) {
        float in[3], out[3];
        for (int c = 0; c < 3; ++c)
            in[c] = out[c] = std::clamp(rgb[c], 0.0f, 1.0f);
        m_lut->apply(out);
        for (int c = 0; c < 3; ++c)
            rgb[c] = in[c] + (out[c] - in[c]) * m_lutAmount;
    }

    for (int c = 0; c < 3; ++c)
        rgb[c] = std::clamp(rgb[c], 0.0f, 1.0f);
}

Lut ColorProcessor::bake(int size, const QString& title) const {
    return Lut::sample(size, [this](float rgb[3]) { process(rgb); }, title);
}

void ColorProcessor::processRow8(uchar* row, int width, QImage::Format format) const {
    // QRgb based formats are 0xAARRGGBB words; the *8888 formats are bytes
    // in R, G, B, A order.
    const bool word = format == QImage::Format_RGB32 || format == QImage::Format_ARGB32 ||
                      format == QImage::Format_ARGB32_Premultiplied;
    const bool premul = format == QImage::Format_ARGB32_Premultiplied || format == QImage::Format_RGBA8888_Premultiplied;
    for (int x = 0; x < width; ++x) {
        int r, g, b, a;
        if (word) {
            const QRgb p = reinterpret_cast<const QRgb*>(row)[x];
            r = qRed(p), g = qGreen(p), b = qBlue(p), a = qAlpha(p);
        } else {
            const uchar* p = row + x * 4;
            r = p[0], g = p[1], b = p[2], a = p[3];
        }
        if (premul && a == 0)
            continue;
        float rgb[3] = {r / 255.0f, g / 255.0f, b / 255.0f};
        if (premul && a < 255)
            for (float& v : rgb)
                v = std::min(1.0f, v * 255.0f / a);
        process(rgb);
        const float k = premul ? a / 255.0f : 1.0f;
        auto enc = [k](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * k * 255.0f)); };
        if (word) {
            reinterpret_cast<QRgb*>(row)[x] = qRgba(enc(rgb[0]), enc(rgb[1]), enc(rgb[2]), a);
        } else {
            uchar* p = row + x * 4;
            p[0] = static_cast<uchar>(enc(rgb[0]));
            p[1] = static_cast<uchar>(enc(rgb[1]));
            p[2] = static_cast<uchar>(enc(rgb[2]));
        }
    }
}

void ColorProcessor::processRow16(quint16* row, int width, bool premul) const {
    for (int x = 0; x < width; ++x) {
        quint16* p = row + x * 4;
        const quint16 a = p[3];
        if (premul && a == 0)
            continue;
        float rgb[3] = {p[0] / 65535.0f, p[1] / 65535.0f, p[2] / 65535.0f};
        if (premul && a < 65535)
            for (float& v : rgb)
                v = std::min(1.0f, v * 65535.0f / a);
        process(rgb);
        const float k = premul ? a / 65535.0f : 1.0f;
        for (int c = 0; c < 3; ++c)
            p[c] = static_cast<quint16>(std::lround(std::clamp(rgb[c], 0.0f, 1.0f) * k * 65535.0f));
    }
}

void ColorProcessor::apply(QImage& image) const {
    if (m_identity || image.isNull())
        return;
    const QImage::Format original = image.format();
    if (!is8Bit(original) && !is16Bit(original))
        image = image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_RGBA64 : QImage::Format_RGBX64);
    const QImage::Format f = image.format();
    std::vector<int> rows(image.height());
    std::iota(rows.begin(), rows.end(), 0);
    const int width = image.width();
    image.detach();
    QtConcurrent::blockingMap(rows, [&](int y) {
        if (is16Bit(f))
            processRow16(reinterpret_cast<quint16*>(image.scanLine(y)), width, f == QImage::Format_RGBA64_Premultiplied);
        else
            processRow8(image.scanLine(y), width, f);
    });
    if (image.format() != original)
        image = image.convertToFormat(original);
}

QImage ColorProcessor::applied(const QImage& image) const {
    QImage copy = image;
    apply(copy);
    return copy;
}

} // namespace lumora

#pragma once

#include <QImage>
#include <QString>
#include <memory>

#include "color/ColorGrade.h"

namespace lumora {

class Lut;

// Applies a ColorGrade to pixels. Input and output are display encoded RGB
// (sRGB / Rec.709 transfer) in 0..1; every step is computed in 32-bit float,
// the exposure, white balance and contrast steps in linear light. The whole
// chain runs for every pixel, in the preview as in the export (power curves
// come from dense tables that stay below 1e-6 of the exact value).
class ColorProcessor {
public:
    explicit ColorProcessor(const ColorGrade& grade);
    ~ColorProcessor();
    ColorProcessor(const ColorProcessor&) = delete;
    ColorProcessor& operator=(const ColorProcessor&) = delete;

    const ColorGrade& grade() const { return m_grade; }
    bool isIdentity() const { return m_identity; }
    // Set when the grade names a LUT that could not be loaded (the LUT step
    // is then skipped).
    const QString& lutError() const { return m_lutError; }

    // One pixel, exact.
    void process(float rgb[3]) const;
    // In place, any QImage format (premultiplied alpha is handled); rows are
    // processed in parallel.
    void apply(QImage& image) const;
    QImage applied(const QImage& image) const;

    // Samples the exact chain into a .cube-style 3D table.
    Lut bake(int size, const QString& title = {}) const;

private:
    void processRow8(uchar* row, int width, QImage::Format format) const;
    void processRow16(quint16* row, int width, bool premultiplied) const;

    ColorGrade m_grade;
    bool m_identity = true;
    QString m_lutError;
    std::shared_ptr<const Lut> m_lut;  // the grade's LUT file

    // Precomputed parameters.
    float m_wb[3] = {1, 1, 1};
    float m_exposureGain = 1;
    float m_contrastPower = 1;
    bool m_contrast = false;
    bool m_tone = false;
    float m_highlights = 0, m_shadows = 0, m_whites = 0, m_blacks = 0;
    bool m_sat = false;
    float m_saturation = 1, m_vibrance = 0;
    bool m_wheels = false;
    float m_lift[3] = {0, 0, 0}, m_gain[3] = {1, 1, 1};
    bool m_hsl = false;
    float m_hslHue[kHslBands] = {}, m_hslSat[kHslBands] = {}, m_hslLum[kHslBands] = {};
    bool m_toneCurves = false;
    std::unique_ptr<struct ColorCurves> m_curves; // contrast and gamma power curves, tone curve tables
    float m_lutAmount = 0;
};

} // namespace lumora

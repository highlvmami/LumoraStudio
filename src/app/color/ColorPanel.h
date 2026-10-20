#pragma once

#include <QWidget>

#include "color/ColorGrade.h"

class QButtonGroup;
class QComboBox;
class QLabel;
class QToolButton;
class QPushButton;

namespace lumora {

class SliderRow;
class ColorWheelWidget;
class CurvesWidget;

// Editor for a ColorGrade: light (exposure, contrast, highlights, shadows,
// whites, blacks), white balance, saturation / vibrance, lift / gamma / gain
// wheels, tone curves, an HSL mixer and a .cube LUT. Used for video clips, photos and photo adjustment
// layers. The panel does not own the grade: it reports each edit and the
// owner pushes the undo step and calls setGrade() back.
class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget* parent = nullptr);

    void setGrade(const ColorGrade& g);
    ColorGrade grade() const { return m_grade; }
    // Shown above the controls, e.g. the clip or layer being edited.
    void setHeader(const QString& text);
    void setEditable(bool on);

    // Most recently used LUT files (QSettings).
    static QStringList recentLuts();
    static void addRecentLut(const QString& path);

signals:
    // `field` identifies the control, so consecutive drags of one control
    // can merge into a single undo step.
    void gradeEdited(const lumora::ColorGrade& g, const QString& undoText, const QString& field);

private:
    void edit(const QString& text, const QString& field, const std::function<void(ColorGrade&)>& change);
    void refreshLutCombo();
    void showHslBand(int band);
    void chooseLut();
    void saveLookAsLut();

    ColorGrade m_grade;
    bool m_syncing = false;
    QLabel* m_header;
    QWidget* m_body;
    SliderRow* m_exposure;
    SliderRow* m_contrast;
    SliderRow* m_highlights;
    SliderRow* m_shadows;
    SliderRow* m_whites;
    SliderRow* m_blacks;
    SliderRow* m_temperature;
    SliderRow* m_tint;
    SliderRow* m_saturation;
    SliderRow* m_vibrance;
    ColorWheelWidget* m_lift;
    ColorWheelWidget* m_gamma;
    ColorWheelWidget* m_gain;
    CurvesWidget* m_curves;
    QButtonGroup* m_hslBands;
    int m_hslBand = 0;
    SliderRow* m_hslHue;
    SliderRow* m_hslSat;
    SliderRow* m_hslLum;
    QComboBox* m_lut;
    QLabel* m_lutStatus;
    SliderRow* m_lutAmount;
    QPushButton* m_saveLut;
    QToolButton* m_reset;
};

} // namespace lumora

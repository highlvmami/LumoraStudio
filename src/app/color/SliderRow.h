#pragma once

#include <QWidget>

class QSlider;
class QDoubleSpinBox;

namespace lumora {

// Label + slider + spin box with double-click reset.
class SliderRow : public QWidget {
    Q_OBJECT
public:
    SliderRow(const QString& label, double min, double max, double step, int decimals, QWidget* parent = nullptr);
    double value() const { return m_value; }
    void setValue(double v);
    // Value restored by a double-click (0 unless set).
    void setDefault(double v) { m_default = v; }

signals:
    void valueChanged(double v);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QSlider* m_slider;
    QDoubleSpinBox* m_spin;
    double m_step;
    double m_value = 0;
    double m_default = 0;
};

} // namespace lumora

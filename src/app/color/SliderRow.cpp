#include "SliderRow.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <cmath>

namespace lumora {

SliderRow::SliderRow(const QString& label, double min, double max, double step, int decimals, QWidget* parent)
    : QWidget(parent), m_step(step) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 2, 0, 2);
    auto* l = new QLabel(label);
    m_spin = new QDoubleSpinBox;
    m_spin->setRange(min, max);
    m_spin->setDecimals(decimals);
    m_spin->setSingleStep(step);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setFixedWidth(64);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(static_cast<int>(std::lround(min / step)), static_cast<int>(std::lround(max / step)));
    m_slider->installEventFilter(this);
    m_slider->setToolTip(tr("Double-click to reset"));
    grid->addWidget(l, 0, 0);
    grid->addWidget(m_spin, 0, 1);
    grid->addWidget(m_slider, 1, 0, 1, 2);
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_value = v * m_step;
        const QSignalBlocker b(m_spin);
        m_spin->setValue(m_value);
        emit valueChanged(m_value);
    });
    connect(m_spin, &QDoubleSpinBox::editingFinished, this, [this] {
        m_value = m_spin->value();
        const QSignalBlocker b(m_slider);
        m_slider->setValue(static_cast<int>(std::lround(m_value / m_step)));
        emit valueChanged(m_value);
    });
}

void SliderRow::setValue(double v) {
    m_value = v;
    const QSignalBlocker b1(m_slider), b2(m_spin);
    m_slider->setValue(static_cast<int>(std::lround(v / m_step)));
    m_spin->setValue(v);
}

bool SliderRow::eventFilter(QObject* o, QEvent* e) {
    if (o == m_slider && e->type() == QEvent::MouseButtonDblClick) {
        setValue(m_default);
        emit valueChanged(m_value);
        return true;
    }
    return QWidget::eventFilter(o, e);
}

} // namespace lumora

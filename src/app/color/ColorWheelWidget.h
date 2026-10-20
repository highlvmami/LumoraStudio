#pragma once

#include <QImage>
#include <QWidget>

#include "color/ColorGrade.h"

class QLabel;
class QSlider;

namespace lumora {

// The round hue disc of one colour wheel. Dragging the puck pushes the
// picture towards that hue; the further from the centre, the stronger.
// Double-click resets. Hold Shift for fine control.
class ColorDisc : public QWidget {
    Q_OBJECT
public:
    explicit ColorDisc(QWidget* parent = nullptr);

    QPointF value() const { return m_value; }
    void setValue(QPointF v);

    QSize sizeHint() const override { return {120, 120}; }
    QSize minimumSizeHint() const override { return {72, 72}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void valueChanged(QPointF v);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QRectF discRect() const;
    void rebuild();

    QPointF m_value;
    QImage m_disc;
    QPointF m_pressPos;
    QPointF m_pressValue;
};

// Title + disc + master slider: one of Lift / Gamma / Gain.
class ColorWheelWidget : public QWidget {
    Q_OBJECT
public:
    ColorWheelWidget(const QString& title, QWidget* parent = nullptr);

    ColorWheel value() const { return m_value; }
    void setValue(const ColorWheel& w);

signals:
    void valueChanged(const lumora::ColorWheel& w);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    ColorDisc* m_disc;
    QSlider* m_master;
    ColorWheel m_value;
    void updateReadout();
};

} // namespace lumora

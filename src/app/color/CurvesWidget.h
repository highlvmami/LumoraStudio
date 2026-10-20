#pragma once

#include <QWidget>
#include <array>

#include "color/ColorGrade.h"

class QButtonGroup;

namespace lumora {

class CurvePlot;

// Tone curves: master (all channels) and red / green / blue. Click on the
// curve to add a point, drag to move it, double-click or right-click a point
// to remove it, double-click the empty plot to reset the curve.
class CurvesWidget : public QWidget {
    Q_OBJECT
public:
    enum Channel { Master, Red, Green, Blue };

    explicit CurvesWidget(QWidget* parent = nullptr);
    void setCurves(const ToneCurve& master, const ToneCurve& red, const ToneCurve& green, const ToneCurve& blue);

signals:
    void curveEdited(int channel, const lumora::ToneCurve& curve);

private:
    std::array<ToneCurve, 4> m_curves;
    int m_channel = Master;
    CurvePlot* m_plot;
    QButtonGroup* m_buttons;
};

class CurvePlot : public QWidget {
    Q_OBJECT
public:
    explicit CurvePlot(QWidget* parent = nullptr);
    void setCurve(const ToneCurve& c, QColor colour, const std::array<ToneCurve, 4>* others, int channel);
    QSize sizeHint() const override { return {220, 220}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void edited(const lumora::ToneCurve& c);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QRectF plotRect() const;
    QPointF toWidget(QPointF v) const;
    QPointF toValue(QPointF w) const;
    int pointAt(QPointF pos) const;
    std::vector<QPointF> editablePoints() const;
    void removePoint(int i);

    ToneCurve m_curve;
    QColor m_colour;
    const std::array<ToneCurve, 4>* m_others = nullptr;
    int m_channel = 0;
    int m_drag = -1;
    int m_justAdded = -1; // a double-click on empty space adds, then resets
};

} // namespace lumora

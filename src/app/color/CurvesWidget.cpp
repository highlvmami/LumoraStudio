#include "CurvesWidget.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "Theme.h"

namespace lumora {

namespace {
QColor channelColour(int channel) {
    switch (channel) {
    case CurvesWidget::Red: return QColor(235, 87, 87);
    case CurvesWidget::Green: return QColor(80, 200, 120);
    case CurvesWidget::Blue: return QColor(86, 140, 245);
    default: return themeColors().text;
    }
}
constexpr double kGrab = 8.0; // pixels
} // namespace

// ------------------------------------------------------------- CurvesWidget

CurvesWidget::CurvesWidget(QWidget* parent) : QWidget(parent) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    m_buttons = new QButtonGroup(this);
    const QString names[4] = {tr("RGB"), tr("Red"), tr("Green"), tr("Blue")};
    const QString tips[4] = {tr("Master curve: brightness of all channels"), tr("Red channel"), tr("Green channel"),
                             tr("Blue channel")};
    for (int i = 0; i < 4; ++i) {
        auto* b = new QToolButton;
        b->setText(names[i]);
        b->setToolTip(tips[i]);
        b->setCheckable(true);
        b->setAutoRaise(true);
        if (i != Master)
            b->setStyleSheet(QStringLiteral("QToolButton:checked { color: %1; }").arg(channelColour(i).name()));
        m_buttons->addButton(b, i);
        row->addWidget(b);
    }
    row->addStretch();
    m_buttons->button(Master)->setChecked(true);
    l->addLayout(row);
    m_plot = new CurvePlot;
    m_plot->setToolTip(tr("Click to add a point and drag it. Double-click or right-click a point to remove it; "
                          "double-click elsewhere to reset the curve."));
    l->addWidget(m_plot);

    connect(m_buttons, &QButtonGroup::idClicked, this, [this](int id) {
        m_channel = id;
        m_plot->setCurve(m_curves[id], channelColour(id), &m_curves, id);
    });
    connect(m_plot, &CurvePlot::edited, this, [this](const ToneCurve& c) {
        m_curves[m_channel] = c;
        emit curveEdited(m_channel, c);
    });
    m_plot->setCurve({}, channelColour(Master), &m_curves, Master);
}

void CurvesWidget::setCurves(const ToneCurve& master, const ToneCurve& red, const ToneCurve& green,
                             const ToneCurve& blue) {
    m_curves = {master, red, green, blue};
    m_plot->setCurve(m_curves[m_channel], channelColour(m_channel), &m_curves, m_channel);
    // Mark the channels that are not straight.
    for (int i = 0; i < 4; ++i) {
        auto* b = m_buttons->button(i);
        QFont f = b->font();
        f.setUnderline(!m_curves[i].isIdentity());
        b->setFont(f);
    }
}

// ---------------------------------------------------------------- CurvePlot

CurvePlot::CurvePlot(QWidget* parent) : QWidget(parent) {
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setMinimumSize(140, 140);
    setMouseTracking(true);
}

void CurvePlot::setCurve(const ToneCurve& c, QColor colour, const std::array<ToneCurve, 4>* others, int channel) {
    if (m_drag >= 0 && c == m_curve)
        return;
    m_curve = c;
    m_colour = colour;
    m_others = others;
    m_channel = channel;
    update();
}

QRectF CurvePlot::plotRect() const {
    const double s = std::min(width(), height()) - 12.0;
    return QRectF((width() - s) / 2, (height() - s) / 2, s, s);
}

QPointF CurvePlot::toWidget(QPointF v) const {
    const QRectF r = plotRect();
    return {r.left() + v.x() * r.width(), r.bottom() - v.y() * r.height()};
}

QPointF CurvePlot::toValue(QPointF w) const {
    const QRectF r = plotRect();
    return {std::clamp((w.x() - r.left()) / r.width(), 0.0, 1.0), std::clamp((r.bottom() - w.y()) / r.height(), 0.0, 1.0)};
}

std::vector<QPointF> CurvePlot::editablePoints() const {
    if (m_curve.points.size() >= 2)
        return m_curve.points;
    return {{0, 0}, {1, 1}};
}

int CurvePlot::pointAt(QPointF pos) const {
    const auto pts = editablePoints();
    int best = -1;
    double bestD = kGrab;
    for (int i = 0; i < int(pts.size()); ++i) {
        const double d = QLineF(toWidget(pts[i]), pos).length();
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void CurvePlot::removePoint(int i) {
    auto pts = editablePoints();
    if (i <= 0 || i >= int(pts.size()) - 1)
        return; // the end points stay
    pts.erase(pts.begin() + i);
    m_curve.points = pts;
    update();
    emit edited(m_curve);
}

void CurvePlot::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& t = themeColors();
    const QRectF r = plotRect();
    p.fillRect(r, t.viewer);
    p.setPen(QPen(t.border, 1));
    for (int i = 1; i < 4; ++i) {
        const double x = r.left() + r.width() * i / 4, y = r.top() + r.height() * i / 4;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    p.drawRect(r);
    p.setPen(QPen(t.textFaint, 1, Qt::DashLine));
    p.drawLine(r.bottomLeft(), r.topRight());

    auto pathOf = [&](const ToneCurve& c) {
        QPainterPath path;
        const int n = std::max(32, int(r.width()));
        for (int i = 0; i <= n; ++i) {
            const double x = double(i) / n;
            const QPointF w = toWidget({x, c.eval(x)});
            if (i == 0)
                path.moveTo(w);
            else
                path.lineTo(w);
        }
        return path;
    };
    // The other channels' curves, faint, when they are not straight.
    if (m_others)
        for (int i = 0; i < 4; ++i) {
            if (i == m_channel || (*m_others)[i].isIdentity())
                continue;
            QColor c = i == 0 ? t.textDim : QColor(i == 1 ? 0xeb5757 : i == 2 ? 0x50c878 : 0x568cf5);
            c.setAlpha(90);
            p.setPen(QPen(c, 1.2));
            p.drawPath(pathOf((*m_others)[i]));
        }
    p.setPen(QPen(m_colour, 2));
    p.drawPath(pathOf(m_curve));
    p.setPen(QPen(m_colour, 1.5));
    p.setBrush(t.panel);
    for (const QPointF& v : editablePoints())
        p.drawEllipse(toWidget(v), 4, 4);
}

void CurvePlot::mousePressEvent(QMouseEvent* e) {
    if (!isEnabled())
        return;
    const int hit = pointAt(e->position());
    if (e->button() == Qt::RightButton) {
        if (hit >= 0)
            removePoint(hit);
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    auto pts = editablePoints();
    if (hit >= 0) {
        m_drag = hit;
        m_curve.points = pts;
        if (hit != m_justAdded)
            m_justAdded = -1;
        return;
    }
    const QPointF v = toValue(e->position());
    // A new point lands on the curve where it was clicked (x only), so a
    // click alone does not change the picture.
    QPointF nv(v.x(), m_curve.eval(v.x()));
    if (std::abs(toWidget(nv).y() - e->position().y()) > 3 * kGrab)
        nv.setY(v.y());
    auto it = std::lower_bound(pts.begin(), pts.end(), nv, [](QPointF a, QPointF b) { return a.x() < b.x(); });
    if (it != pts.end() && std::abs(it->x() - nv.x()) < 0.01)
        return;
    m_drag = m_justAdded = int(it - pts.begin());
    pts.insert(it, nv);
    m_curve.points = pts;
    update();
    emit edited(m_curve);
}

void CurvePlot::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag < 0) {
        setCursor(pointAt(e->position()) >= 0 ? Qt::SizeAllCursor : Qt::CrossCursor);
        return;
    }
    auto& pts = m_curve.points;
    QPointF v = toValue(e->position());
    const int last = int(pts.size()) - 1;
    if (m_drag == 0)
        v.setX(0);
    else if (m_drag == last)
        v.setX(1);
    else
        v.setX(std::clamp(v.x(), pts[m_drag - 1].x() + 0.01, pts[m_drag + 1].x() - 0.01));
    if (v == pts[m_drag])
        return;
    pts[m_drag] = v;
    m_justAdded = -1;
    update();
    emit edited(m_curve);
}

void CurvePlot::mouseReleaseEvent(QMouseEvent*) {
    m_drag = -1;
}

void CurvePlot::mouseDoubleClickEvent(QMouseEvent* e) {
    const int hit = pointAt(e->position());
    const bool onEmpty = hit < 0 || hit == m_justAdded;
    m_justAdded = -1;
    if (!onEmpty && hit > 0 && hit < int(editablePoints().size()) - 1) {
        removePoint(hit);
        return;
    }
    if (onEmpty && !m_curve.isIdentity()) {
        m_curve = {};
        update();
        emit edited(m_curve);
    }
}

} // namespace lumora

#include "ColorWheelWidget.h"

#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace lumora {

// ---------------------------------------------------------------- ColorDisc

ColorDisc::ColorDisc(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Drag towards a colour; double-click to reset. Hold Shift for fine control."));
}

void ColorDisc::setValue(QPointF v) {
    if (v == m_value)
        return;
    m_value = v;
    update();
}

QRectF ColorDisc::discRect() const {
    const double s = std::min(width(), height()) - 8;
    return QRectF((width() - s) / 2, (height() - s) / 2, s, s);
}

void ColorDisc::resizeEvent(QResizeEvent*) { rebuild(); }

void ColorDisc::rebuild() {
    // The disc shows what the wheel does: the same channel offsets as
    // ColorWheel::channelOffsets, muted so it reads as a control and not as
    // a colour picker.
    const QRectF r = discRect();
    const qreal dpr = devicePixelRatioF();
    const int s = std::max(1, static_cast<int>(std::lround(r.width() * dpr)));
    m_disc = QImage(s, s, QImage::Format_ARGB32_Premultiplied);
    m_disc.fill(Qt::transparent);
    const QColor base = palette().color(QPalette::Button);
    const double c = (s - 1) / 2.0;
    for (int y = 0; y < s; ++y) {
        auto* line = reinterpret_cast<QRgb*>(m_disc.scanLine(y));
        for (int x = 0; x < s; ++x) {
            const double dx = (x - c) / c, dy = (c - y) / c;
            const double d = std::hypot(dx, dy);
            if (d > 1.0)
                continue;
            ColorWheel w{dx, dy, 0};
            double off[3];
            w.channelOffsets(off);
            const double mid = 0.5;
            const int rgb[3] = {
                std::clamp(static_cast<int>(255 * (mid + 0.42 * off[0])), 0, 255),
                std::clamp(static_cast<int>(255 * (mid + 0.42 * off[1])), 0, 255),
                std::clamp(static_cast<int>(255 * (mid + 0.42 * off[2])), 0, 255),
            };
            // Blend into the button colour towards the centre and anti-alias
            // the rim.
            const double k = std::min(1.0, d * 1.2);
            const int a = static_cast<int>(255 * std::clamp((1.0 - d) * c, 0.0, 1.0));
            const int r = static_cast<int>(base.red() + (rgb[0] - base.red()) * k);
            const int g = static_cast<int>(base.green() + (rgb[1] - base.green()) * k);
            const int b = static_cast<int>(base.blue() + (rgb[2] - base.blue()) * k);
            line[x] = qPremultiply(qRgba(r, g, b, a));
        }
    }
    m_disc.setDevicePixelRatio(dpr);
}

void ColorDisc::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = discRect();
    if (m_disc.isNull())
        rebuild();
    p.drawImage(r.topLeft(), m_disc);
    const QColor line = palette().color(QPalette::WindowText);
    QColor faint = line;
    faint.setAlpha(60);
    p.setPen(QPen(faint, 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(r);
    p.drawLine(QPointF(r.center().x() - 5, r.center().y()), QPointF(r.center().x() + 5, r.center().y()));
    p.drawLine(QPointF(r.center().x(), r.center().y() - 5), QPointF(r.center().x(), r.center().y() + 5));

    const QPointF puck(r.center().x() + m_value.x() * r.width() / 2, r.center().y() - m_value.y() * r.height() / 2);
    if (!m_value.isNull()) {
        QColor accent = palette().color(QPalette::Highlight);
        p.setPen(QPen(accent, 1.5));
        p.drawLine(r.center(), puck);
    }
    p.setPen(QPen(QColor(0, 0, 0, 160), 3));
    p.drawEllipse(puck, 5.5, 5.5);
    p.setPen(QPen(Qt::white, 1.5));
    p.drawEllipse(puck, 5.5, 5.5);
}

void ColorDisc::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    m_pressPos = e->position();
    m_pressValue = m_value;
    // A click away from the puck jumps there (unless fine control is held).
    if (!(e->modifiers() & Qt::ShiftModifier)) {
        const QRectF r = discRect();
        const QPointF v((e->position().x() - r.center().x()) / (r.width() / 2),
                        (r.center().y() - e->position().y()) / (r.height() / 2));
        const QPointF puck(r.center().x() + m_value.x() * r.width() / 2, r.center().y() - m_value.y() * r.height() / 2);
        if (QLineF(puck, e->position()).length() > 8) {
            const double d = std::hypot(v.x(), v.y());
            m_value = d > 1 ? v / d : v;
            m_pressValue = m_value;
            update();
            emit valueChanged(m_value);
        }
    }
}

void ColorDisc::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton))
        return;
    const QRectF r = discRect();
    const double fine = (e->modifiers() & Qt::ShiftModifier) ? 0.15 : 1.0;
    QPointF v = m_pressValue + QPointF((e->position().x() - m_pressPos.x()) / (r.width() / 2),
                                       (m_pressPos.y() - e->position().y()) / (r.height() / 2)) *
                                   fine;
    const double d = std::hypot(v.x(), v.y());
    if (d > 1)
        v /= d;
    if (v == m_value)
        return;
    m_value = v;
    update();
    emit valueChanged(m_value);
}

void ColorDisc::mouseDoubleClickEvent(QMouseEvent*) {
    m_value = {};
    update();
    emit valueChanged(m_value);
}

// --------------------------------------------------------- ColorWheelWidget

ColorWheelWidget::ColorWheelWidget(const QString& title, QWidget* parent) : QWidget(parent) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    auto* t = new QLabel(title);
    t->setAlignment(Qt::AlignCenter);
    QFont f = t->font();
    f.setBold(true);
    t->setFont(f);
    m_disc = new ColorDisc;
    m_master = new QSlider(Qt::Horizontal);
    m_master->setRange(-100, 100);
    m_master->setToolTip(tr("Brightness of this range; double-click to reset"));
    m_master->installEventFilter(this);
    l->addWidget(t);
    l->addWidget(m_disc, 1);
    l->addWidget(m_master);
    connect(m_disc, &ColorDisc::valueChanged, this, [this](QPointF v) {
        m_value.x = v.x();
        m_value.y = v.y();
        updateReadout();
        emit valueChanged(m_value);
    });
    connect(m_master, &QSlider::valueChanged, this, [this](int v) {
        m_value.master = v / 100.0;
        updateReadout();
        emit valueChanged(m_value);
    });
    updateReadout();
}

void ColorWheelWidget::setValue(const ColorWheel& w) {
    m_value = w;
    m_disc->setValue(QPointF(w.x, w.y));
    const QSignalBlocker b(m_master);
    m_master->setValue(static_cast<int>(std::lround(w.master * 100)));
    updateReadout();
}

void ColorWheelWidget::updateReadout() {
    double off[3];
    m_value.channelOffsets(off);
    auto fmt = [](double v) { return QString::number(v, 'f', 2); };
    setToolTip(tr("R %1 · G %2 · B %3 · brightness %4")
                   .arg(fmt(off[0]), fmt(off[1]), fmt(off[2]))
                   .arg(QString::number(m_value.master * 100, 'f', 0)));
}

bool ColorWheelWidget::eventFilter(QObject* o, QEvent* e) {
    if (o == m_master && e->type() == QEvent::MouseButtonDblClick) {
        m_master->setValue(0);
        return true;
    }
    return QWidget::eventFilter(o, e);
}

} // namespace lumora

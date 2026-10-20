#include "ScopesWidget.h"

#include "Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace lumora {

namespace {
// Scopes are read against a near-black field in every theme, like the
// picture itself.
QColor field() { return themeColors().viewer; }
const QColor kGrid(255, 255, 255, 38);
const QColor kLabel(255, 255, 255, 120);
} // namespace

// ------------------------------------------------------------- ScopesWidget

ScopesWidget::ScopesWidget(QWidget* parent, scopes::Kind initial, const QString& settingsKey)
    : QWidget(parent), m_settingsKey(settingsKey) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(4);
    auto* top = new QHBoxLayout;
    m_kind = new QComboBox;
    m_kind->addItem(tr("Waveform"), static_cast<int>(scopes::Kind::Waveform));
    m_kind->addItem(tr("RGB parade"), static_cast<int>(scopes::Kind::Parade));
    m_kind->addItem(tr("Vectorscope"), static_cast<int>(scopes::Kind::Vectorscope));
    m_kind->addItem(tr("Histogram"), static_cast<int>(scopes::Kind::Histogram));
    top->addWidget(m_kind);
    top->addStretch();
    l->addLayout(top);
    m_view = new ScopeView;
    l->addWidget(m_view, 1);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(66);
    connect(m_timer, &QTimer::timeout, this, &ScopesWidget::recompute);
    connect(m_view, &ScopeView::resized, this, [this] {
        m_dirty = true;
        m_timer->start();
    });
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(m_settingsKey, m_kind->currentData().toInt());
        m_dirty = true;
        recompute();
    });
    const int saved = QSettings().value(m_settingsKey, static_cast<int>(initial)).toInt();
    setKind(static_cast<scopes::Kind>(saved));
}

void ScopesWidget::setKind(scopes::Kind kind) {
    const int i = m_kind->findData(static_cast<int>(kind));
    if (i >= 0)
        m_kind->setCurrentIndex(i);
}

void ScopesWidget::setImage(const QImage& image) {
    m_image = image;
    m_dirty = true;
    if (isVisible() && !m_timer->isActive())
        m_timer->start();
}

void ScopesWidget::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (m_dirty)
        m_timer->start();
}

void ScopesWidget::recompute() {
    if (!isVisible() || !m_dirty)
        return;
    m_dirty = false;
    const auto kind = static_cast<scopes::Kind>(m_kind->currentData().toInt());
    m_view->setTrace({}, kind); // the plot area depends on the kind
    const QSize size = m_view->plotRect().size() * m_view->devicePixelRatioF();
    QImage trace = scopes::render(m_image, kind, size);
    trace.setDevicePixelRatio(m_view->devicePixelRatioF());
    m_view->setTrace(trace, kind);
}

// ---------------------------------------------------------------- ScopeView

ScopeView::ScopeView(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ScopeView::setTrace(const QImage& trace, scopes::Kind kind) {
    m_trace = trace;
    m_kind = kind;
    update();
}

QRect ScopeView::plotRect() const {
    const QRect r = rect().adjusted(28, 8, -8, -8);
    if (m_kind != scopes::Kind::Vectorscope)
        return r;
    const int s = std::min(r.width(), r.height());
    return QRect(r.center().x() - s / 2, r.center().y() - s / 2, s, s);
}

void ScopeView::resizeEvent(QResizeEvent*) { emit resized(); }

void ScopeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), field());
    const QRectF r = plotRect();
    QFont f = font();
    f.setPointSizeF(std::max(6.5, f.pointSizeF() * 0.8));
    p.setFont(f);

    if (m_kind == scopes::Kind::Vectorscope) {
        const QPointF c = r.center();
        const double rad = r.width() / 2;
        p.setPen(QPen(kGrid, 1));
        p.drawEllipse(c, rad, rad);
        p.drawEllipse(c, rad * 0.5, rad * 0.5);
        p.drawLine(QPointF(r.left(), c.y()), QPointF(r.right(), c.y()));
        p.drawLine(QPointF(c.x(), r.top()), QPointF(c.x(), r.bottom()));
        // Skin tone line through a typical skin colour.
        const QPointF skin = scopes::vectorscopePosition(0.85, 0.62, 0.50);
        const double len = std::hypot(skin.x(), skin.y());
        p.setPen(QPen(QColor(255, 200, 150, 90), 1, Qt::DashLine));
        p.drawLine(c, c + QPointF(skin.x() / len * rad, -skin.y() / len * rad));
        // 75 % colour bar targets.
        struct Target {
            double r, g, b;
            const char* name;
        };
        const Target targets[] = {{0.75, 0, 0, "R"},  {0.75, 0, 0.75, "Mg"}, {0, 0, 0.75, "B"},
                                  {0, 0.75, 0.75, "Cy"}, {0, 0.75, 0, "G"},  {0.75, 0.75, 0, "Yl"}};
        for (const auto& t : targets) {
            const QPointF v = scopes::vectorscopePosition(t.r, t.g, t.b);
            const QPointF pt = c + QPointF(v.x() * 2 * rad, -v.y() * 2 * rad);
            p.setPen(QPen(QColor(int(t.r / 0.75 * 200 + 55), int(t.g / 0.75 * 200 + 55), int(t.b / 0.75 * 200 + 55), 170), 1));
            p.drawRect(QRectF(pt - QPointF(5, 5), QSizeF(10, 10)));
            p.setPen(kLabel);
            p.drawText(pt + QPointF(8, 4), QString::fromLatin1(t.name));
        }
    } else if (m_kind == scopes::Kind::Histogram) {
        p.setPen(QPen(kGrid, 1));
        for (int i = 0; i <= 4; ++i) {
            const double x = r.left() + r.width() * i / 4.0;
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
        p.drawLine(r.bottomLeft(), r.bottomRight());
    } else {
        // Waveform / parade: 0, 25, 50, 75, 100 % lines with labels.
        for (int i = 0; i <= 4; ++i) {
            const double y = r.bottom() - r.height() * i / 4.0;
            p.setPen(QPen(kGrid, 1));
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
            p.setPen(kLabel);
            p.drawText(QRectF(0, y - 8, r.left() - 4, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(i * 25));
        }
        if (m_kind == scopes::Kind::Parade) {
            p.setPen(QPen(kGrid, 1));
            for (int i = 1; i < 3; ++i) {
                const double x = r.left() + r.width() * i / 3.0;
                p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            }
        }
    }
    if (!m_trace.isNull()) {
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.drawImage(r.topLeft(), m_trace);
    }
}

} // namespace lumora

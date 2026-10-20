#include "DockTitleBar.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QToolButton>

#include "Icons.h"
#include "Theme.h"

namespace lumora {

DockTitleBar::DockTitleBar(QDockWidget* dock) : QWidget(dock), m_dock(dock) {
    setObjectName(QStringLiteral("dockTitle"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(22, 6, 6, 2);
    layout->setSpacing(2);
    m_label = new QLabel;
    m_label->setObjectName(QStringLiteral("dockTitleText"));
    QFont f = m_label->font();
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
    m_label->setFont(f);
    layout->addWidget(m_label);
    layout->addStretch();

    auto makeButton = [&](const QIcon& icon, const QString& tip) {
        auto* b = new QToolButton;
        b->setObjectName(QStringLiteral("dockTitleButton"));
        b->setIcon(icon);
        b->setIconSize(QSize(12, 12));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(b);
        return b;
    };
    auto* floatBtn = makeButton(Icons::popOut(), tr("Detach or attach the panel"));
    auto* closeBtn = makeButton(Icons::close(), tr("Close the panel"));
    connect(floatBtn, &QToolButton::clicked, dock, [dock] { dock->setFloating(!dock->isFloating()); });
    connect(closeBtn, &QToolButton::clicked, dock, &QDockWidget::close);
    floatBtn->setVisible(dock->features() & QDockWidget::DockWidgetFloatable);
    closeBtn->setVisible(dock->features() & QDockWidget::DockWidgetClosable);
    connect(dock, &QDockWidget::featuresChanged, this, [floatBtn, closeBtn](QDockWidget::DockWidgetFeatures f) {
        floatBtn->setVisible(f & QDockWidget::DockWidgetFloatable);
        closeBtn->setVisible(f & QDockWidget::DockWidgetClosable);
    });
    connect(dock, &QDockWidget::windowTitleChanged, this, &DockTitleBar::updateTitle);
    updateTitle();
}

QSize DockTitleBar::sizeHint() const { return QSize(QWidget::sizeHint().width(), 30); }

void DockTitleBar::updateTitle() { m_label->setText(QLocale().toUpper(m_dock->windowTitle())); }

void DockTitleBar::paintEvent(QPaintEvent*) {
    // A short gradient bar in the brand colours before the name.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const ThemeColors& c = themeColors();
    const QRectF mark(10, m_label->geometry().center().y() - 4.5, 4, 9);
    QLinearGradient g(mark.topLeft(), mark.bottomLeft());
    g.setColorAt(0, c.accent);
    g.setColorAt(1, c.accentAlt);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(mark, 2, 2);
}

void DockTitleBar::mouseDoubleClickEvent(QMouseEvent*) {
    if (m_dock->features() & QDockWidget::DockWidgetFloatable)
        m_dock->setFloating(!m_dock->isFloating());
}

} // namespace lumora

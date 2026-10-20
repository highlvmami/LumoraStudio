#include "TimelinePanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

#include "Icons.h"
#include "panels/TimelineView.h"

namespace lumora {

TimelinePanel::TimelinePanel(TimelineView* view, QWidget* parent) : QWidget(parent), m_view(view) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(2);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(2, 0, 4, 2);
    m_toolbar = new QToolBar;
    m_toolbar->setIconSize(QSize(18, 18));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    top->addWidget(m_toolbar);
    m_info = new QLabel;
    m_info->setTextFormat(Qt::RichText);
    top->addWidget(m_info, 1);

    auto* zoomOut = new QToolButton;
    zoomOut->setIcon(Icons::zoomOut());
    zoomOut->setAutoRaise(true);
    zoomOut->setToolTip(tr("Zoom out (-)"));
    m_zoom = new QSlider(Qt::Horizontal);
    m_zoom->setRange(0, 1000);
    m_zoom->setFixedWidth(140);
    m_zoom->setToolTip(tr("Zoom (Ctrl + mouse wheel)"));
    auto* zoomIn = new QToolButton;
    zoomIn->setIcon(Icons::zoomIn());
    zoomIn->setAutoRaise(true);
    zoomIn->setToolTip(tr("Zoom in (+)"));
    auto* fit = new QToolButton;
    fit->setText(tr("Fit"));
    fit->setAutoRaise(true);
    fit->setToolTip(tr("Show the whole timeline"));
    top->addWidget(zoomOut);
    top->addWidget(m_zoom);
    top->addWidget(zoomIn);
    top->addWidget(fit);
    layout->addLayout(top);

    auto* viewRow = new QHBoxLayout;
    viewRow->setSpacing(0);
    viewRow->addWidget(view, 1);
    m_vscroll = new QScrollBar(Qt::Vertical);
    viewRow->addWidget(m_vscroll);
    layout->addLayout(viewRow, 1);
    m_scroll = new QScrollBar(Qt::Horizontal);
    layout->addWidget(m_scroll);

    // Logarithmic zoom slider: 2 .. 2000 px per second.
    auto toSlider = [](double pps) { return static_cast<int>(std::log(pps / 2.0) / std::log(1000.0) * 1000); };
    auto fromSlider = [](int v) { return 2.0 * std::pow(1000.0, v / 1000.0); };
    connect(m_zoom, &QSlider::valueChanged, this, [this, fromSlider](int v) { m_view->setPixelsPerSecond(fromSlider(v)); });
    connect(zoomIn, &QToolButton::clicked, this, [this] { m_view->setPixelsPerSecond(m_view->pixelsPerSecond() * 1.5); });
    connect(zoomOut, &QToolButton::clicked, this, [this] { m_view->setPixelsPerSecond(m_view->pixelsPerSecond() / 1.5); });
    connect(fit, &QToolButton::clicked, m_view, &TimelineView::zoomToFit);
    connect(m_scroll, &QScrollBar::valueChanged, m_view, &TimelineView::setScrollX);
    connect(m_vscroll, &QScrollBar::valueChanged, m_view, &TimelineView::setScrollY);
    connect(m_view, &TimelineView::scrollChanged, this, [this, toSlider] {
        syncScrollBar();
        const QSignalBlocker b(m_zoom);
        m_zoom->setValue(toSlider(m_view->pixelsPerSecond()));
    });
    syncScrollBar();
}

void TimelinePanel::setInfoText(const QString& text) { m_info->setText(text); }

void TimelinePanel::syncScrollBar() {
    const QSignalBlocker b(m_scroll);
    const int visible = std::max(1, m_view->width() - TimelineView::kHeader);
    m_scroll->setRange(0, std::max(0, m_view->contentWidth() - visible));
    m_scroll->setPageStep(visible);
    m_scroll->setValue(m_view->scrollX());

    const QSignalBlocker bv(m_vscroll);
    const int tall = m_view->contentHeight() - m_view->height();
    m_vscroll->setVisible(tall > 0);
    m_vscroll->setRange(0, std::max(0, tall));
    m_vscroll->setPageStep(m_view->height());
    m_vscroll->setValue(m_view->scrollY());
}

} // namespace lumora

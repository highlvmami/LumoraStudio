#pragma once

#include <QWidget>

class QScrollBar;
class QToolBar;
class QSlider;
class QLabel;

namespace lumora {

class TimelineView;

class TimelinePanel : public QWidget {
    Q_OBJECT
public:
    TimelinePanel(TimelineView* view, QWidget* parent = nullptr);
    QToolBar* toolBar() const { return m_toolbar; }
    TimelineView* view() const { return m_view; }
    void setInfoText(const QString& text);

private:
    void syncScrollBar();

    TimelineView* m_view;
    QToolBar* m_toolbar;
    QScrollBar* m_scroll;
    QSlider* m_zoom;
    QLabel* m_info;
};

} // namespace lumora

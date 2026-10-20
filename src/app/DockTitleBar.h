#pragma once

#include <QWidget>

class QDockWidget;
class QLabel;

namespace lumora {

// The title strip of a panel: a small brand-coloured mark, the name in
// spaced capitals, and float / close buttons.
class DockTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit DockTitleBar(QDockWidget* dock);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    void updateTitle();

    QDockWidget* m_dock;
    QLabel* m_label;
};

} // namespace lumora

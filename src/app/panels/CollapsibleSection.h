#pragma once

#include <QWidget>

class QFormLayout;
class QLabel;
class QToolButton;

class QBoxLayout;

namespace lumora {

// A row of buttons that stacks them when the panel is too narrow for all of
// them side by side, so nothing is cut off.
class AdaptiveRow : public QWidget {
public:
    explicit AdaptiveRow(QWidget* parent = nullptr);
    void addWidget(QWidget* w, int stretch = 0);
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void arrange(int width);
    QBoxLayout* m_box;
};


// A card of settings with a clickable title that folds it away. The open /
// closed state is remembered per `id`. A dot next to the title shows that
// something in the card is changed; the optional reset button sets it back.
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    CollapsibleSection(const QString& id, const QString& title, QWidget* parent = nullptr);

    QFormLayout* form() const { return m_form; }
    void setExpanded(bool on);
    bool isExpanded() const;
    // Shows the "changed" dot.
    void setModified(bool on);
    // Adds a reset button to the title row.
    QToolButton* addResetButton(const QString& tooltip);

private:
    QString m_id;
    QToolButton* m_header;
    QLabel* m_dot;
    QWidget* m_body;
    QFormLayout* m_form;
    QWidget* m_titleRow;
};

} // namespace lumora

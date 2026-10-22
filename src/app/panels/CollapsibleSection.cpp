#include "CollapsibleSection.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"

namespace lumora {

CollapsibleSection::CollapsibleSection(const QString& id, const QString& title, QWidget* parent)
    : QWidget(parent), m_id(id) {
    setObjectName(QStringLiteral("propertySection"));
    setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 2, 6, 6);
    v->setSpacing(2);

    m_titleRow = new QWidget;
    auto* row = new QHBoxLayout(m_titleRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    m_header = new QToolButton;
    m_header->setObjectName(QStringLiteral("sectionHeader"));
    m_header->setText(title);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setCheckable(true);
    m_header->setIconSize(QSize(16, 16));
    m_dot = new QLabel(QStringLiteral("●"));
    m_dot->setObjectName(QStringLiteral("sectionDot"));
    m_dot->setToolTip(tr("Changed"));
    m_dot->hide();
    row->addWidget(m_header, 1);
    row->addWidget(m_dot);
    v->addWidget(m_titleRow);

    m_body = new QWidget;
    m_form = new QFormLayout(m_body);
    m_form->setLabelAlignment(Qt::AlignLeft);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // A narrow panel puts the field under its label instead of cutting it.
    m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_form->setContentsMargins(4, 0, 2, 2);
    m_form->setVerticalSpacing(6);
    v->addWidget(m_body);

    connect(m_header, &QToolButton::toggled, this, [this](bool on) {
        m_body->setVisible(on);
        m_header->setIcon(Icons::chevron(on));
        QSettings().setValue(QStringLiteral("properties/open/") + m_id, on);
    });
    const bool open = QSettings().value(QStringLiteral("properties/open/") + m_id, true).toBool();
    m_header->setChecked(open);
    m_body->setVisible(open);
    m_header->setIcon(Icons::chevron(open));
}

void CollapsibleSection::setExpanded(bool on) { m_header->setChecked(on); }

bool CollapsibleSection::isExpanded() const { return m_header->isChecked(); }

void CollapsibleSection::setModified(bool on) { m_dot->setVisible(on); }

QToolButton* CollapsibleSection::addResetButton(const QString& tooltip) {
    auto* b = new QToolButton;
    b->setObjectName(QStringLiteral("sectionReset"));
    b->setIcon(Icons::reset());
    b->setToolTip(tooltip);
    b->setAutoRaise(true);
    static_cast<QHBoxLayout*>(m_titleRow->layout())->addWidget(b);
    return b;
}

AdaptiveRow::AdaptiveRow(QWidget* parent) : QWidget(parent) {
    m_box = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_box->setContentsMargins(0, 0, 0, 0);
    m_box->setSpacing(4);
}

void AdaptiveRow::addWidget(QWidget* w, int stretch) {
    m_box->addWidget(w, stretch);
    arrange(width());
}

QSize AdaptiveRow::minimumSizeHint() const {
    int w = 0;
    for (int i = 0; i < m_box->count(); ++i)
        if (QWidget* c = m_box->itemAt(i)->widget(); c && !c->isHidden())
            w = std::max(w, c->minimumSizeHint().width());
    return QSize(w, QWidget::minimumSizeHint().height());
}

void AdaptiveRow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    arrange(e->size().width());
}

void AdaptiveRow::arrange(int width) {
    int needed = 0, shown = 0;
    for (int i = 0; i < m_box->count(); ++i)
        if (QWidget* c = m_box->itemAt(i)->widget(); c && !c->isHidden()) {
            needed += c->sizeHint().width();
            ++shown;
        }
    needed += std::max(0, shown - 1) * m_box->spacing();
    const auto dir = width > 0 && width < needed ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (m_box->direction() != dir) {
        m_box->setDirection(dir);
        updateGeometry();
    }
}

} // namespace lumora

#include "WheelGuard.h"

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>

namespace lumora {

void WheelGuard::install() { qApp->installEventFilter(new WheelGuard(qApp)); }

static bool guarded(QObject* o) {
    return qobject_cast<QAbstractSlider*>(o) || qobject_cast<QAbstractSpinBox*>(o) || qobject_cast<QComboBox*>(o);
}

bool WheelGuard::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Polish && guarded(o)) {
        // A wheel would otherwise give the control focus on the way in.
        auto* w = static_cast<QWidget*>(o);
        if (w->focusPolicy() == Qt::WheelFocus)
            w->setFocusPolicy(Qt::StrongFocus);
        return false;
    }
    if (e->type() != QEvent::Wheel || !guarded(o) || static_cast<QWidget*>(o)->hasFocus())
        return false;
    // Ignored and filtered: Qt hands the wheel on to the parent, so the
    // scroll area around the control scrolls.
    e->ignore();
    return true;
}

} // namespace lumora

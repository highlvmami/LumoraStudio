#pragma once

#include <QObject>

namespace lumora {

// Keeps the mouse wheel from changing sliders, number boxes and drop-down
// lists the user has not clicked. Scrolling a panel would otherwise turn
// whatever control passes under the pointer (a clip's volume dropped to
// -40 dB that way); the wheel scrolls the panel instead.
class WheelGuard : public QObject {
public:
    using QObject::QObject;
    // Installs the guard for the whole application.
    static void install();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
};

} // namespace lumora

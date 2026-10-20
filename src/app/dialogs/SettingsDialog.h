#pragma once

#include <QDialog>

class QLabel;

namespace lumora {

class Updater;

// Application settings. For now: updates (check now, daily check).
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Updater* updater, QWidget* parent = nullptr);

private:
    void refreshLastCheck();

    QLabel* m_lastCheck;
};

} // namespace lumora

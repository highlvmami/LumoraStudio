#include "SettingsDialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include "DesktopShortcut.h"
#include "Updater.h"
#include "playback/PlaybackEngine.h"

namespace lumora {

SettingsDialog::SettingsDialog(Updater* updater, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(420);
    auto* layout = new QVBoxLayout(this);

    auto* updates = new QGroupBox(tr("Updates"));
    auto* ul = new QVBoxLayout(updates);
    ul->addWidget(new QLabel(tr("Installed version: %1").arg(QCoreApplication::applicationVersion())));
    m_lastCheck = new QLabel;
    m_lastCheck->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    ul->addWidget(m_lastCheck);
    auto* row = new QHBoxLayout;
    auto* checkNow = new QPushButton(tr("Check for updates"));
    row->addWidget(checkNow);
    row->addStretch(1);
    ul->addLayout(row);
    auto* daily = new QCheckBox(tr("Check automatically once a day"));
    daily->setChecked(Updater::autoCheck());
    ul->addWidget(daily);
    layout->addWidget(updates);

    auto* preview = new QGroupBox(tr("Preview"));
    auto* pl = new QVBoxLayout(preview);
    auto* hardware = new QCheckBox(tr("Decode videos on the graphics card"));
    hardware->setChecked(PlaybackEngine::hardwareDecoding());
    hardware->setToolTip(tr("Plays 4K, HEVC and high frame rate videos smoothly. Turn it off if the preview shows "
                            "wrong colours or stays black. Exports always use the full quality decoder."));
    pl->addWidget(hardware);
    auto* note = new QLabel(tr("When the computer cannot keep up, the preview plays at a lower resolution; "
                               "the paused picture and the exported file stay full quality."));
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    pl->addWidget(note);
    layout->addWidget(preview);
    connect(hardware, &QCheckBox::toggled, this, [](bool on) { PlaybackEngine::setHardwareDecoding(on); });

    if (DesktopShortcut::supported()) {
        auto* shortcut = new QGroupBox(tr("Desktop shortcut"));
        auto* sl = new QVBoxLayout(shortcut);
        auto* state = new QLabel;
        state->setWordWrap(true);
        state->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        auto* srow = new QHBoxLayout;
        auto* make = new QPushButton(tr("Add to desktop"));
        make->setToolTip(tr("Puts a LumoraStudio shortcut on the desktop and in the program menu"));
        srow->addWidget(make);
        srow->addStretch(1);
        sl->addWidget(state);
        sl->addLayout(srow);
        layout->addWidget(shortcut);
        auto refresh = [state, make] {
            const bool there = DesktopShortcut::exists();
            state->setText(there ? tr("LumoraStudio is on the desktop.")
                                 : tr("Open LumoraStudio with a double click from the desktop."));
            make->setText(there ? tr("Renew shortcut") : tr("Add to desktop"));
        };
        connect(make, &QPushButton::clicked, this, [this, state, refresh] {
            QString error;
            if (DesktopShortcut::create(&error))
                refresh();
            else
                state->setText(tr("Could not create the shortcut: %1").arg(error));
        });
        refresh();
    }
    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(checkNow, &QPushButton::clicked, updater, &Updater::checkNow);
    connect(updater, &Updater::checked, this, &SettingsDialog::refreshLastCheck);
    connect(daily, &QCheckBox::toggled, this, [](bool on) { Updater::setAutoCheck(on); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refreshLastCheck();
}

void SettingsDialog::refreshLastCheck() {
    const QDateTime last = Updater::lastCheck();
    m_lastCheck->setText(last.isValid()
                             ? tr("Last checked: %1").arg(QLocale().toString(last.toLocalTime(), QLocale::ShortFormat))
                             : tr("Not checked yet"));
}

} // namespace lumora

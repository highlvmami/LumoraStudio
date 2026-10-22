#pragma once

#include <QString>

class QWidget;

namespace lumora::DesktopShortcut {

// A shortcut to LumoraStudio on the desktop and in the Start menu (Windows .lnk)
// or the application menu (Linux .desktop), so the portable build opens with
// one double click.

bool supported();
// True when the desktop shortcut file exists.
bool exists();
// Creates (or refreshes) the desktop and menu shortcuts for the running executable.
// On failure returns false and fills `error`.
bool create(QString* error = nullptr);
// Run once at startup: offers the shortcut on first run, and points shortcuts made
// earlier at this executable again if the folder was moved.
void offerOrRefresh(QWidget* parent);

} // namespace lumora::DesktopShortcut

#pragma once

#include <QString>

namespace lumora {

enum class ThemeKind { Dark, Light };

void applyTheme(ThemeKind kind);
ThemeKind themeFromString(const QString& s);
QString themeToString(ThemeKind k);

} // namespace lumora

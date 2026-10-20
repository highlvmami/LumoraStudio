#pragma once

#include <QColor>
#include <QString>

class QWidget;

namespace lumora {

enum class ThemeKind { Dark, Light };

// The named colours of the LumoraStudio look. Widgets that paint themselves
// (timeline, viewers, thumbnails, scopes) take their colours from here so
// that the whole interface changes together.
struct ThemeColors {
    QColor canvas;     // window background, the gaps between panels
    QColor panel;      // panel surfaces
    QColor raised;     // buttons, fields, timeline lanes
    QColor hover;      // hovered controls
    QColor border;     // hairlines around panels and fields
    QColor text;
    QColor textDim;    // labels, secondary text
    QColor textFaint;  // placeholders, disabled text
    QColor accent;     // "lumen" amber: selection, playhead, focus
    QColor accentAlt;  // rose, the second colour of the brand gradient
    QColor accentCool; // indigo, the third colour of the brand gradient
    QColor onAccent;   // text drawn on top of the accent
    QColor videoClip;
    QColor audioClip;
    QColor titleClip;
    QColor playhead;
    QColor success;
    QColor warning;
    QColor danger;
    QColor viewer; // behind pictures and scopes
};

void applyTheme(ThemeKind kind);
ThemeKind themeFromString(const QString& s);
QString themeToString(ThemeKind k);

ThemeKind currentTheme();
const ThemeColors& themeColors();

// Marks a widget as a rounded panel surface floating on the canvas.
void makePanelSurface(QWidget* w);

// Gives a button the brand gradient: the one main action of a window.
void markPrimary(QWidget* button);

} // namespace lumora

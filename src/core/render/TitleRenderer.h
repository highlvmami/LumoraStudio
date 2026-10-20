#pragma once

#include <QPainter>
#include <QSize>

#include "timeline/ClipEffects.h"
#include "timeline/Subtitles.h"

namespace lumora {

// Draws a title in an output frame. `local` is the time since the start of the
// clip and `duration` its length; they drive the in/out animations. Needs a
// QGuiApplication (fonts).
void drawTitle(QPainter& p, QSize frame, const TitleData& title, Ticks local, Ticks duration, double opacity);

// Draws a subtitle cue: centred, wrapped to 90 % of the frame width, its
// last line resting at style.y.
void drawSubtitle(QPainter& p, QSize frame, const SubtitleStyle& style, const QString& text);

} // namespace lumora

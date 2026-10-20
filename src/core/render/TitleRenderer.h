#pragma once

#include <QPainter>
#include <QSize>

#include "timeline/ClipEffects.h"

namespace lumora {

// Draws a title in an output frame. `local` is the time since the start of the
// clip and `duration` its length; they drive the in/out animations. Needs a
// QGuiApplication (fonts).
void drawTitle(QPainter& p, QSize frame, const TitleData& title, Ticks local, Ticks duration, double opacity);

} // namespace lumora

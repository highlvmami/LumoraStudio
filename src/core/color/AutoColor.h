#pragma once

#include <QImage>

#include "color/ColorGrade.h"

namespace lumora {

// One-click colour and light correction: measures the picture (brightness
// spread, colour cast, how colourful it is) and sets exposure, whites /
// blacks, contrast, white balance and vibrance so it looks natural. The
// filter, curves, wheels and LUT of `base` are kept. `picture` is the
// picture before any colour settings.
ColorGrade autoCorrect(const QImage& picture, const ColorGrade& base = {});

} // namespace lumora

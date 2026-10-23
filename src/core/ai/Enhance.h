#pragma once

#include <QImage>
#include <QString>
#include <QTransform>
#include <functional>

#include "ai/Vision.h"

namespace lumora::ai {

// Picture repair with local models: enlargement that adds detail
// (Real-ESRGAN), face restoration for old, small or blurry photos (GFPGAN)
// and object removal that fills in what was behind (LaMa).
//
// Each takes any image and returns an RGBA64 one (16 bits per channel, the
// alpha channel kept), or a null image when the model is missing, it fails
// (`error` set) or `progress` cancels.

// Reports progress (0..1); returning false cancels the work.
using EnhanceProgress = std::function<bool(double)>;

// Enlarges `image` 2 or 4 times, drawing in fine detail instead of
// stretching the pixels.
QImage upscale(const QImage& image, int factor, const EnhanceProgress& progress = {}, QString* error = nullptr);

// Restores every face in `image`; `strength` (0 .. 1) blends the restored
// faces over the original ones.
QImage restoreFaces(const QImage& image, double strength, const EnhanceProgress& progress = {}, QString* error = nullptr);

// Removes what `mask` (Grayscale8, any size; white = remove) covers and
// fills it in from the surroundings. Pixels outside the mask stay as they
// were.
QImage inpaint(const QImage& image, const QImage& mask, const EnhanceProgress& progress = {}, QString* error = nullptr);

// The similarity transform (rotation, uniform scale, shift) that best maps
// the face's five landmarks onto the 512 x 512 face template GFPGAN was
// trained on.
QTransform faceAlignment(const Detection& face);

} // namespace lumora::ai

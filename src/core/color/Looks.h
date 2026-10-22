#pragma once

#include <QString>
#include <vector>

#include "color/ColorGrade.h"

namespace lumora {

// A ready-made colour filter ("look"): ordinary colour settings, so after
// choosing one every slider can still be adjusted.
struct Look {
    QString id;
    QString name; // translated
    ColorGrade grade;
};

const std::vector<Look>& looks();
const Look* findLook(const QString& id);

// `grade` with the look applied at `amount` (0 .. 1): the look's values are
// mixed in, the basic exposure of the grade is kept.
ColorGrade applyLook(const ColorGrade& grade, const Look& look, double amount = 1.0);

} // namespace lumora

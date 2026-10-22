#pragma once

#include <QString>
#include <vector>

#include "timeline/ClipEffects.h"

namespace lumora {

// Ready-made title looks: a title's style (font, size, colours, shapes,
// animations and place) with example text.
struct TitleTemplate {
    QString id;
    QString name; // translated
    TitleData title;
    double seconds = 4.0; // suggested length
};

const std::vector<TitleTemplate>& titleTemplates();
const TitleTemplate* findTitleTemplate(const QString& id);

// `title` restyled like the template, keeping its text.
TitleData applyTitleTemplate(const TitleData& title, const TitleTemplate& t);

} // namespace lumora

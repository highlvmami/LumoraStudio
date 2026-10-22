#include "Collage.h"

#include <QCoreApplication>
#include <algorithm>

namespace lumora {

const std::vector<CollageLayout>& collageLayouts() {
    static const std::vector<CollageLayout> layouts = [] {
        auto t = [](const char* s) { return QCoreApplication::translate("lumora::collage", s); };
        auto grid = [](int cols, int rows) {
            std::vector<QRectF> cells;
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                    cells.emplace_back(double(c) / cols, double(r) / rows, 1.0 / cols, 1.0 / rows);
            return cells;
        };
        const double third = 1.0 / 3.0;
        return std::vector<CollageLayout>{
            {QStringLiteral("sideBySide"), t(QT_TRANSLATE_NOOP("lumora::collage", "Side by side")), grid(2, 1)},
            {QStringLiteral("stacked"), t(QT_TRANSLATE_NOOP("lumora::collage", "One above the other")), grid(1, 2)},
            {QStringLiteral("threeColumns"), t(QT_TRANSLATE_NOOP("lumora::collage", "Three columns")), grid(3, 1)},
            {QStringLiteral("bigLeft"), t(QT_TRANSLATE_NOOP("lumora::collage", "One large, two small")),
             {QRectF(0, 0, 2 * third, 1), QRectF(2 * third, 0, third, 0.5), QRectF(2 * third, 0.5, third, 0.5)}},
            {QStringLiteral("bigTop"), t(QT_TRANSLATE_NOOP("lumora::collage", "One on top, three below")),
             {QRectF(0, 0, 1, 2 * third), QRectF(0, 2 * third, third, third), QRectF(third, 2 * third, third, third),
              QRectF(2 * third, 2 * third, third, third)}},
            {QStringLiteral("grid4"), t(QT_TRANSLATE_NOOP("lumora::collage", "Four squares")), grid(2, 2)},
            {QStringLiteral("grid6"), t(QT_TRANSLATE_NOOP("lumora::collage", "Six squares")), grid(3, 2)},
            {QStringLiteral("grid9"), t(QT_TRANSLATE_NOOP("lumora::collage", "Nine squares")), grid(3, 3)},
        };
    }();
    return layouts;
}

const CollageLayout* findCollageLayout(const QString& id) {
    for (const auto& l : collageLayouts())
        if (l.id == id)
            return &l;
    return nullptr;
}

ClipTransform collageTransform(const QRectF& cell, double pictureAspect, double frameAspect, double gap) {
    ClipTransform t;
    if (pictureAspect <= 0 || frameAspect <= 0)
        return t;
    // The gap in fractions of width and height, so it looks the same both ways.
    const double gx = gap * std::min(1.0, 1.0 / frameAspect), gy = gap * std::min(1.0, frameAspect);
    // Inner edges get half a gap from each side; outer edges a whole one.
    auto inset = [](double a, double len, double g) {
        const double left = a <= 1e-9 ? g : g / 2, right = a + len >= 1 - 1e-9 ? g : g / 2;
        return std::pair{a + left, std::max(0.01, len - left - right)};
    };
    const auto [cx, cw] = inset(cell.x(), cell.width(), gx);
    const auto [cy, ch] = inset(cell.y(), cell.height(), gy);
    // Size of the picture at scale 1 (fit inside the frame), in frame fractions.
    const double fitW = std::min(1.0, pictureAspect / frameAspect), fitH = std::min(1.0, frameAspect / pictureAspect);
    const double scale = std::max(cw / fitW, ch / fitH);
    const double sw = fitW * scale, sh = fitH * scale;
    t.scale = scale;
    t.x = cx + cw / 2 - 0.5;
    t.y = cy + ch / 2 - 0.5;
    const double kw = std::min(1.0, cw / sw), kh = std::min(1.0, ch / sh);
    t.crop = QRectF((1 - kw) / 2, (1 - kh) / 2, kw, kh);
    if (!t.hasCrop())
        t.crop = QRectF();
    return t;
}

} // namespace lumora

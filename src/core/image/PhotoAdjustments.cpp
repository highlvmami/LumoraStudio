#include "PhotoAdjustments.h"

#include <QJsonArray>
#include <cmath>

namespace lumora {

QRect PhotoAdjustments::cropRect(QSize size) const {
    if (!hasCrop())
        return QRect(QPoint(0, 0), size);
    const QRectF c = crop.intersected(QRectF(0, 0, 1, 1));
    const int x0 = static_cast<int>(std::lround(c.left() * size.width()));
    const int y0 = static_cast<int>(std::lround(c.top() * size.height()));
    const int x1 = static_cast<int>(std::lround(c.right() * size.width()));
    const int y1 = static_cast<int>(std::lround(c.bottom() * size.height()));
    QRect r(QPoint(x0, y0), QPoint(x1 - 1, y1 - 1));
    if (r.width() < 1 || r.height() < 1)
        return QRect(QPoint(0, 0), size);
    return r.intersected(QRect(QPoint(0, 0), size));
}

QJsonObject PhotoAdjustments::toJson() const {
    QJsonObject o;
    o["exposure"] = exposure;
    o["contrast"] = contrast;
    o["temperature"] = temperature;
    o["tint"] = tint;
    if (hasCrop())
        o["crop"] = QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()};
    return o;
}

PhotoAdjustments PhotoAdjustments::fromJson(const QJsonObject& o) {
    PhotoAdjustments a;
    a.exposure = o.value("exposure").toDouble();
    a.contrast = o.value("contrast").toDouble();
    a.temperature = o.value("temperature").toDouble();
    a.tint = o.value("tint").toDouble();
    const QJsonArray c = o.value("crop").toArray();
    if (c.size() == 4)
        a.crop = QRectF(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(), c[3].toDouble());
    return a;
}

} // namespace lumora

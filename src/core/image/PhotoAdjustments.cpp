#include "PhotoAdjustments.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace lumora {

namespace {

QJsonArray rectToJson(const QRectF& r) { return QJsonArray{r.x(), r.y(), r.width(), r.height()}; }
QRectF rectFromJson(const QJsonValue& v, const QRectF& fallback) {
    const QJsonArray c = v.toArray();
    if (c.size() != 4)
        return fallback;
    return QRectF(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(), c[3].toDouble());
}

struct BlendName {
    BlendMode mode;
    const char* id;
};
constexpr BlendName kBlendNames[] = {
    {BlendMode::Normal, "normal"},   {BlendMode::Multiply, "multiply"}, {BlendMode::Screen, "screen"},
    {BlendMode::Overlay, "overlay"}, {BlendMode::SoftLight, "softLight"}, {BlendMode::Darken, "darken"},
    {BlendMode::Lighten, "lighten"}, {BlendMode::Add, "add"},           {BlendMode::Difference, "difference"},
};

const char* maskShapeId(MaskShape s) {
    switch (s) {
    case MaskShape::None: return "none";
    case MaskShape::Rectangle: return "rectangle";
    case MaskShape::Ellipse: return "ellipse";
    case MaskShape::Linear: return "linear";
    }
    return "none";
}

MaskShape maskShapeFromId(const QString& id) {
    for (MaskShape s : {MaskShape::Rectangle, MaskShape::Ellipse, MaskShape::Linear})
        if (id == QLatin1String(maskShapeId(s)))
            return s;
    return MaskShape::None;
}

double smoothstep(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

} // namespace

QString blendModeId(BlendMode m) {
    for (const auto& n : kBlendNames)
        if (n.mode == m)
            return QString::fromLatin1(n.id);
    return QStringLiteral("normal");
}

BlendMode blendModeFromId(const QString& id) {
    for (const auto& n : kBlendNames)
        if (id == QLatin1String(n.id))
            return n.mode;
    return BlendMode::Normal;
}

QJsonObject LayerMask::toJson() const {
    QJsonObject o;
    o["shape"] = QLatin1String(maskShapeId(shape));
    if (shape != MaskShape::None) {
        o["rect"] = rectToJson(rect);
        o["feather"] = feather;
        o["angle"] = angle;
    }
    if (invert)
        o["invert"] = true;
    return o;
}

LayerMask LayerMask::fromJson(const QJsonObject& o) {
    LayerMask m;
    m.shape = maskShapeFromId(o.value("shape").toString());
    m.rect = rectFromJson(o.value("rect"), m.rect);
    m.feather = o.value("feather").toDouble(m.feather);
    m.angle = o.value("angle").toDouble(m.angle);
    m.invert = o.value("invert").toBool(false);
    return m;
}

double LayerMask::valueAt(double u, double v, double aspect) const {
    double m = 1.0;
    const double f = std::max(feather, 1e-4);
    switch (shape) {
    case MaskShape::None: break;
    case MaskShape::Rectangle: {
        // Distance inside the edge, relative to the half size of each axis.
        const double hx = std::max(rect.width() / 2, 1e-6), hy = std::max(rect.height() / 2, 1e-6);
        const double dx = 1.0 - std::abs(u - rect.center().x()) / hx;
        const double dy = 1.0 - std::abs(v - rect.center().y()) / hy;
        m = smoothstep(std::min(dx, dy) / f);
        break;
    }
    case MaskShape::Ellipse: {
        const double hx = std::max(rect.width() / 2, 1e-6), hy = std::max(rect.height() / 2, 1e-6);
        const double nx = (u - rect.center().x()) / hx, ny = (v - rect.center().y()) / hy;
        const double d = 1.0 - std::sqrt(nx * nx + ny * ny);
        m = smoothstep(d / f);
        break;
    }
    case MaskShape::Linear: {
        // Signed distance from the line, measured in pixels of a picture
        // with the given aspect so the gradient is not skewed.
        const double a = angle * M_PI / 180.0;
        const double px = (u - rect.center().x()) * aspect, py = v - rect.center().y();
        const double d = -(px * -std::sin(a) + py * std::cos(a)); // > 0 above the line (for angle 0)
        const double diag = std::sqrt(aspect * aspect + 1.0);
        m = smoothstep(0.5 + d / (f * diag));
        break;
    }
    }
    return invert ? 1.0 - m : m;
}

bool PhotoLayer::isNoOp() const {
    if (!visible || opacity <= 0.0)
        return true;
    if (kind == LayerKind::Adjustment)
        return grade.isIdentity();
    return imagePath.isEmpty();
}

QJsonObject PhotoLayer::toJson() const {
    QJsonObject o;
    o["id"] = id.toString(QUuid::WithoutBraces);
    o["kind"] = kind == LayerKind::Image ? QStringLiteral("image") : QStringLiteral("adjustment");
    o["name"] = name;
    o["visible"] = visible;
    o["opacity"] = opacity;
    o["blend"] = blendModeId(blend);
    o["mask"] = mask.toJson();
    if (kind == LayerKind::Adjustment) {
        o["grade"] = grade.toJson();
    } else {
        o["image"] = imagePath;
        o["x"] = x;
        o["y"] = y;
        o["scale"] = scale;
    }
    return o;
}

PhotoLayer PhotoLayer::fromJson(const QJsonObject& o) {
    PhotoLayer l;
    l.id = QUuid::fromString(o.value("id").toString());
    if (l.id.isNull())
        l.id = QUuid::createUuid();
    l.kind = o.value("kind").toString() == QLatin1String("image") ? LayerKind::Image : LayerKind::Adjustment;
    l.name = o.value("name").toString();
    l.visible = o.value("visible").toBool(true);
    l.opacity = std::clamp(o.value("opacity").toDouble(1.0), 0.0, 1.0);
    l.blend = blendModeFromId(o.value("blend").toString());
    l.mask = LayerMask::fromJson(o.value("mask").toObject());
    l.grade = ColorGrade::fromJson(o.value("grade").toObject());
    l.imagePath = o.value("image").toString();
    l.x = o.value("x").toDouble(0.0);
    l.y = o.value("y").toDouble(0.0);
    l.scale = o.value("scale").toDouble(1.0);
    return l;
}

bool PhotoAdjustments::hasLayerChanges() const {
    return std::any_of(layers.begin(), layers.end(), [](const PhotoLayer& l) { return !l.isNoOp(); });
}

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
    // The colour settings sit at the top level, as the 0.1 / 0.2 exposure,
    // contrast, temperature and tint did, so older projects read unchanged.
    QJsonObject o = color.toJson();
    if (hasCrop())
        o["crop"] = rectToJson(crop);
    if (!layers.empty()) {
        QJsonArray arr;
        for (const auto& l : layers)
            arr.append(l.toJson());
        o["layers"] = arr;
    }
    return o;
}

PhotoAdjustments PhotoAdjustments::fromJson(const QJsonObject& o) {
    PhotoAdjustments a;
    a.color = ColorGrade::fromJson(o);
    a.crop = rectFromJson(o.value("crop"), QRectF());
    for (const auto& v : o.value("layers").toArray())
        a.layers.push_back(PhotoLayer::fromJson(v.toObject()));
    return a;
}

} // namespace lumora

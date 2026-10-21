#include "PhotoAdjustments.h"

#include <QJsonArray>
#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <cmath>
#include <numeric>

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
    case MaskShape::Brush: return "brush";
    }
    return "none";
}

MaskShape maskShapeFromId(const QString& id) {
    for (MaskShape s : {MaskShape::Rectangle, MaskShape::Ellipse, MaskShape::Linear, MaskShape::Brush})
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

namespace {

double round5(double v) { return std::round(v * 1e5) / 1e5; }

} // namespace

QJsonObject BrushStroke::toJson() const {
    // Points as a flat [x, y, x, y, ...] list, which keeps long strokes small.
    QJsonArray pts;
    for (const auto& p : points) {
        pts.append(round5(p.x()));
        pts.append(round5(p.y()));
    }
    QJsonObject o{{"points", pts}, {"radius", radius}, {"hardness", hardness}, {"flow", flow}};
    if (erase)
        o["erase"] = true;
    return o;
}

BrushStroke BrushStroke::fromJson(const QJsonObject& o) {
    BrushStroke s;
    const QJsonArray pts = o.value("points").toArray();
    for (int i = 0; i + 1 < pts.size(); i += 2)
        s.points.emplace_back(pts[i].toDouble(), pts[i + 1].toDouble());
    s.radius = std::clamp(o.value("radius").toDouble(s.radius), 1e-4, 1.0);
    s.hardness = std::clamp(o.value("hardness").toDouble(s.hardness), 0.0, 1.0);
    s.flow = std::clamp(o.value("flow").toDouble(s.flow), 0.0, 1.0);
    s.erase = o.value("erase").toBool(false);
    return s;
}

namespace {

// Paints one stroke into `coverage` (the largest dab value per pixel, so a
// stroke crossing itself does not get stronger) and returns the touched
// pixel rectangle.
QRect stampStroke(const BrushStroke& s, int w, int h, std::vector<float>& coverage) {
    if (s.points.empty() || w <= 0 || h <= 0)
        return {};
    const double r = std::max(0.5, s.radius * h);
    const double hard = std::clamp(s.hardness, 0.0, 0.99);
    const double spacing = std::max(0.75, r * 0.15);
    QRect touched;
    auto dab = [&](double cx, double cy) {
        const int x0 = std::max(0, static_cast<int>(std::floor(cx - r))), x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + r)));
        const int y0 = std::max(0, static_cast<int>(std::floor(cy - r))), y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + r)));
        if (x0 > x1 || y0 > y1)
            return;
        touched |= QRect(QPoint(x0, y0), QPoint(x1, y1));
        for (int y = y0; y <= y1; ++y) {
            float* row = coverage.data() + static_cast<size_t>(y) * w;
            const double dy = (y + 0.5 - cy) / r;
            for (int x = x0; x <= x1; ++x) {
                const double dx = (x + 0.5 - cx) / r;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d >= 1.0)
                    continue;
                const float v = d <= hard ? 1.0f : static_cast<float>(1.0 - smoothstep((d - hard) / (1.0 - hard)));
                row[x] = std::max(row[x], v);
            }
        }
    };
    QPointF prev(s.points.front().x() * w, s.points.front().y() * h);
    dab(prev.x(), prev.y());
    double carry = 0.0; // distance travelled since the last dab
    for (size_t i = 1; i < s.points.size(); ++i) {
        const QPointF p(s.points[i].x() * w, s.points[i].y() * h);
        const double len = std::hypot(p.x() - prev.x(), p.y() - prev.y());
        double t = spacing - carry;
        while (t <= len) {
            const double f = t / len;
            dab(prev.x() + (p.x() - prev.x()) * f, prev.y() + (p.y() - prev.y()) * f);
            t += spacing;
        }
        carry = len - (t - spacing);
        prev = p;
    }
    if (s.points.size() > 1)
        dab(prev.x(), prev.y());
    return touched;
}

} // namespace

std::vector<float> LayerMask::render(int w, int h) const {
    std::vector<float> out(static_cast<size_t>(std::max(0, w)) * std::max(0, h), 1.0f);
    if (w <= 0 || h <= 0)
        return out;
    if (shape == MaskShape::Brush) {
        std::fill(out.begin(), out.end(), 0.0f);
        std::vector<float> coverage(out.size(), 0.0f);
        for (const auto& s : strokes) {
            const QRect r = stampStroke(s, w, h, coverage);
            if (r.isEmpty())
                continue;
            const float flow = static_cast<float>(std::clamp(s.flow, 0.0, 1.0));
            for (int y = r.top(); y <= r.bottom(); ++y) {
                float* m = out.data() + static_cast<size_t>(y) * w;
                float* c = coverage.data() + static_cast<size_t>(y) * w;
                for (int x = r.left(); x <= r.right(); ++x) {
                    const float a = c[x] * flow;
                    m[x] = s.erase ? m[x] * (1 - a) : m[x] + (1 - m[x]) * a;
                    c[x] = 0.0f;
                }
            }
        }
        if (invert)
            for (float& v : out)
                v = 1.0f - v;
        return out;
    }
    if (shape == MaskShape::None && !invert)
        return out;
    const double aspect = double(w) / h;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);
    QtConcurrent::blockingMap(rows, [&](int y) {
        float* m = out.data() + static_cast<size_t>(y) * w;
        const double v = (y + 0.5) / h;
        for (int x = 0; x < w; ++x)
            m[x] = static_cast<float>(valueAt((x + 0.5) / w, v, aspect));
    });
    return out;
}

QJsonObject LayerMask::toJson() const {
    QJsonObject o;
    o["shape"] = QLatin1String(maskShapeId(shape));
    if (shape == MaskShape::Brush) {
        QJsonArray arr;
        for (const auto& s : strokes)
            arr.append(s.toJson());
        o["strokes"] = arr;
    } else if (shape != MaskShape::None) {
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
    for (const auto& v : o.value("strokes").toArray())
        m.strokes.push_back(BrushStroke::fromJson(v.toObject()));
    return m;
}

double LayerMask::valueAt(double u, double v, double aspect) const {
    double m = 1.0;
    const double f = std::max(feather, 1e-4);
    switch (shape) {
    case MaskShape::None:
    case MaskShape::Brush: break;
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
    if (mask.shape == MaskShape::Brush && mask.strokes.empty() && !mask.invert)
        return true; // nothing painted yet
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
    if (!redaction.tracks.empty() || redaction.enabled)
        o["redaction"] = redaction.toJson();
    if (background.enabled)
        o["background"] = background.toJson();
    return o;
}

PhotoAdjustments PhotoAdjustments::fromJson(const QJsonObject& o) {
    PhotoAdjustments a;
    a.color = ColorGrade::fromJson(o);
    a.crop = rectFromJson(o.value("crop"), QRectF());
    for (const auto& v : o.value("layers").toArray())
        a.layers.push_back(PhotoLayer::fromJson(v.toObject()));
    if (o.contains("redaction"))
        a.redaction = Redaction::fromJson(o.value("redaction").toObject());
    if (o.contains("background"))
        a.background = BackgroundRemoval::fromJson(o.value("background").toObject());
    return a;
}

} // namespace lumora

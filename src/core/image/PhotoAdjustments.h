#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QUuid>
#include <vector>

#include "color/ColorGrade.h"

namespace lumora {

// Where a layer applies. Geometry is in normalised coordinates of the
// (uncropped, orientation corrected) photo, so it holds for the preview
// proxy and the full resolution image alike.
enum class MaskShape {
    None,      // the whole picture
    Rectangle, // `rect`, with a soft edge of `feather`
    Ellipse,   // the ellipse inscribed in `rect`
    Linear,    // a gradient across the line through the centre of `rect` at `angle`
    Brush,     // painted with brush strokes
};

// One brush stroke of a painted mask. Points are normalised photo
// coordinates; the radius is a fraction of the photo height, so a stroke
// covers the same part of the picture at any resolution.
struct BrushStroke {
    std::vector<QPointF> points;
    double radius = 0.04;
    double hardness = 0.5; // 0 = soft edge from the centre, 1 = hard edge
    double flow = 1.0;     // 0 .. 1 strength of the stroke
    bool erase = false;    // takes away from the mask instead of adding

    bool operator==(const BrushStroke&) const = default;
    QJsonObject toJson() const;
    static BrushStroke fromJson(const QJsonObject& o);
};

struct LayerMask {
    MaskShape shape = MaskShape::None;
    QRectF rect{0.25, 0.25, 0.5, 0.5};
    double feather = 0.15; // Rectangle / Ellipse: fraction of the shape's half size; Linear: of the diagonal
    double angle = 0.0;    // Linear: degrees; 0 = opaque at the top, fading downwards
    bool invert = false;
    std::vector<BrushStroke> strokes; // Brush

    bool operator==(const LayerMask&) const = default;
    QJsonObject toJson() const;
    static LayerMask fromJson(const QJsonObject& o);

    // Mask value 0..1 at a normalised point (not for Brush masks).
    double valueAt(double u, double v, double aspect) const;
    // The whole mask at a pixel size, row by row (any shape).
    std::vector<float> render(int width, int height) const;
};

enum class BlendMode { Normal, Multiply, Screen, Overlay, SoftLight, Darken, Lighten, Add, Difference };

QString blendModeId(BlendMode m);
BlendMode blendModeFromId(const QString& id);

enum class LayerKind {
    Adjustment, // colour settings applied to everything below, through the mask
    Image,      // another picture placed on top
};

struct PhotoLayer {
    QUuid id;
    LayerKind kind = LayerKind::Adjustment;
    QString name;
    bool visible = true;
    double opacity = 1.0; // 0 .. 1
    BlendMode blend = BlendMode::Normal;
    LayerMask mask;
    ColorGrade grade;     // Adjustment layers
    QString imagePath;    // Image layers
    double x = 0.0;       // Image layers: centre offset as a fraction of the photo size
    double y = 0.0;
    double scale = 1.0;   // 1 = fit inside the photo

    bool operator==(const PhotoLayer&) const = default;
    // True when the layer changes no pixel.
    bool isNoOp() const;
    QJsonObject toJson() const;
    static PhotoLayer fromJson(const QJsonObject& o);
};

// Non-destructive photo edit settings. The original file is never written;
// these values are stored in the project and applied on display / export:
// first the global colour settings, then the layers from bottom to top,
// then the crop.
struct PhotoAdjustments {
    ColorGrade color;
    // Crop rectangle in normalised coordinates of the (orientation corrected)
    // source image. A null rectangle means "no crop".
    QRectF crop;
    std::vector<PhotoLayer> layers; // bottom first

    bool hasLayerChanges() const;
    // Anything that changes pixel values (as opposed to only cropping).
    bool hasToneChanges() const { return !color.isIdentity() || hasLayerChanges(); }
    bool hasCrop() const { return !crop.isNull() && crop != QRectF(0, 0, 1, 1); }
    bool isIdentity() const { return !hasToneChanges() && !hasCrop(); }

    // Pixel rectangle of the crop for an image of the given size.
    QRect cropRect(QSize imageSize) const;

    QJsonObject toJson() const;
    static PhotoAdjustments fromJson(const QJsonObject& o);

    bool operator==(const PhotoAdjustments&) const = default;
};

} // namespace lumora

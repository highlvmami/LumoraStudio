#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <optional>
#include <vector>

#include "ffmpeg/FFmpeg.h"

struct AVFrame;

namespace lumora::motion {

// Single channel image in 0..255 floats, used for motion analysis. Analysis
// runs on small copies of the frames (a few hundred pixels wide), so it is
// fast and does not depend on the source resolution.
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<float> px;

    bool isNull() const { return width <= 0 || height <= 0; }
    float at(int x, int y) const;               // clamped to the edges
    float sample(double x, double y) const;     // bilinear, clamped
};

GrayImage grayFromImage(const QImage& image, int maxWidth = 0);

// Converts decoded frames to small grey images, keeping the scaler between
// calls.
class GrayConverter {
public:
    GrayImage convert(const AVFrame* frame, int maxWidth);

private:
    av::SwsPtr m_sws;
    int m_key[5] = {};
};

// Half size copy, low-pass filtered first.
GrayImage downsample(const GrayImage& image);

struct Pyramid {
    std::vector<GrayImage> levels; // levels[0] is full size
    static Pyramid build(const GrayImage& image, int levels);
    bool isNull() const { return levels.empty() || levels[0].isNull(); }
};

// Corners worth tracking (Shi-Tomasi), at most one per grid cell, inside
// `area` (pixels; empty = whole image).
std::vector<QPointF> goodFeatures(const GrayImage& image, const QRectF& area = {}, int gridX = 16, int gridY = 9);

// Pyramidal Lucas-Kanade: where each point of `from` went in `to`. Points
// that cannot be followed are nullopt.
std::vector<std::optional<QPointF>> trackPoints(const Pyramid& from, const Pyramid& to, const std::vector<QPointF>& points);

// x' = a x - b y + tx, y' = b x + a y + ty: shift, rotation and zoom.
struct Similarity {
    double a = 1.0, b = 0.0, tx = 0.0, ty = 0.0;
    QPointF map(const QPointF& p) const { return {a * p.x() - b * p.y() + tx, b * p.x() + a * p.y() + ty}; }
    double angleDegrees() const;
    double scale() const;
};

// Robust fit (RANSAC, then least squares on the inliers). Returns nullopt
// when fewer than `minInliers` pairs agree.
std::optional<Similarity> fitSimilarity(const std::vector<QPointF>& from, const std::vector<QPointF>& to,
                                        double threshold = 1.5, int minInliers = 6, int* inliers = nullptr);

// How the picture content moved from `prev` to `cur` (camera motion, the
// other way round).
std::optional<Similarity> estimateMotion(const Pyramid& prev, const Pyramid& cur);

// Follows a region from frame to frame by tracking the corners inside it.
class RegionTracker {
public:
    RegionTracker(const GrayImage& first, const QRectF& region, int levels = 4);
    // The region in the next frame. When the object cannot be followed the
    // last region is kept and `lost` is set.
    QRectF update(const GrayImage& frame, bool* lost = nullptr);
    QRectF region() const { return m_region; }

private:
    // Pulls the region back onto the object's appearance so small errors do
    // not add up from frame to frame.
    void refine(const GrayImage& frame);

    Pyramid m_prev;
    QRectF m_region;
    int m_levels;
    GrayImage m_template; // the object, slowly updated
};

} // namespace lumora::motion

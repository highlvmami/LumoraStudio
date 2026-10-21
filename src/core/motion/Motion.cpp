#include "Motion.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

extern "C" {
#include <libswscale/swscale.h>
}

namespace lumora::motion {

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

float GrayImage::at(int x, int y) const {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    return px[static_cast<size_t>(y) * width + x];
}

float GrayImage::sample(double x, double y) const {
    x = std::clamp(x, 0.0, width - 1.0);
    y = std::clamp(y, 0.0, height - 1.0);
    const int x0 = std::min(static_cast<int>(x), width - 2 < 0 ? 0 : width - 2);
    const int y0 = std::min(static_cast<int>(y), height - 2 < 0 ? 0 : height - 2);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
    const float* r0 = &px[static_cast<size_t>(y0) * width];
    const float* r1 = &px[static_cast<size_t>(y1) * width];
    const float top = r0[x0] + (r0[x1] - r0[x0]) * fx;
    const float bottom = r1[x0] + (r1[x1] - r1[x0]) * fx;
    return top + (bottom - top) * fy;
}

GrayImage grayFromImage(const QImage& image, int maxWidth) {
    if (image.isNull())
        return {};
    QImage g = image.convertToFormat(QImage::Format_Grayscale8);
    if (maxWidth > 0 && g.width() > maxWidth)
        g = g.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    GrayImage out;
    out.width = g.width();
    out.height = g.height();
    out.px.resize(static_cast<size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; ++y) {
        const uchar* line = g.constScanLine(y);
        std::transform(line, line + out.width, out.px.begin() + static_cast<long>(y) * out.width,
                       [](uchar v) { return static_cast<float>(v); });
    }
    return out;
}

GrayImage GrayConverter::convert(const AVFrame* f, int maxWidth) {
    if (!f || f->width <= 0 || f->height <= 0)
        return {};
    double dw = f->width;
    if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0)
        dw = dw * f->sample_aspect_ratio.num / f->sample_aspect_ratio.den;
    int w = static_cast<int>(std::lround(dw)), h = f->height;
    if (maxWidth > 0 && w > maxWidth) {
        h = std::max(2, static_cast<int>(std::lround(static_cast<double>(h) * maxWidth / w)));
        w = maxWidth;
    }
    const int key[5] = {f->width, f->height, f->format, w, h};
    if (!m_sws || !std::equal(std::begin(key), std::end(key), std::begin(m_key))) {
        m_sws.reset(sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), w, h, AV_PIX_FMT_GRAY8,
                                   SWS_AREA, nullptr, nullptr, nullptr));
        if (!m_sws)
            return {};
        std::copy(std::begin(key), std::end(key), std::begin(m_key));
    }
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h);
    uint8_t* dst[4] = {buf.data(), nullptr, nullptr, nullptr};
    int stride[4] = {w, 0, 0, 0};
    sws_scale(m_sws.get(), f->data, f->linesize, 0, f->height, dst, stride);
    GrayImage out;
    out.width = w;
    out.height = h;
    out.px.assign(buf.begin(), buf.end());
    return out;
}

GrayImage downsample(const GrayImage& in) {
    GrayImage out;
    out.width = std::max(1, in.width / 2);
    out.height = std::max(1, in.height / 2);
    out.px.resize(static_cast<size_t>(out.width) * out.height);
    // [1 2 1] x [1 2 1] filter centred on the even pixels.
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const int sx = 2 * x, sy = 2 * y;
            float s = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    s += in.at(sx + dx, sy + dy) * static_cast<float>((dx == 0 ? 2 : 1) * (dy == 0 ? 2 : 1));
            out.px[static_cast<size_t>(y) * out.width + x] = s / 16.0f;
        }
    return out;
}

Pyramid Pyramid::build(const GrayImage& image, int levels) {
    Pyramid p;
    if (image.isNull())
        return p;
    p.levels.push_back(image);
    for (int i = 1; i < levels && p.levels.back().width >= 32 && p.levels.back().height >= 32; ++i)
        p.levels.push_back(downsample(p.levels.back()));
    return p;
}

// ---------------------------------------------------------------------------
// Features and tracking
// ---------------------------------------------------------------------------

std::vector<QPointF> goodFeatures(const GrayImage& img, const QRectF& areaIn, int gridX, int gridY) {
    std::vector<QPointF> out;
    if (img.width < 16 || img.height < 16)
        return out;
    constexpr int border = 6;
    QRectF area = areaIn.isEmpty() ? QRectF(0, 0, img.width, img.height) : areaIn;
    area = area.intersected(QRectF(border, border, img.width - 2 * border, img.height - 2 * border));
    if (area.width() < 3 || area.height() < 3)
        return out;
    const int x0 = static_cast<int>(area.left()), y0 = static_cast<int>(area.top());
    const int x1 = static_cast<int>(area.right()), y1 = static_cast<int>(area.bottom());
    const int w = x1 - x0, h = y1 - y0;
    // Gradient products, then the smaller eigenvalue of the 5x5 structure
    // tensor at each pixel.
    std::vector<float> ixx(static_cast<size_t>(w) * h), ixy(ixx.size()), iyy(ixx.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int X = x0 + x, Y = y0 + y;
            const float gx = (img.at(X + 1, Y) - img.at(X - 1, Y)) * 0.5f;
            const float gy = (img.at(X, Y + 1) - img.at(X, Y - 1)) * 0.5f;
            const size_t i = static_cast<size_t>(y) * w + x;
            ixx[i] = gx * gx;
            ixy[i] = gx * gy;
            iyy[i] = gy * gy;
        }
    gridX = std::max(1, gridX);
    gridY = std::max(1, gridY);
    struct Best {
        float score = 0;
        int x = -1, y = -1;
    };
    std::vector<Best> cells(static_cast<size_t>(gridX) * gridY);
    float maxScore = 0;
    for (int y = 2; y < h - 2; ++y)
        for (int x = 2; x < w - 2; ++x) {
            float a = 0, b = 0, c = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                const size_t row = static_cast<size_t>(y + dy) * w;
                for (int dx = -2; dx <= 2; ++dx) {
                    a += ixx[row + x + dx];
                    b += ixy[row + x + dx];
                    c += iyy[row + x + dx];
                }
            }
            const float score = 0.5f * (a + c - std::sqrt((a - c) * (a - c) + 4 * b * b));
            maxScore = std::max(maxScore, score);
            const int cx = std::min(gridX - 1, x * gridX / w), cy = std::min(gridY - 1, y * gridY / h);
            Best& best = cells[static_cast<size_t>(cy) * gridX + cx];
            if (score > best.score)
                best = {score, x0 + x, y0 + y};
        }
    // Flat areas have nothing to hold on to.
    const float threshold = std::max(maxScore * 0.01f, 25.0f * 25.0f * 0.5f);
    for (const auto& c : cells)
        if (c.x >= 0 && c.score >= threshold)
            out.emplace_back(c.x, c.y);
    return out;
}

std::vector<std::optional<QPointF>> trackPoints(const Pyramid& from, const Pyramid& to, const std::vector<QPointF>& points) {
    std::vector<std::optional<QPointF>> out(points.size());
    if (from.isNull() || to.isNull())
        return out;
    const int levels = static_cast<int>(std::min(from.levels.size(), to.levels.size()));
    constexpr int half = 5;
    constexpr int maxIterations = 20;
    for (size_t k = 0; k < points.size(); ++k) {
        double gx = 0, gy = 0; // displacement guess at the current level
        bool ok = true;
        double residual = 0;
        for (int L = levels - 1; L >= 0 && ok; --L) {
            const GrayImage& I = from.levels[L];
            const GrayImage& J = to.levels[L];
            const double s = 1.0 / (1 << L);
            const double px = points[k].x() * s, py = points[k].y() * s;
            // Template gradients and structure tensor.
            double a = 0, b = 0, c = 0;
            float tix[(2 * half + 1) * (2 * half + 1)], tiy[(2 * half + 1) * (2 * half + 1)], tv[(2 * half + 1) * (2 * half + 1)];
            int n = 0;
            for (int dy = -half; dy <= half; ++dy)
                for (int dx = -half; dx <= half; ++dx, ++n) {
                    const double x = px + dx, y = py + dy;
                    const float ix = (I.sample(x + 1, y) - I.sample(x - 1, y)) * 0.5f;
                    const float iy = (I.sample(x, y + 1) - I.sample(x, y - 1)) * 0.5f;
                    tix[n] = ix;
                    tiy[n] = iy;
                    tv[n] = I.sample(x, y);
                    a += ix * ix;
                    b += ix * iy;
                    c += iy * iy;
                }
            const double det = a * c - b * b;
            const double minEig = 0.5 * (a + c - std::sqrt((a - c) * (a - c) + 4 * b * b)) / n;
            if (det <= 1e-6 || minEig < 1e-3) {
                ok = false;
                break;
            }
            double vx = 0, vy = 0;
            for (int it = 0; it < maxIterations; ++it) {
                double bx = 0, by = 0;
                residual = 0;
                n = 0;
                for (int dy = -half; dy <= half; ++dy)
                    for (int dx = -half; dx <= half; ++dx, ++n) {
                        const double diff = J.sample(px + gx + vx + dx, py + gy + vy + dy) - tv[n];
                        bx += diff * tix[n];
                        by += diff * tiy[n];
                        residual += std::abs(diff);
                    }
                const double ux = -(c * bx - b * by) / det;
                const double uy = -(a * by - b * bx) / det;
                vx += ux;
                vy += uy;
                if (ux * ux + uy * uy < 1e-4)
                    break;
            }
            residual /= n;
            gx += vx;
            gy += vy;
            if (L > 0) {
                gx *= 2;
                gy *= 2;
            }
        }
        if (!ok || residual > 18.0)
            continue;
        const QPointF p(points[k].x() + gx, points[k].y() + gy);
        const GrayImage& J0 = to.levels[0];
        if (p.x() < 0 || p.y() < 0 || p.x() > J0.width - 1 || p.y() > J0.height - 1)
            continue;
        out[k] = p;
    }
    return out;
}

double Similarity::angleDegrees() const { return std::atan2(b, a) * 180.0 / std::numbers::pi; }
double Similarity::scale() const { return std::hypot(a, b); }

namespace {

// Least squares similarity through the pairs selected by `use`.
std::optional<Similarity> leastSquares(const std::vector<QPointF>& from, const std::vector<QPointF>& to,
                                       const std::vector<int>& use) {
    if (use.size() < 2)
        return std::nullopt;
    double mx = 0, my = 0, nx = 0, ny = 0;
    for (int i : use) {
        mx += from[i].x();
        my += from[i].y();
        nx += to[i].x();
        ny += to[i].y();
    }
    const double cnt = static_cast<double>(use.size());
    mx /= cnt;
    my /= cnt;
    nx /= cnt;
    ny /= cnt;
    double sxx = 0, sab = 0, sba = 0;
    for (int i : use) {
        const double x = from[i].x() - mx, y = from[i].y() - my;
        const double u = to[i].x() - nx, v = to[i].y() - ny;
        sxx += x * x + y * y;
        sab += x * u + y * v;
        sba += x * v - y * u;
    }
    if (sxx < 1e-9)
        return std::nullopt;
    Similarity s;
    s.a = sab / sxx;
    s.b = sba / sxx;
    s.tx = nx - (s.a * mx - s.b * my);
    s.ty = ny - (s.b * mx + s.a * my);
    return s;
}

} // namespace

std::optional<Similarity> fitSimilarity(const std::vector<QPointF>& from, const std::vector<QPointF>& to, double threshold,
                                        int minInliers, int* inliersOut) {
    const int n = static_cast<int>(std::min(from.size(), to.size()));
    if (n < std::max(2, minInliers))
        return std::nullopt;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> pick(0, n - 1);
    const double t2 = threshold * threshold;
    std::vector<int> best;
    for (int it = 0; it < 300; ++it) {
        const int i = pick(rng), j = pick(rng);
        if (i == j)
            continue;
        auto s = leastSquares(from, to, {i, j});
        if (!s || std::abs(s->scale() - 1.0) > 0.5)
            continue;
        std::vector<int> inl;
        for (int k = 0; k < n; ++k) {
            const QPointF d = s->map(from[k]) - to[k];
            if (d.x() * d.x() + d.y() * d.y() < t2)
                inl.push_back(k);
        }
        if (inl.size() > best.size())
            best = std::move(inl);
        if (static_cast<int>(best.size()) > n * 9 / 10)
            break;
    }
    if (static_cast<int>(best.size()) < minInliers)
        return std::nullopt;
    auto s = leastSquares(from, to, best);
    if (s && inliersOut)
        *inliersOut = static_cast<int>(best.size());
    return s;
}

std::optional<Similarity> estimateMotion(const Pyramid& prev, const Pyramid& cur) {
    if (prev.isNull() || cur.isNull())
        return std::nullopt;
    const auto features = goodFeatures(prev.levels[0], {}, 20, 12);
    const auto tracked = trackPoints(prev, cur, features);
    std::vector<QPointF> a, b;
    for (size_t i = 0; i < features.size(); ++i)
        if (tracked[i]) {
            a.push_back(features[i]);
            b.push_back(*tracked[i]);
        }
    return fitSimilarity(a, b, 1.5, 8);
}

// ---------------------------------------------------------------------------
// RegionTracker
// ---------------------------------------------------------------------------

namespace {

GrayImage crop(const GrayImage& img, const QPointF& topLeft, int w, int h) {
    GrayImage out;
    out.width = w;
    out.height = h;
    out.px.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.px[static_cast<size_t>(y) * w + x] = img.sample(topLeft.x() + x, topLeft.y() + y);
    return out;
}

// Normalised cross-correlation of the template with the image at `topLeft`.
double ncc(const GrayImage& img, const GrayImage& tpl, const QPointF& topLeft) {
    double st = 0, si = 0, stt = 0, sii = 0, sti = 0;
    const double n = static_cast<double>(tpl.px.size());
    for (int y = 0; y < tpl.height; ++y)
        for (int x = 0; x < tpl.width; ++x) {
            const double t = tpl.px[static_cast<size_t>(y) * tpl.width + x];
            const double i = img.sample(topLeft.x() + x, topLeft.y() + y);
            st += t;
            si += i;
            stt += t * t;
            sii += i * i;
            sti += t * i;
        }
    const double vt = stt - st * st / n, vi = sii - si * si / n;
    if (vt <= 1e-6 || vi <= 1e-6)
        return 0;
    return (sti - st * si / n) / std::sqrt(vt * vi);
}

} // namespace

RegionTracker::RegionTracker(const GrayImage& first, const QRectF& region, int levels)
    : m_prev(Pyramid::build(first, levels)), m_region(region), m_levels(levels) {
    const int w = std::max(4, static_cast<int>(std::lround(region.width())));
    const int h = std::max(4, static_cast<int>(std::lround(region.height())));
    if (!first.isNull())
        m_template = crop(first, region.topLeft(), w, h);
}

void RegionTracker::refine(const GrayImage& frame) {
    if (m_template.isNull() || frame.isNull())
        return;
    // Search a few pixels around the region at the template's size, then a
    // parabola through the best score and its neighbours for sub-pixel.
    const QPointF centre = m_region.center();
    const QPointF base(centre.x() - m_template.width / 2.0, centre.y() - m_template.height / 2.0);
    constexpr int r = 3;
    double scores[2 * r + 1][2 * r + 1];
    int bx = 0, by = 0;
    double best = -2;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const double s = ncc(frame, m_template, base + QPointF(dx, dy));
            scores[dy + r][dx + r] = s;
            if (s > best) {
                best = s;
                bx = dx;
                by = dy;
            }
        }
    if (best < 0.6)
        return;
    auto parabola = [](double l, double c, double h) {
        const double d = l - 2 * c + h;
        return std::abs(d) < 1e-9 ? 0.0 : std::clamp(0.5 * (l - h) / d, -0.5, 0.5);
    };
    double fx = bx, fy = by;
    if (bx > -r && bx < r)
        fx += parabola(scores[by + r][bx + r - 1], best, scores[by + r][bx + r + 1]);
    if (by > -r && by < r)
        fy += parabola(scores[by + r - 1][bx + r], best, scores[by + r + 1][bx + r]);
    m_region.translate(fx, fy);
    // Follow slow changes of the object's look.
    if (best > 0.8) {
        const GrayImage now = crop(frame, base + QPointF(fx, fy), m_template.width, m_template.height);
        for (size_t i = 0; i < m_template.px.size(); ++i)
            m_template.px[i] = 0.9f * m_template.px[i] + 0.1f * now.px[i];
    }
}

QRectF RegionTracker::update(const GrayImage& frame, bool* lost) {
    Pyramid next = Pyramid::build(frame, m_levels);
    if (lost)
        *lost = true;
    if (m_prev.isNull() || next.isNull()) {
        m_prev = std::move(next);
        return m_region;
    }
    // Corners inside the region (a small grid so small objects still get a
    // few), then a robust shift-and-zoom fit; median shift when the fit fails.
    // Corners right at the edge often belong to the background.
    const QRectF inner = m_region.adjusted(m_region.width() * 0.1, m_region.height() * 0.1, -m_region.width() * 0.1,
                                           -m_region.height() * 0.1);
    auto features = goodFeatures(m_prev.levels[0], inner, 8, 8);
    const auto tracked = trackPoints(m_prev, next, features);
    std::vector<QPointF> a, b;
    for (size_t i = 0; i < features.size(); ++i)
        if (tracked[i]) {
            a.push_back(features[i]);
            b.push_back(*tracked[i]);
        }
    m_prev = std::move(next);
    if (a.size() < 3)
        return m_region;
    QPointF shift;
    double scale = 1.0;
    if (auto s = fitSimilarity(a, b, 2.0, std::max(3, static_cast<int>(a.size()) / 3))) {
        const QPointF c = m_region.center();
        shift = s->map(c) - c;
        scale = std::clamp(s->scale(), 0.9, 1.1);
    } else {
        std::vector<double> dx, dy;
        for (size_t i = 0; i < a.size(); ++i) {
            dx.push_back(b[i].x() - a[i].x());
            dy.push_back(b[i].y() - a[i].y());
        }
        std::nth_element(dx.begin(), dx.begin() + static_cast<long>(dx.size() / 2), dx.end());
        std::nth_element(dy.begin(), dy.begin() + static_cast<long>(dy.size() / 2), dy.end());
        shift = QPointF(dx[dx.size() / 2], dy[dy.size() / 2]);
    }
    const QPointF c = m_region.center() + shift;
    const QSizeF size = m_region.size() * scale;
    m_region = QRectF(c.x() - size.width() / 2, c.y() - size.height() / 2, size.width(), size.height());
    refine(frame);
    if (lost)
        *lost = false;
    return m_region;
}

} // namespace lumora::motion

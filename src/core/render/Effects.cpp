#include "Effects.h"

#include <QtConcurrent/QtConcurrentMap>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace lumora::fx {

namespace {

// Runs body(y0, y1) over bands of rows in parallel.
template <typename F>
void parallelRows(int height, F&& body) {
    const int bands = std::clamp(height / 32, 1, 64);
    std::vector<int> ids(bands);
    std::iota(ids.begin(), ids.end(), 0);
    QtConcurrent::blockingMap(ids, [&](int i) {
        const int y0 = static_cast<int>(static_cast<long long>(height) * i / bands);
        const int y1 = static_cast<int>(static_cast<long long>(height) * (i + 1) / bands);
        body(y0, y1);
    });
}

double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / std::max(1e-9, e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// Box filter of radius r over one channel (stride `step` floats, `channels`
// interleaved), separable running sums, edges clamped.
void boxFilter(std::vector<float>& data, int w, int h, int r) {
    if (r <= 0 || w <= 0 || h <= 0)
        return;
    std::vector<float> tmp(data.size());
    const float norm = 1.0f / (2 * r + 1);
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* src = data.data() + static_cast<size_t>(y) * w;
            float* dst = tmp.data() + static_cast<size_t>(y) * w;
            double sum = 0;
            for (int k = -r; k <= r; ++k)
                sum += src[std::clamp(k, 0, w - 1)];
            for (int x = 0; x < w; ++x) {
                dst[x] = static_cast<float>(sum * norm);
                sum += src[std::min(x + r + 1, w - 1)] - src[std::max(x - r, 0)];
            }
        }
    });
    // Vertical pass, a band of columns at a time.
    const int bands = std::clamp(w / 64, 1, 64);
    std::vector<int> ids(bands);
    std::iota(ids.begin(), ids.end(), 0);
    QtConcurrent::blockingMap(ids, [&](int i) {
        const int x0 = w * i / bands, x1 = w * (i + 1) / bands;
        std::vector<double> sum(x1 - x0, 0.0);
        for (int k = -r; k <= r; ++k) {
            const float* src = tmp.data() + static_cast<size_t>(std::clamp(k, 0, h - 1)) * w;
            for (int x = x0; x < x1; ++x)
                sum[x - x0] += src[x];
        }
        for (int y = 0; y < h; ++y) {
            float* dst = data.data() + static_cast<size_t>(y) * w;
            const float* add = tmp.data() + static_cast<size_t>(std::min(y + r + 1, h - 1)) * w;
            const float* sub = tmp.data() + static_cast<size_t>(std::max(y - r, 0)) * w;
            for (int x = x0; x < x1; ++x) {
                dst[x] = static_cast<float>(sum[x - x0] * norm);
                sum[x - x0] += add[x] - sub[x];
            }
        }
    });
}

std::vector<float> channel(const FloatImage& img, int c) {
    std::vector<float> out(static_cast<size_t>(img.width) * img.height);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = img.px[i * 4 + c];
    return out;
}

void setChannel(FloatImage& img, int c, const std::vector<float>& v) {
    for (size_t i = 0; i < v.size(); ++i)
        img.px[i * 4 + c] = v[i];
}

std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Bilinear sample of a single-channel picture.
float sample1(const std::vector<float>& img, int w, int h, float x, float y) {
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float fx = x - x0, fy = y - y0;
    const float a = img[static_cast<size_t>(y0) * w + x0], b = img[static_cast<size_t>(y0) * w + x1];
    const float c = img[static_cast<size_t>(y1) * w + x0], d = img[static_cast<size_t>(y1) * w + x1];
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

void sample4(const FloatImage& img, float x, float y, float out[4]) {
    x = std::clamp(x, 0.0f, static_cast<float>(img.width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(img.height - 1));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
    const float fx = x - x0, fy = y - y0;
    const float* a = img.row(y0) + x0 * 4;
    const float* b = img.row(y0) + x1 * 4;
    const float* c = img.row(y1) + x0 * 4;
    const float* d = img.row(y1) + x1 * 4;
    for (int k = 0; k < 4; ++k)
        out[k] = (a[k] + (b[k] - a[k]) * fx) * (1 - fy) + (c[k] + (d[k] - c[k]) * fx) * fy;
}

std::vector<float> downsample(const std::vector<float>& img, int w, int h, int& nw, int& nh) {
    nw = std::max(1, w / 2);
    nh = std::max(1, h / 2);
    std::vector<float> out(static_cast<size_t>(nw) * nh);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            const int sx = std::min(2 * x, w - 1), sy = std::min(2 * y, h - 1);
            const int sx1 = std::min(sx + 1, w - 1), sy1 = std::min(sy + 1, h - 1);
            out[static_cast<size_t>(y) * nw + x] = 0.25f * (img[static_cast<size_t>(sy) * w + sx] + img[static_cast<size_t>(sy) * w + sx1] +
                                                           img[static_cast<size_t>(sy1) * w + sx] + img[static_cast<size_t>(sy1) * w + sx1]);
        }
    return out;
}

// Grey picture of at most `maxSide` pixels on its long side.
std::vector<float> greySmall(const QImage& img, int maxSide, int& w, int& h) {
    QImage g = img;
    if (std::max(img.width(), img.height()) > maxSide)
        g = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    g = g.convertToFormat(QImage::Format_Grayscale8);
    w = g.width();
    h = g.height();
    std::vector<float> out(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const uchar* line = g.constScanLine(y);
        for (int x = 0; x < w; ++x)
            out[static_cast<size_t>(y) * w + x] = line[x] / 255.0f;
    }
    return out;
}

} // namespace

FloatImage toFloat(const QImage& image) {
    FloatImage out;
    if (image.isNull())
        return out;
    const QImage src = image.format() == QImage::Format_RGBA64 ? image : image.convertToFormat(QImage::Format_RGBA64);
    out.width = src.width();
    out.height = src.height();
    out.px.resize(static_cast<size_t>(out.width) * out.height * 4);
    parallelRows(out.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const auto* line = reinterpret_cast<const quint16*>(src.constScanLine(y));
            float* dst = out.row(y);
            for (int i = 0; i < out.width * 4; ++i)
                dst[i] = line[i] / 65535.0f;
        }
    });
    return out;
}

QImage fromFloat(const FloatImage& image, QImage::Format format) {
    if (image.isNull())
        return {};
    QImage out(image.width, image.height, QImage::Format_RGBA64);
    parallelRows(image.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            auto* line = reinterpret_cast<quint16*>(out.scanLine(y));
            const float* src = image.row(y);
            for (int i = 0; i < image.width * 4; ++i)
                line[i] = static_cast<quint16>(std::lround(std::clamp(src[i], 0.0f, 1.0f) * 65535.0f));
        }
    });
    return format == QImage::Format_RGBA64 ? out : out.convertToFormat(format);
}

void chromaKey(FloatImage& img, const ChromaKey& key) {
    const float kr = key.color.redF(), kg = key.color.greenF(), kb = key.color.blueF();
    // Chroma of the key (BT.709 Cb / Cr).
    const float kcb = -0.1146f * kr - 0.3854f * kg + 0.5f * kb;
    const float kcr = 0.5f * kr - 0.4542f * kg - 0.0458f * kb;
    const float kmag = std::max(0.05f, std::hypot(kcb, kcr));
    const float tol = static_cast<float>(key.tolerance);
    const float soft = static_cast<float>(std::max(0.01, key.softness));
    const int dominant = (kg >= kr && kg >= kb) ? 1 : (kb >= kr ? 2 : 0);
    const float spill = static_cast<float>(key.spill);
    parallelRows(img.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* p = img.row(y);
            for (int x = 0; x < img.width; ++x, p += 4) {
                const float r = p[0], g = p[1], b = p[2];
                const float cb = -0.1146f * r - 0.3854f * g + 0.5f * b;
                const float cr = 0.5f * r - 0.4542f * g - 0.0458f * b;
                const float d = std::hypot(cb - kcb, cr - kcr) / kmag;
                const float a = static_cast<float>(smoothstep(tol, tol + soft, d));
                p[3] *= a;
                if (spill > 0) {
                    // Pull the key channel down to the larger of the other two.
                    const float other = dominant == 1 ? std::max(r, b) : dominant == 2 ? std::max(r, g) : std::max(g, b);
                    float& c = p[dominant];
                    if (c > other)
                        c -= spill * (c - other);
                }
            }
        }
    });
}

void denoise(FloatImage& img, double amount) {
    if (amount <= 0 || img.isNull())
        return;
    // Self-guided filter (He et al.): smooths flat areas, keeps edges.
    const double scale = std::max(1.0, img.height / 1080.0);
    const int r = std::max(1, static_cast<int>(std::lround((1 + 3 * amount) * scale)));
    const float eps = static_cast<float>(std::pow(0.01 + 0.07 * amount, 2));
    const size_t n = static_cast<size_t>(img.width) * img.height;
    for (int c = 0; c < 3; ++c) {
        std::vector<float> I = channel(img, c);
        std::vector<float> meanI = I, meanII(n);
        for (size_t i = 0; i < n; ++i)
            meanII[i] = I[i] * I[i];
        boxFilter(meanI, img.width, img.height, r);
        boxFilter(meanII, img.width, img.height, r);
        std::vector<float> a(n), b(n);
        for (size_t i = 0; i < n; ++i) {
            const float var = std::max(0.0f, meanII[i] - meanI[i] * meanI[i]);
            a[i] = var / (var + eps);
            b[i] = meanI[i] - a[i] * meanI[i];
        }
        boxFilter(a, img.width, img.height, r);
        boxFilter(b, img.width, img.height, r);
        for (size_t i = 0; i < n; ++i)
            I[i] = a[i] * I[i] + b[i];
        setChannel(img, c, I);
    }
}

void blur(FloatImage& img, double radius) {
    if (radius < 0.5 || img.isNull())
        return;
    // Three box passes are close to a Gaussian.
    const int r = std::max(1, static_cast<int>(std::lround(radius / 1.7)));
    for (int c = 0; c < 4; ++c) {
        std::vector<float> ch = channel(img, c);
        for (int pass = 0; pass < 3; ++pass)
            boxFilter(ch, img.width, img.height, r);
        setChannel(img, c, ch);
    }
}

void sharpen(FloatImage& img, double amount) {
    if (amount <= 0 || img.isNull())
        return;
    const int r = std::max(1, static_cast<int>(std::lround(img.height / 1080.0 * 1.5)));
    const float k = static_cast<float>(amount * 1.5);
    for (int c = 0; c < 3; ++c) {
        std::vector<float> I = channel(img, c), B = I;
        boxFilter(B, img.width, img.height, r);
        for (size_t i = 0; i < I.size(); ++i)
            I[i] = I[i] + k * (I[i] - B[i]);
        setChannel(img, c, I);
    }
}

void vignette(FloatImage& img, double amount) {
    if (amount <= 0 || img.isNull())
        return;
    const float cx = img.width / 2.0f, cy = img.height / 2.0f;
    parallelRows(img.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* p = img.row(y);
            const float dy = (y + 0.5f - cy) / cy;
            for (int x = 0; x < img.width; ++x, p += 4) {
                const float dx = (x + 0.5f - cx) / cx;
                const double d = std::sqrt((dx * dx + dy * dy) / 2.0);
                const float f = static_cast<float>(1.0 - amount * smoothstep(0.35, 1.0, d));
                p[0] *= f;
                p[1] *= f;
                p[2] *= f;
            }
        }
    });
}

void grain(FloatImage& img, double amount, std::uint32_t seed) {
    if (amount <= 0 || img.isNull())
        return;
    const float k = static_cast<float>(amount * 0.12);
    parallelRows(img.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* p = img.row(y);
            for (int x = 0; x < img.width; ++x, p += 4) {
                const std::uint32_t h = hash32(static_cast<std::uint32_t>(y * 7919 + x) ^ (seed * 2654435761U));
                // Sum of two uniforms: a soft, roughly Gaussian grain.
                const float n = ((h & 0xffff) / 65535.0f + (h >> 16) / 65535.0f) - 1.0f;
                // Strongest in the mid tones, like film.
                const float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
                const float g = k * n * (0.4f + 2.4f * lum * (1 - lum));
                p[0] += g;
                p[1] += g;
                p[2] += g;
            }
        }
    });
}

void applyPictureEffects(QImage& image, const PictureEffects& e, std::uint32_t seed) {
    if (e.isIdentity() || image.isNull())
        return;
    QImage::Format format = image.format();
    if (e.chromaKey.enabled) {
        // The key needs an alpha channel.
        if (format == QImage::Format_RGB32)
            format = QImage::Format_ARGB32;
        else if (format == QImage::Format_RGBX64)
            format = QImage::Format_RGBA64;
        else if (format == QImage::Format_RGB888 || format == QImage::Format_RGBX8888)
            format = QImage::Format_ARGB32;
    }
    FloatImage f = toFloat(image);
    denoise(f, e.denoise);
    blur(f, e.blur * 0.02 * f.height);
    sharpen(f, e.sharpen);
    if (e.chromaKey.enabled)
        chromaKey(f, e.chromaKey);
    vignette(f, e.vignette);
    grain(f, e.grain, seed);
    image = fromFloat(f, format);
}

// ---------------------------------------------------------------------------
// Frame interpolation
// ---------------------------------------------------------------------------

QImage blendFrames(const QImage& a, const QImage& b, double w) {
    if (a.isNull() || b.isNull() || a.size() != b.size())
        return a;
    if (w <= 0.0)
        return a;
    if (w >= 1.0)
        return b.convertToFormat(a.format());
    const QImage bb = b.format() == a.format() ? b : b.convertToFormat(a.format());
    QImage out = a.copy();
    const int bytes = static_cast<int>(out.bytesPerLine());
    const bool sixteen = a.depth() == 64;
    const float fw = static_cast<float>(w);
    parallelRows(out.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            if (sixteen) {
                auto* d = reinterpret_cast<quint16*>(out.scanLine(y));
                const auto* s = reinterpret_cast<const quint16*>(bb.constScanLine(y));
                for (int i = 0; i < bytes / 2; ++i)
                    d[i] = static_cast<quint16>(d[i] + (s[i] - d[i]) * fw + 0.5f);
            } else {
                uchar* d = out.scanLine(y);
                const uchar* s = bb.constScanLine(y);
                for (int i = 0; i < bytes; ++i)
                    d[i] = static_cast<uchar>(d[i] + (s[i] - d[i]) * fw + 0.5f);
            }
        }
    });
    return out;
}

void opticalFlow(const std::vector<float>& a, const std::vector<float>& b, int width, int height, std::vector<float>& flowX,
                 std::vector<float>& flowY) {
    // Coarse-to-fine Lucas-Kanade: at each pyramid level the second picture
    // is warped by the flow so far and the remaining motion is solved in a
    // 7x7 window around every pixel.
    std::vector<std::vector<float>> pa{a}, pb{b};
    std::vector<std::pair<int, int>> sizes{{width, height}};
    while (pa.size() < 5 && std::min(sizes.back().first, sizes.back().second) >= 24) {
        int nw = 0, nh = 0;
        pa.push_back(downsample(pa.back(), sizes.back().first, sizes.back().second, nw, nh));
        pb.push_back(downsample(pb.back(), sizes.back().first, sizes.back().second, nw, nh));
        sizes.emplace_back(nw, nh);
    }
    std::vector<float> fx, fy;
    for (int level = static_cast<int>(pa.size()) - 1; level >= 0; --level) {
        const int w = sizes[level].first, h = sizes[level].second;
        const size_t n = static_cast<size_t>(w) * h;
        if (fx.empty()) {
            fx.assign(n, 0.0f);
            fy.assign(n, 0.0f);
        } else {
            // Up from the coarser level: twice as many pixels, twice the motion.
            const int pw = sizes[level + 1].first, ph = sizes[level + 1].second;
            std::vector<float> ux(n), uy(n);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const float sx = std::min(x / 2.0f, pw - 1.0f), sy = std::min(y / 2.0f, ph - 1.0f);
                    ux[static_cast<size_t>(y) * w + x] = 2 * sample1(fx, pw, ph, sx, sy);
                    uy[static_cast<size_t>(y) * w + x] = 2 * sample1(fy, pw, ph, sx, sy);
                }
            fx.swap(ux);
            fy.swap(uy);
        }
        const std::vector<float>& A = pa[level];
        const std::vector<float>& B = pb[level];
        std::vector<float> ixx(n), ixy(n), iyy(n), ixt(n), iyt(n);
        for (int iter = 0; iter < 3; ++iter) {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const size_t i = static_cast<size_t>(y) * w + x;
                    const float bx = x + fx[i], by = y + fy[i];
                    const float bw = sample1(B, w, h, bx, by);
                    // Gradients of the average of both pictures.
                    const float gx = 0.25f * (A[static_cast<size_t>(y) * w + std::min(x + 1, w - 1)] -
                                              A[static_cast<size_t>(y) * w + std::max(x - 1, 0)]) +
                                     0.25f * (sample1(B, w, h, bx + 1, by) - sample1(B, w, h, bx - 1, by));
                    const float gy = 0.25f * (A[static_cast<size_t>(std::min(y + 1, h - 1)) * w + x] -
                                              A[static_cast<size_t>(std::max(y - 1, 0)) * w + x]) +
                                     0.25f * (sample1(B, w, h, bx, by + 1) - sample1(B, w, h, bx, by - 1));
                    const float it = bw - A[i];
                    ixx[i] = gx * gx;
                    ixy[i] = gx * gy;
                    iyy[i] = gy * gy;
                    ixt[i] = gx * it;
                    iyt[i] = gy * it;
                }
            for (auto* v : {&ixx, &ixy, &iyy, &ixt, &iyt})
                boxFilter(*v, w, h, 3);
            for (size_t i = 0; i < n; ++i) {
                // Regularised so flat areas (no texture) do not move.
                const float sxx = ixx[i] + 1e-4f, syy = iyy[i] + 1e-4f, sxy = ixy[i];
                const float det = sxx * syy - sxy * sxy;
                if (det < 1e-9f)
                    continue;
                const float du = (-syy * ixt[i] + sxy * iyt[i]) / det;
                const float dv = (sxy * ixt[i] - sxx * iyt[i]) / det;
                fx[i] += std::clamp(du, -2.0f, 2.0f);
                fy[i] += std::clamp(dv, -2.0f, 2.0f);
            }
        }
        boxFilter(fx, w, h, 1);
        boxFilter(fy, w, h, 1);
    }
    flowX = std::move(fx);
    flowY = std::move(fy);
}

QImage interpolateFrames(const QImage& a, const QImage& b, double w) {
    if (a.isNull() || b.isNull() || a.size() != b.size())
        return a;
    if (w <= 0.0)
        return a;
    if (w >= 1.0)
        return b.convertToFormat(a.format());
    // Motion measured on small grey pictures (fast), applied at full size.
    int sw = 0, sh = 0;
    const std::vector<float> ga = greySmall(a, 360, sw, sh);
    const std::vector<float> gb = greySmall(b, 360, sw, sh);
    std::vector<float> fx, fy, bx, by;
    opticalFlow(ga, gb, sw, sh, fx, fy); // a -> b
    opticalFlow(gb, ga, sw, sh, bx, by); // b -> a
    const FloatImage fa = toFloat(a), fb = toFloat(b);
    FloatImage out;
    out.width = fa.width;
    out.height = fa.height;
    out.px.resize(fa.px.size());
    const float scale = static_cast<float>(fa.width) / sw;
    const float fw = static_cast<float>(w);
    parallelRows(out.height, [&](int y0, int y1) {
        float pa[4], pb[4];
        for (int y = y0; y < y1; ++y) {
            float* d = out.row(y);
            for (int x = 0; x < out.width; ++x, d += 4) {
                const float sx = std::min((x + 0.5f) / scale - 0.5f, sw - 1.0f);
                const float sy = std::min((y + 0.5f) / scale - 0.5f, sh - 1.0f);
                // Where this point was in a and will be in b.
                const float ax = x - fw * sample1(fx, sw, sh, sx, sy) * scale;
                const float ay = y - fw * sample1(fy, sw, sh, sx, sy) * scale;
                const float bxp = x - (1 - fw) * sample1(bx, sw, sh, sx, sy) * scale;
                const float byp = y - (1 - fw) * sample1(by, sw, sh, sx, sy) * scale;
                sample4(fa, ax, ay, pa);
                sample4(fb, bxp, byp, pb);
                for (int k = 0; k < 4; ++k)
                    d[k] = pa[k] * (1 - fw) + pb[k] * fw;
            }
        }
    });
    return fromFloat(out, a.format());
}

} // namespace lumora::fx

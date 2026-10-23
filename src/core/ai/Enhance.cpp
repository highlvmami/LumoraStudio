#include "Enhance.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "ai/Onnx.h"

namespace lumora::ai {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::ai)
};

QImage toRgba64(const QImage& image) {
    if (image.format() == QImage::Format_RGBA64)
        return image;
    QImage out = image.convertToFormat(QImage::Format_RGBA64);
    out.setColorSpace(image.colorSpace());
    return out;
}

// Planar RGB (or BGR) tensor of `rect` of an RGBA64 image, values 0..1
// mapped through value * mul + add, padded by repeating the edge pixels to
// `w` x `h`.
Tensor tensorOf(const QImage& rgba64, QRect rect, int w, int h, bool bgr, float mul, float add) {
    Tensor t;
    t.shape = {1, 3, h, w};
    const size_t plane = static_cast<size_t>(w) * h;
    t.data.resize(3 * plane);
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const quint16*>(rgba64.constScanLine(rect.top() + std::min(y, rect.height() - 1)));
        for (int x = 0; x < w; ++x) {
            const quint16* px = line + 4 * (rect.left() + std::min(x, rect.width() - 1));
            for (int c = 0; c < 3; ++c)
                t.data[c * plane + static_cast<size_t>(y) * w + x] = px[bgr ? 2 - c : c] / 65535.0f * mul + add;
        }
    }
    return t;
}

// Writes `rect` of planar network output `t` (values v * mul + add give
// 0..1) into the RGBA64 `out` at `to`, keeping `out`'s alpha.
void writeTensor(QImage& out, QPoint to, const Tensor& t, QRect rect, bool bgr, float mul, float add) {
    const int w = static_cast<int>(t.dim(3)), h = static_cast<int>(t.dim(2));
    const size_t plane = static_cast<size_t>(w) * h;
    for (int y = 0; y < rect.height(); ++y) {
        auto* line = reinterpret_cast<quint16*>(out.scanLine(to.y() + y));
        for (int x = 0; x < rect.width(); ++x) {
            quint16* px = line + 4 * (to.x() + x);
            const size_t i = static_cast<size_t>(rect.top() + y) * w + rect.left() + x;
            for (int c = 0; c < 3; ++c) {
                const float v = std::clamp(t.data[(bgr ? 2 - c : c) * plane + i] * mul + add, 0.0f, 1.0f);
                px[c] = static_cast<quint16>(std::lround(v * 65535.0f));
            }
        }
    }
}

bool outputMatches(const std::vector<Tensor>& out, int w, int h) {
    return !out.empty() && out.front().dim(1) == 3 && out.front().dim(2) == h && out.front().dim(3) == w &&
           static_cast<int64_t>(out.front().data.size()) >= 3LL * w * h;
}

} // namespace

QImage upscale(const QImage& image, int factor, const EnhanceProgress& progress, QString* error) {
    if (image.isNull())
        return {};
    factor = factor >= 4 ? 4 : 2;
    auto net = visionNetwork(factor == 4 ? QStringLiteral("upscale-x4") : QStringLiteral("upscale-x2"), error);
    if (!net)
        return {};
    const QImage src = toRgba64(image);
    QImage out(src.width() * factor, src.height() * factor, QImage::Format_RGBA64);
    out.setColorSpace(image.colorSpace());
    // The alpha channel is only enlarged smoothly; the network sees colour.
    if (src.hasAlphaChannel()) {
        const QImage alpha = src.scaled(out.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y = 0; y < out.height(); ++y) {
            auto* d = reinterpret_cast<quint16*>(out.scanLine(y));
            const auto* a = reinterpret_cast<const quint16*>(alpha.constScanLine(y));
            for (int x = 0; x < out.width(); ++x)
                d[4 * x + 3] = a[4 * x + 3];
        }
    } else {
        out.fill(Qt::black);
    }
    // In tiles, each with a margin the network can look at, so memory stays
    // small and there are no seams; only the middle of each tile is kept.
    constexpr int kTile = 192, kMargin = 16;
    const int cols = (src.width() + kTile - 1) / kTile, rows = (src.height() + kTile - 1) / kTile;
    for (int ty = 0; ty < rows; ++ty) {
        for (int tx = 0; tx < cols; ++tx) {
            const QRect core = QRect(tx * kTile, ty * kTile, kTile, kTile).intersected(src.rect());
            const QRect area = core.adjusted(-kMargin, -kMargin, kMargin, kMargin).intersected(src.rect());
            // Sizes divisible by 4 suit every network.
            const int w = (area.width() + 3) / 4 * 4, h = (area.height() + 3) / 4 * 4;
            const auto result = net->run(tensorOf(src, area, w, h, false, 1.0f, 0.0f), 1, error);
            if (!outputMatches(result, w * factor, h * factor)) {
                if (error && error->isEmpty())
                    *error = Tr::tr("The enlargement model gave an unexpected result.");
                return {};
            }
            const QRect keep((core.left() - area.left()) * factor, (core.top() - area.top()) * factor, core.width() * factor,
                             core.height() * factor);
            writeTensor(out, core.topLeft() * factor, result.front(), keep, false, 1.0f, 0.0f);
            if (progress && !progress(static_cast<double>(ty * cols + tx + 1) / (rows * cols)))
                return {};
        }
    }
    return out;
}

QTransform faceAlignment(const Detection& face) {
    // The FFHQ face template at 512 x 512 (eyes, nose tip, mouth corners).
    static const QPointF kTemplate[5] = {{192.98, 239.95}, {318.90, 240.19}, {256.63, 314.02}, {201.26, 371.41}, {313.09, 371.15}};
    QPointF pc, qc;
    for (int i = 0; i < 5; ++i) {
        pc += face.landmarks[i] / 5.0;
        qc += kTemplate[i] / 5.0;
    }
    double num = 0.0, cross = 0.0, den = 0.0;
    for (int i = 0; i < 5; ++i) {
        const QPointF p = face.landmarks[i] - pc, q = kTemplate[i] - qc;
        num += p.x() * q.x() + p.y() * q.y();
        cross += p.x() * q.y() - p.y() * q.x();
        den += p.x() * p.x() + p.y() * p.y();
    }
    if (den <= 1e-9)
        return {};
    const double a = num / den, b = cross / den;
    return QTransform(a, b, -b, a, qc.x() - (a * pc.x() - b * pc.y()), qc.y() - (b * pc.x() + a * pc.y()));
}

QImage restoreFaces(const QImage& image, double strength, const EnhanceProgress& progress, QString* error) {
    if (image.isNull())
        return {};
    auto net = visionNetwork(QStringLiteral("face-restore"), error);
    if (!net)
        return {};
    const auto faces = detectFaces(image, 0.6f, error);
    if (error && !error->isEmpty())
        return {};
    QImage out = toRgba64(image).copy();
    out.setColorSpace(image.colorSpace());
    strength = std::clamp(strength, 0.0, 1.0);
    // Faces only a few pixels wide have nothing to restore.
    std::vector<Detection> todo;
    for (const auto& f : faces)
        if (f.rect.width() >= 16)
            todo.push_back(f);
    constexpr int kSize = 512;
    // Restored faces fade into the photo in an oval around the face (hair
    // and background at the corners of the square stay as they were).
    QImage fade(kSize, kSize, QImage::Format_Grayscale8);
    for (int y = 0; y < kSize; ++y) {
        uchar* line = fade.scanLine(y);
        for (int x = 0; x < kSize; ++x) {
            const double d = std::hypot((x - 256.0) / 220.0, (y - 285.0) / 250.0);
            const double v = std::clamp((1.0 - d) / 0.3, 0.0, 1.0);
            line[x] = static_cast<uchar>(std::lround(v * v * (3 - 2 * v) * 255.0));
        }
    }
    for (size_t n = 0; n < todo.size(); ++n) {
        const QTransform toFace = faceAlignment(todo[n]);
        if (!toFace.isInvertible())
            continue;
        QImage crop(kSize, kSize, QImage::Format_RGBA64);
        crop.fill(Qt::black);
        {
            QPainter p(&crop);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setTransform(toFace);
            p.drawImage(0, 0, out);
        }
        // GFPGAN: RGB scaled to -1 .. 1.
        const auto result = net->run(tensorOf(crop, crop.rect(), kSize, kSize, false, 2.0f, -1.0f), 1, error);
        if (!outputMatches(result, kSize, kSize)) {
            if (error && error->isEmpty())
                *error = Tr::tr("The face restoration model gave an unexpected result.");
            return {};
        }
        QImage restored(kSize, kSize, QImage::Format_RGBA64);
        writeTensor(restored, {0, 0}, result.front(), crop.rect(), false, 0.5f, 0.5f);
        for (int y = 0; y < kSize; ++y) {
            auto* px = reinterpret_cast<quint16*>(restored.scanLine(y));
            const uchar* a = fade.constScanLine(y);
            for (int x = 0; x < kSize; ++x)
                px[4 * x + 3] = static_cast<quint16>(std::lround(a[x] * 257.0 * strength));
        }
        {
            QPainter p(&out);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.setCompositionMode(QPainter::CompositionMode_SourceAtop); // the photo's own transparency stays
            p.setTransform(toFace.inverted());
            p.drawImage(0, 0, restored);
        }
        if (progress && !progress(static_cast<double>(n + 1) / todo.size()))
            return {};
    }
    return out;
}

QImage inpaint(const QImage& image, const QImage& maskIn, const EnhanceProgress& progress, QString* error) {
    if (image.isNull())
        return {};
    auto net = visionNetwork(QStringLiteral("inpaint"), error);
    if (!net)
        return {};
    QImage out = toRgba64(image).copy();
    out.setColorSpace(image.colorSpace());
    QImage mask = maskIn.convertToFormat(QImage::Format_Grayscale8);
    if (mask.size() != out.size())
        mask = mask.scaled(out.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    // Separate patches of the mask are filled one by one, each from its own
    // surroundings, so two small objects far apart keep their detail.
    constexpr int kGrid = 256;
    const double cell = std::max(1.0, std::max(out.width(), out.height()) / static_cast<double>(kGrid));
    const int gw = static_cast<int>(std::ceil(out.width() / cell)), gh = static_cast<int>(std::ceil(out.height() / cell));
    std::vector<int> label(static_cast<size_t>(gw) * gh, -1);
    std::vector<char> on(label.size(), 0);
    for (int y = 0; y < out.height(); ++y) {
        const uchar* line = mask.constScanLine(y);
        for (int x = 0; x < out.width(); ++x)
            if (line[x] > 8)
                on[static_cast<size_t>(y / cell) * gw + static_cast<size_t>(x / cell)] = 1;
    }
    std::vector<QRect> patches;
    for (int start = 0; start < static_cast<int>(on.size()); ++start) {
        if (!on[start] || label[start] >= 0)
            continue;
        QRect box;
        std::vector<int> stack{start};
        label[start] = static_cast<int>(patches.size());
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int x = i % gw, y = i / gw;
            box |= QRect(x, y, 1, 1);
            // Patches a couple of cells apart count as one.
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh)
                        continue;
                    const int j = ny * gw + nx;
                    if (on[j] && label[j] < 0) {
                        label[j] = label[start];
                        stack.push_back(j);
                    }
                }
        }
        patches.push_back(QRect(static_cast<int>(box.left() * cell), static_cast<int>(box.top() * cell),
                                static_cast<int>(std::ceil(box.width() * cell)), static_cast<int>(std::ceil(box.height() * cell)))
                              .intersected(out.rect()));
    }

    constexpr int kSize = 512;
    for (size_t n = 0; n < patches.size(); ++n) {
        const QRect box = patches[n];
        // Enough of the surroundings to continue the picture: twice the
        // object, at least the network's own size where the photo allows.
        const int side = std::max({kSize, box.width() * 2, box.height() * 2});
        QRect area(0, 0, std::min(side, out.width()), std::min(side, out.height()));
        area.moveCenter(box.center());
        area.moveLeft(std::clamp(area.left(), 0, out.width() - area.width()));
        area.moveTop(std::clamp(area.top(), 0, out.height() - area.height()));

        const QImage piece = out.copy(area).scaled(kSize, kSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // The network's mask is a little wider than the painted one, so no
        // trace of the edge of the object is left.
        const QImage small = mask.copy(area).scaled(kSize, kSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        Tensor m;
        m.shape = {1, 1, kSize, kSize};
        m.data.assign(static_cast<size_t>(kSize) * kSize, 0.0f);
        constexpr int kGrow = 4;
        for (int y = 0; y < kSize; ++y) {
            const uchar* line = small.constScanLine(y);
            for (int x = 0; x < kSize; ++x)
                if (line[x] > 8)
                    for (int dy = -kGrow; dy <= kGrow; ++dy)
                        for (int dx = -kGrow; dx <= kGrow; ++dx) {
                            const int yy = y + dy, xx = x + dx;
                            if (yy >= 0 && xx >= 0 && yy < kSize && xx < kSize)
                                m.data[static_cast<size_t>(yy) * kSize + xx] = 1.0f;
                        }
        }
        // LaMa: BGR 0..1 and the mask in, BGR 0..255 out.
        const auto result = net->run({tensorOf(piece, piece.rect(), kSize, kSize, true, 1.0f, 0.0f), m}, 1, error);
        if (!outputMatches(result, kSize, kSize)) {
            if (error && error->isEmpty())
                *error = Tr::tr("The object removal model gave an unexpected result.");
            return {};
        }
        QImage filled(kSize, kSize, QImage::Format_RGBA64);
        filled.fill(Qt::black);
        writeTensor(filled, {0, 0}, result.front(), filled.rect(), true, 1.0f / 255.0f, 0.0f);
        filled = filled.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // Only the masked pixels change (with the network's wider mask
        // softened back to the photo's size).
        QImage blend(kSize, kSize, QImage::Format_Grayscale8);
        for (int y = 0; y < kSize; ++y) {
            uchar* line = blend.scanLine(y);
            for (int x = 0; x < kSize; ++x)
                line[x] = m.data[static_cast<size_t>(y) * kSize + x] > 0.5f ? 255 : 0;
        }
        blend = blend.scaled(area.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y = 0; y < area.height(); ++y) {
            auto* d = reinterpret_cast<quint16*>(out.scanLine(area.top() + y));
            const auto* s = reinterpret_cast<const quint16*>(filled.constScanLine(y));
            const uchar* b = blend.constScanLine(y);
            const uchar* own = mask.constScanLine(area.top() + y);
            for (int x = 0; x < area.width(); ++x) {
                const int k = std::max<int>(b[x], own[area.left() + x]);
                if (k == 0)
                    continue;
                quint16* px = d + 4 * (area.left() + x);
                for (int c = 0; c < 3; ++c)
                    px[c] = static_cast<quint16>((px[c] * (255 - k) + s[4 * x + c] * k + 127) / 255);
            }
        }
        if (progress && !progress(static_cast<double>(n + 1) / patches.size()))
            return {};
    }
    return out;
}

bool measureFlow(const QImage& a, const QImage& b, std::vector<float>& flowX, std::vector<float>& flowY, QString* error) {
    if (a.isNull() || b.isNull())
        return false;
    auto net = visionNetwork(QStringLiteral("frame-flow"), error);
    if (!net)
        return false;
    // RAFT: RGB 0..255 at its fixed size; outputs the coarse and the full
    // flow, the full one is used.
    auto input = [](const QImage& img) {
        const QImage small = toRgba64(img.scaled(kFlowWidth, kFlowHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        return tensorOf(small, small.rect(), kFlowWidth, kFlowHeight, false, 255.0f, 0.0f);
    };
    const auto out = net->run({input(a), input(b)}, -1, error);
    const size_t plane = static_cast<size_t>(kFlowWidth) * kFlowHeight;
    for (const Tensor& t : out) {
        if (t.dim(1) == 2 && t.dim(2) == kFlowHeight && t.dim(3) == kFlowWidth && t.data.size() >= 2 * plane) {
            flowX.assign(t.data.begin(), t.data.begin() + plane);
            flowY.assign(t.data.begin() + plane, t.data.begin() + 2 * plane);
            return true;
        }
    }
    if (error && error->isEmpty())
        *error = Tr::tr("The in-between frame model gave an unexpected result.");
    return false;
}

} // namespace lumora::ai

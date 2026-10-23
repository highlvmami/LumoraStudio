#include "ObjectCut.h"

#include <QCoreApplication>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <optional>

#include "ai/Onnx.h"
#include "ai/Vision.h"
#include "media/VideoDecoder.h"
#include "motion/Motion.h"

namespace lumora::ai {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::ai)
};

QRect pixelRect(const QRectF& box, QSize size) {
    return QRect(static_cast<int>(std::floor(box.left() * size.width())), static_cast<int>(std::floor(box.top() * size.height())),
                 static_cast<int>(std::ceil(box.width() * size.width())), static_cast<int>(std::ceil(box.height() * size.height())))
        .intersected(QRect(QPoint(0, 0), size));
}

QImage samMatte(const QImage& image, const QRectF& box, QString* error) {
    auto net = visionNetwork(QStringLiteral("segment"), error);
    if (!net)
        return {};
    // EfficientSAM: RGB 0..1 at 1024 x 1024, the box as two corner points
    // (labels 2 and 3) in that space, unused points labelled -1.
    constexpr int kSize = 1024;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888).scaled(kSize, kSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    Tensor img;
    img.shape = {1, 3, kSize, kSize};
    const size_t plane = static_cast<size_t>(kSize) * kSize;
    img.data.resize(3 * plane);
    for (int y = 0; y < kSize; ++y) {
        const uchar* line = rgb.constScanLine(y);
        for (int x = 0; x < kSize; ++x)
            for (int c = 0; c < 3; ++c)
                img.data[c * plane + static_cast<size_t>(y) * kSize + x] = line[x * 3 + c] / 255.0f;
    }
    Tensor points, labels;
    points.shape = {1, 1, 6, 2};
    points.data.assign(12, 0.0f);
    labels.shape = {1, 1, 6};
    labels.data.assign(6, -1.0f);
    points.data[0] = static_cast<float>(box.left() * kSize);
    points.data[1] = static_cast<float>(box.top() * kSize);
    points.data[2] = static_cast<float>(box.right() * kSize);
    points.data[3] = static_cast<float>(box.bottom() * kSize);
    labels.data[0] = 2.0f;
    labels.data[1] = 3.0f;
    const auto out = net->run({img, points, labels}, -1, error);
    if (out.size() < 2 || out[0].data.size() < 3 * plane || out[1].data.size() < 3) {
        if (error && error->isEmpty())
            *error = Tr::tr("The object outlining model gave an unexpected result.");
        return {};
    }
    // Three candidate outlines; the one the model trusts most.
    const int best = static_cast<int>(std::max_element(out[1].data.begin(), out[1].data.begin() + 3) - out[1].data.begin());
    const float* m = out[0].data.data() + best * plane;
    QImage matte(kSize, kSize, QImage::Format_Grayscale8);
    for (int y = 0; y < kSize; ++y) {
        uchar* line = matte.scanLine(y);
        for (int x = 0; x < kSize; ++x) {
            // Logits: a soft edge a couple of units wide around 0.
            const float v = std::clamp(m[static_cast<size_t>(y) * kSize + x] * 0.5f + 0.5f, 0.0f, 1.0f);
            line[x] = static_cast<uchar>(std::lround(v * 255.0f));
        }
    }
    return matte.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace

QImage objectMatte(const QImage& image, const QRectF& box, bool precise, QString* error) {
    if (image.isNull() || box.isEmpty())
        return {};
    QImage matte;
    if (precise) {
        matte = samMatte(image, box, error);
        if (matte.isNull())
            return {};
    } else {
        // The fast subject model on the box (with a little room around it).
        const QRect area = pixelRect(box.adjusted(-box.width() * 0.1, -box.height() * 0.1, box.width() * 0.1, box.height() * 0.1),
                                     image.size());
        if (area.isEmpty())
            return {};
        const QImage part = subjectMatte(image.copy(area), QStringLiteral("fast"), error);
        if (part.isNull())
            return {};
        matte = QImage(image.size(), QImage::Format_Grayscale8);
        matte.fill(0);
        QPainter(&matte).drawImage(area.topLeft(), part);
    }
    // Nothing outside the box belongs to the object.
    const QRect keep = pixelRect(box.adjusted(-box.width() * 0.05, -box.height() * 0.05, box.width() * 0.05, box.height() * 0.05),
                                 matte.size());
    for (int y = 0; y < matte.height(); ++y) {
        uchar* line = matte.scanLine(y);
        if (y < keep.top() || y > keep.bottom()) {
            std::fill(line, line + matte.width(), 0);
            continue;
        }
        std::fill(line, line + std::max(0, keep.left()), 0);
        std::fill(line + std::min(matte.width(), keep.right() + 1), line + matte.width(), 0);
    }
    return matte;
}

ObjectTrackResult trackObject(const QString& path, const Clip& clip, const QRectF& box, const std::function<bool(double)>& progress) {
    ObjectTrackResult r;
    r.track.kind = RedactionKind::Manual;
    VideoDecoder dec;
    QString err;
    auto [from, to] = clip.mediaRange();
    if (!dec.open(path, &err) || to <= from || !dec.seek(from)) {
        r.error = err.isEmpty() ? Tr::tr("The video cannot be read.") : err;
        return r;
    }
    const int rotation = dec.info().rotation;
    constexpr int kWidth = 480;
    // A box every few frames is plenty: the box in between is interpolated.
    const Ticks every = kTicksPerSecond / 8;
    motion::GrayConverter conv;
    std::optional<motion::RegionTracker> tracker;
    DecodedFrame f;
    Ticks lastStored = kInvalidTicks;
    while (dec.next(f)) {
        if (f.pts + std::max<Ticks>(1, f.duration) <= from)
            continue;
        if (f.pts >= to)
            break;
        motion::GrayImage g = conv.convert(f.frame.get(), kWidth);
        if (g.isNull())
            continue;
        if (rotation != 0) {
            // Turned upright like the picture is shown.
            QImage img(g.width, g.height, QImage::Format_Grayscale8);
            for (int y = 0; y < g.height; ++y)
                for (int x = 0; x < g.width; ++x)
                    img.scanLine(y)[x] = static_cast<uchar>(std::clamp(g.px[static_cast<size_t>(y) * g.width + x], 0.0f, 255.0f));
            g = motion::grayFromImage(img.transformed(QTransform().rotate(rotation)));
        }
        QRectF px;
        if (!tracker) {
            px = QRectF(box.x() * g.width, box.y() * g.height, box.width() * g.width, box.height() * g.height);
            tracker.emplace(g, px);
        } else {
            px = tracker->update(g);
        }
        if (lastStored == kInvalidTicks || f.pts - lastStored >= every) {
            r.track.boxes.push_back({f.pts, QRectF(px.x() / g.width, px.y() / g.height, px.width() / g.width, px.height() / g.height)});
            lastStored = f.pts;
        }
        if (progress && !progress(static_cast<double>(f.pts - from) / (to - from))) {
            r.cancelled = true;
            return r;
        }
    }
    if (r.track.boxes.empty())
        r.error = Tr::tr("The video cannot be read.");
    return r;
}

} // namespace lumora::ai

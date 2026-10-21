#include "Vision.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

#include "ai/Onnx.h"

namespace lumora::ai {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::ai)
};

QString fileName(const QString& id) {
    if (id == QLatin1String("face"))
        return QStringLiteral("face_detection_yunet_2023mar.onnx");
    if (id == QLatin1String("plate"))
        return QStringLiteral("yolo-v9-t-640-license-plates-end2end.onnx");
    if (id == QLatin1String("background-fast"))
        return QStringLiteral("u2netp.onnx");
    if (id == QLatin1String("background-quality"))
        return QStringLiteral("isnet-general-use.onnx");
    return {};
}

std::shared_ptr<Network> network(const QString& id, QString* error) {
    if (!onnxAvailable()) {
        if (error)
            *error = Tr::tr("This build of LumoraStudio has no AI tools.");
        return nullptr;
    }
    const QString path = visionModelPath(id);
    if (!QFileInfo::exists(path)) {
        if (error)
            *error = Tr::tr("The %1 model has not been downloaded yet.").arg(visionModel(id).label);
        return nullptr;
    }
    QString why;
    auto net = Network::load(path, &why);
    if (!net && error)
        *error = Tr::tr("The %1 model cannot be read (%2). Download it again.").arg(visionModel(id).label, why);
    return net;
}

QImage toRgb888(const QImage& image) {
    return image.format() == QImage::Format_RGB888 ? image : image.convertToFormat(QImage::Format_RGB888);
}

// The picture scaled to fit a size x size square without distortion, at
// (dx, dy), the rest filled with `pad`.
struct Letterbox {
    QImage image;
    double scale = 1.0;
    int dx = 0, dy = 0;
};

Letterbox letterbox(const QImage& rgb, int size, QColor pad, bool centre) {
    Letterbox lb;
    lb.scale = std::min(static_cast<double>(size) / rgb.width(), static_cast<double>(size) / rgb.height());
    const int w = std::max(1, static_cast<int>(std::lround(rgb.width() * lb.scale)));
    const int h = std::max(1, static_cast<int>(std::lround(rgb.height() * lb.scale)));
    lb.image = QImage(size, size, QImage::Format_RGB888);
    lb.image.fill(pad);
    if (centre) {
        lb.dx = (size - w) / 2;
        lb.dy = (size - h) / 2;
    }
    QPainter p(&lb.image);
    p.drawImage(lb.dx, lb.dy, rgb.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    return lb;
}

// Planar (CHW) float tensor of an RGB888 image: value * scale, minus mean,
// divided by std per channel; `bgr` swaps the channel order.
Tensor planar(const QImage& rgb, bool bgr, float scale, const float mean[3], const float stdev[3]) {
    Tensor t;
    const int w = rgb.width(), h = rgb.height();
    t.shape = {1, 3, h, w};
    t.data.resize(static_cast<size_t>(3) * w * h);
    const size_t plane = static_cast<size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
        const uchar* line = rgb.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) {
                const int src = bgr ? 2 - c : c;
                t.data[c * plane + static_cast<size_t>(y) * w + x] = (line[x * 3 + src] * scale - mean[c]) / stdev[c];
            }
        }
    }
    return t;
}

double iou(const QRectF& a, const QRectF& b) {
    const QRectF i = a.intersected(b);
    if (i.isEmpty())
        return 0.0;
    const double inter = i.width() * i.height();
    return inter / (a.width() * a.height() + b.width() * b.height() - inter);
}

std::vector<Detection> nms(std::vector<Detection> d, double threshold) {
    std::sort(d.begin(), d.end(), [](const Detection& a, const Detection& b) { return a.score > b.score; });
    std::vector<Detection> keep;
    for (const auto& c : d)
        if (std::none_of(keep.begin(), keep.end(), [&](const Detection& k) { return iou(k.rect, c.rect) > threshold; }))
            keep.push_back(c);
    return keep;
}

// Runs `detect` on the whole picture and, for big pictures, also on
// overlapping tiles, so small faces and plates are not lost when the picture
// is shrunk to the network's input size.
template <class F>
std::vector<Detection> detectTiled(const QImage& rgb, F detect) {
    std::vector<Detection> all = detect(rgb);
    const int side = std::max(rgb.width(), rgb.height());
    if (side > 1600) {
        const int n = side > 3600 ? 3 : 2;
        const int tw = rgb.width() / n, th = rgb.height() / n;
        const int ox = tw / 4, oy = th / 4; // overlap
        for (int ty = 0; ty < n; ++ty) {
            for (int tx = 0; tx < n; ++tx) {
                const QRect r = QRect(tx * tw - ox, ty * th - oy, tw + 2 * ox, th + 2 * oy).intersected(rgb.rect());
                for (Detection d : detect(rgb.copy(r))) {
                    d.rect.translate(r.topLeft());
                    all.push_back(d);
                }
            }
        }
    }
    return nms(std::move(all), 0.4);
}

} // namespace

bool visionAvailable() { return onnxAvailable(); }

std::vector<VisionModel> visionModels() {
    const QString releases = QStringLiteral("https://github.com/");
    return {
        {QStringLiteral("face"), Tr::tr("face detection"), 1,
         QUrl(QStringLiteral("https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/face_detection_yunet/"
                             "face_detection_yunet_2023mar.onnx"))},
        {QStringLiteral("plate"), Tr::tr("number plate detection"), 8,
         QUrl(releases + QStringLiteral("ankandrew/open-image-models/releases/download/assets/yolo-v9-t-640-license-plates-end2end.onnx"))},
        {QStringLiteral("background-fast"), Tr::tr("fast background removal"), 5,
         QUrl(releases + QStringLiteral("danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx"))},
        {QStringLiteral("background-quality"), Tr::tr("detailed background removal"), 179,
         QUrl(releases + QStringLiteral("danielgatis/rembg/releases/download/v0.0.0/isnet-general-use.onnx"))},
    };
}

VisionModel visionModel(const QString& id) {
    for (auto& m : visionModels())
        if (m.id == id)
            return m;
    return {};
}

QString visionModelPath(const QString& id) {
    // Tests and CI can point at a folder of already downloaded models.
    const QString override = qEnvironmentVariable("LUMORA_VISION_MODELS");
    if (!override.isEmpty())
        return override + QLatin1Char('/') + fileName(id);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + fileName(id);
}

bool visionModelReady(const QString& id) { return onnxAvailable() && QFileInfo::exists(visionModelPath(id)); }

QString backgroundModelId(const QString& name) {
    return name == QLatin1String("quality") ? QStringLiteral("background-quality") : QStringLiteral("background-fast");
}

std::vector<Detection> detectFaces(const QImage& image, float threshold, QString* error) {
    if (image.isNull())
        return {};
    auto net = network(QStringLiteral("face"), error);
    if (!net)
        return {};
    // YuNet: BGR 0..255, picture in the top left corner of a 640 x 640 input.
    const auto detect = [&](const QImage& rgb) {
        std::vector<Detection> out;
        constexpr int kSize = 640;
        const Letterbox lb = letterbox(rgb, kSize, Qt::black, false);
        const float zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
        const auto outputs = net->run(planar(lb.image, true, 1.0f, zero, one), -1, error);
        if (outputs.size() < 12)
            return out;
        // Outputs: cls_8/16/32, obj_8/16/32, bbox_8/16/32, kps_8/16/32.
        const int strides[3] = {8, 16, 32};
        for (int s = 0; s < 3; ++s) {
            const Tensor& cls = outputs[s];
            const Tensor& obj = outputs[3 + s];
            const Tensor& box = outputs[6 + s];
            const int cols = kSize / strides[s];
            const int rows = kSize / strides[s];
            if (static_cast<int>(cls.data.size()) < rows * cols || static_cast<int>(box.data.size()) < rows * cols * 4)
                continue;
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    const int i = r * cols + c;
                    const float score = std::sqrt(std::clamp(cls.data[i], 0.0f, 1.0f) * std::clamp(obj.data[i], 0.0f, 1.0f));
                    if (score < threshold)
                        continue;
                    const float* b = &box.data[i * 4];
                    const double cx = (c + b[0]) * strides[s], cy = (r + b[1]) * strides[s];
                    const double w = std::exp(b[2]) * strides[s], h = std::exp(b[3]) * strides[s];
                    QRectF rect((cx - w / 2 - lb.dx) / lb.scale, (cy - h / 2 - lb.dy) / lb.scale, w / lb.scale, h / lb.scale);
                    rect = rect.intersected(QRectF(rgb.rect()));
                    if (!rect.isEmpty())
                        out.push_back({rect, score});
                }
            }
        }
        return nms(std::move(out), 0.3);
    };
    return detectTiled(toRgb888(image), detect);
}

std::vector<Detection> detectPlates(const QImage& image, float threshold, QString* error) {
    if (image.isNull())
        return {};
    auto net = network(QStringLiteral("plate"), error);
    if (!net)
        return {};
    // YOLOv9 end to end (with NMS): RGB 0..1, letterboxed with grey; each
    // output row is [batch, x1, y1, x2, y2, class, score].
    const auto detect = [&](const QImage& rgb) {
        std::vector<Detection> out;
        const auto shape = net->inputShape();
        const int size = shape.size() == 4 && shape[2] > 0 ? static_cast<int>(shape[2]) : 640;
        const Letterbox lb = letterbox(rgb, size, QColor(114, 114, 114), true);
        const float zero[3] = {0, 0, 0}, one[3] = {1, 1, 1};
        const auto outputs = net->run(planar(lb.image, false, 1.0f / 255.0f, zero, one), 1, error);
        if (outputs.empty())
            return out;
        const Tensor& t = outputs.front();
        const int cols = t.shape.size() == 2 ? static_cast<int>(t.shape[1]) : 7;
        for (size_t i = 0; cols >= 7 && i + cols <= t.data.size(); i += cols) {
            const float* row = &t.data[i];
            if (row[6] < threshold)
                continue;
            QRectF rect(QPointF((row[1] - lb.dx) / lb.scale, (row[2] - lb.dy) / lb.scale),
                        QPointF((row[3] - lb.dx) / lb.scale, (row[4] - lb.dy) / lb.scale));
            rect = rect.normalized().intersected(QRectF(rgb.rect()));
            if (!rect.isEmpty())
                out.push_back({rect, row[6]});
        }
        return out;
    };
    return detectTiled(toRgb888(image), detect);
}

QImage subjectMatte(const QImage& image, const QString& modelName, QString* error) {
    if (image.isNull())
        return {};
    const QString id = backgroundModelId(modelName);
    auto net = network(id, error);
    if (!net)
        return {};
    const auto shape = net->inputShape();
    const int size = shape.size() == 4 && shape[2] > 0 ? static_cast<int>(shape[2]) : 320;
    const QImage rgb = toRgb888(image).scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // As the models were trained: scaled by the brightest value, then
    // normalised (ImageNet statistics for U²-Net, 0.5 offset for IS-Net).
    int brightest = 1;
    for (int y = 0; y < size; ++y) {
        const uchar* line = rgb.constScanLine(y);
        brightest = std::max<int>(brightest, *std::max_element(line, line + size * 3));
    }
    const bool quality = id == QLatin1String("background-quality");
    const float imagenetMean[3] = {0.485f, 0.456f, 0.406f}, imagenetStd[3] = {0.229f, 0.224f, 0.225f};
    const float halfMean[3] = {0.5f, 0.5f, 0.5f}, one[3] = {1, 1, 1};
    const auto outputs = net->run(planar(rgb, false, 1.0f / brightest, quality ? halfMean : imagenetMean, quality ? one : imagenetStd),
                                  1, error);
    if (outputs.empty() || static_cast<int>(outputs.front().data.size()) < size * size)
        return {};
    const auto& v = outputs.front().data;
    const auto [lo, hi] = std::minmax_element(v.begin(), v.begin() + size * size);
    const float range = std::max(1e-6f, *hi - *lo);
    QImage matte(size, size, QImage::Format_Grayscale8);
    for (int y = 0; y < size; ++y) {
        uchar* line = matte.scanLine(y);
        for (int x = 0; x < size; ++x)
            line[x] = static_cast<uchar>(std::lround(std::clamp((v[y * size + x] - *lo) / range, 0.0f, 1.0f) * 255.0f));
    }
    return matte.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void applyMatte(QImage& image, const QImage& matte, double softness) {
    if (image.isNull() || matte.isNull())
        return;
    const QImage m = matte.size() == image.size() ? matte : matte.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    // The matte is mapped through a ramp centred on 0.5: narrow = crisp edge.
    const double width = 0.06 + 0.6 * std::clamp(softness, 0.0, 1.0);
    const double lo = 0.5 - width / 2;
    float lut[256];
    for (int i = 0; i < 256; ++i) {
        const double x = std::clamp((i / 255.0 - lo) / width, 0.0, 1.0);
        lut[i] = static_cast<float>(x * x * (3 - 2 * x));
    }
    if (image.depth() == 64) {
        if (image.format() != QImage::Format_RGBA64)
            image = image.convertToFormat(QImage::Format_RGBA64);
        for (int y = 0; y < image.height(); ++y) {
            auto* px = reinterpret_cast<quint16*>(image.scanLine(y));
            const uchar* a = m.constScanLine(y);
            for (int x = 0; x < image.width(); ++x)
                px[x * 4 + 3] = static_cast<quint16>(std::lround(px[x * 4 + 3] * lut[a[x]]));
        }
    } else {
        if (image.format() != QImage::Format_ARGB32)
            image = image.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            auto* px = reinterpret_cast<QRgb*>(image.scanLine(y));
            const uchar* a = m.constScanLine(y);
            for (int x = 0; x < image.width(); ++x)
                px[x] = (px[x] & 0x00ffffffu) | (static_cast<QRgb>(std::lround(qAlpha(px[x]) * lut[a[x]])) << 24);
        }
    }
}

} // namespace lumora::ai

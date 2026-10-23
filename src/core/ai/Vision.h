#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QUrl>
#include <array>
#include <memory>
#include <vector>

namespace lumora::ai {

class Network;

// Picture models that run on this computer (ONNX Runtime, CPU). Nothing is
// sent anywhere; each model is downloaded once, the first time it is needed.
//
//   face                YuNet (OpenCV Zoo, MIT)
//   plate               YOLOv9-t number plate detector (open-image-models, MIT)
//   background-fast     U²-Netp (Apache 2.0)
//   background-quality  IS-Net general use (Apache 2.0)
//   upscale-x2/x4       Real-ESRGAN (BSD 3-clause)
//   face-restore        GFPGAN 1.4 (Apache 2.0)
//   inpaint             LaMa (Apache 2.0)
//   frame-flow          RAFT optical flow (BSD 3-clause)
//   segment             EfficientSAM-Ti (Apache 2.0)
struct VisionModel {
    QString id;
    QString label; // shown to the user
    int sizeMB = 0;
    QUrl url;
};

bool visionAvailable();
std::vector<VisionModel> visionModels();
VisionModel visionModel(const QString& id);
// Where a downloaded model is kept (the folder is created when missing).
QString visionModelPath(const QString& id);
bool visionModelReady(const QString& id);
// The loaded model, or nullptr with `error` saying why (not downloaded,
// unreadable, no AI in this build).
std::shared_ptr<Network> visionNetwork(const QString& id, QString* error = nullptr);

// Background removal models by their short name in BackgroundRemoval::model
// ("fast", "quality").
QString backgroundModelId(const QString& name);

struct Detection {
    QRectF rect; // pixels of the analysed image
    float score = 0.0f;
    // Faces: eyes (left, right as seen), nose tip, mouth corners (left,
    // right as seen), in pixels of the analysed image.
    std::array<QPointF, 5> landmarks{};
};

// Faces in `image` (any format). Empty when there are none or the model is
// missing (`error` set then).
std::vector<Detection> detectFaces(const QImage& image, float threshold = 0.6f, QString* error = nullptr);
// Number plates of cars and motorbikes in `image`.
std::vector<Detection> detectPlates(const QImage& image, float threshold = 0.3f, QString* error = nullptr);

// How much of each pixel belongs to the subject (Format_Grayscale8, image
// size; 255 = subject). Null when the model is missing or fails.
QImage subjectMatte(const QImage& image, const QString& modelName, QString* error = nullptr);

// Makes the background of `image` transparent using `matte`. `softness`
// (0 .. 1) widens the transition at the edge of the subject. The image keeps
// its precision: 16-bit images become RGBA64, others ARGB32.
void applyMatte(QImage& image, const QImage& matte, double softness);

} // namespace lumora::ai

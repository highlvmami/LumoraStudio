#pragma once

#include <QColor>
#include <QCoreApplication>
#include <QString>

#include "image/PhotoProcessor.h"

namespace lumora {

// One set of changes applied to many photos at once: size, colour filter,
// watermark, file format and name. The originals are never changed.
struct BatchPhotoSettings {
    enum class Resize { None, LongEdge, Width, Height, Percent };
    Resize resize = Resize::None;
    int pixels = 1920;     // LongEdge / Width / Height
    double percent = 50.0; // Percent
    bool enlarge = false;  // false: small photos keep their size

    bool autoColor = false;
    QString look;          // colour filter id, empty = none
    double lookAmount = 1.0;

    enum class Watermark { None, Text, Picture };
    Watermark watermark = Watermark::None;
    QString watermarkText;
    QColor watermarkColor = Qt::white;
    QString watermarkPicture;
    Qt::Alignment watermarkPosition = Qt::AlignRight | Qt::AlignBottom;
    double watermarkSize = 0.25;    // fraction of the photo width (text) or of the fitted picture
    double watermarkOpacity = 0.7;

    bool keepFormat = false;        // write each photo in its own format where possible
    PhotoExportSettings format;
    QString folder;                 // output folder
    QString pattern = QStringLiteral("{name}"); // {name}, {n}, {date}
};

class BatchPhoto {
    Q_DECLARE_TR_FUNCTIONS(lumora::BatchPhoto)

public:
    explicit BatchPhoto(const BatchPhotoSettings& settings);
    ~BatchPhoto();

    // File the `index`-th photo (0-based, of `count`) is written to. Never the
    // source itself.
    QString outputPath(const QString& source, int index, int count) const;
    // Processes one photo. False with `error` set when it failed.
    bool process(const QString& source, const QString& target, QString* error) const;

    // The size a photo of `size` gets.
    QSize targetSize(QSize size) const;

private:
    BatchPhotoSettings m_s;
    QString m_textPicture; // the text watermark, drawn once into a picture
    QString m_tempDir;
};

} // namespace lumora

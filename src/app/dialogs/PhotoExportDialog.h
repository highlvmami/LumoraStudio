#pragma once

#include <QDialog>
#include <QImage>

#include "image/PhotoAdjustments.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSlider;

namespace lumora {

class PhotoExportDialog : public QDialog {
    Q_OBJECT
public:
    PhotoExportDialog(const QString& sourcePath, const QImage& original, const QImage& working,
                      const PhotoAdjustments& adj, QWidget* parent = nullptr);
    QString savedPath() const { return m_saved; }

private:
    void updateUi();
    void save();

    QString m_source;
    QImage m_original, m_working;
    PhotoAdjustments m_adj;
    QComboBox* m_format;
    QComboBox* m_depth;
    QSlider* m_quality;
    QLabel* m_qualityLabel;
    QLabel* m_note;
    QLineEdit* m_path;
    QString m_saved;
};

} // namespace lumora

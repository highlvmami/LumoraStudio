#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QToolButton;
class QStackedWidget;

namespace lumora {

struct BatchPhotoSettings;

// Changes many photos at once: size, colour, watermark, file type and name.
// New files are written; the originals stay as they are.
class BatchPhotoDialog : public QDialog {
    Q_OBJECT
public:
    explicit BatchPhotoDialog(QWidget* parent = nullptr, const QStringList& files = {});

private:
    void addFiles(const QStringList& files);
    void addFolder();
    BatchPhotoSettings settings() const;
    void updateState();
    void run();
    void saveSettings() const;

    QListWidget* m_files;
    QLabel* m_count;
    QComboBox* m_resize;
    QSpinBox* m_pixels;
    QSpinBox* m_percent;
    QCheckBox* m_enlarge;
    QCheckBox* m_autoColor;
    QComboBox* m_look;
    QSpinBox* m_lookAmount;
    QComboBox* m_watermark;
    QLineEdit* m_markText;
    QToolButton* m_markColor;
    QLineEdit* m_markPicture;
    QComboBox* m_markPosition;
    QSpinBox* m_markSize;
    QSpinBox* m_markOpacity;
    QStackedWidget* m_markPages;
    QComboBox* m_format;
    QSpinBox* m_quality;
    QLineEdit* m_folder;
    QLineEdit* m_pattern;
    QLabel* m_example;
    QColor m_color;
};

} // namespace lumora

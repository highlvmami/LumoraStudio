#pragma once

#include <QWidget>

#include "image/PhotoAdjustments.h"
#include "timeline/ClipEffects.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;

namespace lumora {

// "Smart" tab of the photo workspace: remove objects, restore faces, hide
// faces and number plates, remove the background. The models run on this
// computer.
class SmartToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SmartToolsPanel(QWidget* parent = nullptr);

    void setState(const PhotoRetouch& retouch, const Redaction& redaction, const BackgroundRemoval& background, bool editable);

    // While on, dragging over the photo paints what to remove.
    bool eraseMode() const;
    void setEraseMode(bool on);
    BrushStroke eraseBrush() const;

signals:
    void eraseModeChanged(bool on);
    void eraseBrushChanged();
    void retouchEdited(const lumora::PhotoRetouch& retouch, const QString& text, const QString& field);
    void findRequested();
    // `field` is set for edits that merge while a value is dragged.
    void redactionEdited(const Redaction& redaction, const QString& text, const QString& field);
    void backgroundEdited(const BackgroundRemoval& background, const QString& text, const QString& field);

private:
    PhotoRetouch m_retouch;
    Redaction m_redaction;
    BackgroundRemoval m_background;
    bool m_syncing = false;

    QPushButton* m_erase;
    QPushButton* m_eraseUndo;
    QSlider* m_eraseSize;
    QLabel* m_eraseInfo;
    QCheckBox* m_facesOn;
    QSpinBox* m_facesStrength;
    QLabel* m_redactInfo;
    QPushButton* m_find;
    QPushButton* m_clear;
    QCheckBox* m_faces;
    QCheckBox* m_plates;
    QComboBox* m_style;
    QSpinBox* m_strength;
    QListWidget* m_list;
    QCheckBox* m_bgOn;
    QComboBox* m_bgModel;
    QSpinBox* m_bgSoftness;
    QLabel* m_bgInfo;
};

} // namespace lumora

#pragma once

#include <QWidget>

#include "timeline/ClipEffects.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace lumora {

// "Smart" tab of the photo workspace: hide faces and number plates, remove
// the background. The models run on this computer.
class SmartToolsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SmartToolsPanel(QWidget* parent = nullptr);

    void setState(const Redaction& redaction, const BackgroundRemoval& background, bool editable);

signals:
    void findRequested();
    // `field` is set for edits that merge while a value is dragged.
    void redactionEdited(const Redaction& redaction, const QString& text, const QString& field);
    void backgroundEdited(const BackgroundRemoval& background, const QString& text, const QString& field);

private:
    Redaction m_redaction;
    BackgroundRemoval m_background;
    bool m_syncing = false;

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

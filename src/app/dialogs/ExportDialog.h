#pragma once

#include <QDialog>
#include <QHash>

#include "export/ExportSettings.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLayout;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QUndoStack;

namespace lumora {

class Project;

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(Project* project, QUndoStack* undo, QWidget* parent = nullptr);

    // Runs the export with a progress dialog. Returns true on success.
    static bool runExport(QWidget* parent, const ExportPlan& plan, const ExportSettings& settings);

private:
    void replan();
    void updateExtension();
    void browse();
    void snapCuts();
    void startExport();
    ExportSettings settings() const;
    void buildCustomPanel(QGroupBox* box);
    void applyPreset(int index);
    void codecChanged();
    void encoderChanged();
    void updateQualityLabel();
    void updateCustomVisibility();
    QString sourcePath() const;

    Project* m_project;
    QUndoStack* m_undo;
    QButtonGroup* m_modes;
    QLineEdit* m_path = nullptr;
    QHash<int, QLabel*> m_modeHelp;
    QCheckBox* m_audio;
    QLabel* m_planLabel;
    QPushButton* m_snapBtn;
    QPushButton* m_exportBtn;
    ExportPlan m_plan;

    // Custom settings.
    QGroupBox* m_customBox = nullptr;
    QComboBox* m_preset;
    QComboBox* m_codec;
    QComboBox* m_encoder;
    QComboBox* m_profile;
    QComboBox* m_rateControl;
    QSlider* m_quality;
    QLabel* m_qualityLabel;
    QDoubleSpinBox* m_bitrate;
    QDoubleSpinBox* m_maxBitrate;
    QDoubleSpinBox* m_targetSize;
    QCheckBox* m_twoPass;
    QComboBox* m_resolution;
    QSpinBox* m_width;
    QSpinBox* m_height;
    QComboBox* m_scaleMode;
    QComboBox* m_frameRate;
    QComboBox* m_bitDepth;
    QSlider* m_speed;
    QComboBox* m_audioCodec;
    QComboBox* m_audioBitrate;
    QWidget* m_sizeRowWidget;
    QLayout* m_speedRow = nullptr;
    QWidget* m_qualityRowWidget;
    QFormLayout* m_customForm = nullptr;
    bool m_applyingPreset = false;
};

} // namespace lumora

#pragma once

#include <QDialog>

#include "export/ExportSettings.h"

class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
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

    Project* m_project;
    QUndoStack* m_undo;
    QButtonGroup* m_modes;
    QLineEdit* m_path;
    QCheckBox* m_audio;
    QLabel* m_planLabel;
    QPushButton* m_snapBtn;
    QPushButton* m_exportBtn;
    ExportPlan m_plan;
};

} // namespace lumora

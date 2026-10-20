#pragma once

#include <QUuid>
#include <QWidget>

class QUndoStack;

namespace lumora {

class Project;
class ColorPanel;

// The Color workspace's right-hand panel: the colour settings of the
// selected video clip. Every change is an undo step; dragging a control
// merges into one.
class ClipColorPanel : public QWidget {
    Q_OBJECT
public:
    ClipColorPanel(Project* project, QUndoStack* undo, QWidget* parent = nullptr);

    void showClip(const QUuid& clipId);
    void refresh();

private:
    Project* m_project;
    QUndoStack* m_undo;
    ColorPanel* m_panel;
    QUuid m_clip;
    int m_session = 0;
    bool m_updating = false;
};

} // namespace lumora

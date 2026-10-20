#pragma once

#include <QUuid>
#include <QWidget>

class QLabel;

namespace lumora {

class Project;

// Right-hand panel: technical details of the selected clip or file. In 0.1
// it is read-only; transform / colour / audio / speed controls arrive in
// later versions and slot in here as sections.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(Project* project, QWidget* parent = nullptr);

    void showClip(const QUuid& clipId);
    void showMedia(const QUuid& mediaId);
    void showNothing();
    void refresh();

private:
    Project* m_project;
    QLabel* m_label;
    QUuid m_clip;
    QUuid m_media;
};

} // namespace lumora

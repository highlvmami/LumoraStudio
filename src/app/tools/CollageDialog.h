#pragma once

#include <QDialog>
#include <QUuid>
#include <functional>

class QListWidget;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;

namespace lumora {

class Project;

// Puts several videos or pictures next to each other in one frame. The
// dialog collects the choice; the main window places the clips.
class CollageDialog : public QDialog {
    Q_OBJECT
public:
    enum class Sound { First, All, None };

    CollageDialog(Project* project, std::function<QImage(const QUuid&)> thumbnail, QWidget* parent = nullptr);

    QString layoutId() const;
    QList<QUuid> media() const; // in cell order
    double gap() const;         // fraction of the frame's short side
    double pictureSeconds() const;
    Sound sound() const;

private:
    void addFiles();
    void refreshCount();
    void fillMedia();

    Project* m_project;
    std::function<QImage(const QUuid&)> m_thumbnail;
    QListWidget* m_layouts;
    QListWidget* m_media;
    QSpinBox* m_gap;
    QDoubleSpinBox* m_seconds;
    QComboBox* m_sound;
    QLabel* m_count;
};

} // namespace lumora

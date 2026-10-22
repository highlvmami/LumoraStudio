#pragma once

#include <QDialog>

#include "TimeBase.h"

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QLabel;

namespace lumora {

class Project;

// Saves a part of the timeline as an animated GIF: size, smoothness,
// colours and repeats, with an estimate of the file size.
class GifDialog : public QDialog {
    Q_OBJECT
public:
    // `selection` is the time span of the selected clips (empty when none).
    GifDialog(Project* project, Ticks selectionFrom, Ticks selectionTo, QWidget* parent = nullptr);

    QString savedPath() const { return m_saved; }

private:
    void updateRange();
    void updateEstimate();
    void save();

    Project* m_project;
    Ticks m_selFrom, m_selTo;
    QComboBox* m_part;
    QDoubleSpinBox* m_from;
    QDoubleSpinBox* m_to;
    QComboBox* m_width;
    QComboBox* m_fps;
    QSpinBox* m_colors;
    QCheckBox* m_dither;
    QComboBox* m_loop;
    QLabel* m_estimate;
    QString m_saved;
};

} // namespace lumora

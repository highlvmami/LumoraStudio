#pragma once

#include <QUuid>
#include <QWidget>
#include <atomic>
#include <memory>

#include "audio/AudioEffects.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QUndoStack;

namespace lumora {

class Project;
class SliderRow;

// The Audio workspace's panel: the sound of the selected clip (gain, low
// cut, noise reduction, equaliser, compressor, ducking) and the master
// section for the whole mix (loudness normalisation, limiter, measuring).
// Every change is an undo step; dragging a control merges into one.
class AudioPanel : public QWidget {
    Q_OBJECT
public:
    AudioPanel(Project* project, QUndoStack* undo, QWidget* parent = nullptr);
    ~AudioPanel() override;

    void showClip(const QUuid& clipId);
    void refresh();
    void measureLoudness();

private:
    // The audio clip the controls edit: the selection itself or the sound
    // linked to a selected video clip.
    QUuid audioClip() const;
    void editClip(const QString& text, const QString& field, const std::function<void(AudioEffects&, double&)>& change);
    void editMaster(const QString& text, const QString& field, const std::function<void(AudioMaster&)>& change);

    Project* m_project;
    QUndoStack* m_undo;
    QUuid m_clip;
    int m_session = 0;
    bool m_updating = false;
    bool m_syncing = false;
    std::shared_ptr<std::atomic_bool> m_cancelMeasure;

    QLabel* m_header;
    QWidget* m_clipBody;
    SliderRow* m_gain;
    SliderRow* m_lowCut;
    QComboBox* m_hum;
    SliderRow* m_wind;
    SliderRow* m_denoise;
    SliderRow* m_eqLow;
    SliderRow* m_eqMid;
    SliderRow* m_eqMidFreq;
    SliderRow* m_eqHigh;
    QGroupBox* m_compBox;
    SliderRow* m_compThreshold;
    SliderRow* m_compRatio;
    SliderRow* m_compMakeup;
    SliderRow* m_duck;
    QCheckBox* m_normalize;
    QComboBox* m_target;
    QCheckBox* m_limiter;
    SliderRow* m_ceiling;
    QPushButton* m_measure;
    QLabel* m_measured;
};

} // namespace lumora

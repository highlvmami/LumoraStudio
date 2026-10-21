#pragma once

#include <QUuid>
#include <QWidget>
#include <functional>

#include "timeline/ClipEffects.h"

class QLabel;
class QUndoStack;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QPlainTextEdit;
class QFontComboBox;
class QToolButton;
class QPushButton;
class QFormLayout;
class QLineEdit;
class QListWidget;

namespace lumora {

class Project;
struct Clip;

// Right-hand panel: the editable settings of the selected clip (title or
// subtitle text and style, position / scale / rotation / opacity, transition, fades, audio
// gain) followed by the technical details of its file. Every change is one
// undo step; dragging a value merges into a single step.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(Project* project, QUndoStack* undo, QWidget* parent = nullptr);

    void showClip(const QUuid& clipId);
    void showMedia(const QUuid& mediaId);
    void showNothing();
    void refresh();
    // Puts the cursor in the text of the selected title or subtitle.
    void focusText();

signals:
    // Long analyses run by the main window (progress dialog, playhead).
    void stabilizeRequested(const QUuid& clipId);
    void trackRequested(const QUuid& clipId);
    void redactRequested(const QUuid& clipId);
    // Background removal needs its model: the main window downloads it and
    // turns the removal on.
    void backgroundRequested(const QUuid& clipId, const QString& model);

private:
    QWidget* makeSection(const QString& title, QFormLayout** form);
    QToolButton* makeColorButton();
    void setButtonColor(QToolButton* b, const QColor& c);
    // Applies `op` to the selected clip as an undoable, mergeable edit.
    void edit(const QString& text, const QString& field, const std::function<bool(const Clip&)>& op);
    void applyTitle();
    void applyTransform();
    void applyTransition();
    void applyFades();
    void applySubtitleStyle();
    void refreshEditors(const Clip* clip);
    void refreshInfo(const Clip* clip);

    Project* m_project;
    QUndoStack* m_undo;
    QUuid m_clip;
    QUuid m_media;
    int m_session = 0; // bumps on every selection change so merges stay per selection
    bool m_updating = false;

    QLabel* m_info;
    QLabel* m_header;

    QWidget* m_titleSection;
    QPlainTextEdit* m_titleText;
    QFontComboBox* m_titleFont;
    QDoubleSpinBox* m_titleSize;
    QToolButton* m_titleBold;
    QToolButton* m_titleItalic;
    QComboBox* m_titleAlign;
    QToolButton* m_titleColor;
    QToolButton* m_titleOutlineColor;
    QDoubleSpinBox* m_titleOutline;
    QCheckBox* m_titleShadow;
    QToolButton* m_titleBackColor;
    QSpinBox* m_titleBackOpacity;
    QDoubleSpinBox* m_titleX;
    QDoubleSpinBox* m_titleY;
    QComboBox* m_titleAnimIn;
    QComboBox* m_titleAnimOut;
    QDoubleSpinBox* m_titleAnimDuration;
    QDoubleSpinBox* m_titleDuration;

    QWidget* m_stabSection;
    QCheckBox* m_stabOn;
    QDoubleSpinBox* m_stabSmoothing;
    QSpinBox* m_stabZoom;
    QLabel* m_stabInfo;

    QWidget* m_redactSection;
    QLabel* m_redactInfo;
    QPushButton* m_redactFind;
    QPushButton* m_redactClear;
    QCheckBox* m_redactFaces;
    QCheckBox* m_redactPlates;
    QComboBox* m_redactStyle;
    QSpinBox* m_redactStrength;
    QListWidget* m_redactList;

    QWidget* m_bgSection;
    QCheckBox* m_bgOn;
    QComboBox* m_bgModel;
    QSpinBox* m_bgSoftness;
    QLabel* m_bgInfo;

    QWidget* m_trackSection;
    QPushButton* m_trackStart;
    QPushButton* m_trackClear;
    QLabel* m_trackInfo;

    QWidget* m_freezeSection;
    QDoubleSpinBox* m_freezeDuration;
    QLabel* m_freezeInfo;

    QWidget* m_subSection;
    QPlainTextEdit* m_subText;
    QDoubleSpinBox* m_subDuration;
    QWidget* m_subTrackSection;
    QLineEdit* m_subLanguage;
    QFontComboBox* m_subFont;
    QDoubleSpinBox* m_subSize;
    QToolButton* m_subBold;
    QToolButton* m_subColor;
    QDoubleSpinBox* m_subOutline;
    QToolButton* m_subOutlineColor;
    QSpinBox* m_subBackOpacity;
    QDoubleSpinBox* m_subY;

    QWidget* m_transformSection;
    QDoubleSpinBox* m_posX;
    QDoubleSpinBox* m_posY;
    QDoubleSpinBox* m_scale;
    QDoubleSpinBox* m_rotation;
    QSpinBox* m_opacity;

    QWidget* m_transitionSection;
    QComboBox* m_transitionType;
    QDoubleSpinBox* m_transitionDuration;
    QLabel* m_transitionNote;

    QWidget* m_fadeSection;
    QDoubleSpinBox* m_fadeIn;
    QDoubleSpinBox* m_fadeOut;

    QWidget* m_audioSection;
    QDoubleSpinBox* m_gain;
};

} // namespace lumora

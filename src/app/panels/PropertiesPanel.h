#pragma once

#include <QImage>
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
class QTabBar;
class QStackedWidget;
class QVBoxLayout;
class QFrame;
class QResizeEvent;

namespace lumora {

class Project;
struct Clip;
class CollapsibleSection;
class LookGallery;
class SliderRow;
class SpeedRampView;

// Right-hand panel: the settings of the selected clip, sorted into tabs
// (Edit, Effects, Smart, Sound, Info). Each tab holds cards that fold away;
// only the cards that apply to the selected clip are shown, and tabs without
// any are hidden. Every change is one undo step; dragging a value merges
// into a single step.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    enum Page { EditPage, EffectsPage, SmartPage, SoundPage, InfoPage, PageCount };

    PropertiesPanel(Project* project, QUndoStack* undo, QWidget* parent = nullptr);

    void showClip(const QUuid& clipId);
    void showMedia(const QUuid& mediaId);
    void showNothing();
    void refresh();
    // Puts the cursor in the text of the selected title or subtitle.
    void focusText();
    void showPage(Page page);
    // Small pictures of media items (filter previews).
    void setThumbnailProvider(std::function<QImage(const QUuid&)> provider) { m_thumbnail = std::move(provider); }
    // The colour picked in the viewer for the green screen of a clip.
    void setKeyColor(const QUuid& clipId, const QColor& color);

signals:
    // Long analyses run by the main window (progress dialog, playhead).
    void stabilizeRequested(const QUuid& clipId);
    void trackRequested(const QUuid& clipId);
    void redactRequested(const QUuid& clipId);
    // Background removal needs its model: the main window downloads it and
    // turns the removal on.
    void backgroundRequested(const QUuid& clipId, const QString& model);
    // The user wants to pick the green screen colour in the viewer.
    void keyColorPickRequested(const QUuid& clipId);
    // Opens a workspace with more tools (colour, sound).
    void colorWorkspaceRequested();
    void soundWorkspaceRequested();
    // Smart editing tools for the whole timeline.
    void cutSilencesRequested();
    void beatCutRequested();
    void reframeRequested();

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    // Tab names fit when there is room; otherwise only the open tab keeps
    // its name and the others show just their icon.
    void updateTabLabels();
    CollapsibleSection* makeSection(Page page, const QString& id, const QString& title, QFormLayout** form);
    QToolButton* makeColorButton();
    void setButtonColor(QToolButton* b, const QColor& c);
    // Applies `op` to the selected clip as an undoable, mergeable edit.
    void edit(const QString& text, const QString& field, const std::function<bool(const Clip&)>& op);
    void applyTitle();
    void applyTransform();
    void applyTransition();
    void applyFades();
    void applySubtitleStyle();
    void applySpeed(double speed, bool reverse, SpeedRampPreset ramp);
    void applyEffects(const QString& text, const QString& field, const std::function<void(PictureEffects&)>& change);
    void refreshEditors(const Clip* clip);
    void refreshInfo(const Clip* clip);
    void refreshTabs();
    void rebuildTemplateGallery();

    Project* m_project;
    QUndoStack* m_undo;
    QUuid m_clip;
    QUuid m_media;
    int m_session = 0; // bumps on every selection change so merges stay per selection
    bool m_updating = false;
    std::function<QImage(const QUuid&)> m_thumbnail;

    QFrame* m_headerCard;
    QLabel* m_headerKind;
    QLabel* m_headerName;
    QTabBar* m_tabs;
    QString m_tabNames[PageCount];
    QStackedWidget* m_pages;
    QVBoxLayout* m_pageLayouts[PageCount] = {};
    QLabel* m_emptyHints[PageCount] = {};
    QLabel* m_info;

    CollapsibleSection* m_titleSection;
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
    QComboBox* m_titleDecoration;
    QToolButton* m_titleAccent;
    QDoubleSpinBox* m_titleSpacing;
    QCheckBox* m_titleUpper;
    QComboBox* m_titleAnimIn;
    QComboBox* m_titleAnimOut;
    QDoubleSpinBox* m_titleAnimDuration;
    QDoubleSpinBox* m_titleDuration;
    CollapsibleSection* m_templateSection;
    QListWidget* m_templates;

    CollapsibleSection* m_stabSection;
    QCheckBox* m_stabOn;
    QDoubleSpinBox* m_stabSmoothing;
    QSpinBox* m_stabZoom;
    QLabel* m_stabInfo;

    CollapsibleSection* m_redactSection;
    QLabel* m_redactInfo;
    QPushButton* m_redactFind;
    QPushButton* m_redactClear;
    QCheckBox* m_redactFaces;
    QCheckBox* m_redactPlates;
    QComboBox* m_redactStyle;
    QSpinBox* m_redactStrength;
    QListWidget* m_redactList;

    CollapsibleSection* m_bgSection;
    QCheckBox* m_bgOn;
    QComboBox* m_bgModel;
    QSpinBox* m_bgSoftness;
    QPushButton* m_bgPick;
    QLabel* m_bgInfo;

    CollapsibleSection* m_reframeSection;

    CollapsibleSection* m_trackSection;
    QPushButton* m_trackStart;
    QPushButton* m_trackClear;
    QLabel* m_trackInfo;

    CollapsibleSection* m_freezeSection;
    QDoubleSpinBox* m_freezeDuration;
    QLabel* m_freezeInfo;
    CollapsibleSection* m_stillSection; // pictures and stickers on the timeline
    QDoubleSpinBox* m_stillDuration;
    CollapsibleSection* m_loopSection;  // video clips
    QCheckBox* m_loop;
    QLabel* m_loopInfo;

    CollapsibleSection* m_speedSection;
    QDoubleSpinBox* m_speed;
    QCheckBox* m_speedReverse;
    QComboBox* m_speedRamp;
    SpeedRampView* m_rampView;
    QComboBox* m_speedBlend;
    QCheckBox* m_speedPitch;
    QLabel* m_speedInfo;

    CollapsibleSection* m_subSection;
    QPlainTextEdit* m_subText;
    QDoubleSpinBox* m_subDuration;
    CollapsibleSection* m_subTrackSection;
    QLineEdit* m_subLanguage;
    QFontComboBox* m_subFont;
    QDoubleSpinBox* m_subSize;
    QToolButton* m_subBold;
    QToolButton* m_subColor;
    QDoubleSpinBox* m_subOutline;
    QToolButton* m_subOutlineColor;
    QSpinBox* m_subBackOpacity;
    QDoubleSpinBox* m_subY;

    CollapsibleSection* m_transformSection;
    QDoubleSpinBox* m_posX;
    QDoubleSpinBox* m_posY;
    QDoubleSpinBox* m_scale;
    QDoubleSpinBox* m_rotation;
    QSpinBox* m_opacity;
    QSpinBox* m_cropLeft;
    QSpinBox* m_cropTop;
    QSpinBox* m_cropRight;
    QSpinBox* m_cropBottom;

    CollapsibleSection* m_transitionSection;
    QComboBox* m_transitionType;
    QDoubleSpinBox* m_transitionDuration;
    QLabel* m_transitionNote;

    CollapsibleSection* m_fadeSection;
    QDoubleSpinBox* m_fadeIn;
    QDoubleSpinBox* m_fadeOut;

    CollapsibleSection* m_filterSection;
    LookGallery* m_looks;
    SliderRow* m_lookAmount;
    QPushButton* m_autoColor;
    QUuid m_previewMedia;

    CollapsibleSection* m_keySection;
    QCheckBox* m_keyOn;
    QToolButton* m_keyColor;
    QPushButton* m_keyPick;
    SliderRow* m_keyTolerance;
    SliderRow* m_keySoftness;
    SliderRow* m_keySpill;

    CollapsibleSection* m_fxSection;
    SliderRow* m_fxDenoise;
    SliderRow* m_fxSharpen;
    SliderRow* m_fxBlur;
    SliderRow* m_fxVignette;
    SliderRow* m_fxGrain;

    CollapsibleSection* m_audioSection;
    QDoubleSpinBox* m_gain;
};

} // namespace lumora

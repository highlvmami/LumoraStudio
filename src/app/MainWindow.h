#pragma once

#include <QHash>
#include <QMainWindow>
#include <QUuid>

#include "TimeBase.h"
#include "timeline/Timeline.h"

#include <optional>

class QUndoGroup;
class QUndoStack;
class QStackedWidget;
class QTabBar;
class QDockWidget;
class QAction;
class QLabel;

namespace lumora {

class Project;
class MediaCache;
class PlaybackEngine;
class FullScreenViewer;
class MonitorPanel;
class MediaBinPanel;
class TimelineView;
class TimelinePanel;
class PropertiesPanel;
class PhotoWorkspace;
class ClipColorPanel;
class ScopesWidget;
class AudioPanel;
class HelpPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens a project or imports media files given on the command line.
    void openPaths(const QStringList& paths);
    enum class Workspace { Edit = 0, Color = 1, Audio = 2, Photo = 3, Help = 4 };
    void setWorkspace(Workspace w);

    // Used by --screenshot for the documentation.
    void selectFirstClip(int track = 0, int index = 0);
    void openExportDialog(bool modal);
    void seekSeconds(double s);
    void autoSubtitles();
    // Opens a tool of the Tools menu by name (gif, collage, batch, screen,
    // voiceover); used for the screenshots of the booklet.
    void runTool(const QString& name);
    // Starts following an object with the selected clip.
    // Keeps only an object the user draws a box around, followed through the clip.
    void cutOutObject(const QUuid& clipId);
    void trackClip(const QUuid& clipId = {});
    void showFullScreenPreview();
    void showHelpSection(const QString& id);

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void createActions();
    void createMenus();
    void createDocks();
    void restoreSettings();
    void applyDefaultLayout(Workspace w);
    void selectClipAtPlayhead();
    void updateTitle();
    void markDirty();
    bool maybeSave();

    void newProject();
    void openProject();
    bool loadProject(const QString& path);
    bool saveProject();
    bool saveProjectAs();
    void importMedia();
    void importFiles(const QStringList& paths, Ticks at = kInvalidTicks, bool toTimeline = false, int track = -1);
    // track < 0: insert into the storyline; otherwise place on that track at `at`.
    void addMediaToTimeline(const QList<QUuid>& ids, Ticks at = kInvalidTicks, int track = -1);
    Ticks addRangeToTimeline(const QUuid& media, Ticks in, Ticks out, Ticks at = kInvalidTicks, int track = -1);
    void addTitle(int track = -1, Ticks at = kInvalidTicks);
    // Asks for a picture or video and places it small over the picture at
    // the playhead, on a free track above the storyline.
    void addSticker();
    QUuid placeSticker(const QUuid& mediaId, int track = -1, Ticks at = kInvalidTicks);
    // The lowest video track above V1 with nothing in [at, at + length);
    // adds one when all are taken. Call inside a timeline edit.
    int freeOverlayTrack(Ticks at, Ticks length);
    // Shows move / resize / turn handles for the selected clip in the viewer.
    void updateTransformBox();
    void addTransition();
    void addSubtitle(int track = -1, Ticks at = kInvalidTicks);
    void importSubtitles();
    void exportSubtitles();
    void removeTransition();
    void exportFrame();
    void removeMedia(const QList<QUuid>& ids);
    void exportVideo();
    void splitAtPlayhead();
    void deleteSelection();
    void joinSelection();
    void toggleClipMute();
    void updateMuteAction();
    void freezeFrame();
    void stabilizeClip(const QUuid& clipId);
    void redactClip(const QUuid& clipId);
    void removeBackground(const QUuid& clipId, const QString& model);
    // Lets the user pick the green screen colour of a clip in the viewer.
    void pickKeyColor(const QUuid& clipId);
    // Tools menu.
    void exportGif();
    void makeCollage();
    void batchPhotos();
    void cutSilences();
    void cutToBeat();
    void reframeVertical();
    void recordScreen();
    void recordVoiceover();
    void refreshProgram();
    void showShortcuts();
    QString shortcutsHtml() const;
    void closeFullScreenPreview();
    void setWindowFullScreen(bool on);
    void showAbout();
public:
    void checkForUpdatesOnStartup();
private:
    void setLanguage(const QString& code);

    Project* m_project;
    QUndoGroup* m_undoGroup;
    QUndoStack* m_videoUndo;
    QUndoStack* m_photoUndo;
    MediaCache* m_cache;
    PlaybackEngine* m_engine;

    QTabBar* m_tabs;
    QStackedWidget* m_stack;
    MonitorPanel* m_monitor;
    FullScreenViewer* m_fullScreen = nullptr;
    bool m_wasMaximized = false;
    MediaBinPanel* m_bin;
    TimelineView* m_timelineView;
    TimelinePanel* m_timelinePanel;
    PropertiesPanel* m_properties;
    PhotoWorkspace* m_photo;
    HelpPage* m_help;
    ClipColorPanel* m_clipColor;
    ScopesWidget* m_scopes;
    AudioPanel* m_audio;
    QDockWidget* m_binDock;
    QDockWidget* m_propsDock;
    QDockWidget* m_timelineDock;
    QDockWidget* m_colorDock;
    QDockWidget* m_scopesDock;
    QDockWidget* m_audioDock;
    QLabel* m_statusRight;
    QHash<int, QByteArray> m_dockStates; // per workspace

    bool m_dirty = false;
    Workspace m_workspace = Workspace::Edit;

    QAction* m_actNew;
    QAction* m_actOpen;
    QAction* m_actSave;
    QAction* m_actSaveAs;
    QAction* m_actImport;
    QAction* m_actExport;
    QAction* m_actExportPhoto;
    QAction* m_actQuit;
    QAction* m_actUndo;
    QAction* m_actRedo;
    QAction* m_actSplit;
    QAction* m_actDelete;
    QAction* m_actJoin;
    QAction* m_actFreeze;
    QAction* m_actMuteClip;
    QAction* m_actPlay;
    QAction* m_actPrevFrame;
    QAction* m_actNextFrame;
    QAction* m_actHome;
    QAction* m_actEnd;
    QAction* m_actMarkIn;
    QAction* m_actMarkOut;
    QAction* m_actAddSource;
    QAction* m_actSnap;
    QAction* m_actZoomIn;
    QAction* m_actZoomOut;
    QAction* m_actResetLayout;
    QAction* m_actAddTitle;
    QAction* m_actAddSticker;
    std::optional<TimelineState> m_transformBefore; // while dragging handles in the viewer
    QAction* m_actAddTransition;
    QAction* m_actAddVideoTrack;
    QAction* m_actAddAudioTrack;
    QAction* m_actAddSubtitle;
    QAction* m_actAddSubtitleTrack;
    QAction* m_actImportSubtitles;
    QAction* m_actAutoSubtitles;
    QAction* m_actExportSubtitles;
    QAction* m_actExportFrame;
    QAction* m_actFullScreenPreview;
    QAction* m_actFullScreenWindow;
    QAction* m_actHowTo;
    QAction* m_actGif;
    QAction* m_actCollage;
    QAction* m_actBatchPhotos;
    QAction* m_actRecordScreen;
    QAction* m_actVoiceover;
    QAction* m_actCutSilences;
    QAction* m_actBeatCut;
    QAction* m_actReframe;
    class Updater* m_updater = nullptr;
};

} // namespace lumora

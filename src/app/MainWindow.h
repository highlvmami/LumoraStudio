#pragma once

#include <QMainWindow>
#include <QUuid>

#include "Time.h"

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
class MonitorPanel;
class MediaBinPanel;
class TimelineView;
class TimelinePanel;
class PropertiesPanel;
class PhotoWorkspace;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens a project or imports media files given on the command line.
    void openPaths(const QStringList& paths);
    enum class Workspace { Edit = 0, Color = 1, Audio = 2, Photo = 3 };
    void setWorkspace(Workspace w);

    // Used by --screenshot for the documentation.
    void selectFirstClip();
    void openExportDialog(bool modal);
    void seekSeconds(double s);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void createActions();
    void createMenus();
    void createDocks();
    void restoreSettings();
    void updateTitle();
    void markDirty();
    bool maybeSave();

    void newProject();
    void openProject();
    bool loadProject(const QString& path);
    bool saveProject();
    bool saveProjectAs();
    void importMedia();
    void importFiles(const QStringList& paths, Ticks at = kInvalidTicks, bool toTimeline = false);
    void addMediaToTimeline(const QList<QUuid>& ids, Ticks at = kInvalidTicks);
    void addRangeToTimeline(const QUuid& media, Ticks in, Ticks out, Ticks at = kInvalidTicks);
    void removeMedia(const QList<QUuid>& ids);
    void exportVideo();
    void splitAtPlayhead();
    void deleteSelection();
    void joinSelection();
    void refreshProgram();
    void showShortcuts();
    void showAbout();
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
    MediaBinPanel* m_bin;
    TimelineView* m_timelineView;
    TimelinePanel* m_timelinePanel;
    PropertiesPanel* m_properties;
    PhotoWorkspace* m_photo;
    QDockWidget* m_binDock;
    QDockWidget* m_propsDock;
    QDockWidget* m_timelineDock;
    QLabel* m_statusRight;
    QByteArray m_editDockState;

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
};

} // namespace lumora

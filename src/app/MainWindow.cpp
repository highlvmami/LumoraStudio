#include "MainWindow.h"
#include "Updater.h"
#include "dialogs/SettingsDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QEventLoop>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QComboBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QInputDialog>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <optional>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include "DockTitleBar.h"
#include "Icons.h"
#include "Theme.h"
#include "MediaCache.h"
#include "Tasks.h"
#include "audio/AudioPanel.h"
#include "color/ClipColorPanel.h"
#include "color/ScopesWidget.h"
#include "dialogs/ExportDialog.h"
#include "ffmpeg/FFmpeg.h"
#include "panels/MediaBinPanel.h"
#include "panels/FullScreenViewer.h"
#include "panels/HelpPage.h"
#include "panels/MonitorPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/TimelinePanel.h"
#include "panels/TimelineView.h"
#include "panels/VideoView.h"
#include "playback/PlaybackSequence.h"
#include "photo/PhotoWorkspace.h"
#include "playback/PlaybackEngine.h"
#include "project/Project.h"
#include "ai/Redaction.h"
#include "ai/SpeechToText.h"
#include "ai/Vision.h"
#include "motion/Analysis.h"
#include "render/FrameRenderer.h"
#include "timeline/Subtitles.h"
#include "undo/TimelineCommands.h"
#include "render/Collage.h"
#include "tools/BatchPhotoDialog.h"
#include "tools/CollageDialog.h"
#include "tools/GifDialog.h"
#include "tools/Recorders.h"

namespace lumora {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowIcon(Icons::appIcon());
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
    m_project = new Project(this);
    m_undoGroup = new QUndoGroup(this);
    m_videoUndo = new QUndoStack(m_undoGroup);
    m_photoUndo = new QUndoStack(m_undoGroup);
    m_undoGroup->setActiveStack(m_videoUndo);
    m_cache = new MediaCache(m_project->media(), this);
    m_engine = new PlaybackEngine(this);

    // Workspace tabs.
    m_tabs = new QTabBar;
    m_tabs->setObjectName(QStringLiteral("workspaceTabs"));
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setUsesScrollButtons(false);
    m_tabs->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_tabs->addTab(tr("Edit"));
    m_tabs->addTab(tr("Color"));
    m_tabs->addTab(tr("Audio"));
    m_tabs->addTab(tr("Photo"));
    m_tabs->addTab(tr("How to use"));
    m_tabs->setTabToolTip(1, tr("Colour grading: wheels, LUTs and scopes for the selected clip"));
    m_tabs->setTabToolTip(2, tr("Sound: clean up, equalise and level clips, duck music, set the loudness of the mix"));
    m_tabs->setTabToolTip(4, tr("A booklet that explains every part of LumoraStudio step by step (F1)"));

    m_stack = new QStackedWidget;
    m_monitor = new MonitorPanel(m_engine);
    m_photo = new PhotoWorkspace(m_project->photos(), m_photoUndo);
    makePanelSurface(m_monitor);
    m_stack->addWidget(m_monitor);
    m_stack->addWidget(m_photo);
    m_help = new HelpPage;
    m_help->setShortcutsProvider([this] { return shortcutsHtml(); });
    m_stack->addWidget(m_help);
    setCentralWidget(m_stack);

    m_bin = new MediaBinPanel(m_project->media(), m_cache);
    m_timelineView = new TimelineView(m_project->timeline(), m_project->media(), m_cache, m_videoUndo);
    m_timelinePanel = new TimelinePanel(m_timelineView);
    m_properties = new PropertiesPanel(m_project, m_videoUndo);
    connect(m_properties, &PropertiesPanel::stabilizeRequested, this, &MainWindow::stabilizeClip);
    connect(m_properties, &PropertiesPanel::trackRequested, this, &MainWindow::trackClip);
    connect(m_properties, &PropertiesPanel::redactRequested, this, &MainWindow::redactClip);
    connect(m_properties, &PropertiesPanel::backgroundRequested, this, &MainWindow::removeBackground);
    connect(m_properties, &PropertiesPanel::keyColorPickRequested, this, &MainWindow::pickKeyColor);
    connect(m_properties, &PropertiesPanel::colorWorkspaceRequested, this, [this] { setWorkspace(Workspace::Color); });
    connect(m_properties, &PropertiesPanel::soundWorkspaceRequested, this, [this] { setWorkspace(Workspace::Audio); });
    m_properties->setThumbnailProvider([this](const QUuid& id) { return m_cache->thumbnail(id); });
    m_clipColor = new ClipColorPanel(m_project, m_videoUndo);
    m_clipColor->setThumbnailProvider([this](const QUuid& id) { return m_cache->thumbnail(id); });
    connect(m_cache, &MediaCache::thumbnailReady, m_clipColor, &ClipColorPanel::refresh);
    m_scopes = new ScopesWidget;
    m_audio = new AudioPanel(m_project, m_videoUndo);

    createActions();
    createDocks();
    createMenus();

    // Top bar: the brand, the menus, the workspace switcher and the main
    // actions share one row as corner widgets of the menu bar.
    auto* brand = new QWidget;
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(6, 0, 10, 0);
    brandLayout->setSpacing(8);
    auto* logo = new QLabel;
    logo->setPixmap(Icons::appIcon().pixmap(QSize(22, 22)));
    auto* wordmark = new QLabel(QStringLiteral("<span style='font-weight:700'>Lumora</span>"
                                               "<span style='font-weight:400'>Studio</span>"));
    wordmark->setObjectName(QStringLiteral("brandMark"));
    brandLayout->addWidget(logo);
    brandLayout->addWidget(wordmark);

    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("workspaceBar"));
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 4, 0);
    barLayout->setSpacing(4);
    auto* switcher = new QFrame;
    switcher->setObjectName(QStringLiteral("workspaceSwitcher"));
    auto* switcherLayout = new QHBoxLayout(switcher);
    switcherLayout->setContentsMargins(2, 0, 2, 0);
    switcherLayout->addWidget(m_tabs);
    barLayout->addWidget(switcher);
    barLayout->addSpacing(14);
    for (QAction* a : {m_actImport, m_actUndo, m_actRedo, m_actFullScreenWindow}) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setIconSize(QSize(18, 18));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        barLayout->addWidget(b);
    }
    barLayout->addSpacing(8);
    // Export is the main call to action: a brand-gradient button.
    auto* exportButton = new QToolButton;
    exportButton->setObjectName(QStringLiteral("primaryAction"));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setIconSize(QSize(16, 16));
    exportButton->setIcon(Icons::exportFile(themeColors().onAccent));
    exportButton->setText(m_actExport->iconText());
    exportButton->setToolTip(m_actExport->toolTip());
    exportButton->setEnabled(m_actExport->isEnabled());
    connect(exportButton, &QToolButton::clicked, m_actExport, &QAction::trigger);
    connect(m_actExport, &QAction::changed, exportButton, [this, exportButton] {
        exportButton->setEnabled(m_actExport->isEnabled());
        exportButton->setText(m_actExport->iconText());
    });
    barLayout->addWidget(exportButton);
    if (menuBar()->isNativeMenuBar()) {
        // macOS keeps menus at the top of the screen: use a tool bar instead.
        auto* host = new QToolBar(tr("Workspaces"));
        host->setObjectName(QStringLiteral("workspaceToolBar"));
        host->setMovable(false);
        host->addWidget(brand);
        auto* spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        host->addWidget(spacer);
        host->addWidget(bar);
        addToolBar(Qt::TopToolBarArea, host);
    } else {
        menuBar()->setCornerWidget(brand, Qt::TopLeftCorner);
        menuBar()->setCornerWidget(bar, Qt::TopRightCorner);
    }
    setContentsMargins(8, 0, 8, 4);

    m_statusRight = new QLabel(tr("FFmpeg %1 · Qt %2").arg(av::versionString(), QString::fromLatin1(qVersion())));
    statusBar()->addPermanentWidget(m_statusRight);

    // --- wiring ---
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) { setWorkspace(static_cast<Workspace>(i)); });

    connect(m_monitor->view(), &VideoView::transformEdited, this, [this](const ClipTransform& t, bool finished) {
        Timeline* tl = m_project->timeline();
        const auto sel = m_timelineView->selection();
        if (sel.isEmpty())
            return;
        // Live while dragging; one undo step for the whole drag.
        if (!m_transformBefore)
            m_transformBefore = tl->state();
        tl->setTransform(sel.front(), t);
        if (finished) {
            if (!(*m_transformBefore == tl->state()))
                pushTimelineSnapshot(m_videoUndo, tl, *m_transformBefore, tr("Move picture"));
            m_transformBefore.reset();
            updateTransformBox();
        }
    });
    connect(m_project->timeline(), &Timeline::changed, this, [this] {
        updateTransformBox();
        refreshProgram();
        markDirty();
        updateMuteAction();
    });
    connect(m_project->media(), &MediaPool::itemAdded, this, &MainWindow::markDirty);
    connect(m_project->photos(), &PhotoLibrary::adjustmentsChanged, this, &MainWindow::markDirty);
    connect(m_project->photos(), &PhotoLibrary::added, this, &MainWindow::markDirty);
    connect(m_cache, &MediaCache::keyframesReady, m_properties, &PropertiesPanel::refresh);
    connect(m_cache, &MediaCache::thumbnailReady, m_properties, &PropertiesPanel::refresh);

    connect(m_engine, &PlaybackEngine::positionChanged, this, [this](Ticks t) {
        updateTransformBox();
        if (m_monitor->mode() == MonitorPanel::Mode::Program) {
            m_timelineView->setPlayhead(t);
            if (m_engine->isPlaying())
                m_timelineView->ensurePlayheadVisible();
        }
    });
    connect(m_engine, &PlaybackEngine::frameReady, m_scopes, [this](const QImage& img) { m_scopes->setImage(img); });
    connect(m_engine, &PlaybackEngine::playingChanged, this,
            [this](bool playing) { m_actPlay->setIcon(playing ? Icons::pause() : Icons::play()); });
    connect(m_timelineView, &TimelineView::seekRequested, this, [this](Ticks t) {
        if (m_monitor->mode() != MonitorPanel::Mode::Program)
            m_monitor->showProgram(PlaybackSequence::fromTimeline(m_project->timeline()->state(), *m_project->media(),
                                                                  m_project->timeline()->frameRate()),
                                   true);
        m_engine->seek(t);
        m_timelineView->setPlayhead(t);
    });
    connect(m_timelineView, &TimelineView::selectionChanged, this, [this] {
        const auto sel = m_timelineView->selection();
        if (sel.isEmpty())
            m_properties->showNothing();
        else
            m_properties->showClip(sel.front());
        m_clipColor->showClip(sel.isEmpty() ? QUuid() : sel.front());
        m_audio->showClip(sel.isEmpty() ? QUuid() : sel.front());
        const bool has = !sel.isEmpty();
        updateTransformBox();
        m_actDelete->setEnabled(has);
        m_actJoin->setEnabled(has && m_project->timeline()->canJoinWithNext(sel.front()));
        updateMuteAction();
    });
    connect(m_timelineView, &TimelineView::clipDoubleClicked, this, [this](const QUuid& id) {
        if (const Clip* c = m_project->timeline()->clip(id); c && c->hasMedia())
            if (auto media = m_project->media()->find(c->mediaId)) {
                m_monitor->showSource(media);
                m_engine->seek(c->mediaRange().first);
            }
    });
    connect(m_timelineView, &TimelineView::mediaDropped, this,
            [this](const QList<QUuid>& ids, Ticks at, int track) { addMediaToTimeline(ids, at, track); });
    connect(m_timelineView, &TimelineView::filesDropped, this,
            [this](const QStringList& paths, Ticks at, int track) { importFiles(paths, at, true, track); });
    connect(m_timelineView, &TimelineView::addTitleRequested, this, [this](int track, Ticks at) { addTitle(track, at); });
    connect(m_timelineView, &TimelineView::addSubtitleRequested, this, [this](int track, Ticks at) { addSubtitle(track, at); });
    connect(m_timelineView, &TimelineView::contextMenuForClip, this, [this](const QUuid& id, const QPoint& pos) {
        QMenu menu;
        menu.addAction(m_actSplit);
        menu.addAction(m_actJoin);
        menu.addAction(m_actFreeze);
        menu.addAction(m_actMuteClip);
        if (auto loc = m_project->timeline()->locate(id); loc && loc->track == 0) {
            menu.addSeparator();
            menu.addAction(m_actAddTransition);
            const Clip* c = m_project->timeline()->clip(id);
            if (c && !c->transitionIn.isNone())
                menu.addAction(tr("Remove transition"), this, &MainWindow::removeTransition);
        }
        menu.addSeparator();
        menu.addAction(m_actDelete);
        menu.exec(pos);
    });

    connect(m_bin, &MediaBinPanel::importRequested, this, &MainWindow::importMedia);
    connect(m_bin, &MediaBinPanel::filesDropped, this, [this](const QStringList& p) { importFiles(p); });
    connect(m_bin, &MediaBinPanel::activated, this, [this](const QUuid& id) {
        auto media = m_project->media()->find(id);
        if (!media)
            return;
        if (media->info.kind == MediaKind::Image) {
            m_photo->openFiles({media->path});
            setWorkspace(Workspace::Photo);
        } else {
            m_monitor->showSource(media);
        }
    });
    connect(m_bin, &MediaBinPanel::selectionChanged, this, [this](const QUuid& id) {
        if (!id.isNull())
            m_properties->showMedia(id);
    });
    connect(m_bin, &MediaBinPanel::addToTimeline, this, [this](const QList<QUuid>& ids) { addMediaToTimeline(ids); });
    connect(m_bin, &MediaBinPanel::removeRequested, this, &MainWindow::removeMedia);

    connect(m_monitor, &MonitorPanel::addToTimelineRequested, this,
            [this](const QUuid& id, Ticks in, Ticks out) { addRangeToTimeline(id, in, out); });
    connect(m_monitor, &MonitorPanel::filesDropped, this, [this](const QStringList& p) { importFiles(p, kInvalidTicks, true); });
    connect(m_monitor, &MonitorPanel::mediaDropped, this, [this](const QList<QUuid>& ids) { addMediaToTimeline(ids); });
    connect(m_monitor, &MonitorPanel::modeChanged, this, [this](MonitorPanel::Mode m) {
        updateTransformBox();
        const bool source = m == MonitorPanel::Mode::Source;
        m_actMarkIn->setEnabled(source);
        m_actMarkOut->setEnabled(source);
        m_actAddSource->setEnabled(source);
        if (source && m_monitor->sourceItem())
            m_properties->showMedia(m_monitor->sourceItem()->id);
    });
    connect(m_photo, &PhotoWorkspace::statusMessage, this, [this](const QString& s) { statusBar()->showMessage(s, 6000); });
    connect(m_monitor, &MonitorPanel::fullScreenRequested, this, &MainWindow::showFullScreenPreview);
    connect(m_photo, &PhotoWorkspace::fullScreenRequested, this, &MainWindow::showFullScreenPreview);
    connect(m_photo, &PhotoWorkspace::previewChanged, this, [this] {
        if (m_fullScreen && m_fullScreen->isVisible() && !m_fullScreen->isVideo())
            m_fullScreen->setPhoto(m_photo->previewImage(), m_photo->currentTitle());
    });

    restoreSettings();
    refreshProgram();
    updateTitle();
    m_actDelete->setEnabled(false);
    m_actMuteClip->setEnabled(false);
    m_actJoin->setEnabled(false);
    m_actMarkIn->setEnabled(false);
    m_actMarkOut->setEnabled(false);
    m_actAddSource->setEnabled(false);
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions() {
    auto make = [this](const QString& text, const QKeySequence& key, auto slot, const QIcon& icon = {}) {
        auto* a = new QAction(icon, text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };
    m_actNew = make(tr("&New project"), QKeySequence::New, &MainWindow::newProject);
    m_actOpen = make(tr("&Open project…"), QKeySequence::Open, &MainWindow::openProject);
    m_actSave = make(tr("&Save project"), QKeySequence::Save, &MainWindow::saveProject);
    m_actSaveAs = make(tr("Save project &as…"), QKeySequence::SaveAs, &MainWindow::saveProjectAs);
    m_actImport = make(tr("&Import media…"), QKeySequence(Qt::CTRL | Qt::Key_I), &MainWindow::importMedia, Icons::import());
    m_actExport = make(tr("&Export video…"), QKeySequence(Qt::CTRL | Qt::Key_E), &MainWindow::exportVideo, Icons::exportFile());
    m_actExportPhoto = make(tr("Export &photo…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E),
                            [this] { m_photo->exportCurrent(); });
    m_actQuit = make(tr("&Quit"), QKeySequence::Quit, &QWidget::close);
    m_actQuit->setMenuRole(QAction::QuitRole);

    m_actUndo = m_undoGroup->createUndoAction(this, tr("Undo"));
    m_actUndo->setShortcut(QKeySequence::Undo);
    m_actUndo->setIcon(Icons::undo());
    m_actRedo = m_undoGroup->createRedoAction(this, tr("Redo"));
    m_actRedo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    m_actRedo->setIcon(Icons::redo());
    addAction(m_actUndo);
    addAction(m_actRedo);

    m_actSplit = make(tr("&Split at playhead"), QKeySequence(Qt::Key_S), &MainWindow::splitAtPlayhead, Icons::split());
    m_actSplit->setShortcuts({QKeySequence(Qt::Key_S), QKeySequence(Qt::CTRL | Qt::Key_B)});
    m_actDelete = make(tr("&Delete clip (close gap)"), QKeySequence::Delete, &MainWindow::deleteSelection, Icons::remove());
    m_actDelete->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    m_actJoin = make(tr("&Join with next clip"), QKeySequence(Qt::CTRL | Qt::Key_J), &MainWindow::joinSelection, Icons::join());
    m_actJoin->setToolTip(tr("Rejoin a clip with the next one when they are consecutive parts of the same file"));
    m_actFreeze = make(tr("&Freeze frame…"), QKeySequence(Qt::SHIFT | Qt::Key_F), &MainWindow::freezeFrame, Icons::freeze());
    m_actFreeze->setToolTip(tr("Hold the frame under the playhead for a number of seconds; the rest of the clip continues after it"));
    m_actMuteClip = make(tr("&Mute clip sound"), QKeySequence(Qt::Key_M), &MainWindow::toggleClipMute, Icons::sound());
    m_actMuteClip->setCheckable(true);
    m_actMuteClip->setToolTip(tr("Silence only the selected clips; the rest of the track keeps its sound (M)"));
    m_actPlay = make(tr("Play / pause"), QKeySequence(Qt::Key_Space), [this] { m_engine->togglePlay(); }, Icons::play());
    m_actPrevFrame = make(tr("Previous frame"), QKeySequence(Qt::Key_Left), [this] { m_engine->stepFrames(-1); });
    m_actNextFrame = make(tr("Next frame"), QKeySequence(Qt::Key_Right), [this] { m_engine->stepFrames(1); });
    m_actHome = make(tr("Go to start"), QKeySequence(Qt::Key_Home), [this] { m_engine->seek(0); });
    m_actEnd = make(tr("Go to end"), QKeySequence(Qt::Key_End), [this] { m_engine->seek(m_engine->duration()); });
    m_actMarkIn = make(tr("Set in point"), QKeySequence(Qt::Key_I), [this] { m_monitor->setMarkIn(); }, Icons::markIn());
    m_actMarkOut = make(tr("Set out point"), QKeySequence(Qt::Key_O), [this] { m_monitor->setMarkOut(); }, Icons::markOut());
    m_actAddSource = make(tr("Add source range to timeline"), QKeySequence(Qt::Key_E),
                          [this] { m_monitor->addSourceToTimeline(); }, Icons::add());
    m_actSnap = make(tr("Snapping"), QKeySequence(Qt::Key_N), [this](bool on) { m_timelineView->setSnapping(on); }, Icons::magnet());
    m_actSnap->setCheckable(true);
    m_actSnap->setChecked(true);
    m_actSnap->setToolTip(tr("Snap the playhead to clip edges (N)"));
    m_actZoomIn = make(tr("Zoom in"), QKeySequence(Qt::Key_Plus),
                       [this] { m_timelineView->setPixelsPerSecond(m_timelineView->pixelsPerSecond() * 1.5); });
    m_actZoomIn->setShortcuts({QKeySequence(Qt::Key_Plus), QKeySequence(Qt::Key_Equal)});
    m_actZoomOut = make(tr("Zoom out"), QKeySequence(Qt::Key_Minus),
                        [this] { m_timelineView->setPixelsPerSecond(m_timelineView->pixelsPerSecond() / 1.5); });
    m_actResetLayout = make(tr("Reset panel layout"), {}, [this] {
        if (m_workspace != Workspace::Photo && m_workspace != Workspace::Help)
            applyDefaultLayout(m_workspace);
    });

    m_actAddTitle = make(tr("Add &title"), QKeySequence(Qt::Key_T), [this] { addTitle(); }, Icons::title());
    m_actAddTitle->setToolTip(tr("Add a title at the playhead on a track above the storyline (T)"));
    m_actAddSticker = make(tr("Add stic&ker…"), QKeySequence(Qt::SHIFT | Qt::Key_K), [this] { addSticker(); }, Icons::sticker());
    m_actAddSticker->setToolTip(tr("Put a picture, animated GIF or video on top of the video at the playhead (Shift+K)"));
    m_actAddTransition = make(tr("Add t&ransition"), QKeySequence(Qt::CTRL | Qt::Key_T), &MainWindow::addTransition,
                              Icons::transition());
    m_actAddTransition->setToolTip(tr("Cross dissolve into the selected clip, or at the cut nearest the playhead (Ctrl+T)"));
    m_actAddVideoTrack = make(tr("Add video track"), {}, [this] {
        runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Add video track"),
                        [&] { return m_project->timeline()->addTrack(TrackType::Video) >= 0; });
    });
    m_actAddAudioTrack = make(tr("Add audio track"), {}, [this] {
        runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Add audio track"),
                        [&] { return m_project->timeline()->addTrack(TrackType::Audio) >= 0; });
    });
    m_actAddSubtitle = make(tr("Add &subtitle"), QKeySequence(Qt::SHIFT | Qt::Key_T), [this] { addSubtitle(); });
    m_actAddSubtitle->setToolTip(tr("Add a subtitle at the playhead (Shift+T)"));
    m_actAddSubtitleTrack = make(tr("Add subtitle track"), {}, [this] {
        runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Add subtitle track"),
                        [&] { return m_project->timeline()->addTrack(TrackType::Subtitle) >= 0; });
    });
    m_actImportSubtitles = make(tr("Import subtitles (SRT, VTT)…"), {}, &MainWindow::importSubtitles);
    m_actAutoSubtitles = make(tr("&Automatic subtitles from speech…"), {}, &MainWindow::autoSubtitles, Icons::title());
    m_actAutoSubtitles->setToolTip(tr("Writes subtitles from what is said, on this computer (Whisper)"));
    m_actExportSubtitles = make(tr("Export subtitles…"), {}, &MainWindow::exportSubtitles);
    m_actExportFrame = make(tr("Export current &frame…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),
                            &MainWindow::exportFrame);
    m_actFullScreenPreview = make(tr("Full-screen &preview"), QKeySequence(Qt::Key_F), &MainWindow::showFullScreenPreview);
    m_actFullScreenPreview->setToolTip(tr("Show the video or photo on the whole screen (F, Esc leaves)"));
    m_actFullScreenWindow = make(tr("Full-screen &window"), QKeySequence(Qt::Key_F11), &MainWindow::setWindowFullScreen,
                                 Icons::fullScreen());
    m_actFullScreenWindow->setToolTip(tr("Fill the whole screen with LumoraStudio (F11)"));
    m_actFullScreenWindow->setShortcuts({QKeySequence(Qt::Key_F11), QKeySequence::FullScreen});
    m_actFullScreenWindow->setCheckable(true);
    m_actHowTo = make(tr("&How to use"), QKeySequence::HelpContents, [this] { setWorkspace(Workspace::Help); });
    m_actHowTo->setToolTip(tr("Open the booklet that explains every part of LumoraStudio (F1)"));
    m_actGif = make(tr("Save as &GIF…"), {}, &MainWindow::exportGif, Icons::exportFile());
    m_actGif->setToolTip(tr("Save the video or a part of it as an animated GIF"));
    m_actCollage = make(tr("Video &collage…"), {}, &MainWindow::makeCollage, Icons::collage());
    m_actCollage->setToolTip(tr("Show several videos or pictures side by side in one frame"));
    m_actBatchPhotos = make(tr("Process many &photos…"), {}, &MainWindow::batchPhotos, Icons::batch());
    m_actBatchPhotos->setToolTip(tr("Resize, colour, watermark and convert many photos at once"));
    m_actRecordScreen = make(tr("Record the &screen…"), {}, &MainWindow::recordScreen, Icons::screen());
    m_actRecordScreen->setToolTip(tr("Record what happens on the screen, with your voice if you like"));
    m_actRecordScreen->setEnabled(ScreenRecordDialog::available());
    m_actVoiceover = make(tr("Record &voice-over…"), {}, &MainWindow::recordVoiceover, Icons::microphone());
    m_actVoiceover->setToolTip(tr("Speak over the video while it plays; the recording goes on a sound track"));

    auto* tb = m_timelinePanel->toolBar();
    tb->addAction(m_actSplit);
    tb->addAction(m_actJoin);
    tb->addAction(m_actFreeze);
    tb->addAction(m_actDelete);
    tb->addSeparator();
    tb->addAction(m_actAddTitle);
    tb->addAction(m_actAddSticker);
    tb->addAction(m_actAddTransition);
    tb->addSeparator();
    tb->addAction(m_actSnap);
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_actNew);
    file->addAction(m_actOpen);
    file->addAction(m_actSave);
    file->addAction(m_actSaveAs);
    file->addSeparator();
    file->addAction(m_actImport);
    file->addSeparator();
    file->addAction(m_actExport);
    file->addAction(m_actExportFrame);
    file->addAction(m_actGif);
    file->addAction(m_actExportPhoto);
    file->addSeparator();
    file->addAction(m_actQuit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actUndo);
    edit->addAction(m_actRedo);
    edit->addSeparator();
    edit->addAction(m_actSplit);
    edit->addAction(m_actJoin);
    edit->addAction(m_actFreeze);
    edit->addAction(m_actMuteClip);
    edit->addAction(m_actDelete);
    edit->addSeparator();
    edit->addAction(m_actAddTitle);
    edit->addAction(m_actAddSticker);
    edit->addAction(m_actAddTransition);
    edit->addAction(m_actAddVideoTrack);
    edit->addAction(m_actAddAudioTrack);
    QMenu* subs = edit->addMenu(tr("Subtitles"));
    subs->addAction(m_actAddSubtitle);
    subs->addAction(m_actAddSubtitleTrack);
    subs->addAction(m_actAutoSubtitles);
    subs->addSeparator();
    subs->addAction(m_actImportSubtitles);
    subs->addAction(m_actExportSubtitles);
    edit->addSeparator();
    edit->addAction(m_actMarkIn);
    edit->addAction(m_actMarkOut);
    edit->addAction(m_actAddSource);
    edit->addSeparator();
    edit->addAction(m_actSnap);
    edit->addSeparator();
    QAction* settings = edit->addAction(tr("&Settings…"), this, [this] {
        SettingsDialog dlg(m_updater, this);
        dlg.exec();
    });
    settings->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    settings->setMenuRole(QAction::PreferencesRole);

    QMenu* play = menuBar()->addMenu(tr("&Playback"));
    play->addAction(m_actPlay);
    play->addAction(m_actPrevFrame);
    play->addAction(m_actNextFrame);
    play->addAction(m_actHome);
    play->addAction(m_actEnd);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_actFullScreenPreview);
    view->addAction(m_actFullScreenWindow);
    view->addSeparator();
    view->addAction(m_actZoomIn);
    view->addAction(m_actZoomOut);
    view->addSeparator();
    QMenu* theme = view->addMenu(tr("Theme"));
    auto* themeGroup = new QActionGroup(this);
    const QString currentTheme = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString();
    for (const auto& [code, label] : {std::pair{QStringLiteral("dark"), tr("Dark")}, std::pair{QStringLiteral("light"), tr("Light")}}) {
        QAction* a = theme->addAction(label);
        a->setCheckable(true);
        a->setChecked(code == currentTheme);
        themeGroup->addAction(a);
        connect(a, &QAction::triggered, this, [code] {
            QSettings().setValue(QStringLiteral("ui/theme"), code);
            QMessageBox::information(nullptr, QStringLiteral("LumoraStudio"),
                                     tr("The theme changes the next time LumoraStudio starts."));
        });
    }
    QMenu* lang = view->addMenu(tr("Language"));
    auto* langGroup = new QActionGroup(this);
    const QString currentLang = QSettings().value(QStringLiteral("ui/language")).toString();
    for (const auto& [code, label] : {std::pair{QString(), tr("System default")}, std::pair{QStringLiteral("tr"), QStringLiteral("Türkçe")},
                                      std::pair{QStringLiteral("en"), QStringLiteral("English")}}) {
        QAction* a = lang->addAction(label);
        a->setCheckable(true);
        a->setChecked(code == currentLang);
        langGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, code] { setLanguage(code); });
    }
    view->addSeparator();
    QMenu* panels = view->addMenu(tr("Panels"));
    panels->addAction(m_binDock->toggleViewAction());
    panels->addAction(m_propsDock->toggleViewAction());
    panels->addAction(m_timelineDock->toggleViewAction());
    panels->addAction(m_colorDock->toggleViewAction());
    panels->addAction(m_scopesDock->toggleViewAction());
    panels->addAction(m_audioDock->toggleViewAction());
    view->addAction(m_actResetLayout);

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(m_actRecordScreen);
    tools->addAction(m_actVoiceover);
    tools->addSeparator();
    tools->addAction(m_actCollage);
    tools->addAction(m_actGif);
    tools->addSeparator();
    tools->addAction(m_actBatchPhotos);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_actHowTo);
    help->addAction(tr("Keyboard shortcuts"), this, &MainWindow::showShortcuts);
    help->addAction(tr("Feature list (roadmap)"), this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/highlvmami/LumoraStudio/blob/main/docs/ozellikler.md")));
    });
    help->addSeparator();
    m_updater = new Updater(this);
    help->addAction(tr("Check for updates…"), m_updater, &Updater::checkNow);
    help->addSeparator();
    QAction* about = help->addAction(tr("About LumoraStudio"), this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
}

void MainWindow::createDocks() {
    auto makeDock = [this](const QString& title, const QString& name, QWidget* w) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        makePanelSurface(w);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
        d->setTitleBarWidget(new DockTitleBar(d));
        return d;
    };
    m_binDock = makeDock(tr("Files"), QStringLiteral("filesDock"), m_bin);
    m_propsDock = makeDock(tr("Properties"), QStringLiteral("propertiesDock"), m_properties);
    m_timelineDock = makeDock(tr("Timeline"), QStringLiteral("timelineDock"), m_timelinePanel);
    m_colorDock = makeDock(tr("Color"), QStringLiteral("colorDock"), m_clipColor);
    m_scopesDock = makeDock(tr("Scopes"), QStringLiteral("scopesDock"), m_scopes);
    m_audioDock = makeDock(tr("Audio"), QStringLiteral("audioDock"), m_audio);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_binDock);
    addDockWidget(Qt::RightDockWidgetArea, m_propsDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
    resizeDocks({m_timelineDock}, {280}, Qt::Vertical);
    resizeDocks({m_binDock, m_propsDock}, {300, 280}, Qt::Horizontal);
    // Colour docks belong to the Color workspace.
    addDockWidget(Qt::RightDockWidgetArea, m_colorDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_scopesDock);
    addDockWidget(Qt::RightDockWidgetArea, m_audioDock);
    m_colorDock->hide();
    m_scopesDock->hide();
    m_audioDock->hide();
}

void MainWindow::applyDefaultLayout(Workspace w) {
    const bool color = w == Workspace::Color;
    const bool audio = w == Workspace::Audio;
    for (QDockWidget* d : {m_binDock, m_propsDock, m_timelineDock, m_colorDock, m_scopesDock, m_audioDock})
        d->setFloating(false);
    addDockWidget(Qt::LeftDockWidgetArea, color ? m_scopesDock : m_binDock);
    addDockWidget(Qt::RightDockWidgetArea, color ? m_colorDock : audio ? m_audioDock : m_propsDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
    m_binDock->setVisible(!color);
    m_propsDock->setVisible(!color && !audio);
    m_scopesDock->setVisible(color);
    m_colorDock->setVisible(color);
    m_audioDock->setVisible(audio);
    m_timelineDock->show();
    resizeDocks({m_timelineDock}, {color ? 230 : 280}, Qt::Vertical);
    if (color)
        resizeDocks({m_scopesDock, m_colorDock}, {380, 360}, Qt::Horizontal);
    else if (audio)
        resizeDocks({m_binDock, m_audioDock}, {280, 340}, Qt::Horizontal);
    else
        resizeDocks({m_binDock, m_propsDock}, {300, 280}, Qt::Horizontal);
}

void MainWindow::restoreSettings() {
    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1440, 900);
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
}

void MainWindow::setWorkspace(Workspace w) {
    const bool photo = w == Workspace::Photo;
    const bool help = w == Workspace::Help;
    // Photo and the help booklet fill the window without the video panels.
    const bool fullPage = photo || help;
    const Workspace previous = m_workspace;
    if (w != previous) {
        // Each workspace keeps its own panel arrangement.
        if (previous != Workspace::Photo && previous != Workspace::Help)
            m_dockStates[static_cast<int>(previous)] = saveState();
        if (fullPage)
            m_engine->pause();
        // The booklet always opens on its "pick a section" page.
        if (help)
            m_help->showWelcome();
    }
    m_workspace = w;
    {
        const QSignalBlocker b(m_tabs);
        m_tabs->setCurrentIndex(static_cast<int>(w));
    }
    m_stack->setCurrentIndex(help ? 2 : photo ? 1 : 0);
    if (fullPage) {
        for (QDockWidget* d : {m_binDock, m_propsDock, m_timelineDock, m_colorDock, m_scopesDock, m_audioDock})
            d->hide();
    } else if (w != previous) {
        const QByteArray state = m_dockStates.value(static_cast<int>(w));
        if (state.isEmpty() || !restoreState(state))
            applyDefaultLayout(w);
    }
    if ((w == Workspace::Color || w == Workspace::Audio) && m_timelineView->selection().isEmpty())
        selectClipAtPlayhead();
    if (!help)
        m_undoGroup->setActiveStack(photo ? m_photoUndo : m_videoUndo);
    for (QAction* a : {m_actSplit, m_actDelete, m_actJoin, m_actFreeze, m_actMuteClip, m_actPlay, m_actPrevFrame, m_actNextFrame, m_actHome, m_actEnd,
                       m_actMarkIn, m_actMarkOut, m_actAddSource, m_actZoomIn, m_actZoomOut, m_actExport, m_actAddTitle, m_actAddSticker,
                       m_actAddTransition, m_actAddVideoTrack, m_actAddAudioTrack, m_actExportFrame, m_actAddSubtitle,
                       m_actAddSubtitleTrack, m_actImportSubtitles, m_actExportSubtitles, m_actAutoSubtitles})
        a->setVisible(!fullPage);
    m_actExportPhoto->setVisible(photo);
}

void MainWindow::showHelpSection(const QString& id) {
    setWorkspace(Workspace::Help);
    m_help->showSection(id);
}

void MainWindow::updateTitle() {
    const QString name = m_project->filePath().isEmpty() ? tr("Untitled") : QFileInfo(m_project->filePath()).completeBaseName();
    setWindowTitle(QStringLiteral("%1%2 — LumoraStudio").arg(name, m_dirty ? QStringLiteral(" *") : QString()));
}

void MainWindow::markDirty() {
    if (m_dirty)
        return;
    m_dirty = true;
    updateTitle();
}

bool MainWindow::maybeSave() {
    if (!m_dirty)
        return true;
    const auto r = QMessageBox::question(this, tr("Unsaved changes"), tr("Save the changes to this project?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save)
        return saveProject();
    return r == QMessageBox::Discard;
}

void MainWindow::checkForUpdatesOnStartup() {
    if (m_updater)
        m_updater->checkOnStartup();
}

void MainWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::WindowStateChange) {
        m_actFullScreenWindow->setChecked(isFullScreen());
        m_actFullScreenWindow->setIcon(isFullScreen() ? Icons::exitFullScreen() : Icons::fullScreen());
        m_actFullScreenWindow->setToolTip(isFullScreen() ? tr("Leave full screen (F11)")
                                                         : tr("Fill the whole screen with LumoraStudio (F11)"));
    }
    QMainWindow::changeEvent(e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    m_engine->pause();
    // The saved layout is the Edit workspace's.
    if (m_workspace != Workspace::Edit && m_dockStates.contains(static_cast<int>(Workspace::Edit)))
        restoreState(m_dockStates.value(static_cast<int>(Workspace::Edit)));
    QSettings s;
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("window/state"), saveState());
    e->accept();
}

void MainWindow::newProject() {
    if (!maybeSave())
        return;
    m_engine->pause();
    m_project->clear();
    m_videoUndo->clear();
    m_photoUndo->clear();
    m_dirty = false;
    refreshProgram();
    updateTitle();
}

void MainWindow::openProject() {
    if (!maybeSave())
        return;
    QSettings s;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"), s.value(QStringLiteral("dirs/project")).toString(),
                                                      tr("LumoraStudio projects (*.lumora)"));
    if (!path.isEmpty())
        loadProject(path);
}

bool MainWindow::loadProject(const QString& path) {
    m_engine->pause();
    QString err;
    QStringList missing;
    if (!m_project->load(path, &err, &missing)) {
        QMessageBox::critical(this, tr("Open project"), tr("Cannot open %1:\n%2").arg(path, err));
        return false;
    }
    QSettings().setValue(QStringLiteral("dirs/project"), QFileInfo(path).absolutePath());
    m_videoUndo->clear();
    m_photoUndo->clear();
    m_dirty = false;
    refreshProgram();
    m_timelineView->zoomToFit();
    updateTitle();
    if (!m_project->photos()->entries().empty())
        m_photo->showPhoto(m_project->photos()->entries().front().id);
    if (!missing.isEmpty())
        QMessageBox::warning(this, tr("Missing media"),
                             tr("These files could not be found. They stay in the project; move them back or "
                                "relink them in a later version:\n\n%1")
                                 .arg(missing.join(QLatin1Char('\n'))));
    return true;
}

bool MainWindow::saveProject() {
    if (m_project->filePath().isEmpty())
        return saveProjectAs();
    QString err;
    if (!m_project->save(m_project->filePath(), &err)) {
        QMessageBox::critical(this, tr("Save project"), err);
        return false;
    }
    m_dirty = false;
    updateTitle();
    statusBar()->showMessage(tr("Project saved"), 4000);
    return true;
}

bool MainWindow::saveProjectAs() {
    QSettings s;
    QString path = QFileDialog::getSaveFileName(this, tr("Save project"),
                                                s.value(QStringLiteral("dirs/project")).toString() + QStringLiteral("/project.lumora"),
                                                tr("LumoraStudio projects (*.lumora)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".lumora")))
        path += QStringLiteral(".lumora");
    s.setValue(QStringLiteral("dirs/project"), QFileInfo(path).absolutePath());
    QString err;
    if (!m_project->save(path, &err)) {
        QMessageBox::critical(this, tr("Save project"), err);
        return false;
    }
    m_dirty = false;
    updateTitle();
    return true;
}

void MainWindow::importMedia() {
    QSettings s;
    QStringList patterns;
    for (const auto& e : videoFileExtensions() + imageFileExtensions())
        patterns << QStringLiteral("*.") + e;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Import media"), s.value(QStringLiteral("dirs/media")).toString(),
        tr("Media files (%1);;All files (*)").arg(patterns.join(QLatin1Char(' '))));
    if (files.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/media"), QFileInfo(files.front()).absolutePath());
    importFiles(files);
}

void MainWindow::openPaths(const QStringList& paths) {
    QStringList media;
    for (const auto& p : paths) {
        if (p.endsWith(QStringLiteral(".lumora"), Qt::CaseInsensitive))
            loadProject(p);
        else
            media << p;
    }
    if (!media.isEmpty())
        importFiles(media, kInvalidTicks, true);
}

void MainWindow::importFiles(const QStringList& paths, Ticks at, bool toTimeline, int track) {
    QList<QUuid> ids;
    QStringList failed;
    QStringList photos;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const auto& path : paths) {
        if (path.endsWith(QStringLiteral(".lumora"), Qt::CaseInsensitive))
            continue;
        QString err;
        auto item = m_project->media()->import(path, &err);
        if (!item) {
            failed << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), err);
            continue;
        }
        // Pictures dropped on the timeline become still clips; elsewhere they
        // open in the photo editor.
        if (item->info.kind == MediaKind::Image && !(toTimeline && at != kInvalidTicks))
            photos << item->path;
        else
            ids << item->id;
    }
    QApplication::restoreOverrideCursor();
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Import media"), tr("Some files could not be opened:\n\n%1").arg(failed.join(QLatin1Char('\n'))));
    if (toTimeline && !ids.isEmpty()) {
        const bool wasEmpty = m_project->timeline()->isEmpty();
        addMediaToTimeline(ids, at, track);
        if (wasEmpty)
            m_timelineView->zoomToFit();
    }
    if (!photos.isEmpty() && ids.isEmpty() && toTimeline) {
        m_photo->openFiles(photos);
        setWorkspace(Workspace::Photo);
    }
    if (!ids.isEmpty() || !photos.isEmpty())
        statusBar()->showMessage(tr("Imported %n file(s)", nullptr, static_cast<int>(ids.size() + photos.size())), 4000);
}

void MainWindow::addMediaToTimeline(const QList<QUuid>& ids, Ticks at, int track) {
    for (const auto& id : ids)
        if (auto m = m_project->media()->find(id)) {
            const Ticks end = addRangeToTimeline(id, 0, m->info.duration, at, track);
            // Several files dropped on a track are placed one after the other.
            if (track >= 0 && end != kInvalidTicks)
                at = end;
        }
}

Ticks MainWindow::addRangeToTimeline(const QUuid& mediaId, Ticks in, Ticks out, Ticks at, int track) {
    auto media = m_project->media()->find(mediaId);
    if (!media)
        return kInvalidTicks;
    const bool hasVideo = media->info.video.has_value();
    const bool hasAudio = media->info.audio.has_value();
    Timeline* tl = m_project->timeline();
    const bool first = tl->isEmpty();
    if (media->info.kind == MediaKind::Image) {
        if (track >= 0 && !tl->tracks()[track].isVideo()) {
            statusBar()->showMessage(tr("Pictures go on a video track."), 6000);
            return kInvalidTicks;
        }
        QUuid placed;
        runTimelineEdit(m_videoUndo, tl, tr("Add picture"), [&] {
            if (first) {
                // A sequence that starts with a photo gets its shape.
                const QSignalBlocker b(tl);
                tl->setFrameRate(AVRational{30, 1});
                tl->setResolution(QSize(media->info.imageWidth & ~1, media->info.imageHeight & ~1));
            }
            if (track >= 0 && at == kInvalidTicks)
                at = snapToFrame(m_engine->position(), tl->frameRate());
            placed = tl->addStill(mediaId, ticksFromSeconds(5), std::max(track, tl->primaryVideoTrack()), at);
            return !placed.isNull();
        });
        if (first)
            m_timelineView->zoomToFit();
        const Clip* c = tl->clip(placed);
        return c && track >= 0 ? c->timelineEnd() : kInvalidTicks;
    }
    if (!hasVideo && !hasAudio)
        return kInvalidTicks;
    // Audio-only files go to A1 at the playhead unless dropped on a track.
    if (!hasVideo && track < 0)
        track = tl->primaryAudioTrack();
    if (track >= 0) {
        const bool audioTrack = tl->tracks()[track].type == TrackType::Audio;
        if (audioTrack && !hasAudio) {
            statusBar()->showMessage(tr("This file has no sound; drop it on a video track."), 6000);
            return kInvalidTicks;
        }
        if (at == kInvalidTicks)
            at = snapToFrame(m_engine->position(), tl->frameRate());
        QUuid placed;
        runTimelineEdit(m_videoUndo, tl, tr("Add clip"), [&] {
            if (first && hasVideo) {
                const QSignalBlocker b(tl);
                tl->setFrameRate(media->info.video->frameRate);
                tl->setResolution(QSize(media->info.video->width, media->info.video->height));
            }
            int target = track;
            if (!audioTrack && !hasVideo)
                target = tl->audioTrackFor(track);
            placed = tl->placeMedia(mediaId, in, out, hasVideo && !audioTrack, hasAudio, target, at);
            return !placed.isNull();
        });
        if (first)
            m_timelineView->zoomToFit();
        const Clip* c = tl->clip(placed);
        return c ? c->timelineEnd() : kInvalidTicks;
    }
    runTimelineEdit(m_videoUndo, tl, tr("Add clip"), [&] {
        if (first) {
            // The first clip defines the sequence: same frame rate and size as
            // the source, so nothing is resampled.
            const QSignalBlocker b(tl);
            tl->setFrameRate(media->info.video->frameRate);
            tl->setResolution(QSize(media->info.video->width, media->info.video->height));
        }
        return !tl->insertMedia(mediaId, in, out, true, hasAudio, at).isNull();
    });
    if (first)
        m_timelineView->zoomToFit();
    return kInvalidTicks;
}

void MainWindow::updateTransformBox() {
    VideoView* view = m_monitor->view();
    const auto sel = m_timelineView->selection();
    const Timeline* tl = m_project->timeline();
    const Clip* c = sel.size() == 1 ? tl->clip(sel.front()) : nullptr;
    const Ticks now = m_engine->position();
    auto loc = c ? tl->locate(c->id) : std::nullopt;
    auto media = c && c->hasMedia() ? m_project->media()->find(c->mediaId) : nullptr;
    if (m_monitor->mode() != MonitorPanel::Mode::Program || !c || !media || !loc || !tl->tracks()[loc->track].isVideo() ||
        !c->contains(now)) {
        view->setTransformBox(std::nullopt);
        return;
    }
    // The picture's display size fitted into the frame, as the renderer does.
    QSizeF display;
    if (media->info.kind == MediaKind::Image) {
        display = QSizeF(media->info.imageWidth, media->info.imageHeight);
    } else if (const auto& v = media->info.video) {
        double w = v->width;
        if (v->sampleAspectRatio.num > 0 && v->sampleAspectRatio.den > 0)
            w = w * v->sampleAspectRatio.num / v->sampleAspectRatio.den;
        display = v->rotation == 90 || v->rotation == 270 ? QSizeF(v->height, w) : QSizeF(w, v->height);
    }
    const QSizeF canvas(FrameRenderer::defaultCanvasSize(tl->state(), *m_project->media()));
    if (display.isEmpty() || canvas.isEmpty()) {
        view->setTransformBox(std::nullopt);
        return;
    }
    const QSizeF fit = display.scaled(canvas, Qt::KeepAspectRatio);
    view->setTransformBox(c->transform, QSizeF(fit.width() / canvas.width(), fit.height() / canvas.height()),
                          c->motion.offsetAt(now - c->timelineStart));
}

int MainWindow::freeOverlayTrack(Ticks at, Ticks length) {
    Timeline* tl = m_project->timeline();
    // The lowest video track above the storyline that is free there.
    for (int ti : tl->state().trackIndices(TrackType::Video)) {
        if (ti == 0 || tl->tracks()[ti].locked)
            continue;
        bool free = true;
        for (const auto& c : tl->tracks()[ti].clips)
            free &= c.timelineEnd() <= at || c.timelineStart >= at + length;
        if (free)
            return ti;
    }
    return tl->addTrack(TrackType::Video);
}

void MainWindow::addSticker() {
    QSettings s;
    QStringList patterns;
    for (const auto& e : imageFileExtensions() + videoFileExtensions())
        patterns << QStringLiteral("*.") + e;
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Add sticker"), s.value(QStringLiteral("dirs/stickers"), s.value(QStringLiteral("dirs/media"))).toString(),
        tr("Pictures and videos (%1);;All files (*)").arg(patterns.join(QLatin1Char(' '))));
    if (file.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/stickers"), QFileInfo(file).absolutePath());
    QString err;
    auto item = m_project->media()->import(file, &err);
    if (!item) {
        QMessageBox::warning(this, tr("Add sticker"), tr("The file could not be opened:\n\n%1").arg(err));
        return;
    }
    placeSticker(item->id);
}

QUuid MainWindow::placeSticker(const QUuid& mediaId, int track, Ticks at) {
    auto media = m_project->media()->find(mediaId);
    if (!media)
        return {};
    const bool still = media->info.kind == MediaKind::Image;
    if (!still && !media->info.video) {
        statusBar()->showMessage(tr("This file has no picture to show as a sticker."), 6000);
        return {};
    }
    Timeline* tl = m_project->timeline();
    if (at == kInvalidTicks)
        at = snapToFrame(m_engine->position(), tl->frameRate());
    const Ticks mediaLength = still ? 0 : media->info.duration;
    // Short silent videos (animated GIFs) repeat for as long as a picture shows.
    const Ticks pictureLength = ticksFromSeconds(5);
    const bool loop = !still && !media->info.audio && mediaLength > 0 && mediaLength < pictureLength;
    const Ticks length = still || loop ? pictureLength : mediaLength;
    // A small sticker in the upper right corner, ready to be dragged.
    ClipTransform transform;
    transform.scale = 0.35;
    transform.x = 0.28;
    transform.y = -0.24;
    QUuid id;
    runTimelineEdit(m_videoUndo, tl, tr("Add sticker"), [&] {
        const int target = track > 0 && tl->tracks()[track].isVideo() ? track : freeOverlayTrack(at, length);
        if (still) {
            id = tl->addStill(mediaId, length, target, at, transform);
            return !id.isNull();
        }
        id = tl->placeMedia(mediaId, 0, mediaLength, true, media->info.audio.has_value(), target, at);
        if (id.isNull())
            return false;
        tl->setTransform(id, transform);
        if (loop) {
            tl->setLoop(id, true, mediaLength);
            tl->trimOut(id, length, 0);
        }
        return true;
    });
    if (!id.isNull()) {
        m_timelineView->selectOnly(id);
        m_propsDock->show();
        m_properties->showClip(id);
    }
    return id;
}

void MainWindow::addTitle(int track, Ticks at) {
    Timeline* tl = m_project->timeline();
    if (at == kInvalidTicks)
        at = snapToFrame(m_engine->position(), tl->frameRate());
    const Ticks length = ticksFromSeconds(4);
    QUuid id;
    runTimelineEdit(m_videoUndo, tl, tr("Add title"), [&] {
        const int target = track >= 0 ? track : freeOverlayTrack(at, length);
        TitleData title;
        title.text = tr("Your title");
        id = tl->addTitle(title, length, target, at);
        return !id.isNull();
    });
    if (!id.isNull()) {
        m_timelineView->selectOnly(id);
        m_propsDock->show();
    }
}

void MainWindow::addSubtitle(int track, Ticks at) {
    Timeline* tl = m_project->timeline();
    if (at == kInvalidTicks)
        at = snapToFrame(m_engine->position(), tl->frameRate());
    QUuid id;
    runTimelineEdit(m_videoUndo, tl, tr("Add subtitle"), [&] {
        int target = track;
        if (target < 0) {
            for (int ti : tl->state().trackIndices(TrackType::Subtitle))
                if (!tl->tracks()[ti].locked) {
                    target = ti;
                    break;
                }
            if (target < 0)
                target = tl->addTrack(TrackType::Subtitle);
        }
        // Two seconds, or up to the next subtitle when that comes sooner.
        Ticks length = ticksFromSeconds(2);
        for (const auto& c : tl->tracks()[target].clips)
            if (c.timelineStart > at)
                length = std::min(length, c.timelineStart - at);
        if (length < ticksFromSeconds(0.3))
            length = ticksFromSeconds(2);
        id = tl->addSubtitle(target, at, length, QString());
        return !id.isNull();
    });
    if (!id.isNull()) {
        m_timelineView->selectOnly(id);
        m_propsDock->show();
        m_properties->focusText();
    }
}

void MainWindow::importSubtitles() {
    QSettings s;
    const QString path = QFileDialog::getOpenFileName(this, tr("Import subtitles"), s.value(QStringLiteral("dirs/subtitles"),
                                                      s.value(QStringLiteral("dirs/media"))).toString(),
                                                      tr("Subtitles (*.srt *.vtt);;All files (*)"));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/subtitles"), QFileInfo(path).absolutePath());
    QString err;
    const auto cues = subtitles::readFile(path, &err);
    if (cues.empty()) {
        QMessageBox::warning(this, tr("Import subtitles"),
                             err.isEmpty() ? tr("%1 has no subtitles.").arg(QDir::toNativeSeparators(path))
                                           : tr("Cannot read %1:\n%2").arg(QDir::toNativeSeparators(path), err));
        return;
    }
    // "film.tr.srt" names its language.
    const QStringList parts = QFileInfo(path).completeBaseName().split(QLatin1Char('.'));
    const QString language = parts.size() > 1 ? subtitles::isoLanguage(parts.back()) : QString();
    runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Import subtitles"),
                    [&] { return m_project->timeline()->addSubtitleTrack(cues, language) >= 0; });
    statusBar()->showMessage(tr("%n subtitle(s) imported.", nullptr, int(cues.size())), 5000);
}

void MainWindow::exportSubtitles() {
    const Timeline* tl = m_project->timeline();
    // The track of the selected subtitle, otherwise the first one with cues.
    int track = -1;
    const auto sel = m_timelineView->selection();
    if (!sel.isEmpty())
        if (auto loc = tl->locate(sel.front()); loc && tl->tracks()[loc->track].isSubtitle())
            track = loc->track;
    if (track < 0)
        for (int ti : tl->state().trackIndices(TrackType::Subtitle))
            if (!tl->tracks()[ti].clips.empty()) {
                track = ti;
                break;
            }
    if (track < 0) {
        QMessageBox::information(this, tr("Export subtitles"), tr("There are no subtitles to export yet."));
        return;
    }
    QSettings s;
    const QString base = m_project->filePath().isEmpty() ? tr("Subtitles") : QFileInfo(m_project->filePath()).completeBaseName();
    QString selected;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export subtitles"),
        QDir(s.value(QStringLiteral("dirs/subtitles")).toString()).filePath(base + QStringLiteral(".srt")),
        tr("SubRip (*.srt);;WebVTT (*.vtt)"), &selected);
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/subtitles"), QFileInfo(path).absolutePath());
    const bool vtt = path.endsWith(QLatin1String(".vtt"), Qt::CaseInsensitive) ||
                     (!path.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive) && selected.contains(QLatin1String("vtt")));
    if (!path.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive) && !path.endsWith(QLatin1String(".vtt"), Qt::CaseInsensitive))
        path += vtt ? QStringLiteral(".vtt") : QStringLiteral(".srt");
    QString err;
    if (!subtitles::writeFile(path, tl->state().subtitleCues(track), vtt ? subtitles::Format::Vtt : subtitles::Format::Srt, &err))
        QMessageBox::warning(this, tr("Export subtitles"), tr("Cannot write %1:\n%2").arg(QDir::toNativeSeparators(path), err));
    else
        statusBar()->showMessage(tr("Subtitles saved to %1").arg(QDir::toNativeSeparators(path)), 5000);
}

void MainWindow::addTransition() {
    Timeline* tl = m_project->timeline();
    const auto& clips = tl->tracks()[0].clips;
    QUuid target;
    const auto sel = m_timelineView->selection();
    if (!sel.isEmpty())
        if (auto loc = tl->locate(sel.front()); loc && loc->track == 0 && loc->index > 0)
            target = sel.front();
    if (target.isNull()) {
        // The cut nearest to the playhead.
        const Ticks t = m_engine->position();
        Ticks best = -1;
        for (size_t k = 1; k < clips.size(); ++k) {
            const Ticks d = std::abs(clips[k].timelineStart - t);
            if (best < 0 || d < best) {
                best = d;
                target = clips[k].id;
            }
        }
    }
    if (target.isNull()) {
        statusBar()->showMessage(tr("A transition needs two clips next to each other on the storyline."), 6000);
        return;
    }
    const Clip* c = tl->clip(target);
    Transition tr_ = c->transitionIn.isNone() ? Transition{TransitionType::CrossDissolve, ticksFromSeconds(1)} : c->transitionIn;
    runTimelineEdit(m_videoUndo, tl, tr("Add transition"), [&] { return tl->setTransition(target, tr_); });
    m_timelineView->selectOnly(target);
    auto dur = [this](const QUuid& id) {
        auto m = m_project->media()->find(id);
        return m ? m->info.duration : Ticks(0);
    };
    if (auto loc = tl->locate(target); loc && tl->state().transitionDuration(loc->index, dur) <= 0)
        statusBar()->showMessage(tr("There is no extra footage beyond this cut, so the transition cannot be shown. "
                                    "Trim the clips to leave some media on both sides."),
                                 8000);
}

void MainWindow::removeTransition() {
    const auto sel = m_timelineView->selection();
    if (sel.isEmpty())
        return;
    runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Remove transition"),
                    [&] { return m_project->timeline()->setTransition(sel.front(), Transition{}); });
}

void MainWindow::exportFrame() {
    m_engine->pause();
    if (m_project->timeline()->isEmpty())
        return;
    QSettings s;
    QString path = QFileDialog::getSaveFileName(this, tr("Export current frame"),
                                                s.value(QStringLiteral("dirs/export")).toString() + QStringLiteral("/frame.png"),
                                                tr("PNG image (*.png);;TIFF image (*.tif *.tiff)"));
    if (path.isEmpty())
        return;
    s.setValue(QStringLiteral("dirs/export"), QFileInfo(path).absolutePath());
    // Rendered at full precision and the timeline's own size.
    FrameRenderer renderer(m_project->media());
    renderer.setTimeline(m_project->timeline()->state());
    renderer.setOutputSize(FrameRenderer::defaultCanvasSize(m_project->timeline()->state(), *m_project->media()));
    renderer.setPrecision(FrameRenderer::Precision::Final);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QImage img = renderer.render(snapToFrame(m_engine->position(), m_project->timeline()->frameRate()));
    QApplication::restoreOverrideCursor();
    // 16-bit PNG/TIFF keeps the full precision of the render.
    img = img.convertToFormat(QImage::Format_RGBX64);
    if (img.isNull() || !img.save(path))
        QMessageBox::warning(this, tr("Export current frame"), tr("Could not save %1.").arg(path));
    else
        statusBar()->showMessage(tr("Frame saved to %1").arg(QDir::toNativeSeparators(path)), 6000);
}

void MainWindow::removeMedia(const QList<QUuid>& ids) {
    for (const auto& id : ids) {
        bool used = false;
        for (const auto& t : m_project->timeline()->tracks())
            for (const auto& c : t.clips)
                used |= c.mediaId == id;
        if (used) {
            QMessageBox::information(this, tr("Remove from project"),
                                     tr("This file is used on the timeline. Delete its clips first."));
            continue;
        }
        m_project->media()->remove(id);
        markDirty();
    }
}

void MainWindow::exportVideo() {
    m_engine->pause();
    if (m_project->timeline()->isEmpty()) {
        QMessageBox::information(this, tr("Export video"), tr("The timeline is empty. Add a video first."));
        return;
    }
    ExportDialog dlg(m_project, m_videoUndo, this);
    dlg.exec();
}

void MainWindow::openExportDialog(bool modal) {
    auto* dlg = new ExportDialog(m_project, m_videoUndo, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (modal)
        dlg->exec();
    else
        dlg->show();
}

void MainWindow::seekSeconds(double s) {
    m_engine->seek(ticksFromSeconds(s));
    m_timelineView->setPlayhead(m_engine->position());
}

void MainWindow::selectClipAtPlayhead() {
    const Ticks t = m_engine->position();
    const auto& tracks = m_project->timeline()->tracks();
    if (tracks.empty())
        return;
    for (const auto& c : tracks[0].clips)
        if (c.contains(t)) {
            m_timelineView->selectOnly(c.id);
            return;
        }
}

void MainWindow::selectFirstClip(int track, int index) {
    const auto& tracks = m_project->timeline()->tracks();
    if (track < 0 || track >= static_cast<int>(tracks.size()))
        return;
    const auto& clips = tracks[track].clips;
    if (index > 0 && index < static_cast<int>(clips.size())) {
        m_timelineView->selectOnly(clips[index].id);
        return;
    }
    if (!clips.empty())
        m_timelineView->selectOnly(clips.front().id);
}

void MainWindow::splitAtPlayhead() {
    if (m_monitor->mode() != MonitorPanel::Mode::Program)
        return;
    const Ticks t = snapToFrame(m_engine->position(), m_project->timeline()->frameRate());
    if (!runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Split"), [&] { return m_project->timeline()->splitAt(t); }))
        statusBar()->showMessage(tr("Move the playhead over a clip to split it."), 4000);
}

void MainWindow::freezeFrame() {
    if (m_monitor->mode() != MonitorPanel::Mode::Program)
        return;
    Timeline* tl = m_project->timeline();
    const Ticks t = snapToFrame(m_engine->position(), tl->frameRate());
    // The selected video clip under the playhead, else the storyline clip.
    const Clip* target = nullptr;
    const auto sel = m_timelineView->selection();
    if (!sel.isEmpty())
        if (auto loc = tl->locate(sel.front()); loc && tl->tracks()[loc->track].isVideo()) {
            const Clip* c = tl->clip(sel.front());
            if (c->hasMedia() && c->contains(t))
                target = c;
        }
    if (!target)
        if (const Clip* c = tl->clipAt(tl->primaryVideoTrack(), t); c && c->hasMedia())
            target = c;
    if (!target) {
        statusBar()->showMessage(tr("Move the playhead over a video clip to freeze its frame."), 4000);
        return;
    }
    const QUuid clipId = target->id;
    QSettings settings;
    bool ok = false;
    const double seconds = QInputDialog::getDouble(this, tr("Freeze frame"), tr("Hold the frame for (seconds):"),
                                                   settings.value(QStringLiteral("edit/freezeSeconds"), 2.0).toDouble(),
                                                   0.04, 3600.0, 2, &ok);
    if (!ok)
        return;
    settings.setValue(QStringLiteral("edit/freezeSeconds"), seconds);
    QUuid freezeId;
    runTimelineEdit(m_videoUndo, tl, tr("Freeze frame"), [&] {
        freezeId = tl->freezeFrame(clipId, t, ticksFromSeconds(seconds));
        return !freezeId.isNull();
    });
    if (!freezeId.isNull())
        m_timelineView->selectOnly(freezeId);
}


void MainWindow::autoSubtitles() {
    if (!speech::available()) {
        QMessageBox::information(this, tr("Automatic subtitles"), tr("This build of LumoraStudio has no speech recognition."));
        return;
    }
    Timeline* tl = m_project->timeline();
    if (tl->isEmpty())
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Automatic subtitles"));
    auto* layout = new QVBoxLayout(&dlg);
    auto* intro = new QLabel(tr("LumoraStudio listens to the timeline and writes what is said as a new subtitle track. "
                                "Recognition runs on this computer; nothing is sent anywhere. The model is downloaded once."));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* form = new QFormLayout;
    auto* language = new QComboBox;
    const std::pair<QString, QString> languages[] = {
        {QStringLiteral("tr"), tr("Turkish")}, {QStringLiteral("en"), tr("English")},  {QStringLiteral("de"), tr("German")},
        {QStringLiteral("fr"), tr("French")},  {QStringLiteral("es"), tr("Spanish")},  {QStringLiteral("ar"), tr("Arabic")},
        {QStringLiteral("ru"), tr("Russian")}, {QStringLiteral("auto"), tr("Detect automatically")}};
    for (const auto& [code, name] : languages)
        language->addItem(name, code);
    QSettings settings;
    language->setCurrentIndex(std::max(0, language->findData(settings.value(QStringLiteral("speech/language"), QStringLiteral("tr")))));
    form->addRow(tr("Spoken language"), language);
    auto* model = new QComboBox;
    for (const auto& m : speech::models()) {
        const bool have = QFileInfo::exists(speech::modelPath(m.id));
        model->addItem(have ? tr("%1 · downloaded").arg(m.label) : tr("%1 · %2 MB download").arg(m.label).arg(m.sizeMB), m.id);
    }
    model->setCurrentIndex(std::max(0, model->findData(settings.value(QStringLiteral("speech/model"), speech::defaultModel()))));
    form->addRow(tr("Model"), model);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Write subtitles"));
    markPrimary(buttons->button(QDialogButtonBox::Ok));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString lang = language->currentData().toString();
    const QString modelId = model->currentData().toString();
    settings.setValue(QStringLiteral("speech/language"), lang);
    settings.setValue(QStringLiteral("speech/model"), modelId);

    const QString path = speech::modelPath(modelId);
    if (!QFileInfo::exists(path)) {
        QString error;
        if (!downloadFile(this, speech::modelUrl(modelId), path, tr("Downloading the speech recognition model…"), &error)) {
            if (!error.isEmpty())
                QMessageBox::warning(this, tr("Automatic subtitles"), tr("The model could not be downloaded: %1").arg(error));
            return;
        }
    }
    speech::Options options;
    options.modelPath = path;
    options.language = lang;
    const TimelineState state = tl->state();
    const MediaPool* pool = m_project->media();
    const Ticks end = tl->duration();
    const auto result = withProgress<speech::Result>(this, tr("Listening and writing subtitles…"),
                                                     [state, pool, end, options](const motion::Progress& p) {
                                                         return speech::transcribe(state, *pool, 0, end, options, p);
                                                     });
    if (!result || result->cancelled)
        return;
    if (!result->error.isEmpty()) {
        QMessageBox::warning(this, tr("Automatic subtitles"), result->error);
        return;
    }
    if (result->cues.empty()) {
        QMessageBox::information(this, tr("Automatic subtitles"), tr("No speech was heard on the timeline."));
        return;
    }
    const QString iso = lang == QLatin1String("auto") ? QString() : subtitles::isoLanguage(lang);
    runTimelineEdit(m_videoUndo, tl, tr("Automatic subtitles"), [&] { return tl->addSubtitleTrack(result->cues, iso) >= 0; });
    statusBar()->showMessage(tr("%n subtitle(s) written. Check them: select a cue to correct its text in Properties.", nullptr,
                                static_cast<int>(result->cues.size())),
                             8000);
}

void MainWindow::stabilizeClip(const QUuid& clipId) {
    Timeline* tl = m_project->timeline();
    const Clip* c = tl->clip(clipId);
    auto media = c ? m_project->media()->find(c->mediaId) : nullptr;
    if (!c || c->kind != ClipKind::Media || !media || !media->info.video)
        return;
    const QString path = media->path;
    const auto [from, to] = c->mediaRange();
    const auto camera = withProgress<std::vector<CameraSample>>(
        this, tr("Measuring the camera movement…"),
        [path, from, to](const motion::Progress& p) { return motion::analyzeCameraPath(path, from, to, p); });
    if (!camera)
        return;
    c = tl->clip(clipId);
    if (!c)
        return;
    if (camera->size() < 2) {
        QMessageBox::warning(this, tr("Stabilise"), tr("The camera movement could not be measured in this clip."));
        return;
    }
    Stabilization s = c->stabilization;
    s.enabled = true;
    s.path = *camera;
    s.zoom = s.requiredZoom(motion::pictureAspect(*m_project->media(), *c));
    runTimelineEdit(m_videoUndo, tl, tr("Stabilise"), [&] { return tl->setStabilization(clipId, s); });
}

void MainWindow::redactClip(const QUuid& clipId) {
    Timeline* tl = m_project->timeline();
    const Clip* c = tl->clip(clipId);
    auto media = c ? m_project->media()->find(c->mediaId) : nullptr;
    if (!c || !c->hasMedia() || !media || !media->info.video)
        return;
    const QString title = tr("Hide faces and plates");
    if (!ensureVisionModels(this, {QStringLiteral("face"), QStringLiteral("plate")}, title))
        return;
    const QString path = media->path;
    // A freeze frame only needs its one picture.
    const Ticks from = c->mediaRange().first;
    const Ticks to = c->kind == ClipKind::Freeze ? c->freezeTime + tl->frameDuration() : c->mediaRange().second;
    const auto result = withProgress<ai::RedactionResult>(this, tr("Looking for faces and number plates…"),
                                                          [path, from, to](const TaskProgress& p) {
                                                              return ai::findRedactions(path, from, to, ai::RedactionOptions{}, p);
                                                          });
    if (!result || result->cancelled)
        return;
    if (!result->error.isEmpty()) {
        QMessageBox::warning(this, title, result->error);
        return;
    }
    c = tl->clip(clipId);
    if (!c)
        return;
    Redaction r = c->redaction;
    r.enabled = true;
    r.tracks = result->tracks;
    r.analyzedFrom = from;
    r.analyzedTo = to;
    runTimelineEdit(m_videoUndo, tl, title, [&] { return tl->setRedaction(clipId, r); });
    statusBar()->showMessage(r.tracks.empty() ? tr("No faces or number plates were found.")
                                              : tr("Found %1 face(s) and %2 number plate(s). Play the clip back to check.")
                                                    .arg(r.count(RedactionKind::Face))
                                                    .arg(r.count(RedactionKind::Plate)),
                             8000);
}

void MainWindow::removeBackground(const QUuid& clipId, const QString& model) {
    // The preview always uses the fast model, so the detailed one brings it along.
    QStringList ids{ai::backgroundModelId(QStringLiteral("fast"))};
    if (model != QLatin1String("fast"))
        ids << ai::backgroundModelId(model);
    if (!ensureVisionModels(this, ids, tr("Remove the background")))
        return;
    Timeline* tl = m_project->timeline();
    const Clip* c = tl->clip(clipId);
    if (!c)
        return;
    BackgroundRemoval b = c->background;
    b.enabled = true;
    b.model = model;
    runTimelineEdit(m_videoUndo, tl, tr("Remove the background"), [&] { return tl->setBackgroundRemoval(clipId, b); });
}

void MainWindow::trackClip(const QUuid& id) {
    const QUuid clipId = !id.isNull() ? id : m_timelineView->selection().isEmpty() ? QUuid() : m_timelineView->selection().front();
    const Clip* c = m_project->timeline()->clip(clipId);
    if (!c)
        return;
    // The object is picked on the first frame of the clip.
    if (m_monitor->mode() != MonitorPanel::Mode::Program)
        m_monitor->showProgram(PlaybackSequence::fromTimeline(m_project->timeline()->state(), *m_project->media(),
                                                              m_project->timeline()->frameRate()),
                               true);
    m_engine->pause();
    m_engine->seek(c->timelineStart);
    VideoView* view = m_monitor->view();
    auto picked = std::make_shared<QMetaObject::Connection>();
    auto cancelled = std::make_shared<QMetaObject::Connection>();
    *picked = connect(view, &VideoView::regionPicked, this, [this, clipId, picked, cancelled](const QRectF& region) {
        disconnect(*picked);
        disconnect(*cancelled);
        Timeline* tl = m_project->timeline();
        const Clip* c = tl->clip(clipId);
        if (!c)
            return;
        const TimelineState state = tl->state();
        const MediaPool* pool = m_project->media();
        const Ticks from = c->timelineStart, to = c->timelineEnd();
        const AVRational rate = tl->frameRate();
        const auto path = withProgress<MotionPath>(this, tr("Following the object…"), [state, pool, from, to, region, rate](const motion::Progress& p) {
            return motion::trackRegion(state, *pool, from, to, region, rate, p);
        });
        if (!path || path->isEmpty())
            return;
        runTimelineEdit(m_videoUndo, tl, tr("Follow an object"), [&] { return tl->setMotionPath(clipId, *path); });
        statusBar()->showMessage(tr("The clip now follows the object. Play it back to check."), 5000);
    });
    *cancelled = connect(view, &VideoView::regionPickCancelled, this, [picked, cancelled] {
        disconnect(*picked);
        disconnect(*cancelled);
    });
    view->startRegionPick(tr("Draw a box around the object to follow (Esc cancels)"));
}

void MainWindow::pickKeyColor(const QUuid& clipId) {
    const Clip* c = m_project->timeline()->clip(clipId);
    if (!c)
        return;
    if (m_monitor->mode() != MonitorPanel::Mode::Program)
        m_monitor->showProgram(PlaybackSequence::fromTimeline(m_project->timeline()->state(), *m_project->media(),
                                                              m_project->timeline()->frameRate()),
                               true);
    m_engine->pause();
    const Ticks now = m_engine->position();
    if (now < c->timelineStart || now >= c->timelineEnd())
        m_engine->seek(c->timelineStart);
    VideoView* view = m_monitor->view();
    auto picked = std::make_shared<QMetaObject::Connection>();
    auto cancelled = std::make_shared<QMetaObject::Connection>();
    *picked = connect(view, &VideoView::regionPicked, this, [this, clipId, picked, cancelled](const QRectF& region) {
        disconnect(*picked);
        disconnect(*cancelled);
        Timeline* tl = m_project->timeline();
        const Clip* c = tl->clip(clipId);
        if (!c)
            return;
        // Only this clip, with its green screen off, so the picked colour is
        // the one in the clip itself.
        TimelineState state = tl->state();
        for (Track& track : state.tracks) {
            const bool own = std::any_of(track.clips.begin(), track.clips.end(), [&](const Clip& x) { return x.id == clipId; });
            track.hidden = !own;
            for (Clip& x : track.clips)
                if (x.id == clipId)
                    x.effects.chromaKey.enabled = false;
        }
        FrameRenderer r(m_project->media());
        r.setTimeline(state);
        const QSize canvas = FrameRenderer::defaultCanvasSize(state, *m_project->media());
        r.setCanvasSize(canvas);
        const int w = std::min(640, canvas.width());
        const QSize size(w, std::max(2, canvas.height() * w / std::max(1, canvas.width())));
        r.setOutputSize(size);
        r.setPrecision(FrameRenderer::Precision::Preview);
        Ticks t = m_engine->position();
        if (t < c->timelineStart || t >= c->timelineEnd())
            t = c->timelineStart;
        const QImage frame = r.render(t).convertToFormat(QImage::Format_ARGB32);
        const QRect px = QRectF(region.x() * size.width(), region.y() * size.height(), region.width() * size.width(),
                                region.height() * size.height())
                             .toAlignedRect()
                             .intersected(frame.rect());
        double sum[3] = {0, 0, 0};
        double n = 0;
        for (int y = px.top(); y <= px.bottom(); ++y)
            for (int x = px.left(); x <= px.right(); ++x) {
                const QRgb p = frame.pixel(x, y);
                const double a = qAlpha(p) / 255.0;
                if (a <= 0)
                    continue;
                sum[0] += qRed(p) / a;
                sum[1] += qGreen(p) / a;
                sum[2] += qBlue(p) / a;
                n += 1;
            }
        if (n <= 0)
            return;
        const QColor color(std::clamp(int(sum[0] / n + 0.5), 0, 255), std::clamp(int(sum[1] / n + 0.5), 0, 255),
                           std::clamp(int(sum[2] / n + 0.5), 0, 255));
        m_properties->setKeyColor(clipId, color);
    });
    *cancelled = connect(view, &VideoView::regionPickCancelled, this, [picked, cancelled] {
        disconnect(*picked);
        disconnect(*cancelled);
    });
    view->startRegionPick(tr("Draw a box over the screen behind the subject (Esc cancels)"));
}

void MainWindow::runTool(const QString& name) {
    const std::pair<const char*, QAction*> tools[] = {{"gif", m_actGif},
                                                      {"collage", m_actCollage},
                                                      {"batch", m_actBatchPhotos},
                                                      {"screen", m_actRecordScreen},
                                                      {"voiceover", m_actVoiceover}};
    for (const auto& [id, action] : tools)
        if (name == QLatin1String(id))
            action->trigger();
}

void MainWindow::exportGif() {
    m_engine->pause();
    Timeline* tl = m_project->timeline();
    if (tl->isEmpty()) {
        QMessageBox::information(this, tr("Save as GIF"), tr("The timeline is empty. Add a video first."));
        return;
    }
    Ticks from = kInvalidTicks, to = kInvalidTicks;
    for (const QUuid& id : m_timelineView->selection())
        if (const Clip* c = tl->clip(id)) {
            from = from == kInvalidTicks ? c->timelineStart : std::min(from, c->timelineStart);
            to = to == kInvalidTicks ? c->timelineEnd() : std::max(to, c->timelineEnd());
        }
    GifDialog dlg(m_project, from == kInvalidTicks ? 0 : from, to == kInvalidTicks ? 0 : to, this);
    if (dlg.exec() == QDialog::Accepted)
        statusBar()->showMessage(tr("GIF saved to %1").arg(QDir::toNativeSeparators(dlg.savedPath())), 6000);
}

void MainWindow::makeCollage() {
    m_engine->pause();
    CollageDialog dlg(m_project, [this](const QUuid& id) { return m_cache->thumbnail(id); }, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const CollageLayout* layout = findCollageLayout(dlg.layoutId());
    const QList<QUuid> ids = dlg.media();
    if (!layout || ids.isEmpty())
        return;
    Timeline* tl = m_project->timeline();
    const Ticks at = snapToFrame(m_engine->position(), tl->frameRate());
    const int count = std::min<int>(ids.size(), int(layout->cells.size()));
    // As long as the shortest video; pictures only: the chosen length.
    Ticks length = 0;
    for (int i = 0; i < count; ++i)
        if (auto m = m_project->media()->find(ids[i]); m && m->info.kind != MediaKind::Image && m->info.duration > 0)
            length = length == 0 ? m->info.duration : std::min(length, m->info.duration);
    if (length == 0)
        length = ticksFromSeconds(dlg.pictureSeconds());
    const QSize canvas = FrameRenderer::defaultCanvasSize(tl->state(), *m_project->media());
    const double frameAspect = canvas.height() > 0 ? double(canvas.width()) / canvas.height() : 16.0 / 9.0;
    QList<QUuid> placed;
    bool firstSound = true;
    runTimelineEdit(m_videoUndo, tl, tr("Video collage"), [&] {
        for (int i = 0; i < count; ++i) {
            auto m = m_project->media()->find(ids[i]);
            if (!m)
                continue;
            const bool still = m->info.kind == MediaKind::Image;
            double aspect = 16.0 / 9.0;
            if (still && m->info.imageHeight > 0) {
                aspect = double(m->info.imageWidth) / m->info.imageHeight;
            } else if (m->info.video && m->info.video->height > 0) {
                const auto& v = *m->info.video;
                aspect = double(v.width) / v.height;
                if (v.sampleAspectRatio.num > 0 && v.sampleAspectRatio.den > 0)
                    aspect *= double(v.sampleAspectRatio.num) / v.sampleAspectRatio.den;
                if (v.rotation % 180 != 0)
                    aspect = 1.0 / aspect;
            }
            const ClipTransform transform = collageTransform(layout->cells[i], aspect, frameAspect, dlg.gap());
            const int target = freeOverlayTrack(at, length);
            QUuid id;
            if (still) {
                id = tl->addStill(m->id, length, target, at, transform);
            } else {
                bool sound = m->info.audio.has_value();
                if (dlg.sound() == CollageDialog::Sound::None || (dlg.sound() == CollageDialog::Sound::First && !firstSound))
                    sound = false;
                if (sound)
                    firstSound = false;
                id = tl->placeMedia(m->id, 0, length, true, sound, target, at);
                if (!id.isNull())
                    tl->setTransform(id, transform);
            }
            if (!id.isNull())
                placed << id;
        }
        return !placed.isEmpty();
    });
    if (!placed.isEmpty()) {
        m_timelineView->selectOnly(placed.front());
        statusBar()->showMessage(tr("Collage of %n clip(s) added at the playhead.", nullptr, int(placed.size())), 5000);
    }
}

void MainWindow::batchPhotos() {
    QStringList files;
    // The photos open in the Photo workspace, to start with.
    for (const auto& e : m_project->photos()->entries())
        files << e.path;
    BatchPhotoDialog dlg(this, files);
    dlg.exec();
}

void MainWindow::recordScreen() {
    m_engine->pause();
    auto* dlg = new ScreenRecordDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &ScreenRecordDialog::recorded, this, [this](const QString& path) {
        importFiles({path});
        statusBar()->showMessage(tr("The screen recording is in your files: %1").arg(QDir::toNativeSeparators(path)), 8000);
    });
    dlg->open();
}

void MainWindow::recordVoiceover() {
    if (m_monitor->mode() != MonitorPanel::Mode::Program)
        m_monitor->showProgram(PlaybackSequence::fromTimeline(m_project->timeline()->state(), *m_project->media(),
                                                              m_project->timeline()->frameRate()),
                               true);
    m_engine->pause();
    const Ticks at = snapToFrame(m_engine->position(), m_project->timeline()->frameRate());
    auto* dlg = new VoiceoverDialog(at, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &VoiceoverDialog::playRequested, this, [this](Ticks from, bool muted) {
        m_engine->seek(from);
        m_engine->setMuted(muted);
        m_engine->play();
    });
    connect(dlg, &VoiceoverDialog::stopRequested, this, [this] {
        m_engine->pause();
        m_engine->setMuted(false);
    });
    connect(dlg, &QObject::destroyed, this, [this] { m_engine->setMuted(false); });
    connect(dlg, &VoiceoverDialog::recorded, this, [this](const QString& path, Ticks from) {
        QString err;
        auto item = m_project->media()->import(path, &err);
        if (!item) {
            QMessageBox::warning(this, tr("Record voice-over"), tr("The recording could not be opened:\n\n%1").arg(err));
            return;
        }
        Timeline* tl = m_project->timeline();
        const Ticks length = item->info.duration;
        QUuid id;
        runTimelineEdit(m_videoUndo, tl, tr("Record voice-over"), [&] {
            // The first sound track above A1 that is free there, else a new one.
            int target = -1;
            for (int ti : tl->state().trackIndices(TrackType::Audio)) {
                if (ti == tl->state().trackIndices(TrackType::Audio).front() || tl->tracks()[ti].locked)
                    continue;
                bool free = true;
                for (const auto& c : tl->tracks()[ti].clips)
                    free &= c.timelineEnd() <= from || c.timelineStart >= from + length;
                if (free) {
                    target = ti;
                    break;
                }
            }
            if (target < 0)
                target = tl->addTrack(TrackType::Audio);
            id = tl->placeMedia(item->id, 0, length, false, true, target, from);
            return !id.isNull();
        });
        if (!id.isNull())
            m_timelineView->selectOnly(id);
    });
    dlg->show();
}

void MainWindow::deleteSelection() {
    const auto sel = m_timelineView->selection();
    if (sel.isEmpty())
        return;
    runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Delete clip"),
                    [&] { return m_project->timeline()->removeClips(std::vector<QUuid>(sel.begin(), sel.end())); });
}

void MainWindow::toggleClipMute() {
    const auto sel = m_timelineView->selection();
    if (sel.isEmpty()) {
        updateMuteAction();
        return;
    }
    Timeline* tl = m_project->timeline();
    const std::vector<QUuid> ids(sel.begin(), sel.end());
    const bool mute = !tl->clipsMuted(ids);
    runTimelineEdit(m_videoUndo, tl, mute ? tr("Mute clip sound") : tr("Unmute clip sound"),
                    [&] { return tl->setClipsMuted(ids, mute); });
    updateMuteAction();
}

void MainWindow::updateMuteAction() {
    const auto sel = m_timelineView->selection();
    bool media = false;
    for (const auto& id : sel)
        if (const Clip* c = m_project->timeline()->clip(id); c && c->hasMedia())
            media = true;
    m_actMuteClip->setEnabled(media);
    m_actMuteClip->setChecked(media && m_project->timeline()->clipsMuted(std::vector<QUuid>(sel.begin(), sel.end())));
}

void MainWindow::joinSelection() {
    const auto sel = m_timelineView->selection();
    if (sel.isEmpty())
        return;
    runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Join clips"),
                    [&] { return m_project->timeline()->joinWithNext(sel.front()); });
}

void MainWindow::refreshProgram() {
    const auto seq = PlaybackSequence::fromTimeline(m_project->timeline()->state(), *m_project->media(),
                                                    m_project->timeline()->frameRate());
    m_monitor->showProgram(seq, false);
    const Timeline* tl = m_project->timeline();
    if (tl->isEmpty()) {
        m_timelinePanel->setInfoText(QString());
    } else {
        m_timelinePanel->setInfoText(QStringLiteral("<span style='color:%4'>%1 · %2 · %3</span>")
                                         .arg(QStringLiteral("%1×%2").arg(tl->state().resolution.width()).arg(tl->state().resolution.height()),
                                              formatFrameRate(tl->frameRate()), formatSeconds(tl->duration()),
                                              palette().color(QPalette::PlaceholderText).name()));
    }
}

void MainWindow::setLanguage(const QString& code) {
    QSettings().setValue(QStringLiteral("ui/language"), code);
    QMessageBox::information(this, QStringLiteral("LumoraStudio"),
                             tr("The language changes the next time LumoraStudio starts.") + QStringLiteral("\n\n") +
                                 QStringLiteral("Dil, LumoraStudio bir sonraki açılışta değişir."));
}

void MainWindow::showShortcuts() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Keyboard shortcuts"));
    dlg.resize(460, 520);
    auto* layout = new QVBoxLayout(&dlg);
    auto* browser = new QTextBrowser;
    browser->setHtml(shortcutsHtml());
    layout->addWidget(browser);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    dlg.exec();
}

QString MainWindow::shortcutsHtml() const {
    QString html = QStringLiteral("<table cellspacing=6>");
    for (QAction* a : {m_actPlay, m_actPrevFrame, m_actNextFrame, m_actHome, m_actEnd, m_actSplit, m_actDelete, m_actJoin,
                       m_actFreeze, m_actMuteClip, m_actMarkIn, m_actMarkOut, m_actAddSource, m_actSnap, m_actZoomIn, m_actZoomOut, m_actUndo, m_actRedo,
                       m_actFullScreenPreview, m_actFullScreenWindow, m_actAddTitle, m_actAddSubtitle, m_actAddSticker,
                       m_actAddTransition, m_actImport, m_actExport, m_actExportFrame, m_actExportPhoto, m_actSave, m_actOpen,
                       m_actHowTo}) {
        QStringList keys;
        for (const auto& k : a->shortcuts())
            keys << k.toString(QKeySequence::NativeText);
        keys.removeDuplicates();
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
                    .arg(keys.join(QStringLiteral(" / ")).toHtmlEscaped(), a->text().remove(QLatin1Char('&')).toHtmlEscaped());
    }
    html += QStringLiteral("<tr><td><b>Ctrl + %1</b></td><td>%2</td></tr>").arg(tr("wheel"), tr("Zoom the timeline"));
    // Keys of the Photo workspace.
    for (const auto& [key, text] : {std::pair{QStringLiteral("C"), tr("Crop the photo")},
                                    std::pair{QStringLiteral("B"), tr("Remove the photo's background")},
                                    std::pair{QStringLiteral("\\"), tr("Compare the photo with the original")}})
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>").arg(key, text.toHtmlEscaped());
    html += QStringLiteral("</table>");
    return html;
}

void MainWindow::showFullScreenPreview() {
    if (!m_fullScreen) {
        m_fullScreen = new FullScreenViewer(m_engine, this);
        connect(m_fullScreen, &FullScreenViewer::closed, this, &MainWindow::closeFullScreenPreview);
        connect(m_fullScreen, &FullScreenViewer::photoStepRequested, m_photo, &PhotoWorkspace::stepPhoto);
    }
    if (m_workspace == Workspace::Photo) {
        const QImage img = m_photo->previewImage();
        if (img.isNull())
            return;
        m_fullScreen->showPhoto(img, m_photo->currentTitle(), this);
        return;
    }
    VideoView* view = m_monitor->view();
    if (view->isPickingRegion())
        return;
    const MediaItemPtr source = m_monitor->mode() == MonitorPanel::Mode::Source ? m_monitor->sourceItem() : nullptr;
    const QString title = source ? QFileInfo(source->info.path).fileName() : tr("Timeline");
    m_fullScreen->showVideo(view->image(), title, this);
}

void MainWindow::closeFullScreenPreview() {
    if (!m_fullScreen || !m_fullScreen->isVideo())
        return;
    // Back to the monitor's own size; redraw a paused frame at that size.
    VideoView* view = m_monitor->view();
    m_engine->setPreviewSize(view->size() * view->devicePixelRatioF());
    if (!m_engine->isPlaying())
        m_engine->seek(m_engine->position());
}

void MainWindow::setWindowFullScreen(bool on) {
    if (on == isFullScreen())
        return;
    if (on) {
        m_wasMaximized = isMaximized();
        showFullScreen();
    } else if (m_wasMaximized) {
        showMaximized();
    } else {
        showNormal();
    }
    m_actFullScreenWindow->setChecked(on);
}

void MainWindow::showAbout() {
    QMessageBox::about(this, tr("About LumoraStudio"),
                       tr("<h3>LumoraStudio %1</h3>"
                          "<p>A free video and photo editor whose first rule is: quality never drops unless you "
                          "ask for it.</p>"
                          "<p>Cut and join without re-encoding, export with the source's own settings, edit photos "
                          "non-destructively and save them losslessly.</p>"
                          "<p>FFmpeg %2 · Qt %3</p>")
                           .arg(QCoreApplication::applicationVersion(), av::versionString(), QString::fromLatin1(qVersion())));
}

} // namespace lumora

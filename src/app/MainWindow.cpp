#include "MainWindow.h"

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
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include "Icons.h"
#include "MediaCache.h"
#include "dialogs/ExportDialog.h"
#include "ffmpeg/FFmpeg.h"
#include "panels/MediaBinPanel.h"
#include "panels/MonitorPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/TimelinePanel.h"
#include "panels/TimelineView.h"
#include "photo/PhotoWorkspace.h"
#include "playback/PlaybackEngine.h"
#include "project/Project.h"
#include "render/FrameRenderer.h"
#include "undo/TimelineCommands.h"

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
    m_tabs->setTabEnabled(1, false);
    m_tabs->setTabToolTip(1, tr("Colour wheels, curves, LUTs and scopes arrive in version 0.3"));
    m_tabs->setTabEnabled(2, false);
    m_tabs->setTabToolTip(2, tr("Audio tools arrive in version 0.4"));

    m_stack = new QStackedWidget;
    m_monitor = new MonitorPanel(m_engine);
    m_photo = new PhotoWorkspace(m_project->photos(), m_photoUndo);
    m_stack->addWidget(m_monitor);
    m_stack->addWidget(m_photo);
    setCentralWidget(m_stack);

    m_bin = new MediaBinPanel(m_project->media(), m_cache);
    m_timelineView = new TimelineView(m_project->timeline(), m_project->media(), m_cache, m_videoUndo);
    m_timelinePanel = new TimelinePanel(m_timelineView);
    m_properties = new PropertiesPanel(m_project, m_videoUndo);

    createActions();
    createDocks();
    createMenus();

    auto* tabBarHost = new QToolBar(tr("Workspaces"));
    tabBarHost->setObjectName(QStringLiteral("workspaceBar"));
    tabBarHost->setMovable(false);
    tabBarHost->addWidget(m_tabs);
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tabBarHost->addWidget(spacer);
    tabBarHost->addAction(m_actImport);
    tabBarHost->addAction(m_actUndo);
    tabBarHost->addAction(m_actRedo);
    tabBarHost->addAction(m_actExport);
    tabBarHost->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tabBarHost->setIconSize(QSize(18, 18));
    addToolBar(Qt::TopToolBarArea, tabBarHost);

    m_statusRight = new QLabel(tr("FFmpeg %1 · Qt %2").arg(av::versionString(), QString::fromLatin1(qVersion())));
    statusBar()->addPermanentWidget(m_statusRight);

    // --- wiring ---
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) { setWorkspace(static_cast<Workspace>(i)); });

    connect(m_project->timeline(), &Timeline::changed, this, [this] {
        refreshProgram();
        markDirty();
    });
    connect(m_project->media(), &MediaPool::itemAdded, this, &MainWindow::markDirty);
    connect(m_project->photos(), &PhotoLibrary::adjustmentsChanged, this, &MainWindow::markDirty);
    connect(m_project->photos(), &PhotoLibrary::added, this, &MainWindow::markDirty);
    connect(m_cache, &MediaCache::keyframesReady, m_properties, &PropertiesPanel::refresh);

    connect(m_engine, &PlaybackEngine::positionChanged, this, [this](Ticks t) {
        if (m_monitor->mode() == MonitorPanel::Mode::Program) {
            m_timelineView->setPlayhead(t);
            if (m_engine->isPlaying())
                m_timelineView->ensurePlayheadVisible();
        }
    });
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
        const bool has = !sel.isEmpty();
        m_actDelete->setEnabled(has);
        m_actJoin->setEnabled(has && m_project->timeline()->canJoinWithNext(sel.front()));
    });
    connect(m_timelineView, &TimelineView::clipDoubleClicked, this, [this](const QUuid& id) {
        if (const Clip* c = m_project->timeline()->clip(id); c && c->kind == ClipKind::Media)
            if (auto media = m_project->media()->find(c->mediaId)) {
                m_monitor->showSource(media);
                m_engine->seek(c->sourceIn);
            }
    });
    connect(m_timelineView, &TimelineView::mediaDropped, this,
            [this](const QList<QUuid>& ids, Ticks at, int track) { addMediaToTimeline(ids, at, track); });
    connect(m_timelineView, &TimelineView::filesDropped, this,
            [this](const QStringList& paths, Ticks at, int track) { importFiles(paths, at, true, track); });
    connect(m_timelineView, &TimelineView::addTitleRequested, this, [this](int track, Ticks at) { addTitle(track, at); });
    connect(m_timelineView, &TimelineView::contextMenuForClip, this, [this](const QUuid& id, const QPoint& pos) {
        QMenu menu;
        menu.addAction(m_actSplit);
        menu.addAction(m_actJoin);
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
        const bool source = m == MonitorPanel::Mode::Source;
        m_actMarkIn->setEnabled(source);
        m_actMarkOut->setEnabled(source);
        m_actAddSource->setEnabled(source);
        if (source && m_monitor->sourceItem())
            m_properties->showMedia(m_monitor->sourceItem()->id);
    });
    connect(m_photo, &PhotoWorkspace::statusMessage, this, [this](const QString& s) { statusBar()->showMessage(s, 6000); });

    restoreSettings();
    refreshProgram();
    updateTitle();
    m_actDelete->setEnabled(false);
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
        for (QDockWidget* d : {m_binDock, m_propsDock, m_timelineDock}) {
            d->setFloating(false);
            d->show();
        }
        addDockWidget(Qt::LeftDockWidgetArea, m_binDock);
        addDockWidget(Qt::RightDockWidgetArea, m_propsDock);
        addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
        resizeDocks({m_timelineDock}, {280}, Qt::Vertical);
        resizeDocks({m_binDock, m_propsDock}, {300, 280}, Qt::Horizontal);
    });

    m_actAddTitle = make(tr("Add &title"), QKeySequence(Qt::Key_T), [this] { addTitle(); }, Icons::title());
    m_actAddTitle->setToolTip(tr("Add a title at the playhead on a track above the storyline (T)"));
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
    m_actExportFrame = make(tr("Export current &frame…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),
                            &MainWindow::exportFrame);

    auto* tb = m_timelinePanel->toolBar();
    tb->addAction(m_actSplit);
    tb->addAction(m_actJoin);
    tb->addAction(m_actDelete);
    tb->addSeparator();
    tb->addAction(m_actAddTitle);
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
    file->addAction(m_actExportPhoto);
    file->addSeparator();
    file->addAction(m_actQuit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actUndo);
    edit->addAction(m_actRedo);
    edit->addSeparator();
    edit->addAction(m_actSplit);
    edit->addAction(m_actJoin);
    edit->addAction(m_actDelete);
    edit->addSeparator();
    edit->addAction(m_actAddTitle);
    edit->addAction(m_actAddTransition);
    edit->addAction(m_actAddVideoTrack);
    edit->addAction(m_actAddAudioTrack);
    edit->addSeparator();
    edit->addAction(m_actMarkIn);
    edit->addAction(m_actMarkOut);
    edit->addAction(m_actAddSource);
    edit->addSeparator();
    edit->addAction(m_actSnap);

    QMenu* play = menuBar()->addMenu(tr("&Playback"));
    play->addAction(m_actPlay);
    play->addAction(m_actPrevFrame);
    play->addAction(m_actNextFrame);
    play->addAction(m_actHome);
    play->addAction(m_actEnd);

    QMenu* view = menuBar()->addMenu(tr("&View"));
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
    view->addAction(m_actResetLayout);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Keyboard shortcuts"), this, &MainWindow::showShortcuts);
    help->addAction(tr("Feature list (roadmap)"), this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/highlvmami/LumoraStudio/blob/main/docs/ozellikler.md")));
    });
    help->addSeparator();
    QAction* about = help->addAction(tr("About LumoraStudio"), this, &MainWindow::showAbout);
    about->setMenuRole(QAction::AboutRole);
}

void MainWindow::createDocks() {
    auto makeDock = [this](const QString& title, const QString& name, QWidget* w) {
        auto* d = new QDockWidget(title, this);
        d->setObjectName(name);
        d->setWidget(w);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
        return d;
    };
    m_binDock = makeDock(tr("Files"), QStringLiteral("filesDock"), m_bin);
    m_propsDock = makeDock(tr("Properties"), QStringLiteral("propertiesDock"), m_properties);
    m_timelineDock = makeDock(tr("Timeline"), QStringLiteral("timelineDock"), m_timelinePanel);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_binDock);
    addDockWidget(Qt::RightDockWidgetArea, m_propsDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
    resizeDocks({m_timelineDock}, {280}, Qt::Vertical);
    resizeDocks({m_binDock, m_propsDock}, {300, 280}, Qt::Horizontal);
}

void MainWindow::restoreSettings() {
    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1440, 900);
    restoreState(s.value(QStringLiteral("window/state")).toByteArray());
}

void MainWindow::setWorkspace(Workspace w) {
    if (w == Workspace::Color || w == Workspace::Audio)
        w = Workspace::Edit;
    const bool photo = w == Workspace::Photo;
    if (photo && m_workspace != Workspace::Photo) {
        m_engine->pause();
        m_editDockState = saveState();
    }
    m_workspace = w;
    {
        const QSignalBlocker b(m_tabs);
        m_tabs->setCurrentIndex(static_cast<int>(w));
    }
    m_stack->setCurrentIndex(photo ? 1 : 0);
    if (photo) {
        for (QDockWidget* d : {m_binDock, m_propsDock, m_timelineDock})
            d->hide();
    } else if (!m_editDockState.isEmpty()) {
        restoreState(m_editDockState);
        m_editDockState.clear();
    }
    m_undoGroup->setActiveStack(photo ? m_photoUndo : m_videoUndo);
    for (QAction* a : {m_actSplit, m_actDelete, m_actJoin, m_actPlay, m_actPrevFrame, m_actNextFrame, m_actHome, m_actEnd,
                       m_actMarkIn, m_actMarkOut, m_actAddSource, m_actZoomIn, m_actZoomOut, m_actExport, m_actAddTitle,
                       m_actAddTransition, m_actAddVideoTrack, m_actAddAudioTrack, m_actExportFrame})
        a->setVisible(!photo);
    m_actExportPhoto->setVisible(photo);
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

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    m_engine->pause();
    if (m_workspace == Workspace::Photo && !m_editDockState.isEmpty())
        restoreState(m_editDockState);
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
        if (item->info.kind == MediaKind::Image)
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
    if (!hasVideo && !hasAudio) {
        statusBar()->showMessage(tr("Photos are edited in the Photo workspace; photo-to-video arrives in a later version."), 6000);
        return kInvalidTicks;
    }
    Timeline* tl = m_project->timeline();
    const bool first = tl->isEmpty();
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

void MainWindow::addTitle(int track, Ticks at) {
    Timeline* tl = m_project->timeline();
    if (at == kInvalidTicks)
        at = snapToFrame(m_engine->position(), tl->frameRate());
    const Ticks length = ticksFromSeconds(4);
    QUuid id;
    runTimelineEdit(m_videoUndo, tl, tr("Add title"), [&] {
        int target = track;
        if (target < 0) {
            // The lowest video track above the storyline that is free there.
            for (int ti : tl->state().trackIndices(TrackType::Video)) {
                if (ti == 0 || tl->tracks()[ti].locked)
                    continue;
                bool free = true;
                for (const auto& c : tl->tracks()[ti].clips)
                    free &= c.timelineEnd() <= at || c.timelineStart >= at + length;
                if (free) {
                    target = ti;
                    break;
                }
            }
            if (target < 0)
                target = tl->addTrack(TrackType::Video);
        }
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

void MainWindow::deleteSelection() {
    const auto sel = m_timelineView->selection();
    if (sel.isEmpty())
        return;
    runTimelineEdit(m_videoUndo, m_project->timeline(), tr("Delete clip"),
                    [&] { return m_project->timeline()->removeClips(std::vector<QUuid>(sel.begin(), sel.end())); });
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
    QString html = QStringLiteral("<table cellspacing=6>");
    for (QAction* a : {m_actPlay, m_actPrevFrame, m_actNextFrame, m_actHome, m_actEnd, m_actSplit, m_actDelete, m_actJoin,
                       m_actMarkIn, m_actMarkOut, m_actAddSource, m_actSnap, m_actZoomIn, m_actZoomOut, m_actUndo, m_actRedo,
                       m_actImport, m_actExport, m_actExportPhoto, m_actSave, m_actOpen}) {
        QStringList keys;
        for (const auto& k : a->shortcuts())
            keys << k.toString(QKeySequence::NativeText);
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
                    .arg(keys.join(QStringLiteral(" / ")).toHtmlEscaped(), a->text().remove(QLatin1Char('&')).toHtmlEscaped());
    }
    html += QStringLiteral("<tr><td><b>Ctrl + %1</b></td><td>%2</td></tr>").arg(tr("wheel"), tr("Zoom the timeline"));
    html += QStringLiteral("</table>");
    browser->setHtml(html);
    layout->addWidget(browser);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    dlg.exec();
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

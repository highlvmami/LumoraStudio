#include <QAbstractButton>
#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibraryInfo>
#include <QLocale>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QRadioButton>
#include <QScrollArea>
#include <QTabBar>
#include <QTimer>
#include <QTranslator>
#include <QWindow>

#include "MainWindow.h"
#include "Updater.h"
#include "Theme.h"
#include "media/MediaPool.h"

extern "C" {
#include <libavutil/log.h>
}

using namespace lumora;

namespace {

bool g_dumpWidgets = false;

// Saves a screenshot of `w`. With --widgets, also writes <out>.json listing
// the visible widgets with their rectangles inside the picture, so the
// "How to use" pictures can be marked up by a script.
void saveShot(QWidget* w, const QString& out) {
    if (w->isWindow()) {
        w->grab().save(out);
    } else {
        // A panel's content is transparent: draw it on the panel colour.
        QPixmap pm(w->size() * w->devicePixelRatioF());
        pm.setDevicePixelRatio(w->devicePixelRatioF());
        pm.fill(themeColors().panel);
        w->render(&pm, QPoint(), QRegion(), QWidget::DrawChildren);
        pm.save(out);
    }
    if (!g_dumpWidgets)
        return;
    QJsonArray items;
    auto add = [&](const QString& cls, const QString& name, const QString& text, const QString& tip, QRect r) {
        if (r.isEmpty())
            return;
        items.append(QJsonObject{{"class", cls}, {"name", name}, {"text", text}, {"tip", tip},
                                 {"rect", QJsonArray{r.x(), r.y(), r.width(), r.height()}}});
    };
    for (QWidget* c : w->findChildren<QWidget*>()) {
        if (!c->isVisibleTo(w))
            continue;
        const QRect r(c->mapTo(w, QPoint(0, 0)), c->size());
        if (!QRect(QPoint(0, 0), w->size()).intersects(r))
            continue;
        QString text;
        if (auto* b = qobject_cast<QAbstractButton*>(c))
            text = b->text();
        else if (auto* l = qobject_cast<QLabel*>(c))
            text = l->text();
        else if (auto* g = qobject_cast<QGroupBox*>(c))
            text = g->title();
        else if (auto* cb = qobject_cast<QComboBox*>(c))
            text = cb->currentText();
        else if (auto* d = qobject_cast<QDockWidget*>(c))
            text = d->windowTitle();
        add(QString::fromLatin1(c->metaObject()->className()), c->objectName(), text, c->toolTip(), r);
        if (auto* tabs = qobject_cast<QTabBar*>(c))
            for (int i = 0; i < tabs->count(); ++i)
                add(QStringLiteral("tab"), c->objectName(), tabs->tabText(i), tabs->tabToolTip(i),
                    tabs->tabRect(i).translated(r.topLeft()));
        if (auto* bar = qobject_cast<QMenuBar*>(c))
            for (QAction* a : bar->actions())
                add(QStringLiteral("menu"), QString(), a->text(), QString(), bar->actionGeometry(a).translated(r.topLeft()));
    }
    QFile f(out + QStringLiteral(".json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{"size", QJsonArray{w->width(), w->height()}}, {"widgets", items}}).toJson());
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LumoraStudio"));
    QApplication::setApplicationName(QStringLiteral("LumoraStudio"));
    QApplication::setApplicationVersion(QStringLiteral(LUMORA_VERSION));
    av_log_set_level(AV_LOG_ERROR);
    qRegisterMetaType<lumora::Ticks>("lumora::Ticks");
    qRegisterMetaType<lumora::MediaItemPtr>();

    QSettings settings;
    applyTheme(themeFromString(settings.value(QStringLiteral("ui/theme"), QStringLiteral("dark")).toString()));

    // Interface language: setting, else the system language (Turkish or English).
    QString lang = settings.value(QStringLiteral("ui/language")).toString();
    if (lang.isEmpty())
        lang = QLocale().language() == QLocale::Turkish ? QStringLiteral("tr") : QStringLiteral("en");
    QTranslator appTranslator, qtTranslator;
    if (lang != QLatin1String("en")) {
        if (appTranslator.load(QStringLiteral(":/i18n/lumora_%1.qm").arg(lang)))
            QApplication::installTranslator(&appTranslator);
        // Qt's own strings (dialog buttons and the like). windeployqt ships
        // them next to the exe as a combined qt_<lang>.qm.
        const QString appTranslations = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
        if (qtTranslator.load(QStringLiteral("qtbase_%1").arg(lang), QLibraryInfo::path(QLibraryInfo::TranslationsPath)) ||
            qtTranslator.load(QStringLiteral("qt_%1").arg(lang), appTranslations) ||
            qtTranslator.load(QStringLiteral("qtbase_%1").arg(lang), appTranslations))
            QApplication::installTranslator(&qtTranslator);
        QLocale::setDefault(QLocale(lang));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("LumoraStudio video and photo editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Project (.lumora) or media files to open"));
    // Used to produce the screenshots in docs/screenshots.
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Save a screenshot and quit"), QStringLiteral("png"));
    QCommandLineOption workspaceOpt(QStringLiteral("workspace"), QStringLiteral("edit, color, audio, photo or help"), QStringLiteral("name"));
    QCommandLineOption selectOpt(QStringLiteral("select-first-clip"), QStringLiteral("Select the first clip"));
    QCommandLineOption selectClipOpt(QStringLiteral("select-clip"), QStringLiteral("Select clip TRACK:INDEX"), QStringLiteral("clip"));
    QCommandLineOption exportOpt(QStringLiteral("export-dialog"), QStringLiteral("Open the export dialog"));
    QCommandLineOption exportCustomOpt(QStringLiteral("export-custom"), QStringLiteral("Open the export dialog in custom mode"));
    QCommandLineOption seekOpt(QStringLiteral("seek"), QStringLiteral("Move the playhead (seconds)"), QStringLiteral("s"));
    QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("Window size WxH"), QStringLiteral("size"));
    QCommandLineOption autoSubsOpt(QStringLiteral("auto-subtitles-dialog"), QStringLiteral("Open the automatic subtitles dialog"));
    QCommandLineOption trackOpt(QStringLiteral("track-selected"), QStringLiteral("Start following an object with the selected clip"));
    QCommandLineOption helpSectionOpt(QStringLiteral("help-section"), QStringLiteral("Open a page of the How to use booklet"),
                                      QStringLiteral("id"));
    QCommandLineOption widgetsOpt(QStringLiteral("widgets"), QStringLiteral("Also write the widget rectangles of the screenshot"));
    QCommandLineOption grabScrollOpt(QStringLiteral("grab-scroll"),
                                     QStringLiteral("Screenshot the whole scrolled content of the first widget of this class"),
                                     QStringLiteral("class"));
    QCommandLineOption fullScreenOpt(QStringLiteral("full-screen-preview"), QStringLiteral("Open the full-screen preview"));
    for (auto* o : {&shotOpt, &workspaceOpt, &selectOpt, &selectClipOpt, &exportOpt, &exportCustomOpt, &seekOpt, &sizeOpt,
                    &autoSubsOpt, &trackOpt, &fullScreenOpt, &helpSectionOpt, &widgetsOpt, &grabScrollOpt}) {
        o->setFlags(QCommandLineOption::HiddenFromHelp);
        parser.addOption(*o);
    }
    parser.process(app);
    g_dumpWidgets = parser.isSet(widgetsOpt);

    MainWindow w;
    if (parser.isSet(sizeOpt)) {
        const QStringList wh = parser.value(sizeOpt).split(QLatin1Char('x'));
        if (wh.size() == 2)
            w.resize(wh[0].toInt(), wh[1].toInt());
    }
    w.show();
    w.openPaths(parser.positionalArguments());
    if (parser.value(workspaceOpt) == QLatin1String("photo"))
        w.setWorkspace(MainWindow::Workspace::Photo);
    if (parser.value(workspaceOpt) == QLatin1String("color"))
        w.setWorkspace(MainWindow::Workspace::Color);
    if (parser.value(workspaceOpt) == QLatin1String("audio"))
        w.setWorkspace(MainWindow::Workspace::Audio);
    if (parser.value(workspaceOpt) == QLatin1String("help"))
        w.setWorkspace(MainWindow::Workspace::Help);
    if (parser.isSet(helpSectionOpt))
        w.showHelpSection(parser.value(helpSectionOpt));
    if (parser.isSet(selectOpt))
        w.selectFirstClip();
    if (parser.isSet(selectClipOpt)) {
        const QStringList ti = parser.value(selectClipOpt).split(QLatin1Char(':'));
        w.selectFirstClip(ti.value(0).toInt(), ti.value(1).toInt());
    }
    if (parser.isSet(seekOpt))
        QTimer::singleShot(800, &w, [&w, s = parser.value(seekOpt).toDouble()] { w.seekSeconds(s); });

    if (!parser.isSet(shotOpt)) {
        Updater::removeLeftovers();
        w.checkForUpdatesOnStartup();
    }

    if (parser.isSet(shotOpt)) {
        const QString out = parser.value(shotOpt);
        const bool exportCustom = parser.isSet(exportCustomOpt);
        const bool exportDlg = parser.isSet(exportOpt) || exportCustom;
        const bool autoSubs = parser.isSet(autoSubsOpt), track = parser.isSet(trackOpt);
        const bool fullScreen = parser.isSet(fullScreenOpt);
        const QString grabScroll = parser.value(grabScrollOpt);
        QTimer::singleShot(2500, &w, [&w, out, exportDlg, exportCustom, autoSubs, track, fullScreen, grabScroll] {
            if (!grabScroll.isEmpty()) {
                for (QWidget* c : w.findChildren<QWidget*>())
                    if (c->isVisible() && c->inherits(grabScroll.toLatin1().constData()))
                        if (auto* scroll = c->findChild<QScrollArea*>(); scroll && scroll->widget()) {
                            QWidget* content = scroll->widget();
                            scroll->setWidgetResizable(false);
                            content->resize(scroll->viewport()->width(), content->sizeHint().height());
                            saveShot(content, out);
                            break;
                        }
                QCoreApplication::exit(0);
                return;
            }
            if (fullScreen) {
                w.showFullScreenPreview();
                QTimer::singleShot(1500, &w, [out] {
                    for (QWidget* top : QApplication::topLevelWidgets())
                        if (top->isVisible() && top->inherits("lumora::FullScreenViewer")) {
                            saveShot(top, out);
                            break;
                        }
                    QCoreApplication::exit(0);
                });
                return;
            }
            if (autoSubs) {
                QTimer::singleShot(1200, &w, [out] {
                    for (QWidget* top : QApplication::topLevelWidgets())
                        if (top->isVisible() && top->inherits("QDialog")) {
                            top->adjustSize();
                            saveShot(top, out);
                            break;
                        }
                    QCoreApplication::exit(0);
                });
                w.autoSubtitles();
                return;
            }
            if (track) {
                w.trackClip();
                QTimer::singleShot(1200, &w, [&w, out] {
                    saveShot(&w, out);
                    QCoreApplication::exit(0);
                });
                return;
            }
            if (exportDlg) {
                w.openExportDialog(false);
                if (exportCustom)
                    for (QWidget* top : QApplication::topLevelWidgets())
                        if (top->isVisible() && top->inherits("lumora::ExportDialog")) {
                            const auto modes = top->findChildren<QRadioButton*>();
                            if (!modes.isEmpty())
                                modes.last()->click(); // custom is the last mode
                        }
                QTimer::singleShot(1200, &w, [out] {
                    for (QWidget* top : QApplication::topLevelWidgets())
                        if (top->isVisible() && top->inherits("lumora::ExportDialog")) {
                            top->adjustSize();
                            saveShot(top, out);
                            break;
                        }
                    QCoreApplication::exit(0);
                });
                return;
            }
            saveShot(&w, out);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}

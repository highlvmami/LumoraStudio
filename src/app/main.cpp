#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QScreen>
#include <QSettings>
#include <QRadioButton>
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
    QCommandLineOption workspaceOpt(QStringLiteral("workspace"), QStringLiteral("edit, color or photo"), QStringLiteral("name"));
    QCommandLineOption selectOpt(QStringLiteral("select-first-clip"), QStringLiteral("Select the first clip"));
    QCommandLineOption selectClipOpt(QStringLiteral("select-clip"), QStringLiteral("Select clip TRACK:INDEX"), QStringLiteral("clip"));
    QCommandLineOption exportOpt(QStringLiteral("export-dialog"), QStringLiteral("Open the export dialog"));
    QCommandLineOption exportCustomOpt(QStringLiteral("export-custom"), QStringLiteral("Open the export dialog in custom mode"));
    QCommandLineOption seekOpt(QStringLiteral("seek"), QStringLiteral("Move the playhead (seconds)"), QStringLiteral("s"));
    QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("Window size WxH"), QStringLiteral("size"));
    for (auto* o : {&shotOpt, &workspaceOpt, &selectOpt, &selectClipOpt, &exportOpt, &exportCustomOpt, &seekOpt, &sizeOpt}) {
        o->setFlags(QCommandLineOption::HiddenFromHelp);
        parser.addOption(*o);
    }
    parser.process(app);

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
        QTimer::singleShot(2500, &w, [&w, out, exportDlg, exportCustom] {
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
                            top->grab().save(out);
                            break;
                        }
                    QCoreApplication::exit(0);
                });
                return;
            }
            w.grab().save(out);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}

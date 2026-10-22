#include "DesktopShortcut.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace lumora::DesktopShortcut {

namespace {

const QString kAsked = QStringLiteral("shortcut/asked");
const QString kMade = QStringLiteral("shortcut/made");

QString desktopDir() {
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

QString menuDir() {
    // Windows: the user's Start menu "Programs" folder; Linux: ~/.local/share/applications.
    return QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
}

#ifdef Q_OS_WIN
QString desktopFile() { return QDir(desktopDir()).filePath(QStringLiteral("LumoraStudio.lnk")); }

bool writeLink(const QString& linkPath, QString* error) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(init);
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&link));
    if (SUCCEEDED(hr)) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
        link->SetPath(reinterpret_cast<const wchar_t*>(exe.utf16()));
        link->SetWorkingDirectory(reinterpret_cast<const wchar_t*>(dir.utf16()));
        link->SetIconLocation(reinterpret_cast<const wchar_t*>(exe.utf16()), 0);
        const QString desc = QCoreApplication::translate("lumora::DesktopShortcut", "Free video and photo editor");
        link->SetDescription(reinterpret_cast<const wchar_t*>(desc.utf16()));
        IPersistFile* file = nullptr;
        hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
        if (SUCCEEDED(hr)) {
            const QString native = QDir::toNativeSeparators(linkPath);
            hr = file->Save(reinterpret_cast<const wchar_t*>(native.utf16()), TRUE);
            file->Release();
        }
        link->Release();
    }
    if (uninit)
        CoUninitialize();
    if (FAILED(hr) && error)
        *error = QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
    return SUCCEEDED(hr);
}
#elif defined(Q_OS_LINUX)
QString desktopFile() { return QDir(desktopDir()).filePath(QStringLiteral("lumorastudio.desktop")); }

QString iconPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("icons/hicolor/256x256/apps/lumorastudio.png"));
}

bool writeEntry(const QString& path, QString* error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QString exe = QCoreApplication::applicationFilePath();
    const QString desc = QCoreApplication::translate("lumora::DesktopShortcut", "Free video and photo editor");
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=LumoraStudio\n"
                           "Comment=%1\n"
                           "Exec=\"%2\" %F\n"
                           "Icon=%3\n"
                           "Terminal=false\n"
                           "Categories=AudioVideo;Video;Graphics;Photography;\n")
                .arg(desc, exe, iconPath())
                .toUtf8());
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    return true;
}
#endif

} // namespace

bool supported() {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return !desktopDir().isEmpty();
#else
    return false;
#endif
}

bool exists() {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return supported() && QFileInfo::exists(desktopFile());
#else
    return false;
#endif
}

bool create(QString* error) {
    if (!supported()) {
        if (error)
            *error = QCoreApplication::translate("lumora::DesktopShortcut", "Not available on this system");
        return false;
    }
#ifdef Q_OS_WIN
    if (!writeLink(desktopFile(), error))
        return false;
    if (!menuDir().isEmpty())
        writeLink(QDir(menuDir()).filePath(QStringLiteral("LumoraStudio.lnk")), nullptr);
#elif defined(Q_OS_LINUX)
    const QString icon = iconPath();
    QDir().mkpath(QFileInfo(icon).absolutePath());
    QFile::remove(icon);
    QFile::copy(QStringLiteral(":/logo/lumora-256.png"), icon);
    QFile::setPermissions(icon, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    if (!writeEntry(desktopFile(), error))
        return false;
    if (!menuDir().isEmpty())
        writeEntry(QDir(menuDir()).filePath(QStringLiteral("lumorastudio.desktop")), nullptr);
#endif
    QSettings().setValue(kMade, true);
    return true;
}

void offerOrRefresh(QWidget* parent) {
    if (!supported())
        return;
    QSettings s;
    if (s.value(kMade).toBool()) {
        // Only refresh a shortcut that is still there: a deleted one stays deleted.
        if (exists())
            create();
        return;
    }
    if (s.value(kAsked).toBool())
        return;
    s.setValue(kAsked, true);
    if (exists())
        return;
    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("LumoraStudio"));
    box.setIconPixmap(QIcon(QStringLiteral(":/logo/lumora-64.png")).pixmap(64, 64));
    box.setText(QCoreApplication::translate("lumora::DesktopShortcut", "Put LumoraStudio on the desktop?"));
    box.setInformativeText(QCoreApplication::translate(
        "lumora::DesktopShortcut",
        "A shortcut on the desktop and in the program menu opens LumoraStudio with a double click. "
        "You can add it later from Settings too."));
    QPushButton* yes = box.addButton(QCoreApplication::translate("lumora::DesktopShortcut", "Add shortcut"),
                                     QMessageBox::AcceptRole);
    box.addButton(QCoreApplication::translate("lumora::DesktopShortcut", "Not now"), QMessageBox::RejectRole);
    box.setDefaultButton(yes);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    QString error;
    if (!create(&error))
        QMessageBox::warning(parent, QStringLiteral("LumoraStudio"),
                             QCoreApplication::translate("lumora::DesktopShortcut", "Could not create the shortcut: %1")
                                 .arg(error));
}

} // namespace lumora::DesktopShortcut

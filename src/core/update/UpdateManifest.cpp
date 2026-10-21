#include "UpdateManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lumora::update {

std::optional<Manifest> Manifest::fromJson(const QByteArray& json) {
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    Manifest m;
    m.version = o.value(QLatin1String("version")).toString();
    const QJsonObject files = o.value(QLatin1String("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = QDir::cleanPath(it.key());
        // Never write outside the app folder.
        if (path.isEmpty() || QDir::isAbsolutePath(path) || path.startsWith(QLatin1String("..")))
            return std::nullopt;
        m.files.insert(path, it.value().toString().toLatin1().toLower());
    }
    if (m.files.isEmpty())
        return std::nullopt;
    const QJsonObject patch = o.value(QLatin1String("patch")).toObject();
    m.patchFrom = patch.value(QLatin1String("from")).toString();
    for (const QJsonValue& f : patch.value(QLatin1String("files")).toArray())
        m.patchFiles.insert(QDir::cleanPath(f.toString()));
    return m;
}

QByteArray fileSha256(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return {};
    return hash.result().toHex();
}

QStringList outdatedFiles(const Manifest& manifest, const QString& appDir) {
    const QDir dir(appDir);
    QStringList out;
    for (auto it = manifest.files.begin(); it != manifest.files.end(); ++it)
        if (fileSha256(dir.filePath(it.key())) != it.value())
            out.append(it.key());
    out.sort();
    return out;
}

bool patchCovers(const Manifest& manifest, const QStringList& outdated) {
    if (manifest.patchFiles.isEmpty())
        return false;
    for (const QString& f : outdated)
        if (!manifest.patchFiles.contains(f))
            return false;
    return true;
}

} // namespace lumora::update

#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>

namespace lumora::update {

// Describes a release package: the SHA-256 of every file in it and, when the
// release also ships a patch, which files the patch carries (those that
// changed since the previous release). The updater compares it with the
// installed files and downloads the small patch instead of the whole package
// whenever the patch has everything that is missing.
struct Manifest {
    QString version;
    QHash<QString, QByteArray> files; // path relative to the app folder ("/") -> sha256 hex
    QString patchFrom;                // version the patch was made against
    QSet<QString> patchFiles;

    static std::optional<Manifest> fromJson(const QByteArray& json);
};

// Lower-case hex SHA-256 of a file; empty when it cannot be read.
QByteArray fileSha256(const QString& path);

// Files of the manifest that are missing in `appDir` or differ from it.
QStringList outdatedFiles(const Manifest& manifest, const QString& appDir);

// True when the patch alone brings `appDir` to the new version.
bool patchCovers(const Manifest& manifest, const QStringList& outdated);

} // namespace lumora::update

#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QStringList>

#include "image/PhotoLibrary.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

namespace lumora {

// A LumoraStudio project (.lumora): references to media, the edit decisions
// on the timeline and the photo settings. It is a human-readable JSON file
// and never contains pixels; the source files are only ever read.
class Project : public QObject {
    Q_OBJECT
public:
    // 2: multiple tracks, titles, transitions, transforms, fades, gain.
    // 3 adds subtitle tracks; projects without them are still written as 2 so
    // older versions can open them. 4 adds freeze frames, 5 stabilisation and
    // motion tracking; both are written only when the timeline uses them.
    static constexpr int kFormatVersion = 9;
    static constexpr const char* kExtension = "lumora";

    explicit Project(QObject* parent = nullptr);

    MediaPool* media() const { return m_media; }
    Timeline* timeline() const { return m_timeline; }
    PhotoLibrary* photos() const { return m_photos; }

    const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& p) { m_filePath = p; }

    void clear();
    QByteArray toJson() const;
    bool save(const QString& path, QString* error = nullptr);
    // Replaces the content with the file. Media that cannot be found is
    // listed in `missing` (kept in the project, to be relinked later).
    bool load(const QString& path, QString* error = nullptr, QStringList* missing = nullptr);
    bool fromJson(const QByteArray& json, const QString& baseDir, QString* error = nullptr,
                  QStringList* missing = nullptr);

signals:
    void loaded();

private:
    MediaPool* m_media;
    Timeline* m_timeline;
    PhotoLibrary* m_photos;
    QString m_filePath;
};

} // namespace lumora

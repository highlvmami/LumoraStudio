#include "Project.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace lumora {

namespace {

QJsonValue ticksToJson(Ticks t) { return QString::number(t); } // 64-bit safe
Ticks ticksFromJson(const QJsonValue& v) { return v.toString().toLongLong(); }

QString resolvePath(const QJsonObject& o, const QString& baseDir) {
    const QString abs = o.value("path").toString();
    if (QFileInfo::exists(abs))
        return abs;
    const QString rel = o.value("relativePath").toString();
    if (!rel.isEmpty() && !baseDir.isEmpty()) {
        const QString candidate = QDir(baseDir).absoluteFilePath(rel);
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    return abs;
}

} // namespace

Project::Project(QObject* parent)
    : QObject(parent), m_media(new MediaPool(this)), m_timeline(new Timeline(this)), m_photos(new PhotoLibrary(this)) {}

void Project::clear() {
    m_timeline->reset();
    m_media->clear();
    m_photos->clear();
    m_filePath.clear();
}

QByteArray Project::toJson() const {
    const QString baseDir = m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).absolutePath();
    auto pathFields = [&](QJsonObject& o, const QString& path) {
        o["path"] = path;
        if (!baseDir.isEmpty())
            o["relativePath"] = QDir(baseDir).relativeFilePath(path);
    };

    QJsonObject root;
    root["format"] = "LumoraStudio project";
    const auto& stracks = m_timeline->state().tracks;
    const bool subtitles = std::any_of(stracks.begin(), stracks.end(), [](const Track& t) { return t.isSubtitle(); });
    auto anyClip = [&](auto pred) {
        return std::any_of(stracks.begin(), stracks.end(),
                           [&](const Track& t) { return std::any_of(t.clips.begin(), t.clips.end(), pred); });
    };
    const bool motion = anyClip([](const Clip& c) { return !c.stabilization.path.empty() || c.stabilization.enabled || !c.motion.isEmpty(); });
    const bool freezes = anyClip([](const Clip& c) { return c.kind == ClipKind::Freeze; });
    const auto& photoEntries = m_photos->entries();
    const bool ai = anyClip([](const Clip& c) { return !c.redaction.tracks.empty() || c.redaction.enabled || c.background.enabled; }) ||
                    std::any_of(photoEntries.begin(), photoEntries.end(), [](const auto& p) { return p.adjustments.hasAiChanges(); });
    root["version"] = ai ? 6 : motion ? 5 : freezes ? 4 : subtitles ? 3 : 2;
    root["application"] = QCoreApplication::applicationVersion();

    QJsonArray media;
    for (const auto& m : m_media->items()) {
        QJsonObject o;
        o["id"] = m->id.toString(QUuid::WithoutBraces);
        pathFields(o, m->path);
        o["size"] = QString::number(m->info.fileSize);
        media.append(o);
    }
    root["media"] = media;

    const TimelineState& st = m_timeline->state();
    QJsonObject tl;
    tl["frameRate"] = QJsonArray{st.frameRate.num, st.frameRate.den};
    tl["resolution"] = QJsonArray{st.resolution.width(), st.resolution.height()};
    if (!st.audioMaster.isIdentity())
        tl["audioMaster"] = st.audioMaster.toJson();
    QJsonArray tracks;
    for (const auto& t : st.tracks) {
        QJsonObject to;
        to["id"] = t.id.toString(QUuid::WithoutBraces);
        to["type"] = t.isVideo() ? "video" : t.isAudio() ? "audio" : "subtitle";
        if (!t.language.isEmpty())
            to["language"] = t.language;
        if (t.isSubtitle())
            to["subtitleStyle"] = t.subtitleStyle.toJson();
        to["name"] = t.name;
        to["locked"] = t.locked;
        to["muted"] = t.muted;
        to["hidden"] = t.hidden;
        to["magnetic"] = t.magnetic;
        QJsonArray clips;
        for (const auto& c : t.clips) {
            QJsonObject co;
            co["id"] = c.id.toString(QUuid::WithoutBraces);
            if (c.kind == ClipKind::Title) {
                co["kind"] = "title";
                co["title"] = c.title.toJson();
            } else if (c.kind == ClipKind::Subtitle) {
                co["kind"] = "subtitle";
                co["text"] = c.text;
            } else {
                if (c.kind == ClipKind::Freeze) {
                    co["kind"] = "freeze";
                    co["freezeTime"] = ticksToJson(c.freezeTime);
                }
                co["media"] = c.mediaId.toString(QUuid::WithoutBraces);
            }
            co["in"] = ticksToJson(c.sourceIn);
            co["out"] = ticksToJson(c.sourceOut);
            co["start"] = ticksToJson(c.timelineStart);
            if (!c.linkId.isNull())
                co["link"] = c.linkId.toString(QUuid::WithoutBraces);
            if (!c.anchorId.isNull()) {
                co["anchor"] = c.anchorId.toString(QUuid::WithoutBraces);
                co["anchorSource"] = ticksToJson(c.anchorSource);
            }
            if (!c.transform.isIdentity())
                co["transform"] = c.transform.toJson();
            if (!c.color.isIdentity())
                co["color"] = c.color.toJson();
            if (!c.stabilization.path.empty() || c.stabilization.enabled)
                co["stabilization"] = c.stabilization.toJson();
            if (!c.motion.isEmpty())
                co["motion"] = c.motion.toJson();
            if (!c.redaction.tracks.empty() || c.redaction.enabled)
                co["redaction"] = c.redaction.toJson();
            if (c.background.enabled)
                co["background"] = c.background.toJson();
            if (!c.transitionIn.isNone())
                co["transition"] = c.transitionIn.toJson();
            if (c.fadeIn > 0)
                co["fadeIn"] = ticksToJson(c.fadeIn);
            if (c.fadeOut > 0)
                co["fadeOut"] = ticksToJson(c.fadeOut);
            if (c.gainDb != 0.0)
                co["gainDb"] = c.gainDb;
            if (!c.audio.isIdentity())
                co["audio"] = c.audio.toJson();
            clips.append(co);
        }
        to["clips"] = clips;
        tracks.append(to);
    }
    tl["tracks"] = tracks;
    tl["timeBase"] = QStringLiteral("1/%1").arg(kTicksPerSecond);
    root["timeline"] = tl;

    QJsonArray photos;
    for (const auto& p : m_photos->entries()) {
        QJsonObject o;
        o["id"] = p.id.toString(QUuid::WithoutBraces);
        pathFields(o, p.path);
        o["adjustments"] = p.adjustments.toJson();
        photos.append(o);
    }
    root["photos"] = photos;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool Project::save(const QString& path, QString* error) {
    const QString old = m_filePath;
    m_filePath = QFileInfo(path).absoluteFilePath();
    QSaveFile f(m_filePath); // atomic: never leaves a half-written project
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        m_filePath = old;
        return false;
    }
    f.write(toJson());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        m_filePath = old;
        return false;
    }
    return true;
}

bool Project::load(const QString& path, QString* error, QStringList* missing) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (!fromJson(f.readAll(), QFileInfo(path).absolutePath(), error, missing))
        return false;
    m_filePath = QFileInfo(path).absoluteFilePath();
    emit loaded();
    return true;
}

bool Project::fromJson(const QByteArray& json, const QString& baseDir, QString* error, QStringList* missing) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull() || !doc.isObject()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("version").toInt() > kFormatVersion) {
        if (error)
            *error = tr("This project was saved by a newer LumoraStudio version.");
        return false;
    }

    m_timeline->reset();
    m_media->clear();
    m_photos->clear();

    for (const auto& v : root.value("media").toArray()) {
        const QJsonObject o = v.toObject();
        const QString path = resolvePath(o, baseDir);
        auto item = std::make_shared<MediaItem>();
        item->id = QUuid::fromString(o.value("id").toString());
        item->path = path;
        if (auto info = probeMedia(path)) {
            item->info = *info;
        } else {
            item->info.path = path;
            if (missing)
                missing->append(path);
        }
        if (!item->info.video) {
            std::lock_guard lock(item->mutex);
            item->keyframesReady = true;
        }
        m_media->add(item);
    }

    TimelineState st;
    const QJsonObject tl = root.value("timeline").toObject();
    const QJsonArray fr = tl.value("frameRate").toArray();
    if (fr.size() == 2)
        st.frameRate = AVRational{fr[0].toInt(), fr[1].toInt()};
    const QJsonArray res = tl.value("resolution").toArray();
    if (res.size() == 2)
        st.resolution = QSize(res[0].toInt(), res[1].toInt());
    st.audioMaster = AudioMaster::fromJson(tl.value("audioMaster").toObject());
    for (const auto& tv : tl.value("tracks").toArray()) {
        const QJsonObject to = tv.toObject();
        Track t;
        t.id = QUuid::fromString(to.value("id").toString());
        const QString type = to.value("type").toString();
        t.type = type == "audio" ? TrackType::Audio : type == "subtitle" ? TrackType::Subtitle : TrackType::Video;
        t.language = to.value("language").toString();
        t.subtitleStyle = SubtitleStyle::fromJson(to.value("subtitleStyle").toObject());
        t.name = to.value("name").toString();
        t.locked = to.value("locked").toBool();
        t.muted = to.value("muted").toBool();
        t.hidden = to.value("hidden").toBool();
        t.magnetic = to.value("magnetic").toBool();
        for (const auto& cv : to.value("clips").toArray()) {
            const QJsonObject co = cv.toObject();
            Clip c;
            c.id = QUuid::fromString(co.value("id").toString());
            if (co.value("kind").toString() == "title") {
                c.kind = ClipKind::Title;
                c.title = TitleData::fromJson(co.value("title").toObject());
            } else if (co.value("kind").toString() == "subtitle") {
                c.kind = ClipKind::Subtitle;
                c.text = co.value("text").toString();
            } else if (co.value("kind").toString() == "freeze") {
                c.kind = ClipKind::Freeze;
                c.freezeTime = ticksFromJson(co.value("freezeTime"));
            }
            c.mediaId = QUuid::fromString(co.value("media").toString());
            c.sourceIn = ticksFromJson(co.value("in"));
            c.sourceOut = ticksFromJson(co.value("out"));
            c.timelineStart = ticksFromJson(co.value("start"));
            c.linkId = QUuid::fromString(co.value("link").toString());
            c.anchorId = QUuid::fromString(co.value("anchor").toString());
            c.anchorSource = ticksFromJson(co.value("anchorSource"));
            if (co.contains("transform"))
                c.transform = ClipTransform::fromJson(co.value("transform").toObject());
            if (co.contains("color"))
                c.color = ColorGrade::fromJson(co.value("color").toObject());
            if (co.contains("stabilization"))
                c.stabilization = Stabilization::fromJson(co.value("stabilization").toObject());
            c.motion = MotionPath::fromJson(co.value("motion").toArray());
            if (co.contains("redaction"))
                c.redaction = Redaction::fromJson(co.value("redaction").toObject());
            if (co.contains("background"))
                c.background = BackgroundRemoval::fromJson(co.value("background").toObject());
            if (co.contains("transition"))
                c.transitionIn = Transition::fromJson(co.value("transition").toObject());
            c.fadeIn = ticksFromJson(co.value("fadeIn"));
            c.fadeOut = ticksFromJson(co.value("fadeOut"));
            c.gainDb = co.value("gainDb").toDouble(0.0);
            c.audio = AudioEffects::fromJson(co.value("audio").toObject());
            t.clips.push_back(c);
        }
        st.tracks.push_back(t);
    }
    m_timeline->setState(st);

    for (const auto& v : root.value("photos").toArray()) {
        const QJsonObject o = v.toObject();
        const QString path = resolvePath(o, baseDir);
        if (!QFileInfo::exists(path) && missing)
            missing->append(path);
        m_photos->add(path, PhotoAdjustments::fromJson(o.value("adjustments").toObject()));
    }
    return true;
}

} // namespace lumora

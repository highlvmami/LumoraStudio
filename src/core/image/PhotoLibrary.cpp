#include "PhotoLibrary.h"

#include <QFileInfo>
#include <algorithm>

namespace lumora {

QUuid PhotoLibrary::add(const QString& path, const PhotoAdjustments& adj) {
    if (const auto* e = findByPath(path))
        return e->id;
    PhotoEntry e{QUuid::createUuid(), QFileInfo(path).absoluteFilePath(), adj};
    m_entries.push_back(e);
    emit added(e.id);
    return e.id;
}

void PhotoLibrary::remove(const QUuid& id) {
    auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const auto& e) { return e.id == id; });
    if (it == m_entries.end())
        return;
    m_entries.erase(it);
    emit removed(id);
}

void PhotoLibrary::clear() {
    m_entries.clear();
    emit cleared();
}

const PhotoEntry* PhotoLibrary::find(const QUuid& id) const {
    for (const auto& e : m_entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

const PhotoEntry* PhotoLibrary::findByPath(const QString& path) const {
    for (const auto& e : m_entries)
        if (QFileInfo(e.path) == QFileInfo(path))
            return &e;
    return nullptr;
}

void PhotoLibrary::setAdjustments(const QUuid& id, const PhotoAdjustments& adj) {
    for (auto& e : m_entries)
        if (e.id == id) {
            if (e.adjustments == adj)
                return;
            e.adjustments = adj;
            emit adjustmentsChanged(id);
            return;
        }
}

PhotoAdjustCommand::PhotoAdjustCommand(PhotoLibrary* lib, QUuid photo, PhotoAdjustments before,
                                       PhotoAdjustments after, const QString& text, Control control)
    : QUndoCommand(text), m_lib(lib), m_photo(photo), m_before(before), m_after(after), m_control(control),
      m_time(QDateTime::currentDateTimeUtc()) {}

void PhotoAdjustCommand::undo() { m_lib->setAdjustments(m_photo, m_before); }
void PhotoAdjustCommand::redo() { m_lib->setAdjustments(m_photo, m_after); }

bool PhotoAdjustCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = static_cast<const PhotoAdjustCommand*>(other);
    if (o->m_photo != m_photo || o->m_control != m_control || m_control == Reset || m_control == Other)
        return false;
    if (m_time.msecsTo(o->m_time) > 1500)
        return false;
    m_after = o->m_after;
    m_time = o->m_time;
    return true;
}

} // namespace lumora

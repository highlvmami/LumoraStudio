#pragma once

#include <QDateTime>
#include <QObject>
#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "image/PhotoAdjustments.h"

namespace lumora {

struct PhotoEntry {
    QUuid id;
    QString path;
    PhotoAdjustments adjustments;
};

// Photos opened in the project with their non-destructive settings.
class PhotoLibrary : public QObject {
    Q_OBJECT
public:
    explicit PhotoLibrary(QObject* parent = nullptr) : QObject(parent) {}

    QUuid add(const QString& path, const PhotoAdjustments& adj = {});
    void remove(const QUuid& id);
    void clear();
    const PhotoEntry* find(const QUuid& id) const;
    const PhotoEntry* findByPath(const QString& path) const;
    const std::vector<PhotoEntry>& entries() const { return m_entries; }
    void setAdjustments(const QUuid& id, const PhotoAdjustments& adj);

signals:
    void added(const QUuid& id);
    void removed(const QUuid& id);
    void adjustmentsChanged(const QUuid& id);
    void cleared();

private:
    std::vector<PhotoEntry> m_entries;
};

// Undo entry for a change of photo settings. Rapid successive changes of the
// same control (a slider drag) merge into one step.
class PhotoAdjustCommand : public QUndoCommand {
public:
    enum Control { Exposure = 1, Contrast, Temperature, Tint, Crop, Reset, Other, Color, Layer };

    // Commands merge when control and key match (e.g. Color + "shadows",
    // or Layer + "<layer id>:opacity").
    PhotoAdjustCommand(PhotoLibrary* lib, QUuid photo, PhotoAdjustments before, PhotoAdjustments after,
                       const QString& text, Control control, const QString& key = {});

    void undo() override;
    void redo() override;
    int id() const override { return 0x5048; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    PhotoLibrary* m_lib;
    QUuid m_photo;
    PhotoAdjustments m_before, m_after;
    Control m_control;
    QString m_key;
    QDateTime m_time;
};

} // namespace lumora

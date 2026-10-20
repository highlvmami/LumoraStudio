#pragma once

#include <QUndoCommand>
#include <QUndoStack>
#include <functional>

#include "timeline/Timeline.h"

namespace lumora {

// Undo for the timeline works by snapshotting the (small) edit state before
// and after an operation. This keeps every edit operation trivially
// undoable, including future complex ones (ripple/roll/slip, compound clips),
// without writing an inverse for each. Undo depth is unlimited.
class TimelineSnapshotCommand : public QUndoCommand {
public:
    TimelineSnapshotCommand(Timeline* timeline, TimelineState before, TimelineState after, const QString& text,
                            const QString& mergeKey = {});

    void undo() override;
    void redo() override;
    // Consecutive edits with the same non-empty merge key (e.g. dragging a
    // slider in the properties panel) become one undo step.
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    Timeline* m_timeline;
    QString m_mergeKey;
    TimelineState m_before;
    TimelineState m_after;
    bool m_firstRedo = true;
};

// Runs `op` on the timeline and pushes an undo entry if it changed anything.
bool runTimelineEdit(QUndoStack* stack, Timeline* timeline, const QString& text, const std::function<bool()>& op,
                     const QString& mergeKey = {});

// Pushes an entry for an edit that was already applied interactively (e.g. a
// trim drag), given the state captured before it started.
void pushTimelineSnapshot(QUndoStack* stack, Timeline* timeline, const TimelineState& before, const QString& text);

} // namespace lumora

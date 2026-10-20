#include "TimelineCommands.h"

namespace lumora {

TimelineSnapshotCommand::TimelineSnapshotCommand(Timeline* timeline, TimelineState before, TimelineState after,
                                                 const QString& text)
    : QUndoCommand(text), m_timeline(timeline), m_before(std::move(before)), m_after(std::move(after)) {}

void TimelineSnapshotCommand::undo() { m_timeline->setState(m_before); }

void TimelineSnapshotCommand::redo() {
    if (m_firstRedo) {
        // The edit is already applied when the command is pushed.
        m_firstRedo = false;
        return;
    }
    m_timeline->setState(m_after);
}

bool runTimelineEdit(QUndoStack* stack, Timeline* timeline, const QString& text, const std::function<bool()>& op) {
    TimelineState before = timeline->state();
    if (!op())
        return false;
    if (timeline->state() == before)
        return false;
    stack->push(new TimelineSnapshotCommand(timeline, std::move(before), timeline->state(), text));
    return true;
}

void pushTimelineSnapshot(QUndoStack* stack, Timeline* timeline, const TimelineState& before, const QString& text) {
    if (timeline->state() == before)
        return;
    stack->push(new TimelineSnapshotCommand(timeline, before, timeline->state(), text));
}

} // namespace lumora

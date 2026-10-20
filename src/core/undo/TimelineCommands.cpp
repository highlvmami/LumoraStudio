#include "TimelineCommands.h"

namespace lumora {

TimelineSnapshotCommand::TimelineSnapshotCommand(Timeline* timeline, TimelineState before, TimelineState after,
                                                 const QString& text, const QString& mergeKey)
    : QUndoCommand(text), m_timeline(timeline), m_mergeKey(mergeKey), m_before(std::move(before)),
      m_after(std::move(after)) {}

bool TimelineSnapshotCommand::mergeWith(const QUndoCommand* other) {
    auto* o = dynamic_cast<const TimelineSnapshotCommand*>(other);
    if (!o || o->m_mergeKey != m_mergeKey || o->m_timeline != m_timeline)
        return false;
    m_after = o->m_after;
    return true;
}

void TimelineSnapshotCommand::undo() { m_timeline->setState(m_before); }

void TimelineSnapshotCommand::redo() {
    if (m_firstRedo) {
        // The edit is already applied when the command is pushed.
        m_firstRedo = false;
        return;
    }
    m_timeline->setState(m_after);
}

bool runTimelineEdit(QUndoStack* stack, Timeline* timeline, const QString& text, const std::function<bool()>& op,
                     const QString& mergeKey) {
    TimelineState before = timeline->state();
    if (!op())
        return false;
    if (timeline->state() == before)
        return false;
    stack->push(new TimelineSnapshotCommand(timeline, std::move(before), timeline->state(), text, mergeKey));
    return true;
}

void pushTimelineSnapshot(QUndoStack* stack, Timeline* timeline, const TimelineState& before, const QString& text) {
    if (timeline->state() == before)
        return;
    stack->push(new TimelineSnapshotCommand(timeline, before, timeline->state(), text));
}

} // namespace lumora

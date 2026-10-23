#pragma once

#include <QList>
#include <QString>
#include <QUuid>

class QWidget;
class QUndoStack;

namespace lumora {

class Project;

// Smart editing tools (Tools menu, Properties): each asks for its options,
// does the work behind a progress dialog and makes one undoable change.
// They return a line for the status bar, empty when nothing changed.
namespace smartedit {

// Removes pauses and hesitations ("ııı", "eee") from the storyline.
QString cutSilences(QWidget* parent, Project* project, QUndoStack* undo);
// Moves the storyline cuts onto the beat of the music. A selected audio
// clip is taken as the music; otherwise the longest music clip.
QString cutToBeat(QWidget* parent, Project* project, QUndoStack* undo, const QList<QUuid>& selection);
// Turns the project into a portrait (or square) video that keeps the
// speaker in the frame.
QString reframe(QWidget* parent, Project* project, QUndoStack* undo);

} // namespace smartedit
} // namespace lumora

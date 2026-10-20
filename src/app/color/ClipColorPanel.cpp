#include "ClipColorPanel.h"

#include <QFileInfo>
#include <QScrollArea>
#include <QVBoxLayout>

#include "color/ColorPanel.h"
#include "project/Project.h"
#include "undo/TimelineCommands.h"

namespace lumora {

ClipColorPanel::ClipColorPanel(Project* project, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), m_project(project), m_undo(undo) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_panel = new ColorPanel;
    scroll->setWidget(m_panel);
    l->addWidget(scroll);

    connect(m_panel, &ColorPanel::gradeEdited, this,
            [this](const ColorGrade& g, const QString& text, const QString& field) {
                if (m_clip.isNull())
                    return;
                m_updating = true;
                const QString key =
                    field.isEmpty() ? QString() : QStringLiteral("%1:%2:color:%3").arg(m_session).arg(m_clip.toString(), field);
                runTimelineEdit(m_undo, m_project->timeline(), text,
                                [&] { return m_project->timeline()->setColor(m_clip, g); }, key);
                m_updating = false;
            });
    connect(m_project->timeline(), &Timeline::changed, this, [this] {
        if (!m_updating)
            refresh();
    });
    refresh();
}

void ClipColorPanel::showClip(const QUuid& clipId) {
    if (clipId == m_clip)
        return;
    m_clip = clipId;
    ++m_session;
    refresh();
}

void ClipColorPanel::refresh() {
    const Timeline* tl = m_project->timeline();
    const Clip* c = m_clip.isNull() ? nullptr : tl->clip(m_clip);
    const auto loc = c ? tl->locate(m_clip) : std::nullopt;
    if (c && loc && tl->tracks()[loc->track].type == TrackType::Audio)
        c = nullptr;
    if (!c || c->kind != ClipKind::Media) {
        m_panel->setGrade({});
        m_panel->setEditable(false);
        m_panel->setHeader(c ? tr("Titles have no colour settings; select a video clip.")
                             : tr("Select a video clip in the timeline to grade it."));
        return;
    }
    QString name;
    if (auto m = m_project->media()->find(c->mediaId))
        name = QFileInfo(m->path).fileName();
    m_panel->setHeader(name);
    m_panel->setEditable(true);
    m_panel->setGrade(c->color);
}

} // namespace lumora

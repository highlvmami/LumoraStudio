#include "SmartEdit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <cmath>

#include "Tasks.h"
#include "Theme.h"
#include "ai/Reframe.h"
#include "ai/SmartCut.h"
#include "ai/Vision.h"
#include "audio/Beats.h"
#include "motion/Analysis.h"
#include "project/Project.h"
#include "undo/TimelineCommands.h"

namespace lumora::smartedit {

namespace {

struct SmartEdit {
    Q_DECLARE_TR_FUNCTIONS(lumora::SmartEdit)
};

QLabel* intro(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

// A dialog with `content`, OK named `okText` and Cancel. True when accepted.
bool ask(QWidget* parent, const QString& title, const QString& okText, const std::function<void(QVBoxLayout*)>& content) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(460);
    auto* layout = new QVBoxLayout(&dlg);
    content(layout);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(okText);
    markPrimary(buttons->button(QDialogButtonBox::Ok));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    return dlg.exec() == QDialog::Accepted;
}

QDoubleSpinBox* seconds(double min, double max, double step, double value) {
    auto* sb = new QDoubleSpinBox;
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setDecimals(2);
    sb->setSuffix(QStringLiteral(" s"));
    sb->setValue(value);
    return sb;
}

QString duration(Ticks t) {
    const double s = ticksToSeconds(t);
    return s < 60 ? SmartEdit::tr("%1 s").arg(s, 0, 'f', 1)
                  : SmartEdit::tr("%1 min %2 s").arg(static_cast<int>(s) / 60).arg(static_cast<int>(s) % 60);
}

} // namespace

QString cutSilences(QWidget* parent, Project* project, QUndoStack* undo) {
    const QString title = SmartEdit::tr("Cut silences and hesitations");
    Timeline* tl = project->timeline();
    if (tl->isEmpty()) {
        QMessageBox::information(parent, title, SmartEdit::tr("The timeline is empty. Add a video first."));
        return {};
    }
    QSettings settings;
    QCheckBox* pauses = nullptr;
    QCheckBox* fillers = nullptr;
    QDoubleSpinBox* minPause = nullptr;
    QDoubleSpinBox* keep = nullptr;
    QComboBox* language = nullptr;
    QComboBox* model = nullptr;
    const bool ok = ask(parent, title, SmartEdit::tr("Cut"), [&](QVBoxLayout* l) {
        l->addWidget(intro(SmartEdit::tr("LumoraStudio listens to the main track and removes the pauses and the "
                                         "hesitations. Music on other tracks stays where it is. Undo brings everything back.")));
        pauses = new QCheckBox(SmartEdit::tr("Remove pauses"));
        pauses->setChecked(settings.value(QStringLiteral("smartcut/pauses"), true).toBool());
        l->addWidget(pauses);
        auto* form = new QFormLayout;
        minPause = seconds(0.2, 5.0, 0.1, settings.value(QStringLiteral("smartcut/minPause"), 0.6).toDouble());
        minPause->setToolTip(SmartEdit::tr("Shorter pauses belong to speaking and stay"));
        form->addRow(SmartEdit::tr("Shortest pause"), minPause);
        keep = seconds(0.0, 1.0, 0.05, settings.value(QStringLiteral("smartcut/keep"), 0.15).toDouble());
        keep->setToolTip(SmartEdit::tr("A little of each pause stays before and after the words, so they do not run into each other"));
        form->addRow(SmartEdit::tr("Breathing room"), keep);
        l->addLayout(form);
        fillers = new QCheckBox(SmartEdit::tr("Remove hesitations (ııı, eee, hmm, um)"));
        fillers->setToolTip(SmartEdit::tr("Uses speech recognition on this computer; the model is downloaded once"));
        fillers->setChecked(settings.value(QStringLiteral("smartcut/fillers"), true).toBool());
        l->addWidget(fillers);
        auto* speechForm = new QFormLayout;
        language = speechLanguageBox();
        model = speechModelBox();
        speechForm->addRow(SmartEdit::tr("Spoken language"), language);
        speechForm->addRow(SmartEdit::tr("Model"), model);
        l->addLayout(speechForm);
        auto sync = [=] {
            minPause->setEnabled(pauses->isChecked());
            keep->setEnabled(pauses->isChecked());
            language->setEnabled(fillers->isChecked());
            model->setEnabled(fillers->isChecked());
        };
        QObject::connect(pauses, &QCheckBox::toggled, sync);
        QObject::connect(fillers, &QCheckBox::toggled, sync);
        sync();
    });
    if (!ok || (!pauses->isChecked() && !fillers->isChecked()))
        return {};
    settings.setValue(QStringLiteral("smartcut/pauses"), pauses->isChecked());
    settings.setValue(QStringLiteral("smartcut/fillers"), fillers->isChecked());
    settings.setValue(QStringLiteral("smartcut/minPause"), minPause->value());
    settings.setValue(QStringLiteral("smartcut/keep"), keep->value());

    smartcut::Options options;
    options.silences = pauses->isChecked();
    options.minSilence = minPause->value();
    options.keep = keep->value();
    options.fillers = fillers->isChecked();
    if (options.fillers) {
        options.speech.modelPath = ensureSpeechModel(parent, language, model, title);
        if (options.speech.modelPath.isEmpty())
            return {};
        options.speech.language = language->currentData().toString();
    }
    const TimelineState state = tl->state();
    const MediaPool* pool = project->media();
    const auto result = withProgress<smartcut::Result>(parent, SmartEdit::tr("Listening for pauses and hesitations…"),
                                                       [state, pool, options](const TaskProgress& p) {
                                                           return smartcut::findCuts(state, *pool, options, p);
                                                       });
    if (!result || result->cancelled)
        return {};
    if (!result->error.isEmpty()) {
        QMessageBox::warning(parent, title, result->error);
        return {};
    }
    if (result->cuts.empty()) {
        QMessageBox::information(parent, title, SmartEdit::tr("No pauses or hesitations were found."));
        return {};
    }
    std::vector<std::pair<Ticks, Ticks>> ranges;
    for (const auto& c : result->cuts)
        ranges.emplace_back(c.from, c.to);
    if (!runTimelineEdit(undo, tl, title, [&] { return tl->removeRanges(ranges, true); }))
        return {};
    return SmartEdit::tr("Removed %1 pause(s) and %2 hesitation(s): the video is %3 shorter. Undo (Ctrl+Z) brings them back.")
        .arg(result->count(smartcut::CutKind::Silence))
        .arg(result->count(smartcut::CutKind::Filler))
        .arg(duration(result->total()));
}

QString cutToBeat(QWidget* parent, Project* project, QUndoStack* undo, const QList<QUuid>& selection) {
    const QString title = SmartEdit::tr("Cut to the beat");
    Timeline* tl = project->timeline();
    // The selected audio clip, else the longest music clip.
    QUuid music;
    for (const QUuid& id : selection) {
        const auto loc = tl->locate(id);
        if (loc && tl->tracks()[loc->track].isAudio() && loc->track != tl->primaryAudioTrack()) {
            music = id;
            break;
        }
    }
    if (music.isNull())
        music = beats::findMusicClip(tl->state());
    const Clip* clip = music.isNull() ? nullptr : tl->clip(music);
    if (!clip || tl->tracks()[tl->primaryVideoTrack()].clips.size() < 2) {
        QMessageBox::information(parent, title,
                                 SmartEdit::tr("Put the clips on the main track and the music on an audio track below them "
                                               "first. Then LumoraStudio moves the cuts onto the beat."));
        return {};
    }
    QString musicName;
    if (auto m = project->media()->find(clip->mediaId))
        musicName = QFileInfo(m->path).fileName();
    QComboBox* mode = nullptr;
    const bool ok = ask(parent, title, SmartEdit::tr("Cut to the beat"), [&](QVBoxLayout* l) {
        l->addWidget(intro(SmartEdit::tr("LumoraStudio finds the beat of <b>%1</b> and moves the cuts between the clips "
                                         "on the main track onto it. Clips get only as long as their video allows.")
                               .arg(musicName.toHtmlEscaped())));
        auto* form = new QFormLayout;
        mode = new QComboBox;
        mode->addItem(SmartEdit::tr("Move each cut to the nearest beat"), 0);
        mode->addItem(SmartEdit::tr("Every clip lasts 1 beat (fast)"), 1);
        mode->addItem(SmartEdit::tr("Every clip lasts 2 beats"), 2);
        mode->addItem(SmartEdit::tr("Every clip lasts 4 beats (one bar)"), 4);
        mode->addItem(SmartEdit::tr("Every clip lasts 8 beats (slow)"), 8);
        mode->setCurrentIndex(std::max(0, mode->findData(QSettings().value(QStringLiteral("beats/every"), 0))));
        form->addRow(SmartEdit::tr("Cuts"), mode);
        l->addLayout(form);
    });
    if (!ok)
        return {};
    const int every = mode->currentData().toInt();
    QSettings().setValue(QStringLiteral("beats/every"), every);
    const TimelineState state = tl->state();
    const MediaPool* pool = project->media();
    const auto result = withProgress<beats::ClipBeats>(parent, SmartEdit::tr("Listening for the beat…"),
                                                       [state, pool, music](const TaskProgress& p) {
                                                           return beats::clipBeats(state, *pool, music, p);
                                                       });
    if (!result || result->cancelled)
        return {};
    if (!result->error.isEmpty()) {
        QMessageBox::warning(parent, title, result->error);
        return {};
    }
    int changed = 0;
    runTimelineEdit(undo, tl, title, [&] {
        changed = beats::cutToBeats(*tl, result->times, every, [pool](const QUuid& id) {
            auto m = pool->find(id);
            return m ? m->info.duration : Ticks(0);
        });
        return changed > 0;
    });
    if (changed == 0) {
        QMessageBox::information(parent, title, SmartEdit::tr("The cuts are already on the beat (%1 BPM).").arg(std::lround(result->bpm)));
        return {};
    }
    return SmartEdit::tr("%1 clip(s) cut to the beat of the music (%2 BPM).").arg(changed).arg(std::lround(result->bpm));
}

QString reframe(QWidget* parent, Project* project, QUndoStack* undo) {
    const QString title = SmartEdit::tr("Vertical video");
    Timeline* tl = project->timeline();
    const MediaPool* pool = project->media();
    // The storyline clips with a picture and their files.
    struct Job {
        QUuid id;
        QString path;
        bool video = false; // a moving picture (stills are only filled)
    };
    std::vector<Job> jobs;
    QSize source;
    for (const Clip& c : tl->tracks()[tl->primaryVideoTrack()].clips) {
        auto m = c.hasMedia() ? pool->find(c.mediaId) : nullptr;
        if (!m || (!m->info.video && m->info.kind != MediaKind::Image))
            continue;
        jobs.push_back({c.id, m->path, m->info.kind == MediaKind::Video && c.kind == ClipKind::Media});
        if (source.isEmpty() && m->info.video) {
            const auto& v = *m->info.video;
            source = (v.rotation == 90 || v.rotation == 270) ? QSize(v.height, v.width) : QSize(v.width, v.height);
        }
    }
    if (jobs.empty()) {
        QMessageBox::information(parent, title, SmartEdit::tr("The main track has no video. Add a video first."));
        return {};
    }
    if (source.isEmpty())
        source = QSize(1920, 1080);
    QComboBox* format = nullptr;
    QCheckBox* follow = nullptr;
    const bool ok = ask(parent, title, SmartEdit::tr("Make it vertical"), [&](QVBoxLayout* l) {
        l->addWidget(intro(SmartEdit::tr("The picture fills the new frame edge to edge (nothing is stretched) and moves "
                                         "with the person speaking, so they stay in view. Faces are found on this computer.")));
        auto* form = new QFormLayout;
        format = new QComboBox;
        format->addItem(SmartEdit::tr("Portrait 9:16 (Reels, TikTok, Shorts)"), 9.0 / 16.0);
        format->addItem(SmartEdit::tr("Portrait 4:5 (Instagram posts)"), 4.0 / 5.0);
        format->addItem(SmartEdit::tr("Square 1:1"), 1.0);
        format->setCurrentIndex(std::max(0, format->findData(QSettings().value(QStringLiteral("reframe/aspect"), 9.0 / 16.0))));
        form->addRow(SmartEdit::tr("Format"), format);
        l->addLayout(form);
        follow = new QCheckBox(SmartEdit::tr("Keep the speaker in the frame"));
        follow->setChecked(true);
        l->addWidget(follow);
    });
    if (!ok)
        return {};
    const double aspect = format->currentData().toDouble();
    QSettings().setValue(QStringLiteral("reframe/aspect"), aspect);
    const QSize frame = ai::reframeSize(aspect, source);
    const bool faces = follow->isChecked() && ensureVisionModels(parent, {QStringLiteral("face")}, title);
    if (follow->isChecked() && !faces)
        return {};

    // Where the speaker is in every video clip.
    std::map<QUuid, std::vector<ai::SubjectSample>> subjects;
    if (faces) {
        std::vector<std::pair<QUuid, Clip>> clips;
        std::vector<QString> paths;
        for (const Job& j : jobs)
            if (j.video) {
                clips.emplace_back(j.id, *tl->clip(j.id));
                paths.push_back(j.path);
            }
        using Found = std::map<QUuid, std::vector<ai::SubjectSample>>;
        const auto found = withProgress<std::optional<Found>>(
            parent, SmartEdit::tr("Finding the speaker…"), [clips, paths](const TaskProgress& p) -> std::optional<Found> {
                Found out;
                for (size_t i = 0; i < clips.size(); ++i) {
                    const auto r = ai::findSubject(paths[i], clips[i].second, [&](double f) {
                        return p((i + f) / clips.size());
                    });
                    if (r.cancelled)
                        return std::nullopt;
                    if (r.ok())
                        out[clips[i].first] = r.samples;
                }
                return out;
            });
        if (!found || !*found)
            return {};
        subjects = **found;
    }

    int followed = 0;
    const bool changed = runTimelineEdit(undo, tl, title, [&] {
        bool any = false;
        if (tl->state().resolution != frame) {
            tl->setResolution(frame);
            any = true;
        }
        for (const Job& j : jobs) {
            const Clip* c = tl->clip(j.id);
            if (!c)
                continue;
            const double pa = motion::pictureAspect(*pool, *c);
            ClipTransform t = c->transform;
            t.scale = ai::coverScale(pa, frame);
            t.x = t.y = 0.0;
            any |= tl->setTransform(j.id, t);
            MotionPath path;
            if (auto it = subjects.find(j.id); it != subjects.end()) {
                path = ai::cameraPath(it->second, pa, frame);
                followed += !path.isEmpty();
            }
            any |= tl->setMotionPath(j.id, path);
        }
        return any;
    });
    if (!changed)
        return {};
    return SmartEdit::tr("The video is now %1 × %2. %3 clip(s) follow the speaker; move a clip by hand in the viewer if "
                         "you want it elsewhere.")
        .arg(frame.width())
        .arg(frame.height())
        .arg(followed);
}

} // namespace lumora::smartedit

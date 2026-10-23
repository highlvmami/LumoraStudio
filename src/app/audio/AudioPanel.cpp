#include "AudioPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>

#include "audio/Loudness.h"
#include "color/SliderRow.h"
#include "project/Project.h"
#include "undo/TimelineCommands.h"

namespace lumora {

namespace {
QLabel* hint(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    return l;
}
} // namespace

AudioPanel::AudioPanel(Project* project, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), m_project(project), m_undo(undo) {
    auto* outerL = new QVBoxLayout(this);
    outerL->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* inner = new QWidget;
    auto* l = new QVBoxLayout(inner);
    l->setContentsMargins(8, 8, 8, 8);
    scroll->setWidget(inner);
    outerL->addWidget(scroll);

    m_header = new QLabel;
    m_header->setWordWrap(true);
    QFont hf = m_header->font();
    hf.setBold(true);
    m_header->setFont(hf);
    l->addWidget(m_header);

    m_clipBody = new QWidget;
    auto* cl = new QVBoxLayout(m_clipBody);
    cl->setContentsMargins(0, 0, 0, 0);
    l->addWidget(m_clipBody);

    auto* clean = new QGroupBox(tr("Clip sound"));
    auto* cleanL = new QVBoxLayout(clean);
    m_gain = new SliderRow(tr("Volume (dB)"), -40, 20, 0.1, 1);
    m_lowCut = new SliderRow(tr("Low cut (Hz)"), 0, 300, 1, 0);
    m_lowCut->setToolTip(tr("Removes rumble, wind and handling noise below this frequency; 0 = off. "
                            "80 Hz suits most voices."));
    m_hum = new QComboBox;
    m_hum->addItem(tr("Hum removal: off"), 0);
    m_hum->addItem(tr("Remove 50 Hz hum (Europe, Türkiye)"), 50);
    m_hum->addItem(tr("Remove 60 Hz hum (Americas)"), 60);
    m_hum->setToolTip(tr("Takes out the buzz of mains electricity (cables, lights, amplifiers) and its overtones "
                         "with very narrow filters; the voice around it is not touched."));
    m_wind = new SliderRow(tr("Wind reduction (%)"), 0, 100, 1, 0);
    m_wind->setToolTip(tr("Pulls down the rumble and the gusts of wind on the microphone while a clean voice stays "
                          "as it is."));
    m_denoise = new SliderRow(tr("Noise reduction (%)"), 0, 100, 1, 0);
    m_denoise->setToolTip(tr("Lowers steady background noise (hum, hiss, fans, air conditioning) between and "
                             "under the words. High values can sound watery."));
    cleanL->addWidget(m_gain);
    cleanL->addWidget(m_lowCut);
    cleanL->addWidget(m_hum);
    for (auto* s : {m_wind, m_denoise})
        cleanL->addWidget(s);
    cl->addWidget(clean);

    auto* eq = new QGroupBox(tr("Equaliser"));
    auto* eqL = new QVBoxLayout(eq);
    m_eqLow = new SliderRow(tr("Bass (dB)"), -15, 15, 0.5, 1);
    m_eqMid = new SliderRow(tr("Middle (dB)"), -15, 15, 0.5, 1);
    m_eqMidFreq = new SliderRow(tr("Middle at (Hz)"), 200, 5000, 10, 0);
    m_eqMidFreq->setDefault(1000);
    m_eqHigh = new SliderRow(tr("Treble (dB)"), -15, 15, 0.5, 1);
    for (auto* s : {m_eqLow, m_eqMid, m_eqMidFreq, m_eqHigh})
        eqL->addWidget(s);
    cl->addWidget(eq);

    m_compBox = new QGroupBox(tr("Compressor"));
    m_compBox->setCheckable(true);
    m_compBox->setToolTip(tr("Evens out loud and quiet parts, so a voice stays at one level"));
    auto* compL = new QVBoxLayout(m_compBox);
    m_compThreshold = new SliderRow(tr("Threshold (dBFS)"), -60, 0, 0.5, 1);
    m_compThreshold->setDefault(-20);
    m_compRatio = new SliderRow(tr("Ratio"), 1, 20, 0.1, 1);
    m_compRatio->setDefault(3);
    m_compMakeup = new SliderRow(tr("Make-up gain (dB)"), 0, 24, 0.5, 1);
    for (auto* s : {m_compThreshold, m_compRatio, m_compMakeup})
        compL->addWidget(s);
    cl->addWidget(m_compBox);

    auto* duck = new QGroupBox(tr("Ducking"));
    auto* duckL = new QVBoxLayout(duck);
    m_duck = new SliderRow(tr("Lower under other sound (dB)"), -30, 0, 0.5, 1);
    duckL->addWidget(m_duck);
    duckL->addWidget(hint(tr("For music: it gets quieter by itself while other clips (a voice) play.")));
    cl->addWidget(duck);

    auto* master = new QGroupBox(tr("Master (whole mix)"));
    auto* ml = new QVBoxLayout(master);
    m_normalize = new QCheckBox(tr("Normalise loudness on export"));
    m_normalize->setToolTip(tr("Measures the finished mix (EBU R128) and brings it to the target loudness. "
                               "The limiter is then always on, so nothing clips."));
    m_target = new QComboBox;
    m_target->addItem(tr("-14 LUFS: YouTube, Spotify, social media"), -14.0);
    m_target->addItem(tr("-16 LUFS: podcasts, Apple"), -16.0);
    m_target->addItem(tr("-23 LUFS: television (EBU R128)"), -23.0);
    m_limiter = new QCheckBox(tr("Limiter"));
    m_limiter->setToolTip(tr("Stops peaks from going above the ceiling, without audible clipping"));
    m_ceiling = new SliderRow(tr("Ceiling (dBFS)"), -6, 0, 0.1, 1);
    m_ceiling->setDefault(-1);
    m_measure = new QPushButton(tr("Measure loudness"));
    m_measured = hint(QString());
    m_measured->hide();
    for (QWidget* w : std::initializer_list<QWidget*>{m_normalize, m_target, m_limiter, m_ceiling, m_measure, m_measured})
        ml->addWidget(w);
    ml->addWidget(hint(tr("Sound changes never re-encode the picture: on export the video is still copied as it is.")));
    l->addWidget(master);
    l->addStretch();

    auto bind = [this](SliderRow* s, const QString& text, const QString& key,
                       const std::function<void(AudioEffects&, double&, double)>& set) {
        connect(s, &SliderRow::valueChanged, this, [this, text, key, set](double v) {
            editClip(text, key, [&](AudioEffects& fx, double& gain) { set(fx, gain, v); });
        });
    };
    bind(m_gain, tr("Volume"), QStringLiteral("gain"), [](AudioEffects&, double& g, double v) { g = v; });
    bind(m_lowCut, tr("Low cut"), QStringLiteral("lowCut"), [](AudioEffects& f, double&, double v) { f.lowCut = v; });
    bind(m_wind, tr("Wind reduction"), QStringLiteral("wind"), [](AudioEffects& f, double&, double v) { f.wind = v; });
    connect(m_hum, &QComboBox::activated, this, [this] {
        const int hz = m_hum->currentData().toInt();
        editClip(tr("Hum removal"), {}, [&](AudioEffects& f, double&) { f.humHz = hz; });
    });
    bind(m_denoise, tr("Noise reduction"), QStringLiteral("denoise"),
         [](AudioEffects& f, double&, double v) { f.denoise = v; });
    bind(m_eqLow, tr("Equaliser"), QStringLiteral("eqLow"), [](AudioEffects& f, double&, double v) { f.eqLow = v; });
    bind(m_eqMid, tr("Equaliser"), QStringLiteral("eqMid"), [](AudioEffects& f, double&, double v) { f.eqMid = v; });
    bind(m_eqMidFreq, tr("Equaliser"), QStringLiteral("eqMidFreq"),
         [](AudioEffects& f, double&, double v) { f.eqMidFreq = v; });
    bind(m_eqHigh, tr("Equaliser"), QStringLiteral("eqHigh"), [](AudioEffects& f, double&, double v) { f.eqHigh = v; });
    bind(m_compThreshold, tr("Compressor"), QStringLiteral("compThreshold"),
         [](AudioEffects& f, double&, double v) { f.compThreshold = v; });
    bind(m_compRatio, tr("Compressor"), QStringLiteral("compRatio"),
         [](AudioEffects& f, double&, double v) { f.compRatio = v; });
    bind(m_compMakeup, tr("Compressor"), QStringLiteral("compMakeup"),
         [](AudioEffects& f, double&, double v) { f.compMakeup = v; });
    bind(m_duck, tr("Ducking"), QStringLiteral("duck"), [](AudioEffects& f, double&, double v) { f.duckDb = v; });
    connect(m_compBox, &QGroupBox::toggled, this, [this](bool on) {
        editClip(on ? tr("Turn on compressor") : tr("Turn off compressor"), {},
                 [&](AudioEffects& f, double&) { f.compressor = on; });
    });
    connect(m_normalize, &QCheckBox::toggled, this, [this](bool on) {
        editMaster(tr("Loudness normalisation"), {}, [&](AudioMaster& m) { m.normalize = on; });
    });
    connect(m_target, &QComboBox::activated, this, [this] {
        editMaster(tr("Target loudness"), {}, [&](AudioMaster& m) { m.targetLufs = m_target->currentData().toDouble(); });
    });
    connect(m_limiter, &QCheckBox::toggled, this, [this](bool on) {
        editMaster(tr("Limiter"), {}, [&](AudioMaster& m) { m.limiter = on; });
    });
    connect(m_ceiling, &SliderRow::valueChanged, this, [this](double v) {
        editMaster(tr("Limiter ceiling"), QStringLiteral("ceiling"), [&](AudioMaster& m) { m.ceilingDb = v; });
    });
    connect(m_measure, &QPushButton::clicked, this, &AudioPanel::measureLoudness);
    connect(m_project->timeline(), &Timeline::changed, this, [this] {
        if (!m_updating)
            refresh();
        m_measured->hide(); // the mix changed
    });
    refresh();
}

AudioPanel::~AudioPanel() {
    if (m_cancelMeasure)
        *m_cancelMeasure = true;
}

void AudioPanel::showClip(const QUuid& clipId) {
    if (clipId == m_clip)
        return;
    m_clip = clipId;
    ++m_session;
    refresh();
}

QUuid AudioPanel::audioClip() const {
    const Timeline* tl = m_project->timeline();
    if (m_clip.isNull() || !tl->clip(m_clip))
        return {};
    for (const QUuid& id : tl->linkedClips(m_clip)) {
        const auto loc = tl->locate(id);
        if (loc && tl->tracks()[loc->track].type == TrackType::Audio)
            return id;
    }
    return {};
}

void AudioPanel::editClip(const QString& text, const QString& field,
                          const std::function<void(AudioEffects&, double&)>& change) {
    if (m_syncing)
        return;
    const QUuid id = audioClip();
    const Clip* c = id.isNull() ? nullptr : m_project->timeline()->clip(id);
    if (!c)
        return;
    AudioEffects fx = c->audio;
    double gain = c->gainDb;
    change(fx, gain);
    if (fx == c->audio && gain == c->gainDb)
        return;
    m_updating = true;
    const QString key = field.isEmpty() ? QString() : QStringLiteral("%1:%2:audio:%3").arg(m_session).arg(id.toString(), field);
    Timeline* tl = m_project->timeline();
    runTimelineEdit(m_undo, tl, text, [&] {
        bool ok = false;
        if (fx != tl->clip(id)->audio)
            ok |= tl->setAudioEffects(id, fx);
        if (gain != tl->clip(id)->gainDb)
            ok |= tl->setGain(id, gain);
        return ok;
    }, key);
    m_updating = false;
}

void AudioPanel::editMaster(const QString& text, const QString& field, const std::function<void(AudioMaster&)>& change) {
    if (m_syncing)
        return;
    Timeline* tl = m_project->timeline();
    AudioMaster m = tl->state().audioMaster;
    change(m);
    if (m == tl->state().audioMaster)
        return;
    m_updating = true;
    runTimelineEdit(m_undo, tl, text, [&] { return tl->setAudioMaster(m); },
                    field.isEmpty() ? QString() : QStringLiteral("master:") + field);
    m_updating = false;
    refresh();
}

void AudioPanel::refresh() {
    m_syncing = true;
    const Timeline* tl = m_project->timeline();
    const QUuid id = audioClip();
    const Clip* c = id.isNull() ? nullptr : tl->clip(id);
    m_clipBody->setEnabled(c);
    if (!c) {
        m_header->setText(m_clip.isNull() ? tr("Select a clip with sound in the timeline.")
                                          : tr("This clip has no sound."));
        m_gain->setValue(0);
        m_hum->setCurrentIndex(0);
        for (auto* s : {m_lowCut, m_wind, m_denoise, m_eqLow, m_eqMid, m_eqHigh, m_compMakeup, m_duck})
            s->setValue(0);
        m_eqMidFreq->setValue(1000);
        m_compThreshold->setValue(-20);
        m_compRatio->setValue(3);
        m_compBox->setChecked(false);
    } else {
        QString name;
        if (auto m = m_project->media()->find(c->mediaId))
            name = QFileInfo(m->path).fileName();
        m_header->setText(name);
        const AudioEffects& f = c->audio;
        m_gain->setValue(c->gainDb);
        m_lowCut->setValue(f.lowCut);
        m_hum->setCurrentIndex(std::max(0, m_hum->findData(f.humHz)));
        m_wind->setValue(f.wind);
        m_denoise->setValue(f.denoise);
        m_eqLow->setValue(f.eqLow);
        m_eqMid->setValue(f.eqMid);
        m_eqMidFreq->setValue(f.eqMidFreq);
        m_eqHigh->setValue(f.eqHigh);
        m_compBox->setChecked(f.compressor);
        m_compThreshold->setValue(f.compThreshold);
        m_compRatio->setValue(f.compRatio);
        m_compMakeup->setValue(f.compMakeup);
        m_duck->setValue(f.duckDb);
    }
    const AudioMaster& m = tl->state().audioMaster;
    m_normalize->setChecked(m.normalize);
    int ti = m_target->findData(m.targetLufs);
    if (ti < 0) {
        m_target->addItem(tr("%1 LUFS").arg(m.targetLufs, 0, 'f', 1), m.targetLufs);
        ti = m_target->count() - 1;
    }
    m_target->setCurrentIndex(ti);
    m_target->setEnabled(m.normalize);
    m_limiter->setChecked(m.limiterActive());
    m_limiter->setEnabled(!m.normalize); // normalising always limits
    m_ceiling->setValue(m.ceilingDb);
    m_ceiling->setEnabled(m.limiterActive());
    m_syncing = false;
}

void AudioPanel::measureLoudness() {
    if (m_cancelMeasure) {
        *m_cancelMeasure = true; // a second click cancels
        return;
    }
    const TimelineState state = m_project->timeline()->state();
    const Ticks end = m_project->timeline()->duration();
    if (end <= 0)
        return;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancelMeasure = cancel;
    m_measure->setText(tr("Measuring… (click to stop)"));
    QPointer<AudioPanel> self(this);
    const MediaPool* pool = m_project->media();
    (void)QtConcurrent::run([self, state, end, pool, cancel] {
        const LoudnessResult r = loudness::measureTimeline(state, pool, 0, end, {}, cancel.get());
        QMetaObject::invokeMethod(qApp, [self, r, cancel] {
            if (!self || self->m_cancelMeasure != cancel)
                return;
            self->m_cancelMeasure.reset();
            self->m_measure->setText(tr("Measure loudness"));
            if (!r.ok)
                return;
            const AudioMaster& m = self->m_project->timeline()->state().audioMaster;
            QString text;
            if (r.integrated <= loudness::kSilent)
                text = tr("The mix is silent.");
            else {
                text = tr("Loudness %1 LUFS, peak %2 dBFS.")
                           .arg(r.integrated, 0, 'f', 1)
                           .arg(r.peakDb, 0, 'f', 1);
                const double gain = loudness::normalizationGain(r.integrated, m.targetLufs);
                text += QLatin1Char(' ') +
                        tr("Normalising to %1 LUFS changes it by %2 dB.")
                            .arg(m.targetLufs, 0, 'f', 0)
                            .arg(QStringLiteral("%1%2").arg(gain >= 0 ? QStringLiteral("+") : QString()).arg(gain, 0, 'f', 1));
            }
            self->m_measured->setText(text);
            self->m_measured->show();
        });
    });
}

} // namespace lumora

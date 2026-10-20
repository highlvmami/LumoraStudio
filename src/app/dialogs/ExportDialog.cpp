#include "ExportDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QSpinBox>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>

#include "export/Encoders.h"
#include "export/ExportPlanner.h"
#include "export/TimelineExporter.h"
#include "project/Project.h"
#include "undo/TimelineCommands.h"

namespace lumora {

ExportDialog::ExportDialog(Project* project, QUndoStack* undo, QWidget* parent)
    : QDialog(parent), m_project(project), m_undo(undo) {
    setWindowTitle(tr("Export video"));
    setMinimumWidth(620);
    auto* layout = new QVBoxLayout(this);

    auto* modeBox = new QGroupBox(tr("Quality"));
    auto* modeLayout = new QVBoxLayout(modeBox);
    m_modes = new QButtonGroup(this);
    struct ModeText {
        ExportMode mode;
        QString title;
        QString help;
    };
    const ModeText modes[] = {
        {ExportMode::StreamCopy, tr("Lossless cut (no re-encoding) — recommended"),
         tr("Video and audio are copied bit for bit. Quality is exactly the source's and export takes seconds. "
            "Cuts start on key frames.")},
        {ExportMode::MatchSource, tr("Same as source (re-encode)"),
         tr("Frame-exact cuts. Codec, resolution, frame rate, bit depth, colour and bit rate are taken from the "
            "source. Audio is copied when possible.")},
        {ExportMode::LosslessIntermediate, tr("Lossless intermediate (FFV1 / MKV)"),
         tr("Frame-exact and mathematically lossless. Very large files; ideal for archiving or further editing.")},
        {ExportMode::Custom, tr("Custom: codec, size and quality of your choice"),
         tr("Ready-made settings for YouTube, Instagram, TikTok, ProRes and more, or choose everything yourself, "
            "including your graphics card's encoder.")},
    };
    for (const auto& m : modes) {
        auto* rb = new QRadioButton(m.title);
        auto* help = new QLabel(m.help);
        help->setWordWrap(true);
        help->setContentsMargins(24, 0, 0, 6);
        help->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        rb->setToolTip(m.help);
        m_modes->addButton(rb, static_cast<int>(m.mode));
        m_modeHelp.insert(static_cast<int>(m.mode), help);
        modeLayout->addWidget(rb);
        modeLayout->addWidget(help);
    }
    // Plain cuts default to the lossless cut; timelines with titles,
    // transitions or several tracks have to be rendered.
    const bool effects = project->timeline()->state().hasEffects();
    m_modes->button(static_cast<int>(effects ? ExportMode::MatchSource : ExportMode::StreamCopy))->setChecked(true);
    layout->addWidget(modeBox);

    m_customBox = new QGroupBox(tr("Custom settings"));
    buildCustomPanel(m_customBox);
    layout->addWidget(m_customBox);

    auto* form = new QFormLayout;
    auto* pathRow = new QHBoxLayout;
    m_path = new QLineEdit;
    auto* browseBtn = new QPushButton(tr("Browse…"));
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseBtn);
    form->addRow(tr("File:"), pathRow);
    m_audio = new QCheckBox(tr("Include audio"));
    m_audio->setChecked(true);
    form->addRow(QString(), m_audio);
    layout->addLayout(form);

    auto* planBox = new QGroupBox(tr("What will happen"));
    auto* planLayout = new QVBoxLayout(planBox);
    m_planLabel = new QLabel;
    m_planLabel->setWordWrap(true);
    m_planLabel->setTextFormat(Qt::RichText);
    planLayout->addWidget(m_planLabel);
    m_snapBtn = new QPushButton(tr("Snap cuts to key frames"));
    m_snapBtn->setToolTip(tr("Moves each clip's start back to the previous key frame on the timeline, so the "
                             "timeline shows exactly what the lossless export contains. Can be undone."));
    planLayout->addWidget(m_snapBtn, 0, Qt::AlignLeft);
    layout->addWidget(planBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_exportBtn = buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    m_exportBtn->setDefault(true);
    layout->addWidget(buttons);

    // Default output name next to the first source.
    const auto& clips = project->timeline()->tracks()[0].clips;
    QString base = QDir::homePath() + QStringLiteral("/LumoraStudio");
    QString srcPath;
    if (!clips.empty())
        if (auto m = project->media()->find(clips.front().mediaId))
            srcPath = m->path;
    if (!srcPath.isEmpty()) {
        const QFileInfo fi(srcPath);
        base = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("_edit");
    }
    m_path->setText(QDir::toNativeSeparators(base + QLatin1Char('.') +
                                             ExportPlanner::suggestedExtension(ExportMode::StreamCopy, srcPath)));
    const int youtube = m_preset->findData(QStringLiteral("youtube-1080"));
    if (youtube > 0)
        applyPreset(youtube);
    updateExtension();

    connect(m_modes, &QButtonGroup::idClicked, this, [this] {
        updateCustomVisibility();
        updateExtension();
        replan();
    });
    connect(m_audio, &QCheckBox::toggled, this, &ExportDialog::replan);
    connect(m_path, &QLineEdit::editingFinished, this, &ExportDialog::replan);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(m_snapBtn, &QPushButton::clicked, this, &ExportDialog::snapCuts);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::startExport);
    updateCustomVisibility();
    replan();
}

QString ExportDialog::sourcePath() const {
    for (const auto& t : m_project->timeline()->tracks())
        for (const auto& c : t.clips)
            if (auto m = m_project->media()->find(c.mediaId); m && m->info.video)
                return m->path;
    return {};
}

void ExportDialog::buildCustomPanel(QGroupBox* box) {
    auto* form = new QFormLayout(box);
    m_customForm = form;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_preset = new QComboBox;
    m_preset->addItem(tr("Custom"), QString());
    for (const auto& p : encoders::presets())
        m_preset->addItem(p.label, p.id);
    form->addRow(tr("Preset"), m_preset);

    m_codec = new QComboBox;
    for (const auto& c : encoders::videoCodecs())
        if (!encoders::available(c.codec).empty())
            m_codec->addItem(c.label, static_cast<int>(c.codec));
    m_encoder = new QComboBox;
    auto* codecRow = new QHBoxLayout;
    codecRow->addWidget(m_codec, 1);
    codecRow->addWidget(m_encoder, 1);
    form->addRow(tr("Video codec"), codecRow);
    m_profile = new QComboBox;
    form->addRow(tr("Profile"), m_profile);

    m_rateControl = new QComboBox;
    form->addRow(tr("Quality mode"), m_rateControl);
    auto* qualityRow = new QWidget;
    auto* ql = new QHBoxLayout(qualityRow);
    ql->setContentsMargins(0, 0, 0, 0);
    m_quality = new QSlider(Qt::Horizontal);
    m_qualityLabel = new QLabel;
    m_qualityLabel->setMinimumWidth(150);
    ql->addWidget(m_quality, 1);
    ql->addWidget(m_qualityLabel);
    m_qualityRowWidget = qualityRow;
    form->addRow(tr("Quality"), qualityRow);
    m_bitrate = new QDoubleSpinBox;
    m_bitrate->setRange(0.1, 1000);
    m_bitrate->setDecimals(1);
    m_bitrate->setSuffix(QStringLiteral(" Mbit/s"));
    m_bitrate->setValue(12);
    form->addRow(tr("Bit rate"), m_bitrate);
    m_maxBitrate = new QDoubleSpinBox;
    m_maxBitrate->setRange(0, 2000);
    m_maxBitrate->setDecimals(1);
    m_maxBitrate->setSuffix(QStringLiteral(" Mbit/s"));
    m_maxBitrate->setSpecialValueText(tr("automatic"));
    form->addRow(tr("Peak bit rate"), m_maxBitrate);
    m_targetSize = new QDoubleSpinBox;
    m_targetSize->setRange(1, 1000000);
    m_targetSize->setDecimals(0);
    m_targetSize->setSuffix(QStringLiteral(" MB"));
    m_targetSize->setValue(100);
    form->addRow(tr("File size"), m_targetSize);
    m_twoPass = new QCheckBox(tr("Two passes (better quality for the same size, takes longer)"));
    form->addRow(QString(), m_twoPass);

    m_resolution = new QComboBox;
    m_resolution->addItem(tr("Same as timeline"), QSize());
    const std::pair<QSize, QString> sizes[] = {
        {{3840, 2160}, QStringLiteral("3840 × 2160 (4K UHD)")}, {{2560, 1440}, QStringLiteral("2560 × 1440 (QHD)")},
        {{1920, 1080}, QStringLiteral("1920 × 1080 (Full HD)")}, {{1280, 720}, QStringLiteral("1280 × 720 (HD)")},
        {{1080, 1920}, tr("1080 × 1920 (vertical 9:16)")},      {{1080, 1350}, tr("1080 × 1350 (portrait 4:5)")},
        {{1080, 1080}, tr("1080 × 1080 (square)")}};
    for (const auto& [sz, label] : sizes)
        m_resolution->addItem(label, sz);
    m_resolution->addItem(tr("Other…"), QSize(-1, -1));
    m_scaleMode = new QComboBox;
    m_scaleMode->addItem(tr("Fit, black bars"), static_cast<int>(ScaleMode::Fit));
    m_scaleMode->addItem(tr("Fit inside, no bars"), static_cast<int>(ScaleMode::Inside));
    auto* resRow = new QHBoxLayout;
    resRow->addWidget(m_resolution, 2);
    resRow->addWidget(m_scaleMode, 1);
    form->addRow(tr("Frame size"), resRow);
    auto* customSize = new QWidget;
    auto* cs = new QHBoxLayout(customSize);
    cs->setContentsMargins(0, 0, 0, 0);
    m_width = new QSpinBox;
    m_height = new QSpinBox;
    for (QSpinBox* sb : {m_width, m_height}) {
        sb->setRange(16, 8192);
        sb->setSingleStep(2);
    }
    m_width->setValue(1920);
    m_height->setValue(1080);
    cs->addWidget(m_width);
    cs->addWidget(new QLabel(QStringLiteral("×")));
    cs->addWidget(m_height);
    cs->addStretch(1);
    m_sizeRowWidget = customSize;
    form->addRow(QString(), customSize);

    m_frameRate = new QComboBox;
    m_frameRate->addItem(tr("Same as timeline"), QSize(0, 1));
    const std::pair<QSize, QString> rates[] = {{{24000, 1001}, QStringLiteral("23.976")}, {{24, 1}, QStringLiteral("24")},
                                               {{25, 1}, QStringLiteral("25")},           {{30000, 1001}, QStringLiteral("29.97")},
                                               {{30, 1}, QStringLiteral("30")},           {{50, 1}, QStringLiteral("50")},
                                               {{60000, 1001}, QStringLiteral("59.94")}, {{60, 1}, QStringLiteral("60")}};
    for (const auto& [r, label] : rates)
        m_frameRate->addItem(label + QStringLiteral(" fps"), r); // QSize carries num/den
    m_bitDepth = new QComboBox;
    auto* rateRow = new QHBoxLayout;
    rateRow->addWidget(m_frameRate, 1);
    rateRow->addWidget(m_bitDepth, 1);
    form->addRow(tr("Frame rate / depth"), rateRow);

    m_speed = new QSlider(Qt::Horizontal);
    m_speed->setRange(0, 4);
    m_speed->setValue(2);
    m_speed->setPageStep(1);
    m_speed->setTickPosition(QSlider::TicksBelow);
    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(tr("Faster")));
    speedRow->addWidget(m_speed, 1);
    speedRow->addWidget(new QLabel(tr("Smaller file")));
    m_speedRow = speedRow;
    form->addRow(tr("Encoding speed"), speedRow);

    m_audioCodec = new QComboBox;
    m_audioCodec->addItem(tr("Copy when possible, else AAC"), static_cast<int>(AV_CODEC_ID_NONE));
    m_audioCodec->addItem(QStringLiteral("AAC"), static_cast<int>(AV_CODEC_ID_AAC));
    m_audioCodec->addItem(QStringLiteral("Opus"), static_cast<int>(AV_CODEC_ID_OPUS));
    m_audioCodec->addItem(tr("FLAC (lossless)"), static_cast<int>(AV_CODEC_ID_FLAC));
    m_audioCodec->addItem(tr("PCM 24-bit (uncompressed)"), static_cast<int>(AV_CODEC_ID_PCM_S24LE));
    m_audioBitrate = new QComboBox;
    for (int kbps : {128, 192, 256, 320})
        m_audioBitrate->addItem(QStringLiteral("%1 kbit/s").arg(kbps), kbps * 1000);
    m_audioBitrate->setCurrentIndex(3);
    auto* audioRow = new QHBoxLayout;
    audioRow->addWidget(m_audioCodec, 2);
    audioRow->addWidget(m_audioBitrate, 1);
    form->addRow(tr("Audio"), audioRow);

    auto changed = [this] {
        if (m_applyingPreset)
            return;
        m_preset->setCurrentIndex(0); // any change makes it a custom setting
        updateCustomVisibility();
        updateExtension();
        replan();
    };
    connect(m_preset, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!m_applyingPreset && i > 0)
            applyPreset(i);
    });
    connect(m_codec, &QComboBox::currentIndexChanged, this, [this, changed] {
        codecChanged();
        changed();
    });
    connect(m_encoder, &QComboBox::currentIndexChanged, this, [this, changed] {
        encoderChanged();
        changed();
    });
    for (QComboBox* c : {m_profile, m_rateControl, m_resolution, m_scaleMode, m_frameRate, m_bitDepth, m_audioCodec,
                         m_audioBitrate})
        connect(c, &QComboBox::currentIndexChanged, this, changed);
    for (QDoubleSpinBox* sb : {m_bitrate, m_maxBitrate, m_targetSize})
        connect(sb, &QDoubleSpinBox::valueChanged, this, changed);
    for (QSpinBox* sb : {m_width, m_height})
        connect(sb, &QSpinBox::valueChanged, this, changed);
    connect(m_twoPass, &QCheckBox::toggled, this, changed);
    connect(m_speed, &QSlider::valueChanged, this, changed);
    connect(m_quality, &QSlider::valueChanged, this, [this, changed] {
        updateQualityLabel();
        changed();
    });

    m_applyingPreset = true;
    codecChanged();
    m_applyingPreset = false;
}

void ExportDialog::updateQualityLabel() {
    const auto q = encoders::qualityScale(m_encoder->currentData().toString());
    const int v = m_quality->value();
    const bool lossless = q.lowerIsBetter ? v <= q.visuallyLossless : v >= q.visuallyLossless;
    const bool high = q.lowerIsBetter ? v <= q.high : v >= q.high;
    const QString words = lossless ? tr("visually lossless") : high ? tr("high") : tr("smaller file");
    m_qualityLabel->setText(QStringLiteral("%1 (%2)").arg(v).arg(words));
}

void ExportDialog::codecChanged() {
    const auto codec = static_cast<AVCodecID>(m_codec->currentData().toInt());
    const bool was = m_applyingPreset;
    m_applyingPreset = true;
    m_encoder->clear();
    for (const auto& e : encoders::available(codec))
        m_encoder->addItem(e.label, e.name);
    m_profile->clear();
    for (const auto& [id, label] : encoders::profiles(codec))
        m_profile->addItem(label, id);
    const int def = m_profile->findData(encoders::defaultProfile(codec));
    if (def >= 0)
        m_profile->setCurrentIndex(def);
    encoderChanged();
    m_applyingPreset = was;
}

void ExportDialog::encoderChanged() {
    const QString enc = m_encoder->currentData().toString();
    const auto codec = static_cast<AVCodecID>(m_codec->currentData().toInt());
    const bool was = m_applyingPreset;
    m_applyingPreset = true;
    const int rc = m_rateControl->currentData().isValid() ? m_rateControl->currentData().toInt() : 0;
    m_rateControl->clear();
    if (encoders::hasRateControl(codec)) {
        if (encoders::qualityScale(enc).supported)
            m_rateControl->addItem(tr("Constant quality (recommended)"), static_cast<int>(RateControl::Quality));
        m_rateControl->addItem(tr("Variable bit rate"), static_cast<int>(RateControl::VariableBitrate));
        if (encoders::supportsConstantBitrate(enc))
            m_rateControl->addItem(tr("Constant bit rate"), static_cast<int>(RateControl::ConstantBitrate));
        m_rateControl->addItem(tr("Target file size"), static_cast<int>(RateControl::TargetSize));
    }
    const int idx = m_rateControl->findData(rc);
    m_rateControl->setCurrentIndex(idx >= 0 ? idx : 0);
    const auto q = encoders::qualityScale(enc);
    m_quality->setRange(q.min, q.max);
    m_quality->setInvertedAppearance(q.lowerIsBetter); // right is always better
    m_quality->setValue(q.visuallyLossless);
    const int depth = m_bitDepth->currentData().isValid() ? m_bitDepth->currentData().toInt() : 0;
    m_bitDepth->clear();
    const auto depths = encoders::bitDepths(enc, codec, m_profile->currentData().toString());
    if (codec != AV_CODEC_ID_PRORES && codec != AV_CODEC_ID_DNXHD)
        m_bitDepth->addItem(tr("Same bit depth as source"), 0);
    for (int d : depths)
        m_bitDepth->addItem(QStringLiteral("%1-bit").arg(d), d);
    const int di = m_bitDepth->findData(depth);
    m_bitDepth->setCurrentIndex(di >= 0 ? di : 0);
    m_twoPass->setEnabled(encoders::supportsTwoPass(enc));
    m_applyingPreset = was;
    updateQualityLabel();
}

void ExportDialog::applyPreset(int index) {
    const QString id = m_preset->itemData(index).toString();
    for (const auto& p : encoders::presets()) {
        if (p.id != id)
            continue;
        m_applyingPreset = true;
        m_codec->setCurrentIndex(m_codec->findData(static_cast<int>(p.video.codec)));
        codecChanged();
        if (!p.video.profile.isEmpty())
            m_profile->setCurrentIndex(std::max(0, m_profile->findData(p.video.profile)));
        encoderChanged();
        m_rateControl->setCurrentIndex(std::max(0, m_rateControl->findData(static_cast<int>(p.video.rateControl))));
        if (p.video.quality >= 0)
            m_quality->setValue(p.video.quality);
        if (p.video.bitrate > 0)
            m_bitrate->setValue(p.video.bitrate / 1e6);
        m_maxBitrate->setValue(p.video.maxBitrate / 1e6);
        int res = m_resolution->findData(p.video.resolution);
        m_resolution->setCurrentIndex(res >= 0 ? res : 0);
        m_scaleMode->setCurrentIndex(std::max(0, m_scaleMode->findData(static_cast<int>(p.video.scaleMode))));
        const int fr = m_frameRate->findData(QSize(p.video.frameRate.num, p.video.frameRate.den));
        m_frameRate->setCurrentIndex(fr >= 0 ? fr : 0);
        const int bd = m_bitDepth->findData(p.video.bitDepth);
        m_bitDepth->setCurrentIndex(bd >= 0 ? bd : 0);
        m_speed->setValue(p.video.speed);
        const int ac = m_audioCodec->findData(static_cast<int>(p.audio.copyWhenPossible ? AV_CODEC_ID_NONE : p.audio.codec));
        m_audioCodec->setCurrentIndex(ac >= 0 ? ac : 0);
        if (p.audio.bitrate > 0) {
            const int ab = m_audioBitrate->findData(static_cast<int>(p.audio.bitrate));
            if (ab >= 0)
                m_audioBitrate->setCurrentIndex(ab);
        }
        m_preset->setCurrentIndex(index);
        m_applyingPreset = false;
        updateQualityLabel();
        // Presets name their container.
        QFileInfo fi(QDir::fromNativeSeparators(m_path->text()));
        m_path->setText(QDir::toNativeSeparators(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
                                                 QLatin1Char('.') + p.extension));
        updateCustomVisibility();
        replan();
        return;
    }
}

void ExportDialog::updateCustomVisibility() {
    if (!m_customBox)
        return;
    // Only the chosen mode keeps its explanation, so the dialog stays compact.
    for (auto it = m_modeHelp.cbegin(); it != m_modeHelp.cend(); ++it)
        it.value()->setVisible(it.key() == m_modes->checkedId());
    const bool custom = m_modes->checkedId() == static_cast<int>(ExportMode::Custom);
    m_customBox->setVisible(custom);
    const auto codec = static_cast<AVCodecID>(m_codec->currentData().toInt());
    const auto rc = static_cast<RateControl>(m_rateControl->currentData().toInt());
    const bool hasRc = encoders::hasRateControl(codec);
    m_customForm->setRowVisible(m_profile, m_profile->count() > 0);
    m_customForm->setRowVisible(m_rateControl, hasRc);
    m_customForm->setRowVisible(m_qualityRowWidget, hasRc && rc == RateControl::Quality);
    m_customForm->setRowVisible(m_bitrate, hasRc && (rc == RateControl::ConstantBitrate || rc == RateControl::VariableBitrate));
    m_customForm->setRowVisible(m_maxBitrate, hasRc && rc == RateControl::VariableBitrate);
    m_customForm->setRowVisible(m_targetSize, hasRc && rc == RateControl::TargetSize);
    m_customForm->setRowVisible(m_twoPass, hasRc && rc != RateControl::Quality);
    m_customForm->setRowVisible(m_speedRow, hasRc);
    m_customForm->setRowVisible(m_sizeRowWidget, m_resolution->currentData().toSize() == QSize(-1, -1));
    const int ac = m_audioCodec->currentData().toInt();
    m_audioBitrate->setEnabled(ac != AV_CODEC_ID_FLAC && ac != AV_CODEC_ID_PCM_S24LE);
    adjustSize();
}

ExportSettings ExportDialog::settings() const {
    ExportSettings s;
    s.mode = static_cast<ExportMode>(m_modes->checkedId());
    s.outputPath = QDir::fromNativeSeparators(m_path->text().trimmed());
    s.includeAudio = m_audio->isChecked();
    if (s.mode == ExportMode::Custom && m_customBox) {
        VideoEncodeOptions& v = s.video;
        v.codec = static_cast<AVCodecID>(m_codec->currentData().toInt());
        v.encoder = m_encoder->currentData().toString();
        v.profile = m_profile->currentData().toString();
        v.rateControl = static_cast<RateControl>(m_rateControl->currentData().toInt());
        v.quality = m_quality->value();
        v.bitrate = static_cast<std::int64_t>(m_bitrate->value() * 1e6);
        v.maxBitrate = static_cast<std::int64_t>(m_maxBitrate->value() * 1e6);
        v.targetSizeMB = m_targetSize->value();
        v.twoPass = m_twoPass->isEnabled() && m_twoPass->isChecked();
        QSize res = m_resolution->currentData().toSize();
        if (res == QSize(-1, -1))
            res = QSize(m_width->value(), m_height->value());
        v.resolution = res;
        v.scaleMode = static_cast<ScaleMode>(m_scaleMode->currentData().toInt());
        const QSize fr = m_frameRate->currentData().toSize();
        v.frameRate = AVRational{fr.width(), std::max(1, fr.height())};
        v.bitDepth = m_bitDepth->currentData().toInt();
        v.speed = m_speed->value();
        AudioEncodeOptions& a = s.audio;
        a.codec = static_cast<AVCodecID>(m_audioCodec->currentData().toInt());
        a.copyWhenPossible = a.codec == AV_CODEC_ID_NONE;
        a.bitrate = m_audioBitrate->isEnabled() ? m_audioBitrate->currentData().toInt() : 0;
        a.sampleRate = 0;
    }
    return s;
}

void ExportDialog::updateExtension() {
    const ExportSettings s = settings();
    QString ext = ExportPlanner::suggestedExtension(s.mode, sourcePath());
    if (s.mode == ExportMode::Custom) {
        switch (s.video.codec) {
        case AV_CODEC_ID_VP9: ext = QStringLiteral("webm"); break;
        case AV_CODEC_ID_PRORES:
        case AV_CODEC_ID_DNXHD: ext = QStringLiteral("mov"); break;
        case AV_CODEC_ID_FFV1: ext = QStringLiteral("mkv"); break;
        default: ext = QStringLiteral("mp4");
        }
        if (s.audio.codec == AV_CODEC_ID_PCM_S24LE && ext == QLatin1String("mp4"))
            ext = QStringLiteral("mov");
        if (s.audio.codec == AV_CODEC_ID_AAC && ext == QLatin1String("webm"))
            ext = QStringLiteral("mkv");
        // Keep a container the user picked if it can hold the streams.
        const QString current = QFileInfo(s.outputPath).suffix().toLower();
        if (current == QLatin1String("mkv") && s.video.codec != AV_CODEC_ID_PRORES)
            ext = current;
    }
    QFileInfo fi(settings().outputPath);
    m_path->setText(QDir::toNativeSeparators(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
                                             QLatin1Char('.') + ext));
}

void ExportDialog::browse() {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export video"), m_path->text(),
        tr("Video files (*.mp4 *.mov *.mkv *.webm *.avi *.mxf *.ts);;All files (*)"));
    if (path.isEmpty())
        return;
    m_path->setText(QDir::toNativeSeparators(path));
    replan();
}

void ExportDialog::replan() {
    m_plan = ExportPlanner::plan(m_project->timeline()->state(), *m_project->media(), settings());
    QString html;
    auto line = [&](const QString& color, const QString& text) {
        html += QStringLiteral("<p style='margin:2px 0'><span style='color:%1'>●</span> %2</p>").arg(color, text);
    };
    if (m_plan.video == VideoStrategy::Render && m_plan.blockers.isEmpty()) {
        const QString enc = m_plan.videoEncoder + (m_plan.hardwareEncoder ? tr(" (graphics card)") : QString());
        if (m_plan.mode == ExportMode::LosslessIntermediate)
            line(QStringLiteral("#3fb950"), tr("Video: rendered and encoded losslessly with %1").arg(enc));
        else if (m_plan.mode == ExportMode::MatchSource)
            line(QStringLiteral("#d29922"), tr("Video: rendered and encoded with %1 using the source settings").arg(enc));
        else
            line(QStringLiteral("#d29922"), tr("Video: rendered and encoded with %1").arg(enc));
        QString format = QStringLiteral("%1 × %2 · %3").arg(m_plan.outputSize.width()).arg(m_plan.outputSize.height())
                             .arg(formatFrameRate(m_plan.outputRate));
        if (m_plan.videoBitrate > 0 && m_plan.mode == ExportMode::Custom)
            format += QStringLiteral(" · %1 Mbit/s").arg(m_plan.videoBitrate / 1e6, 0, 'f', 1);
        if (m_plan.twoPass)
            format += tr(" · two passes");
        line(QStringLiteral("#8b8b95"), format);
        switch (m_plan.audio) {
        case AudioStrategy::Copy: line(QStringLiteral("#3fb950"), tr("Audio: copied unchanged")); break;
        case AudioStrategy::Encode:
            line(m_plan.audioEncoder == QLatin1String("flac") || m_plan.audioEncoder.startsWith(QLatin1String("pcm"))
                     ? QStringLiteral("#3fb950")
                     : QStringLiteral("#d29922"),
                 tr("Audio: mixed and encoded with %1").arg(m_plan.audioEncoder));
            break;
        case AudioStrategy::None: line(QStringLiteral("#8b8b95"), tr("Audio: none")); break;
        }
        line(QStringLiteral("#8b8b95"), tr("Container: %1 · about %2").arg(m_plan.containerName, formatSeconds(m_plan.expectedDuration)));
    } else if (!m_plan.segments.empty()) {
        if (m_plan.video == VideoStrategy::Copy)
            line(QStringLiteral("#3fb950"), tr("Video: copied unchanged (no quality loss)"));
        else if (m_plan.mode == ExportMode::LosslessIntermediate)
            line(QStringLiteral("#3fb950"), tr("Video: encoded losslessly with %1").arg(m_plan.videoEncoder));
        else if (!m_plan.videoEncoder.isEmpty())
            line(QStringLiteral("#d29922"), tr("Video: re-encoded with %1 using the source settings").arg(m_plan.videoEncoder));
        switch (m_plan.audio) {
        case AudioStrategy::Copy: line(QStringLiteral("#3fb950"), tr("Audio: copied unchanged")); break;
        case AudioStrategy::Encode:
            line(m_plan.audioEncoder == QLatin1String("flac") ? QStringLiteral("#3fb950") : QStringLiteral("#d29922"),
                 tr("Audio: encoded with %1").arg(m_plan.audioEncoder));
            break;
        case AudioStrategy::None: line(QStringLiteral("#8b8b95"), tr("Audio: none")); break;
        }
        line(QStringLiteral("#8b8b95"), tr("Container: %1 · %n clip(s) · about %2", nullptr,
                                           static_cast<int>(m_plan.segments.size()))
                                            .arg(m_plan.containerName, formatSeconds(m_plan.expectedDuration)));
    }
    for (const auto& n : m_plan.notes)
        line(QStringLiteral("#d29922"), n.toHtmlEscaped());
    for (const auto& b : m_plan.blockers)
        line(QStringLiteral("#f85149"), b.toHtmlEscaped());
    m_planLabel->setText(html);

    bool shifted = false;
    for (const auto& s : m_plan.segments)
        shifted |= s.keyframeShiftFrames > 0;
    m_snapBtn->setVisible(m_plan.mode == ExportMode::StreamCopy && m_plan.video == VideoStrategy::Copy && shifted);
    m_exportBtn->setEnabled(m_plan.ok());
}

void ExportDialog::snapCuts() {
    Timeline* tl = m_project->timeline();
    runTimelineEdit(m_undo, tl, tr("Snap cuts to key frames"), [&] {
        bool any = false;
        for (const auto& seg : m_plan.segments)
            if (seg.keyframeShiftFrames > 0)
                if (const Clip* c = tl->clip(seg.clipId); c && c->sourceIn == seg.requestedIn)
                    any |= tl->trimIn(seg.clipId, seg.actualIn);
        return any;
    });
    replan();
}

void ExportDialog::startExport() {
    const ExportSettings s = settings();
    if (QFileInfo::exists(s.outputPath)) {
        for (const auto& seg : m_plan.segments)
            if (QFileInfo(seg.path) == QFileInfo(s.outputPath)) {
                QMessageBox::warning(this, tr("Export video"), tr("The output file cannot be one of the source files."));
                return;
            }
        if (QMessageBox::question(this, tr("Export video"), tr("%1 already exists. Replace it?").arg(s.outputPath)) !=
            QMessageBox::Yes)
            return;
    }
    if (runExport(this, m_plan, s))
        accept();
}

bool ExportDialog::runExport(QWidget* parent, const ExportPlan& plan, const ExportSettings& settings) {
    QProgressDialog progress(tr("Exporting…"), tr("Cancel"), 0, 1000, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    std::atomic_bool cancel{false};
    std::atomic<int> value{0};

    QFutureWatcher<ExportResult> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<ExportResult>::finished, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, [&] { cancel = true; });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] { progress.setValue(value.load()); });
    timer.start(100);
    QElapsedTimer elapsed;
    elapsed.start();
    watcher.setFuture(QtConcurrent::run([&]() {
        TimelineExporter exporter;
        return exporter.run(plan, settings, [&](double f) { value = static_cast<int>(f * 1000); }, &cancel);
    }));
    loop.exec();
    timer.stop();
    progress.close();
    const ExportResult r = watcher.result();
    if (r.cancelled)
        return false;
    if (!r.ok) {
        QMessageBox::critical(parent, tr("Export failed"), r.error);
        return false;
    }
    QMessageBox box(QMessageBox::Information, tr("Export finished"),
                    tr("Saved %1 (%2) in %3 s.")
                        .arg(QFileInfo(settings.outputPath).fileName(), QLocale().formattedDataSize(r.fileSize))
                        .arg(elapsed.elapsed() / 1000.0, 0, 'f', 1),
                    QMessageBox::Ok, parent);
    auto* open = box.addButton(tr("Show in folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(settings.outputPath).absolutePath()));
    return true;
}

} // namespace lumora

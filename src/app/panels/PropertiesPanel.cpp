#include "PropertiesPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>

#include "Theme.h"
#include "ai/Vision.h"
#include "export/ExportPlanner.h"
#include "motion/Analysis.h"
#include "project/Project.h"
#include "timeline/Subtitles.h"
#include "undo/TimelineCommands.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace lumora {

namespace {

QString g_dim = QStringLiteral("#8b8b95");

QString row(const QString& k, const QString& v) {
    return QStringLiteral("<tr><td style='color:%3;padding-right:10px'>%1</td><td>%2</td></tr>")
        .arg(k.toHtmlEscaped(), v, g_dim);
}

QString section(const QString& title) {
    return QStringLiteral("<tr><td colspan=2 style='padding-top:10px'><b>%1</b></td></tr>").arg(title.toHtmlEscaped());
}

QString bitrate(std::int64_t bps) {
    if (bps <= 0)
        return QStringLiteral("—");
    if (bps >= 1'000'000)
        return QStringLiteral("%1 Mbit/s").arg(bps / 1e6, 0, 'f', 1);
    return QStringLiteral("%1 kbit/s").arg(bps / 1000);
}

QString nameOr(const char* s) { return s ? QString::fromUtf8(s) : QStringLiteral("—"); }

} // namespace

PropertiesPanel::PropertiesPanel(Project* project, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), m_project(project), m_undo(undo) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);

    m_header = new QLabel;
    m_header->setTextFormat(Qt::RichText);
    layout->addWidget(m_header);

    auto dspin = [](double min, double max, double step, int decimals, const QString& suffix) {
        auto* sb = new QDoubleSpinBox;
        sb->setRange(min, max);
        sb->setSingleStep(step);
        sb->setDecimals(decimals);
        sb->setSuffix(suffix);
        sb->setKeyboardTracking(false);
        sb->setAccelerated(true);
        return sb;
    };
    const QString sec = QStringLiteral(" s");

    // --- title ---
    QFormLayout* f = nullptr;
    m_titleSection = makeSection(tr("Title"), &f);
    m_titleText = new QPlainTextEdit;
    m_titleText->setFixedHeight(64);
    m_titleText->setPlaceholderText(tr("Type the title text"));
    f->addRow(m_titleText);
    m_titleFont = new QFontComboBox;
    f->addRow(tr("Font"), m_titleFont);
    auto* styleRow = new QHBoxLayout;
    m_titleSize = dspin(1, 50, 0.5, 1, QStringLiteral(" %"));
    m_titleSize->setToolTip(tr("Text height as a percentage of the frame height"));
    m_titleBold = new QToolButton;
    m_titleBold->setText(QStringLiteral("B"));
    m_titleBold->setCheckable(true);
    QFont bf = m_titleBold->font();
    bf.setBold(true);
    m_titleBold->setFont(bf);
    m_titleItalic = new QToolButton;
    m_titleItalic->setText(QStringLiteral("I"));
    m_titleItalic->setCheckable(true);
    QFont itf = m_titleItalic->font();
    itf.setItalic(true);
    m_titleItalic->setFont(itf);
    m_titleColor = makeColorButton();
    m_titleColor->setToolTip(tr("Text colour"));
    styleRow->addWidget(m_titleSize, 1);
    styleRow->addWidget(m_titleBold);
    styleRow->addWidget(m_titleItalic);
    styleRow->addWidget(m_titleColor);
    f->addRow(tr("Size"), styleRow);
    m_titleAlign = new QComboBox;
    m_titleAlign->addItems({tr("Left"), tr("Centre"), tr("Right")});
    f->addRow(tr("Alignment"), m_titleAlign);
    auto* posRow = new QHBoxLayout;
    m_titleX = dspin(0, 100, 1, 1, QStringLiteral(" %"));
    m_titleY = dspin(0, 100, 1, 1, QStringLiteral(" %"));
    m_titleX->setPrefix(QStringLiteral("X "));
    m_titleY->setPrefix(QStringLiteral("Y "));
    posRow->addWidget(m_titleX);
    posRow->addWidget(m_titleY);
    f->addRow(tr("Position"), posRow);
    auto* outlineRow = new QHBoxLayout;
    m_titleOutline = dspin(0, 30, 1, 0, QStringLiteral(" %"));
    m_titleOutline->setToolTip(tr("Outline width as a percentage of the text size"));
    m_titleOutlineColor = makeColorButton();
    outlineRow->addWidget(m_titleOutline, 1);
    outlineRow->addWidget(m_titleOutlineColor);
    f->addRow(tr("Outline"), outlineRow);
    auto* backRow = new QHBoxLayout;
    m_titleBackOpacity = new QSpinBox;
    m_titleBackOpacity->setRange(0, 100);
    m_titleBackOpacity->setSuffix(QStringLiteral(" %"));
    m_titleBackOpacity->setKeyboardTracking(false);
    m_titleBackColor = makeColorButton();
    backRow->addWidget(m_titleBackOpacity, 1);
    backRow->addWidget(m_titleBackColor);
    f->addRow(tr("Background"), backRow);
    m_titleShadow = new QCheckBox(tr("Soft shadow"));
    f->addRow(QString(), m_titleShadow);
    m_titleAnimIn = new QComboBox;
    m_titleAnimOut = new QComboBox;
    for (QComboBox* c : {m_titleAnimIn, m_titleAnimOut}) {
        c->addItem(tr("None"), static_cast<int>(TitleAnimation::None));
        c->addItem(tr("Fade"), static_cast<int>(TitleAnimation::Fade));
        c->addItem(tr("Slide up"), static_cast<int>(TitleAnimation::SlideUp));
        c->addItem(tr("Pop"), static_cast<int>(TitleAnimation::Pop));
        c->addItem(tr("Typewriter"), static_cast<int>(TitleAnimation::Typewriter));
    }
    f->addRow(tr("Animation in"), m_titleAnimIn);
    f->addRow(tr("Animation out"), m_titleAnimOut);
    m_titleAnimDuration = dspin(0, 10, 0.1, 2, sec);
    f->addRow(tr("Animation length"), m_titleAnimDuration);
    m_titleDuration = dspin(0.04, 3600, 0.5, 2, sec);
    f->addRow(tr("Duration"), m_titleDuration);
    layout->addWidget(m_titleSection);

    // --- freeze frame ---
    m_freezeSection = makeSection(tr("Freeze frame"), &f);
    m_freezeDuration = dspin(0.04, 3600, 0.5, 2, sec);
    f->addRow(tr("Hold for"), m_freezeDuration);
    m_freezeInfo = new QLabel;
    m_freezeInfo->setWordWrap(true);
    m_freezeInfo->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    f->addRow(m_freezeInfo);
    layout->addWidget(m_freezeSection);

    // --- picture (stills and stickers) ---
    m_stillSection = makeSection(tr("Picture"), &f);
    m_stillDuration = dspin(0.04, 3600, 0.5, 2, sec);
    f->addRow(tr("Duration"), m_stillDuration);
    layout->addWidget(m_stillSection);

    // --- repeat ---
    m_loopSection = makeSection(tr("Repeat"), &f);
    m_loop = new QCheckBox(tr("Play over and over"));
    m_loop->setToolTip(tr("Repeat the clip, e.g. an animated sticker; drag its end on the timeline to make it longer"));
    f->addRow(m_loop);
    m_loopInfo = new QLabel(tr("Drag the end of the clip on the timeline to make it as long as you like. The sound is not repeated."));
    m_loopInfo->setWordWrap(true);
    m_loopInfo->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    f->addRow(m_loopInfo);
    layout->addWidget(m_loopSection);

    // --- subtitle ---
    m_subSection = makeSection(tr("Subtitle"), &f);
    m_subText = new QPlainTextEdit;
    m_subText->setFixedHeight(56);
    m_subText->setPlaceholderText(tr("Type what is said; two lines at most read best"));
    f->addRow(m_subText);
    m_subDuration = dspin(0.04, 600, 0.1, 2, sec);
    f->addRow(tr("Duration"), m_subDuration);
    layout->addWidget(m_subSection);

    m_subTrackSection = makeSection(tr("Subtitle track"), &f);
    m_subLanguage = new QLineEdit;
    m_subLanguage->setPlaceholderText(tr("e.g. tur, eng"));
    m_subLanguage->setToolTip(tr("Language of the track (ISO 639-2 code or a name); players show it in their subtitle menu"));
    f->addRow(tr("Language"), m_subLanguage);
    m_subFont = new QFontComboBox;
    f->addRow(tr("Font"), m_subFont);
    auto* subStyleRow = new QHBoxLayout;
    m_subSize = dspin(1, 20, 0.5, 1, QStringLiteral(" %"));
    m_subSize->setToolTip(tr("Text height as a percentage of the frame height"));
    m_subBold = new QToolButton;
    m_subBold->setText(QStringLiteral("B"));
    m_subBold->setCheckable(true);
    m_subBold->setFont(bf);
    m_subColor = makeColorButton();
    m_subColor->setToolTip(tr("Text colour"));
    subStyleRow->addWidget(m_subSize, 1);
    subStyleRow->addWidget(m_subBold);
    subStyleRow->addWidget(m_subColor);
    f->addRow(tr("Size"), subStyleRow);
    auto* subOutlineRow = new QHBoxLayout;
    m_subOutline = dspin(0, 30, 1, 0, QStringLiteral(" %"));
    m_subOutlineColor = makeColorButton();
    subOutlineRow->addWidget(m_subOutline, 1);
    subOutlineRow->addWidget(m_subOutlineColor);
    f->addRow(tr("Outline"), subOutlineRow);
    m_subBackOpacity = new QSpinBox;
    m_subBackOpacity->setRange(0, 100);
    m_subBackOpacity->setSuffix(QStringLiteral(" %"));
    m_subBackOpacity->setKeyboardTracking(false);
    m_subBackOpacity->setToolTip(tr("A dark box behind the text"));
    f->addRow(tr("Background"), m_subBackOpacity);
    m_subY = dspin(5, 100, 1, 0, QStringLiteral(" %"));
    m_subY->setToolTip(tr("Where the bottom line sits, from the top of the frame"));
    f->addRow(tr("Height in frame"), m_subY);
    auto* subNote = new QLabel(tr("The style is used when subtitles are burnt into the picture and in the preview. "
                                  "Embedded subtitles are styled by the player."));
    subNote->setWordWrap(true);
    subNote->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    f->addRow(subNote);
    layout->addWidget(m_subTrackSection);

    // --- transform ---
    m_transformSection = makeSection(tr("Position and size"), &f);
    auto* xy = new QHBoxLayout;
    m_posX = dspin(-200, 200, 1, 1, QStringLiteral(" %"));
    m_posY = dspin(-200, 200, 1, 1, QStringLiteral(" %"));
    m_posX->setPrefix(QStringLiteral("X "));
    m_posY->setPrefix(QStringLiteral("Y "));
    xy->addWidget(m_posX);
    xy->addWidget(m_posY);
    f->addRow(tr("Position"), xy);
    m_scale = dspin(1, 1000, 1, 1, QStringLiteral(" %"));
    f->addRow(tr("Scale"), m_scale);
    m_rotation = dspin(-360, 360, 1, 1, QStringLiteral("°"));
    f->addRow(tr("Rotation"), m_rotation);
    m_opacity = new QSpinBox;
    m_opacity->setRange(0, 100);
    m_opacity->setSuffix(QStringLiteral(" %"));
    m_opacity->setKeyboardTracking(false);
    f->addRow(tr("Opacity"), m_opacity);
    auto* resetTransform = new QPushButton(tr("Reset"));
    f->addRow(QString(), resetTransform);
    layout->addWidget(m_transformSection);

    // --- transition ---
    m_transitionSection = makeSection(tr("Transition from the previous clip"), &f);
    m_transitionType = new QComboBox;
    const std::pair<TransitionType, QString> types[] = {
        {TransitionType::None, tr("None (cut)")},          {TransitionType::CrossDissolve, tr("Cross dissolve")},
        {TransitionType::DipToBlack, tr("Dip to black")},  {TransitionType::DipToWhite, tr("Dip to white")},
        {TransitionType::WipeLeft, tr("Wipe left")},       {TransitionType::WipeRight, tr("Wipe right")},
        {TransitionType::WipeUp, tr("Wipe up")},           {TransitionType::WipeDown, tr("Wipe down")},
        {TransitionType::SlideLeft, tr("Push left")},      {TransitionType::SlideRight, tr("Push right")},
        {TransitionType::Zoom, tr("Zoom")},                {TransitionType::Blur, tr("Blur")}};
    for (const auto& [t, label] : types)
        m_transitionType->addItem(label, static_cast<int>(t));
    f->addRow(tr("Type"), m_transitionType);
    m_transitionDuration = dspin(0.04, 10, 0.1, 2, sec);
    f->addRow(tr("Duration"), m_transitionDuration);
    m_transitionNote = new QLabel;
    m_transitionNote->setWordWrap(true);
    f->addRow(m_transitionNote);
    layout->addWidget(m_transitionSection);

    // --- stabilisation ---
    m_stabSection = makeSection(tr("Stabilisation"), &f);
    m_stabOn = new QCheckBox(tr("Steady the shaky camera"));
    f->addRow(m_stabOn);
    m_stabSmoothing = dspin(0.2, 5, 0.1, 1, sec);
    m_stabSmoothing->setToolTip(tr("How much camera movement is smoothed away; larger values also remove slow wobble"));
    f->addRow(tr("Smoothing"), m_stabSmoothing);
    m_stabZoom = new QSpinBox;
    m_stabZoom->setRange(100, 150);
    m_stabZoom->setSuffix(QStringLiteral(" %"));
    m_stabZoom->setToolTip(tr("Enlarges the picture so its moving edges stay outside the frame"));
    f->addRow(tr("Zoom"), m_stabZoom);
    m_stabInfo = new QLabel;
    m_stabInfo->setWordWrap(true);
    m_stabInfo->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    f->addRow(m_stabInfo);
    layout->addWidget(m_stabSection);

    // --- motion tracking ---
    m_trackSection = makeSection(tr("Motion tracking"), &f);
    m_trackInfo = new QLabel;
    m_trackInfo->setWordWrap(true);
    m_trackInfo->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    f->addRow(m_trackInfo);
    m_trackStart = new QPushButton(tr("Follow an object…"));
    m_trackStart->setToolTip(tr("Draw a box around something in the picture; this clip then moves with it"));
    m_trackClear = new QPushButton(tr("Stop following"));
    auto* trackRow = new QHBoxLayout;
    trackRow->addWidget(m_trackStart);
    trackRow->addWidget(m_trackClear);
    f->addRow(trackRow);
    layout->addWidget(m_trackSection);

    // --- faces and number plates ---
    m_redactSection = makeSection(tr("Hide faces and plates"), &f);
    m_redactInfo = new QLabel;
    m_redactInfo->setWordWrap(true);
    m_redactInfo->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    f->addRow(m_redactInfo);
    m_redactFind = new QPushButton(tr("Find faces and plates"));
    m_redactFind->setToolTip(tr("Looks through the clip on this computer and follows every face and number plate"));
    m_redactClear = new QPushButton(tr("Remove"));
    auto* redactRow = new QHBoxLayout;
    redactRow->addWidget(m_redactFind, 1);
    redactRow->addWidget(m_redactClear);
    f->addRow(redactRow);
    m_redactFaces = new QCheckBox(tr("Faces"));
    m_redactPlates = new QCheckBox(tr("Number plates"));
    auto* kindRow = new QHBoxLayout;
    kindRow->addWidget(m_redactFaces);
    kindRow->addWidget(m_redactPlates);
    kindRow->addStretch();
    f->addRow(tr("Hide"), kindRow);
    m_redactStyle = new QComboBox;
    m_redactStyle->addItem(tr("Blur"), static_cast<int>(RedactionStyle::Blur));
    m_redactStyle->addItem(tr("Pixelate"), static_cast<int>(RedactionStyle::Pixelate));
    m_redactStyle->addItem(tr("Black box"), static_cast<int>(RedactionStyle::Fill));
    f->addRow(tr("Look"), m_redactStyle);
    m_redactStrength = new QSpinBox;
    m_redactStrength->setRange(0, 100);
    m_redactStrength->setSuffix(QStringLiteral(" %"));
    f->addRow(tr("Strength"), m_redactStrength);
    m_redactList = new QListWidget;
    m_redactList->setToolTip(tr("Untick what should stay visible"));
    f->addRow(m_redactList);
    layout->addWidget(m_redactSection);

    // --- background removal ---
    m_bgSection = makeSection(tr("Background"), &f);
    m_bgOn = new QCheckBox(tr("Remove the background"));
    m_bgOn->setToolTip(tr("Keeps the person or object in front and makes the rest transparent"));
    f->addRow(m_bgOn);
    m_bgModel = new QComboBox;
    m_bgModel->addItem(tr("Fast"), QStringLiteral("fast"));
    m_bgModel->addItem(tr("Detailed (slow)"), QStringLiteral("quality"));
    m_bgModel->setToolTip(tr("The detailed model finds hair and fine edges better; the preview uses the fast one"));
    f->addRow(tr("Model"), m_bgModel);
    m_bgSoftness = new QSpinBox;
    m_bgSoftness->setRange(0, 100);
    m_bgSoftness->setSuffix(QStringLiteral(" %"));
    f->addRow(tr("Edge softness"), m_bgSoftness);
    m_bgInfo = new QLabel;
    m_bgInfo->setWordWrap(true);
    m_bgInfo->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    f->addRow(m_bgInfo);
    layout->addWidget(m_bgSection);

    // --- fades ---
    m_fadeSection = makeSection(tr("Fades"), &f);
    m_fadeIn = dspin(0, 600, 0.1, 2, sec);
    m_fadeOut = dspin(0, 600, 0.1, 2, sec);
    f->addRow(tr("Fade in"), m_fadeIn);
    f->addRow(tr("Fade out"), m_fadeOut);
    layout->addWidget(m_fadeSection);

    // --- audio ---
    m_audioSection = makeSection(tr("Sound"), &f);
    m_gain = dspin(-60, 24, 0.5, 1, QStringLiteral(" dB"));
    f->addRow(tr("Volume"), m_gain);
    layout->addWidget(m_audioSection);

    m_info = new QLabel;
    m_info->setTextFormat(Qt::RichText);
    m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_info);
    layout->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);

    // Let the panel be narrow: combo boxes and spin boxes shrink with it.
    for (QComboBox* c : content->findChildren<QComboBox*>()) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(6);
    }
    for (QAbstractSpinBox* sb : content->findChildren<QAbstractSpinBox*>())
        sb->setMinimumWidth(60);
    // Paired X / Y fields: the arrows would leave no room for the value; the
    // wheel and arrow keys still step them.
    for (QDoubleSpinBox* sb : {m_titleX, m_titleY, m_posX, m_posY})
        sb->setButtonSymbols(QAbstractSpinBox::NoButtons);

    // --- wiring ---
    auto titleChanged = [this] { applyTitle(); };
    connect(m_titleText, &QPlainTextEdit::textChanged, this, titleChanged);
    connect(m_titleFont, &QFontComboBox::currentFontChanged, this, titleChanged);
    for (QDoubleSpinBox* sb : {m_titleSize, m_titleOutline, m_titleX, m_titleY, m_titleAnimDuration})
        connect(sb, &QDoubleSpinBox::valueChanged, this, titleChanged);
    connect(m_titleBackOpacity, &QSpinBox::valueChanged, this, titleChanged);
    for (QToolButton* b : {m_titleBold, m_titleItalic})
        connect(b, &QToolButton::toggled, this, titleChanged);
    connect(m_titleShadow, &QCheckBox::toggled, this, titleChanged);
    for (QComboBox* c : {m_titleAlign, m_titleAnimIn, m_titleAnimOut})
        connect(c, &QComboBox::currentIndexChanged, this, titleChanged);
    for (QToolButton* b : {m_titleColor, m_titleOutlineColor, m_titleBackColor})
        connect(b, &QToolButton::clicked, this, [this, b] {
            const QColor c = QColorDialog::getColor(b->property("color").value<QColor>(), this, tr("Choose a colour"));
            if (c.isValid()) {
                setButtonColor(b, c);
                applyTitle();
            }
        });
    connect(m_subText, &QPlainTextEdit::textChanged, this, [this] {
        edit(tr("Edit subtitle"), QStringLiteral("subtitle"),
             [&](const Clip& c) { return m_project->timeline()->setSubtitleText(c.id, m_subText->toPlainText()); });
    });
    connect(m_subDuration, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit(tr("Change subtitle length"), QStringLiteral("duration"),
             [&](const Clip& c) { return m_project->timeline()->setDuration(c.id, ticksFromSeconds(v)); });
    });
    connect(m_subLanguage, &QLineEdit::editingFinished, this, [this] {
        edit(tr("Subtitle language"), {}, [&](const Clip& c) {
            const auto loc = m_project->timeline()->locate(c.id);
            const QString text = m_subLanguage->text().trimmed();
            const QString code = text.isEmpty() ? QString() : subtitles::isoLanguage(text);
            return loc && m_project->timeline()->setTrackLanguage(loc->track, code.isEmpty() ? text : code);
        });
    });
    connect(m_subFont, &QFontComboBox::currentFontChanged, this, [this] { applySubtitleStyle(); });
    for (QDoubleSpinBox* sb : {m_subSize, m_subOutline, m_subY})
        connect(sb, &QDoubleSpinBox::valueChanged, this, [this] { applySubtitleStyle(); });
    connect(m_subBackOpacity, &QSpinBox::valueChanged, this, [this] { applySubtitleStyle(); });
    connect(m_subBold, &QToolButton::toggled, this, [this] { applySubtitleStyle(); });
    for (QToolButton* b : {m_subColor, m_subOutlineColor})
        connect(b, &QToolButton::clicked, this, [this, b] {
            const QColor c = QColorDialog::getColor(b->property("color").value<QColor>(), this, tr("Choose a colour"));
            if (c.isValid()) {
                setButtonColor(b, c);
                applySubtitleStyle();
            }
        });
    connect(m_stabOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || m_clip.isNull())
            return;
        const Clip* c = m_project->timeline()->clip(m_clip);
        if (on && c && c->stabilization.path.size() < 2) {
            emit stabilizeRequested(m_clip); // needs the camera path first
            refresh();
            return;
        }
        edit(on ? tr("Stabilise") : tr("Turn off stabilisation"), {}, [&](const Clip& c) {
            Stabilization s = c.stabilization;
            s.enabled = on;
            return m_project->timeline()->setStabilization(c.id, s);
        });
    });
    connect(m_stabSmoothing, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit(tr("Change smoothing"), QStringLiteral("stab-smoothing"), [&](const Clip& c) {
            Stabilization s = c.stabilization;
            s.smoothing = v;
            s.zoom = s.requiredZoom(motion::pictureAspect(*m_project->media(), c));
            return m_project->timeline()->setStabilization(c.id, s);
        });
    });
    connect(m_stabZoom, &QSpinBox::valueChanged, this, [this](int v) {
        edit(tr("Change stabilisation zoom"), QStringLiteral("stab-zoom"), [&](const Clip& c) {
            Stabilization s = c.stabilization;
            s.zoom = v / 100.0;
            return m_project->timeline()->setStabilization(c.id, s);
        });
    });
    connect(m_redactFind, &QPushButton::clicked, this, [this] {
        if (!m_clip.isNull())
            emit redactRequested(m_clip);
    });
    connect(m_redactClear, &QPushButton::clicked, this, [this] {
        edit(tr("Remove hidden regions"), {}, [&](const Clip& c) {
            Redaction r;
            r.style = c.redaction.style;
            r.strength = c.redaction.strength;
            return m_project->timeline()->setRedaction(c.id, r);
        });
    });
    for (QCheckBox* cb : {m_redactFaces, m_redactPlates})
        connect(cb, &QCheckBox::toggled, this, [this] {
            edit(tr("Hide faces and plates"), {}, [&](const Clip& c) {
                Redaction r = c.redaction;
                r.faces = m_redactFaces->isChecked();
                r.plates = m_redactPlates->isChecked();
                return m_project->timeline()->setRedaction(c.id, r);
            });
        });
    connect(m_redactStyle, &QComboBox::currentIndexChanged, this, [this] {
        edit(tr("Change how regions are hidden"), {}, [&](const Clip& c) {
            Redaction r = c.redaction;
            r.style = static_cast<RedactionStyle>(m_redactStyle->currentData().toInt());
            return m_project->timeline()->setRedaction(c.id, r);
        });
    });
    connect(m_redactStrength, &QSpinBox::valueChanged, this, [this](int v) {
        edit(tr("Change hiding strength"), QStringLiteral("redact-strength"), [&](const Clip& c) {
            Redaction r = c.redaction;
            r.strength = v / 100.0;
            return m_project->timeline()->setRedaction(c.id, r);
        });
    });
    connect(m_redactList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        const int i = item->data(Qt::UserRole).toInt();
        const bool on = item->checkState() == Qt::Checked;
        edit(on ? tr("Hide region") : tr("Show region"), {}, [&](const Clip& c) {
            Redaction r = c.redaction;
            if (i < 0 || i >= static_cast<int>(r.tracks.size()))
                return false;
            r.tracks[i].enabled = on;
            return m_project->timeline()->setRedaction(c.id, r);
        });
    });
    connect(m_bgOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating || m_clip.isNull())
            return;
        const QString model = m_bgModel->currentData().toString();
        if (on && !ai::visionModelReady(ai::backgroundModelId(model))) {
            emit backgroundRequested(m_clip, model); // downloads the model first
            refresh();
            return;
        }
        edit(on ? tr("Remove the background") : tr("Keep the background"), {}, [&](const Clip& c) {
            BackgroundRemoval b = c.background;
            b.enabled = on;
            b.model = model;
            return m_project->timeline()->setBackgroundRemoval(c.id, b);
        });
    });
    connect(m_bgModel, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updating || m_clip.isNull())
            return;
        const QString model = m_bgModel->currentData().toString();
        const Clip* c = m_project->timeline()->clip(m_clip);
        if (c && c->background.enabled && !ai::visionModelReady(ai::backgroundModelId(model))) {
            emit backgroundRequested(m_clip, model);
            refresh();
            return;
        }
        edit(tr("Change background model"), {}, [&](const Clip& c) {
            BackgroundRemoval b = c.background;
            b.model = model;
            return m_project->timeline()->setBackgroundRemoval(c.id, b);
        });
    });
    connect(m_bgSoftness, &QSpinBox::valueChanged, this, [this](int v) {
        edit(tr("Change edge softness"), QStringLiteral("bg-softness"), [&](const Clip& c) {
            BackgroundRemoval b = c.background;
            b.softness = v / 100.0;
            return m_project->timeline()->setBackgroundRemoval(c.id, b);
        });
    });
    connect(m_trackStart, &QPushButton::clicked, this, [this] {
        if (!m_clip.isNull())
            emit trackRequested(m_clip);
    });
    connect(m_trackClear, &QPushButton::clicked, this, [this] {
        edit(tr("Stop following"), {}, [&](const Clip& c) { return m_project->timeline()->setMotionPath(c.id, {}); });
    });
    connect(m_freezeDuration, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit(tr("Change freeze length"), QStringLiteral("duration"),
             [&](const Clip& c) { return m_project->timeline()->setDuration(c.id, ticksFromSeconds(v)); });
    });
    connect(m_stillDuration, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit(tr("Change picture length"), QStringLiteral("duration"),
             [&](const Clip& c) { return m_project->timeline()->setDuration(c.id, ticksFromSeconds(v)); });
    });
    connect(m_loop, &QCheckBox::toggled, this, [this](bool on) {
        edit(on ? tr("Repeat clip") : tr("Stop repeating clip"), {}, [&](const Clip& c) {
            auto m = m_project->media()->find(c.mediaId);
            return m && m_project->timeline()->setLoop(c.id, on, m->info.duration);
        });
    });
    connect(m_titleDuration, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit(tr("Change title length"), QStringLiteral("duration"),
             [&](const Clip& c) { return m_project->timeline()->setDuration(c.id, ticksFromSeconds(v)); });
    });

    for (QDoubleSpinBox* sb : {m_posX, m_posY, m_scale, m_rotation})
        connect(sb, &QDoubleSpinBox::valueChanged, this, [this] { applyTransform(); });
    connect(m_opacity, &QSpinBox::valueChanged, this, [this] { applyTransform(); });
    connect(resetTransform, &QPushButton::clicked, this, [this] {
        edit(tr("Reset position and size"), QStringLiteral("transform-reset"),
             [&](const Clip& c) { return m_project->timeline()->setTransform(c.id, ClipTransform{}); });
    });

    connect(m_transitionType, &QComboBox::currentIndexChanged, this, [this] { applyTransition(); });
    connect(m_transitionDuration, &QDoubleSpinBox::valueChanged, this, [this] { applyTransition(); });
    connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, [this] { applyFades(); });
    connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, [this] { applyFades(); });
    connect(m_gain, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        edit(tr("Change volume"), QStringLiteral("gain"), [&](const Clip& c) { return m_project->timeline()->setGain(c.id, v); });
    });

    connect(project->timeline(), &Timeline::changed, this, &PropertiesPanel::refresh);
    showNothing();
}

QWidget* PropertiesPanel::makeSection(const QString& title, QFormLayout** form) {
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 6, 0, 4);
    v->setSpacing(4);
    auto* label = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    v->addWidget(label);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setStyleSheet(QStringLiteral("color: palette(mid);"));
    v->addWidget(line);
    *form = new QFormLayout;
    (*form)->setLabelAlignment(Qt::AlignLeft);
    (*form)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    (*form)->setContentsMargins(0, 2, 0, 0);
    v->addLayout(*form);
    return box;
}

QToolButton* PropertiesPanel::makeColorButton() {
    auto* b = new QToolButton;
    b->setFixedSize(26, 22);
    setButtonColor(b, Qt::white);
    return b;
}

void PropertiesPanel::setButtonColor(QToolButton* b, const QColor& c) {
    b->setProperty("color", c);
    b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid palette(mid); border-radius: 3px; }")
                         .arg(c.name()));
}

void PropertiesPanel::showClip(const QUuid& clipId) {
    m_clip = clipId;
    m_media = {};
    ++m_session;
    refresh();
}

void PropertiesPanel::showMedia(const QUuid& mediaId) {
    m_clip = {};
    m_media = mediaId;
    ++m_session;
    refresh();
}

void PropertiesPanel::showNothing() {
    m_clip = {};
    m_media = {};
    ++m_session;
    refresh();
}

void PropertiesPanel::edit(const QString& text, const QString& field, const std::function<bool(const Clip&)>& op) {
    if (m_updating || m_clip.isNull())
        return;
    const Clip* c = m_project->timeline()->clip(m_clip);
    if (!c)
        return;
    const Clip copy = *c;
    m_updating = true; // the panel refreshes itself from the timeline afterwards
    runTimelineEdit(m_undo, m_project->timeline(), text, [&] { return op(copy); },
                    QStringLiteral("%1:%2:%3").arg(m_session).arg(m_clip.toString(), field));
    m_updating = false;
    refresh();
}

void PropertiesPanel::applyTitle() {
    if (m_updating)
        return;
    edit(tr("Edit title"), QStringLiteral("title"), [&](const Clip& c) {
        TitleData t = c.title;
        t.text = m_titleText->toPlainText();
        t.fontFamily = m_titleFont->currentFont().family();
        t.size = m_titleSize->value() / 100.0;
        t.bold = m_titleBold->isChecked();
        t.italic = m_titleItalic->isChecked();
        t.color = m_titleColor->property("color").value<QColor>();
        t.outlineColor = m_titleOutlineColor->property("color").value<QColor>();
        t.outlineWidth = m_titleOutline->value() / 100.0;
        t.shadow = m_titleShadow->isChecked();
        t.backgroundColor = m_titleBackColor->property("color").value<QColor>();
        t.backgroundOpacity = m_titleBackOpacity->value() / 100.0;
        t.align = m_titleAlign->currentIndex();
        t.x = m_titleX->value() / 100.0;
        t.y = m_titleY->value() / 100.0;
        t.animationIn = static_cast<TitleAnimation>(m_titleAnimIn->currentData().toInt());
        t.animationOut = static_cast<TitleAnimation>(m_titleAnimOut->currentData().toInt());
        t.animationDuration = ticksFromSeconds(m_titleAnimDuration->value());
        return m_project->timeline()->setTitle(c.id, t);
    });
}

void PropertiesPanel::applySubtitleStyle() {
    if (m_updating)
        return;
    edit(tr("Subtitle style"), QStringLiteral("subtitleStyle"), [&](const Clip& c) {
        const auto loc = m_project->timeline()->locate(c.id);
        if (!loc)
            return false;
        SubtitleStyle st = m_project->timeline()->tracks()[loc->track].subtitleStyle;
        st.fontFamily = m_subFont->currentFont().family();
        st.size = m_subSize->value() / 100.0;
        st.bold = m_subBold->isChecked();
        st.color = m_subColor->property("color").value<QColor>();
        st.outlineColor = m_subOutlineColor->property("color").value<QColor>();
        st.outlineWidth = m_subOutline->value() / 100.0;
        st.backgroundOpacity = m_subBackOpacity->value() / 100.0;
        st.y = m_subY->value() / 100.0;
        return m_project->timeline()->setSubtitleStyle(loc->track, st);
    });
}

void PropertiesPanel::focusText() {
    if (m_subSection->isVisible())
        m_subText->setFocus();
    else if (m_titleSection->isVisible())
        m_titleText->setFocus();
}

void PropertiesPanel::applyTransform() {
    edit(tr("Change position and size"), QStringLiteral("transform"), [&](const Clip& c) {
        ClipTransform t;
        t.x = m_posX->value() / 100.0;
        t.y = m_posY->value() / 100.0;
        t.scale = m_scale->value() / 100.0;
        t.rotation = m_rotation->value();
        t.opacity = m_opacity->value() / 100.0;
        return m_project->timeline()->setTransform(c.id, t);
    });
}

void PropertiesPanel::applyTransition() {
    edit(tr("Change transition"), QStringLiteral("transition"), [&](const Clip& c) {
        Transition t;
        t.type = static_cast<TransitionType>(m_transitionType->currentData().toInt());
        t.duration = t.type == TransitionType::None ? 0 : ticksFromSeconds(m_transitionDuration->value());
        return m_project->timeline()->setTransition(c.id, t);
    });
}

void PropertiesPanel::applyFades() {
    edit(tr("Change fades"), QStringLiteral("fades"), [&](const Clip& c) {
        return m_project->timeline()->setFades(c.id, ticksFromSeconds(m_fadeIn->value()), ticksFromSeconds(m_fadeOut->value()));
    });
}

void PropertiesPanel::refresh() {
    if (m_updating)
        return;
    g_dim = palette().color(QPalette::PlaceholderText).name();
    const Clip* clip = m_clip.isNull() ? nullptr : m_project->timeline()->clip(m_clip);
    refreshEditors(clip);
    refreshInfo(clip);
}

void PropertiesPanel::refreshEditors(const Clip* clip) {
    m_updating = true;
    int track = -1, index = -1;
    if (clip) {
        if (auto loc = m_project->timeline()->locate(clip->id)) {
            track = loc->track;
            index = loc->index;
        }
    }
    const Timeline* tl = m_project->timeline();
    const bool isTitle = clip && clip->kind == ClipKind::Title;
    const bool isSubtitle = clip && clip->kind == ClipKind::Subtitle;
    auto clipMedia = clip && clip->hasMedia() ? m_project->media()->find(clip->mediaId) : nullptr;
    const bool isStill = clip && clip->kind == ClipKind::Freeze && clipMedia && clipMedia->info.kind == MediaKind::Image;
    const bool isFreeze = clip && clip->kind == ClipKind::Freeze && !isStill;
    const bool video = clip && track >= 0 && tl->tracks()[track].type == TrackType::Video;
    const bool audio = clip && track >= 0 && tl->tracks()[track].type == TrackType::Audio;

    if (clip) {
        const QString trackName = track >= 0 ? tl->tracks()[track].name : QString();
        m_header->setText(QStringLiteral("<span style='color:%3'>%1 · %2</span>")
                              .arg(trackName,
                                   isTitle      ? tr("Title")
                                   : isSubtitle ? tr("Subtitle")
                                   : isFreeze   ? tr("Freeze frame")
                                   : isStill    ? tr("Picture")
                                   : video      ? tr("Video clip")
                                                : tr("Audio clip"),
                                   g_dim));
    }
    m_header->setVisible(clip != nullptr);

    auto setSpin = [](QDoubleSpinBox* sb, double v) {
        if (std::abs(sb->value() - v) > 1e-9)
            sb->setValue(v);
    };
    m_titleSection->setVisible(isTitle);
    if (isTitle) {
        const TitleData& t = clip->title;
        if (m_titleText->toPlainText() != t.text)
            m_titleText->setPlainText(t.text);
        if (!t.fontFamily.isEmpty() && m_titleFont->currentFont().family() != t.fontFamily)
            m_titleFont->setCurrentFont(QFont(t.fontFamily));
        setSpin(m_titleSize, t.size * 100);
        m_titleBold->setChecked(t.bold);
        m_titleItalic->setChecked(t.italic);
        setButtonColor(m_titleColor, t.color);
        setButtonColor(m_titleOutlineColor, t.outlineColor);
        setSpin(m_titleOutline, t.outlineWidth * 100);
        m_titleShadow->setChecked(t.shadow);
        setButtonColor(m_titleBackColor, t.backgroundColor);
        m_titleBackOpacity->setValue(qRound(t.backgroundOpacity * 100));
        m_titleAlign->setCurrentIndex(std::clamp(t.align, 0, 2));
        setSpin(m_titleX, t.x * 100);
        setSpin(m_titleY, t.y * 100);
        m_titleAnimIn->setCurrentIndex(m_titleAnimIn->findData(static_cast<int>(t.animationIn)));
        m_titleAnimOut->setCurrentIndex(m_titleAnimOut->findData(static_cast<int>(t.animationOut)));
        setSpin(m_titleAnimDuration, ticksToSeconds(t.animationDuration));
        setSpin(m_titleDuration, ticksToSeconds(clip->duration()));
    }

    const bool stab = video && clip->kind == ClipKind::Media;
    m_stabSection->setVisible(stab);
    if (stab) {
        const Stabilization& s = clip->stabilization;
        m_stabOn->setChecked(s.isActive());
        m_stabSmoothing->setEnabled(s.isActive());
        m_stabZoom->setEnabled(s.isActive());
        setSpin(m_stabSmoothing, s.smoothing);
        m_stabZoom->setValue(qRound(s.zoom * 100));
        m_stabInfo->setText(s.path.size() > 1 ? QString() : tr("The camera movement is measured once when you turn it on."));
        m_stabInfo->setVisible(s.path.size() < 2);
    }
    const bool ai = video && clip->hasMedia();
    // Faces are searched in video frames; a picture can still lose its background.
    m_redactSection->setVisible(ai && !isStill);
    m_bgSection->setVisible(ai);
    if (ai) {
        const Redaction& r = clip->redaction;
        const int faces = r.count(RedactionKind::Face), plates = r.count(RedactionKind::Plate);
        const bool found = !r.tracks.empty();
        QString info;
        if (!r.enabled)
            info = tr("Blurs every face and number plate in the clip and follows them as they move. Runs on this computer.");
        else if (!found)
            info = tr("No faces or number plates were found in this clip.");
        else
            info = tr("Found %1 face(s) and %2 number plate(s).").arg(faces).arg(plates);
        const bool partial = r.enabled && clip->kind == ClipKind::Media &&
                             (clip->sourceIn < r.analyzedFrom - tl->frameDuration() || clip->sourceOut > r.analyzedTo + tl->frameDuration());
        if (partial)
            info += QLatin1Char(' ') + tr("The clip was lengthened since: search again to cover the new part.");
        m_redactInfo->setText(info);
        m_redactFind->setText(r.enabled ? tr("Search again") : tr("Find faces and plates"));
        m_redactClear->setEnabled(r.enabled);
        for (QWidget* w : std::initializer_list<QWidget*>{m_redactFaces, m_redactPlates, m_redactStyle, m_redactStrength})
            w->setEnabled(found);
        m_redactFaces->setChecked(r.faces);
        m_redactPlates->setChecked(r.plates);
        m_redactStyle->setCurrentIndex(std::max(0, m_redactStyle->findData(static_cast<int>(r.style))));
        m_redactStrength->setValue(qRound(r.strength * 100));
        m_redactList->setVisible(found);
        m_redactList->clear();
        int faceNo = 0, plateNo = 0;
        const Ticks origin = clip->kind == ClipKind::Media ? clip->sourceIn : clip->freezeTime;
        for (int i = 0; i < static_cast<int>(r.tracks.size()); ++i) {
            const RedactionTrack& t = r.tracks[i];
            const bool face = t.kind != RedactionKind::Plate;
            QString label = face ? tr("Face %1").arg(++faceNo) : tr("Plate %1").arg(++plateNo);
            if (clip->kind == ClipKind::Media)
                label += QStringLiteral(" · %1 – %2")
                             .arg(formatTimecode(std::max<Ticks>(0, t.start() - origin), tl->frameRate()),
                                  formatTimecode(std::max<Ticks>(0, t.end() - origin), tl->frameRate()));
            auto* item = new QListWidgetItem(label, m_redactList);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(t.enabled ? Qt::Checked : Qt::Unchecked);
            item->setData(Qt::UserRole, i);
        }
        m_redactList->setFixedHeight(std::min(130, m_redactList->count() * m_redactList->fontMetrics().height() * 3 / 2 + 8));

        const BackgroundRemoval& b = clip->background;
        m_bgOn->setChecked(b.enabled);
        m_bgModel->setCurrentIndex(std::max(0, m_bgModel->findData(b.model)));
        m_bgSoftness->setValue(qRound(b.softness * 100));
        m_bgModel->setEnabled(b.enabled);
        m_bgSoftness->setEnabled(b.enabled);
        QString bgInfo;
        if (b.enabled && !ai::visionModelReady(ai::backgroundModelId(b.model)))
            bgInfo = tr("The model is not on this computer yet: turn this off and on again to download it.");
        else if (b.enabled && track == 0)
            bgInfo = tr("On the main track the background turns black. Put the clip on a track above another clip to show that clip behind it.");
        else if (!b.enabled)
            bgInfo = tr("Cuts the person or object out of every frame. Playback gets slower.");
        m_bgInfo->setText(bgInfo);
        m_bgInfo->setVisible(!bgInfo.isEmpty());
    }

    const bool follow = video && track != 0 && (clip->hasMedia() || clip->kind == ClipKind::Title);
    m_trackSection->setVisible(follow);
    if (follow) {
        m_trackClear->setEnabled(!clip->motion.isEmpty());
        m_trackInfo->setText(clip->motion.isEmpty()
                                 ? tr("Make this clip move with something in the picture below it, e.g. a name above a person.")
                                 : tr("This clip follows a tracked object (%1 positions).").arg(clip->motion.points.size()));
    }

    m_freezeSection->setVisible(isFreeze);
    if (isFreeze) {
        setSpin(m_freezeDuration, ticksToSeconds(clip->duration()));
        m_freezeInfo->setText(tr("Shows the frame at %1 of the source.").arg(formatTimecode(clip->freezeTime, tl->frameRate())));
    }

    m_stillSection->setVisible(isStill);
    if (isStill)
        setSpin(m_stillDuration, ticksToSeconds(clip->duration()));
    const bool loopable = video && clip->kind == ClipKind::Media && clipMedia && clipMedia->info.duration > 0;
    m_loopSection->setVisible(loopable);
    if (loopable) {
        m_loop->setChecked(clip->loops());
        m_loopInfo->setVisible(clip->loops());
    }

    m_subSection->setVisible(isSubtitle);
    m_subTrackSection->setVisible(isSubtitle);
    if (isSubtitle) {
        if (m_subText->toPlainText() != clip->text)
            m_subText->setPlainText(clip->text);
        setSpin(m_subDuration, ticksToSeconds(clip->duration()));
        const Track& t = tl->tracks()[track];
        if (!m_subLanguage->hasFocus())
            m_subLanguage->setText(t.language);
        const SubtitleStyle& st = t.subtitleStyle;
        if (!st.fontFamily.isEmpty() && m_subFont->currentFont().family() != st.fontFamily)
            m_subFont->setCurrentFont(QFont(st.fontFamily));
        setSpin(m_subSize, st.size * 100);
        m_subBold->setChecked(st.bold);
        setButtonColor(m_subColor, st.color);
        setButtonColor(m_subOutlineColor, st.outlineColor);
        setSpin(m_subOutline, st.outlineWidth * 100);
        m_subBackOpacity->setValue(qRound(st.backgroundOpacity * 100));
        setSpin(m_subY, st.y * 100);
    }

    m_transformSection->setVisible(video);
    if (video) {
        const ClipTransform& t = clip->transform;
        setSpin(m_posX, t.x * 100);
        setSpin(m_posY, t.y * 100);
        setSpin(m_scale, t.scale * 100);
        setSpin(m_rotation, t.rotation);
        m_opacity->setValue(qRound(t.opacity * 100));
    }

    const bool transition = video && track == 0 && index > 0;
    m_transitionSection->setVisible(transition);
    if (transition) {
        const Transition& t = clip->transitionIn;
        m_transitionType->setCurrentIndex(m_transitionType->findData(static_cast<int>(t.isNone() ? TransitionType::None : t.type)));
        if (!t.isNone())
            setSpin(m_transitionDuration, ticksToSeconds(t.duration));
        else if (m_transitionDuration->value() <= 0.04)
            m_transitionDuration->setValue(1.0);
        m_transitionDuration->setEnabled(!t.isNone());
        auto dur = [this](const QUuid& id) {
            auto m = m_project->media()->find(id);
            return m ? m->info.duration : Ticks(0);
        };
        const Ticks effective = t.isNone() ? 0 : tl->state().transitionDuration(index, dur);
        QString note;
        if (!t.isNone() && effective <= 0)
            note = tr("Not shown: there is no footage beyond this cut. Trim the clips to leave media on both sides.");
        else if (!t.isNone() && effective < t.duration)
            note = tr("Shortened to %1 s by the footage available around the cut.").arg(ticksToSeconds(effective), 0, 'f', 2);
        m_transitionNote->setText(note.isEmpty() ? QString()
                                                 : QStringLiteral("<span style='color:%1'>%2</span>").arg(themeColors().warning.name(), note.toHtmlEscaped()));
        m_transitionNote->setVisible(!note.isEmpty());
    }

    m_fadeSection->setVisible(clip && !isSubtitle);
    if (clip && !isSubtitle) {
        const double maxFade = ticksToSeconds(clip->duration());
        m_fadeIn->setMaximum(maxFade);
        m_fadeOut->setMaximum(maxFade);
        setSpin(m_fadeIn, ticksToSeconds(clip->fadeIn));
        setSpin(m_fadeOut, ticksToSeconds(clip->fadeOut));
    }
    m_audioSection->setVisible(audio);
    if (audio)
        setSpin(m_gain, clip->gainDb);
    m_updating = false;
}

void PropertiesPanel::refreshInfo(const Clip* clip) {
    g_dim = palette().color(QPalette::PlaceholderText).name();
    QString html = QStringLiteral("<table cellspacing=2>");
    QUuid mediaId = m_media;
    if (clip)
        mediaId = clip->mediaId;
    auto media = mediaId.isNull() ? nullptr : m_project->media()->find(mediaId);
    if (!media) {
        m_info->setText(clip ? QString()
                             : QStringLiteral("<p style='color:%2'>%1</p>")
                                   .arg(tr("Select a clip or a file to see its properties."), g_dim));
        return;
    }
    const MediaInfo& info = media->info;
    const AVRational rate = info.video ? info.video->frameRate : m_project->timeline()->frameRate();

    if (clip) {
        html += section(tr("Clip"));
        html += row(tr("In"), formatTimecode(clip->sourceIn, rate));
        html += row(tr("Out"), formatTimecode(clip->sourceOut, rate));
        html += row(tr("Duration"), formatSeconds(clip->duration()));
        html += row(tr("Timeline position"), formatTimecode(clip->timelineStart, m_project->timeline()->frameRate()));
        if (info.video && media->hasKeyframes()) {
            const auto kf = media->keyframesCopy();
            const Ticks fd = frameDuration(rate);
            const Ticks k = previousKeyframe(kf, clip->sourceIn, fd / 2);
            const int frames = static_cast<int>((clip->sourceIn - k + fd / 2) / fd);
            html += row(tr("Lossless cut"),
                        frames <= 0 ? QStringLiteral("<span style='color:%1'>✔ %2</span>")
                                          .arg(themeColors().success.name(), tr("starts on a key frame"))
                                    : QStringLiteral("<span style='color:%1'>%2</span>")
                                          .arg(themeColors().warning.name(),
                                               tr("starts %n frame(s) after a key frame", nullptr, frames)));
        }
    }

    html += section(tr("File"));
    html += row(tr("Name"), QFileInfo(info.path).fileName().toHtmlEscaped());
    html += row(tr("Folder"), QDir::toNativeSeparators(QFileInfo(info.path).absolutePath()).toHtmlEscaped());
    html += row(tr("Format"), info.formatLongName.toHtmlEscaped());
    html += row(tr("Size"), QLocale().formattedDataSize(info.fileSize));
    if (info.kind == MediaKind::Image) {
        html += row(tr("Resolution"), QStringLiteral("%1 × %2").arg(info.imageWidth).arg(info.imageHeight));
        html += row(tr("Bit depth"), QStringLiteral("%1-bit").arg(info.imageBitDepth));
    } else {
        html += row(tr("Duration"), formatSeconds(info.duration));
        html += row(tr("Bit rate"), bitrate(info.bitRate));
    }

    if (info.video) {
        const VideoStreamInfo& v = *info.video;
        html += section(tr("Video"));
        html += row(tr("Codec"), (v.codecName.toUpper() + (v.profile.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(v.profile))).toHtmlEscaped());
        html += row(tr("Resolution"), QStringLiteral("%1 × %2").arg(v.width).arg(v.height));
        html += row(tr("Frame rate"), formatFrameRate(v.frameRate) +
                                         (v.variableFrameRate ? QStringLiteral(" (%1)").arg(tr("variable")) : QString()));
        html += row(tr("Pixel format"), nameOr(av_get_pix_fmt_name(v.pixelFormat)));
        html += row(tr("Bit depth"), QStringLiteral("%1-bit").arg(v.bitDepth));
        const bool untagged = v.colorPrimaries == AVCOL_PRI_UNSPECIFIED && v.colorTrc == AVCOL_TRC_UNSPECIFIED &&
                              v.colorSpace == AVCOL_SPC_UNSPECIFIED;
        html += row(tr("Colour"), untagged ? tr("not tagged")
                                           : QStringLiteral("%1 / %2 / %3")
                                                 .arg(nameOr(av_color_primaries_name(v.colorPrimaries)),
                                                      nameOr(av_color_transfer_name(v.colorTrc)),
                                                      nameOr(av_color_space_name(v.colorSpace))));
        html += row(tr("Range"), v.colorRange == AVCOL_RANGE_JPEG    ? tr("full")
                                 : v.colorRange == AVCOL_RANGE_MPEG ? tr("limited")
                                                                    : tr("not tagged"));
        if (v.isHdr())
            html += row(tr("HDR"), v.colorTrc == AVCOL_TRC_SMPTE2084 ? QStringLiteral("HDR10 (PQ)") : QStringLiteral("HLG"));
        if (v.rotation)
            html += row(tr("Rotation"), QStringLiteral("%1°").arg(v.rotation));
        html += row(tr("B-frames"), v.hasBFrames ? tr("yes") : tr("no"));
        html += row(tr("Bit rate"), bitrate(v.bitRate));
        if (media->hasKeyframes()) {
            const auto kf = media->keyframesCopy();
            if (kf.size() > 1)
                html += row(tr("Key frames"), tr("%1 (every %2 s on average)")
                                                  .arg(kf.size())
                                                  .arg(ticksToSeconds(kf.back() - kf.front()) / (kf.size() - 1), 0, 'f', 2));
        }
    }
    if (info.audio) {
        const AudioStreamInfo& a = *info.audio;
        html += section(tr("Audio"));
        html += row(tr("Codec"), a.codecName.toUpper().toHtmlEscaped());
        html += row(tr("Sample rate"), QStringLiteral("%1 Hz").arg(a.sampleRate));
        html += row(tr("Channels"), QStringLiteral("%1 (%2)").arg(a.channels).arg(a.channelLayout.toHtmlEscaped()));
        html += row(tr("Sample format"), a.sampleFormat.toHtmlEscaped());
        html += row(tr("Bit rate"), bitrate(a.bitRate));
    }
    html += QStringLiteral("</table>");
    m_info->setText(html);
}

} // namespace lumora

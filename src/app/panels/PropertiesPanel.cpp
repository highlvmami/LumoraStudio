#include "PropertiesPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>

#include "Icons.h"
#include "Tasks.h"
#include "Theme.h"
#include "ai/Vision.h"
#include "color/AutoColor.h"
#include "color/LookGallery.h"
#include "color/SliderRow.h"
#include "export/ExportPlanner.h"
#include "motion/Analysis.h"
#include "panels/CollapsibleSection.h"
#include "project/Project.h"
#include "render/TitleRenderer.h"
#include "render/TitleTemplates.h"
#include "timeline/Subtitles.h"
#include "undo/TimelineCommands.h"

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace lumora {

// The shape of a speed ramp: relative speed along the clip.
class SpeedRampView : public QWidget {
public:
    explicit SpeedRampView(QWidget* parent = nullptr) : QWidget(parent) { setFixedHeight(46); }
    void setRamp(const TimeRemap& r) {
        m_remap = r;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const ThemeColors& tc = themeColors();
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(tc.raised);
        p.drawRoundedRect(r, 6, 6);
        if (m_remap.ramp.empty())
            return;
        double maxRate = 1.0;
        for (const auto& pt : m_remap.ramp)
            maxRate = std::max(maxRate, pt.rate);
        // Speed on a log scale so slow and fast parts read alike.
        const double top = std::log2(maxRate) + 0.5, bottom = -3.5;
        auto yOf = [&](double rate) { return r.bottom() - 4 - (std::log2(std::max(0.05, rate)) - bottom) / (top - bottom) * (r.height() - 8); };
        p.setPen(QPen(tc.textFaint, 1, Qt::DashLine));
        p.drawLine(QPointF(r.left() + 4, yOf(1.0)), QPointF(r.right() - 4, yOf(1.0)));
        QPainterPath path;
        for (int i = 0; i <= 60; ++i) {
            const double pos = i / 60.0;
            const QPointF pt(r.left() + 4 + pos * (r.width() - 8), yOf(m_remap.rateAt(pos)));
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        p.setPen(QPen(tc.accent, 2));
        p.drawPath(path);
    }

private:
    TimeRemap m_remap;
};

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

QLabel* hint(const QString& text = {}) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setObjectName(QStringLiteral("propertyHint"));
    return l;
}

const char* const kTabKey = "properties/tab";

} // namespace

QString transitionName(TransitionType t) {
    switch (t) {
    case TransitionType::None: return PropertiesPanel::tr("None (cut)");
    case TransitionType::CrossDissolve: return PropertiesPanel::tr("Cross dissolve");
    case TransitionType::DipToBlack: return PropertiesPanel::tr("Dip to black");
    case TransitionType::DipToWhite: return PropertiesPanel::tr("Dip to white");
    case TransitionType::WipeLeft: return PropertiesPanel::tr("Wipe left");
    case TransitionType::WipeRight: return PropertiesPanel::tr("Wipe right");
    case TransitionType::WipeUp: return PropertiesPanel::tr("Wipe up");
    case TransitionType::WipeDown: return PropertiesPanel::tr("Wipe down");
    case TransitionType::SlideLeft: return PropertiesPanel::tr("Push left");
    case TransitionType::SlideRight: return PropertiesPanel::tr("Push right");
    case TransitionType::Zoom: return PropertiesPanel::tr("Zoom");
    case TransitionType::Blur: return PropertiesPanel::tr("Blur");
    case TransitionType::PushUp: return PropertiesPanel::tr("Push up");
    case TransitionType::PushDown: return PropertiesPanel::tr("Push down");
    case TransitionType::Iris: return PropertiesPanel::tr("Circle");
    case TransitionType::ClockWipe: return PropertiesPanel::tr("Clock wipe");
    case TransitionType::BarnDoor: return PropertiesPanel::tr("Doors");
    case TransitionType::DiagonalWipe: return PropertiesPanel::tr("Diagonal wipe");
    case TransitionType::Flash: return PropertiesPanel::tr("Flash");
    case TransitionType::Pixelate: return PropertiesPanel::tr("Pixelate");
    case TransitionType::Spin: return PropertiesPanel::tr("Spin");
    case TransitionType::Glitch: return PropertiesPanel::tr("Glitch");
    case TransitionType::ZoomBlur: return PropertiesPanel::tr("Zoom blur");
    }
    return {};
}

PropertiesPanel::PropertiesPanel(Project* project, QUndoStack* undo, QWidget* parent)
    : QWidget(parent), m_project(project), m_undo(undo) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 4);
    outer->setSpacing(6);

    // What is selected: kind, track and name.
    m_headerCard = new QFrame;
    m_headerCard->setObjectName(QStringLiteral("propertyHeader"));
    auto* hl = new QVBoxLayout(m_headerCard);
    hl->setContentsMargins(10, 6, 10, 6);
    hl->setSpacing(0);
    m_headerKind = new QLabel;
    m_headerKind->setObjectName(QStringLiteral("propertyHint"));
    m_headerName = new QLabel;
    QFont nf = m_headerName->font();
    nf.setBold(true);
    m_headerName->setFont(nf);
    m_headerName->setTextFormat(Qt::PlainText);
    hl->addWidget(m_headerKind);
    hl->addWidget(m_headerName);
    outer->addWidget(m_headerCard);

    m_tabs = new QTabBar;
    m_tabs->setObjectName(QStringLiteral("propertyTabs"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setElideMode(Qt::ElideNone);
    m_tabs->setIconSize(QSize(16, 16));
    m_tabNames[EditPage] = tr("Edit");
    m_tabNames[EffectsPage] = tr("Effects");
    m_tabNames[SmartPage] = tr("Smart");
    m_tabNames[SoundPage] = tr("Sound");
    m_tabNames[InfoPage] = tr("Info");
    const QIcon tabIcons[PageCount] = {Icons::sliders(), Icons::effects(), Icons::smart(), Icons::sound(), Icons::info()};
    const QString tabTips[PageCount] = {
        tr("Text, length, speed, position, transitions and fades"),
        tr("Colour filters, green screen and picture effects"),
        tr("Automatic tools: stabilise, follow, hide faces, remove the background"),
        tr("Volume of the clip"),
        tr("Details of the file"),
    };
    for (int i = 0; i < PageCount; ++i) {
        m_tabs->addTab(tabIcons[i], m_tabNames[i]);
        m_tabs->setTabToolTip(i, QStringLiteral("<b>%1</b><br>%2").arg(m_tabNames[i].toHtmlEscaped(), tabTips[i].toHtmlEscaped()));
    }
    outer->addWidget(m_tabs);

    m_pages = new QStackedWidget;
    for (int i = 0; i < PageCount; ++i) {
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        auto* content = new QWidget;
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(0, 2, 2, 8);
        layout->setSpacing(8);
        m_emptyHints[i] = hint();
        m_emptyHints[i]->setAlignment(Qt::AlignCenter);
        m_emptyHints[i]->setContentsMargins(8, 20, 8, 8);
        layout->addWidget(m_emptyHints[i]);
        m_pageLayouts[i] = layout;
        scroll->setWidget(content);
        m_pages->addWidget(scroll);
    }
    outer->addWidget(m_pages, 1);
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) {
        m_pages->setCurrentIndex(i);
        updateTabLabels();
        if (!m_updating && i >= 0)
            QSettings().setValue(QLatin1String(kTabKey), i);
    });

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
    auto percent = [] {
        auto* sb = new QSpinBox;
        sb->setRange(0, 100);
        sb->setSuffix(QStringLiteral(" %"));
        sb->setKeyboardTracking(false);
        return sb;
    };
    const QString sec = QStringLiteral(" s");
    QFormLayout* f = nullptr;

    // ===== Edit =====

    // --- title templates ---
    m_templateSection = makeSection(EditPage, QStringLiteral("titleTemplates"), tr("Title templates"), &f);
    m_templates = new QListWidget;
    m_templates->setViewMode(QListView::IconMode);
    m_templates->setWrapping(true);
    m_templates->setResizeMode(QListView::Adjust);
    m_templates->setMovement(QListView::Static);
    m_templates->setIconSize(QSize(96, 54));
    m_templates->setGridSize(QSize(104, 78));
    m_templates->setUniformItemSizes(true);
    m_templates->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_templates->setFrameShape(QFrame::NoFrame);
    m_templates->setFixedHeight(2 * 78 + 8);
    m_templates->setToolTip(tr("Click a style: the title keeps its text"));
    f->addRow(m_templates);
    rebuildTemplateGallery();

    // --- title ---
    m_titleSection = makeSection(EditPage, QStringLiteral("title"), tr("Title"), &f);
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
    m_titleBackOpacity = percent();
    m_titleBackColor = makeColorButton();
    backRow->addWidget(m_titleBackOpacity, 1);
    backRow->addWidget(m_titleBackColor);
    f->addRow(tr("Background"), backRow);
    auto* decoRow = new QHBoxLayout;
    m_titleDecoration = new QComboBox;
    m_titleDecoration->addItem(tr("None"), static_cast<int>(TitleDecoration::None));
    m_titleDecoration->addItem(tr("Side bar"), static_cast<int>(TitleDecoration::Bar));
    m_titleDecoration->addItem(tr("Underline"), static_cast<int>(TitleDecoration::Underline));
    m_titleDecoration->addItem(tr("Frame"), static_cast<int>(TitleDecoration::Frame));
    m_titleDecoration->addItem(tr("Band"), static_cast<int>(TitleDecoration::Band));
    m_titleDecoration->addItem(tr("Corners"), static_cast<int>(TitleDecoration::Brackets));
    m_titleAccent = makeColorButton();
    m_titleAccent->setToolTip(tr("Colour of the shape"));
    decoRow->addWidget(m_titleDecoration, 1);
    decoRow->addWidget(m_titleAccent);
    f->addRow(tr("Shape"), decoRow);
    auto* letterRow = new QHBoxLayout;
    m_titleSpacing = dspin(-10, 100, 1, 0, QStringLiteral(" %"));
    m_titleSpacing->setToolTip(tr("Extra space between letters"));
    m_titleUpper = new QCheckBox(tr("CAPITALS"));
    letterRow->addWidget(m_titleSpacing, 1);
    letterRow->addWidget(m_titleUpper);
    f->addRow(tr("Spacing"), letterRow);
    m_titleShadow = new QCheckBox(tr("Soft shadow"));
    f->addRow(m_titleShadow);
    m_titleAnimIn = new QComboBox;
    m_titleAnimOut = new QComboBox;
    for (QComboBox* c : {m_titleAnimIn, m_titleAnimOut}) {
        c->addItem(tr("None"), static_cast<int>(TitleAnimation::None));
        c->addItem(tr("Fade"), static_cast<int>(TitleAnimation::Fade));
        c->addItem(tr("Slide up"), static_cast<int>(TitleAnimation::SlideUp));
        c->addItem(tr("Slide in from the side"), static_cast<int>(TitleAnimation::SlideLeft));
        c->addItem(tr("Pop"), static_cast<int>(TitleAnimation::Pop));
        c->addItem(tr("Zoom"), static_cast<int>(TitleAnimation::Zoom));
        c->addItem(tr("Blur"), static_cast<int>(TitleAnimation::Blur));
        c->addItem(tr("Typewriter"), static_cast<int>(TitleAnimation::Typewriter));
    }
    f->addRow(tr("Animation in"), m_titleAnimIn);
    f->addRow(tr("Animation out"), m_titleAnimOut);
    m_titleAnimDuration = dspin(0, 10, 0.1, 2, sec);
    f->addRow(tr("Animation length"), m_titleAnimDuration);
    m_titleDuration = dspin(0.04, 3600, 0.5, 2, sec);
    f->addRow(tr("Duration"), m_titleDuration);

    // --- freeze frame ---
    m_freezeSection = makeSection(EditPage, QStringLiteral("freeze"), tr("Freeze frame"), &f);
    m_freezeDuration = dspin(0.04, 3600, 0.5, 2, sec);
    f->addRow(tr("Hold for"), m_freezeDuration);
    m_freezeInfo = hint();
    f->addRow(m_freezeInfo);

    // --- picture (stills and stickers) ---
    m_stillSection = makeSection(EditPage, QStringLiteral("picture"), tr("Picture"), &f);
    m_stillDuration = dspin(0.04, 3600, 0.5, 2, sec);
    f->addRow(tr("Duration"), m_stillDuration);

    // --- subtitle ---
    m_subSection = makeSection(EditPage, QStringLiteral("subtitle"), tr("Subtitle"), &f);
    m_subText = new QPlainTextEdit;
    m_subText->setFixedHeight(56);
    m_subText->setPlaceholderText(tr("Type what is said; two lines at most read best"));
    f->addRow(m_subText);
    m_subDuration = dspin(0.04, 600, 0.1, 2, sec);
    f->addRow(tr("Duration"), m_subDuration);

    m_subTrackSection = makeSection(EditPage, QStringLiteral("subtitleTrack"), tr("Subtitle track"), &f);
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
    m_subBackOpacity = percent();
    m_subBackOpacity->setToolTip(tr("A dark box behind the text"));
    f->addRow(tr("Background"), m_subBackOpacity);
    m_subY = dspin(5, 100, 1, 0, QStringLiteral(" %"));
    m_subY->setToolTip(tr("Where the bottom line sits, from the top of the frame"));
    f->addRow(tr("Height in frame"), m_subY);
    f->addRow(hint(tr("The style is used when subtitles are burnt into the picture and in the preview. "
                      "Embedded subtitles are styled by the player.")));

    // --- speed ---
    m_speedSection = makeSection(EditPage, QStringLiteral("speed"), tr("Speed"), &f);
    auto* speedReset = m_speedSection->addResetButton(tr("Normal speed"));
    m_speed = dspin(0.05, 20, 0.05, 2, QStringLiteral(" ×"));
    m_speed->setToolTip(tr("2 × plays twice as fast, 0.5 × at half speed (slow motion)"));
    f->addRow(tr("Speed"), m_speed);
    auto* presetRow = new QHBoxLayout;
    presetRow->setSpacing(3);
    for (double v : {0.25, 0.5, 1.0, 2.0, 4.0}) {
        auto* b = new QToolButton;
        b->setText(QStringLiteral("%1×").arg(QLocale().toString(v)));
        b->setToolTip(tr("Play at %1 × speed").arg(QLocale().toString(v)));
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        b->setMinimumWidth(30);
        connect(b, &QToolButton::clicked, this, [this, v] { m_speed->setValue(v); });
        presetRow->addWidget(b, 1);
    }
    f->addRow(presetRow);
    m_speedReverse = new QCheckBox(tr("Play backwards"));
    f->addRow(m_speedReverse);
    m_speedRamp = new QComboBox;
    m_speedRamp->setToolTip(tr("Gets gradually faster and slower within the clip"));
    m_speedRamp->addItem(tr("Even speed"), static_cast<int>(SpeedRampPreset::None));
    m_speedRamp->addItem(tr("Montage: fast, slow, fast"), static_cast<int>(SpeedRampPreset::Montage));
    m_speedRamp->addItem(tr("Hero: slows down for the moment"), static_cast<int>(SpeedRampPreset::Hero));
    m_speedRamp->addItem(tr("Bullet: very slow in the middle"), static_cast<int>(SpeedRampPreset::Bullet));
    m_speedRamp->addItem(tr("Rush in: fast, then normal"), static_cast<int>(SpeedRampPreset::FlashIn));
    m_speedRamp->addItem(tr("Rush out: normal, then fast"), static_cast<int>(SpeedRampPreset::FlashOut));
    m_speedRamp->addItem(tr("Speed up"), static_cast<int>(SpeedRampPreset::SlowIn));
    m_speedRamp->addItem(tr("Slow down"), static_cast<int>(SpeedRampPreset::SlowOut));
    f->addRow(tr("Speed ramp"), m_speedRamp);
    m_rampView = new SpeedRampView;
    f->addRow(m_rampView);
    m_speedBlend = new QComboBox;
    m_speedBlend->addItem(tr("Off: repeat frames"), static_cast<int>(FrameBlending::None));
    m_speedBlend->addItem(tr("Mix neighbouring frames"), static_cast<int>(FrameBlending::Blend));
    m_speedBlend->addItem(tr("Make in-between frames (optical flow)"), static_cast<int>(FrameBlending::OpticalFlow));
    if (ai::visionAvailable())
        m_speedBlend->addItem(tr("In-between frames with AI (best, slow export)"), static_cast<int>(FrameBlending::AiFlow));
    m_speedBlend->setToolTip(tr("For slow motion: new pictures are drawn between the filmed frames, so it plays smoothly"));
    f->addRow(tr("Smooth slow motion"), m_speedBlend);
    m_speedPitch = new QCheckBox(tr("Keep the voice's pitch"));
    m_speedPitch->setToolTip(tr("The sound gets faster or slower without sounding higher or lower"));
    f->addRow(m_speedPitch);
    m_speedInfo = hint();
    f->addRow(m_speedInfo);

    // --- repeat ---
    m_loopSection = makeSection(EditPage, QStringLiteral("repeat"), tr("Repeat"), &f);
    m_loop = new QCheckBox(tr("Play over and over"));
    m_loop->setToolTip(tr("Repeat the clip, e.g. an animated sticker; drag its end on the timeline to make it longer"));
    f->addRow(m_loop);
    m_loopInfo = hint(tr("Drag the end of the clip on the timeline to make it as long as you like. The sound is not repeated."));
    f->addRow(m_loopInfo);

    // --- transform ---
    m_transformSection = makeSection(EditPage, QStringLiteral("transform"), tr("Position and size"), &f);
    auto* resetTransform = m_transformSection->addResetButton(tr("Reset position, size and crop"));
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
    m_opacity = percent();
    f->addRow(tr("Opacity"), m_opacity);
    auto* cropGrid = new QGridLayout;
    cropGrid->setSpacing(3);
    int cropCell = 0;
    m_cropLeft = new QSpinBox;
    m_cropTop = new QSpinBox;
    m_cropRight = new QSpinBox;
    m_cropBottom = new QSpinBox;
    const std::pair<QSpinBox*, QString> crops[] = {{m_cropLeft, tr("Left")}, {m_cropTop, tr("Top")},
                                                   {m_cropRight, tr("Right")}, {m_cropBottom, tr("Bottom")}};
    for (const auto& [sb, name] : crops) {
        sb->setRange(0, 95);
        sb->setSuffix(QStringLiteral("%"));
        sb->setKeyboardTracking(false);
        sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
        sb->setToolTip(tr("Cut away from the %1 edge").arg(name.toLower()));
        sb->setPrefix(name + QLatin1Char(' '));
        sb->setMinimumWidth(40);
        cropGrid->addWidget(sb, cropCell / 2, cropCell % 2);
        ++cropCell;
    }
    f->addRow(tr("Crop"), cropGrid);

    // --- transition ---
    m_transitionSection = makeSection(EditPage, QStringLiteral("transition"), tr("Transition from the previous clip"), &f);
    m_transitionType = new QComboBox;
    m_transitionType->setMaxVisibleItems(24);
    for (TransitionType t : allTransitionTypes())
        m_transitionType->addItem(transitionName(t), static_cast<int>(t));
    f->addRow(tr("Type"), m_transitionType);
    m_transitionDuration = dspin(0.04, 10, 0.1, 2, sec);
    f->addRow(tr("Duration"), m_transitionDuration);
    m_transitionNote = new QLabel;
    m_transitionNote->setWordWrap(true);
    f->addRow(m_transitionNote);

    // --- fades ---
    m_fadeSection = makeSection(EditPage, QStringLiteral("fades"), tr("Fades"), &f);
    m_fadeIn = dspin(0, 600, 0.1, 2, sec);
    m_fadeOut = dspin(0, 600, 0.1, 2, sec);
    f->addRow(tr("Fade in"), m_fadeIn);
    f->addRow(tr("Fade out"), m_fadeOut);

    // ===== Effects =====

    m_filterSection = makeSection(EffectsPage, QStringLiteral("filters"), tr("Colour filters"), &f);
    m_autoColor = new QPushButton(Icons::smart(), tr("Auto colour and light"));
    m_autoColor->setToolTip(tr("Measures the picture and sets exposure, contrast and white balance so it looks natural"));
    f->addRow(m_autoColor);
    m_looks = new LookGallery;
    f->addRow(m_looks);
    m_lookAmount = new SliderRow(tr("Strength (%)"), 0, 100, 1, 0);
    m_lookAmount->setDefault(100);
    f->addRow(m_lookAmount);
    auto* moreColor = new QPushButton(tr("More colour settings…"));
    moreColor->setToolTip(tr("Opens the Colour workspace: light, wheels, curves, HSL and LUTs"));
    f->addRow(moreColor);
    connect(moreColor, &QPushButton::clicked, this, &PropertiesPanel::colorWorkspaceRequested);

    m_keySection = makeSection(EffectsPage, QStringLiteral("chromaKey"), tr("Green screen"), &f);
    m_keyOn = new QCheckBox(tr("Remove the screen colour"));
    m_keyOn->setToolTip(tr("Makes the green (or blue) background transparent, so the clip below shows through"));
    f->addRow(m_keyOn);
    auto* keyRow = new AdaptiveRow;
    m_keyColor = makeColorButton();
    m_keyColor->setToolTip(tr("Screen colour"));
    auto* keyGreen = new QToolButton;
    keyGreen->setText(tr("Green"));
    auto* keyBlue = new QToolButton;
    keyBlue->setText(tr("Blue"));
    m_keyPick = new QPushButton(tr("Pick…"));
    m_keyPick->setToolTip(tr("Draw a small box over the screen in the viewer to take its exact colour"));
    keyRow->addWidget(m_keyColor);
    keyRow->addWidget(keyGreen);
    keyRow->addWidget(keyBlue);
    keyRow->addWidget(m_keyPick, 1);
    f->addRow(tr("Colour"), keyRow);
    m_keyTolerance = new SliderRow(tr("Tolerance (%)"), 0, 100, 1, 0);
    m_keyTolerance->setDefault(35);
    m_keySoftness = new SliderRow(tr("Edge softness (%)"), 0, 100, 1, 0);
    m_keySoftness->setDefault(15);
    m_keySpill = new SliderRow(tr("Remove colour spill (%)"), 0, 100, 1, 0);
    m_keySpill->setDefault(50);
    m_keySpill->setToolTip(tr("Takes away the green glow the screen leaves on hair and edges"));
    f->addRow(m_keyTolerance);
    f->addRow(m_keySoftness);
    f->addRow(m_keySpill);
    f->addRow(hint(tr("Put the clip on a track above the new background.")));

    m_fxSection = makeSection(EffectsPage, QStringLiteral("pictureEffects"), tr("Picture effects"), &f);
    auto* fxReset = m_fxSection->addResetButton(tr("Turn all picture effects off"));
    m_fxDenoise = new SliderRow(tr("Reduce noise (%)"), 0, 100, 1, 0);
    m_fxDenoise->setToolTip(tr("Smooths grain and speckles, e.g. in dark phone videos, and keeps edges sharp"));
    m_fxSharpen = new SliderRow(tr("Sharpen (%)"), 0, 100, 1, 0);
    m_fxBlur = new SliderRow(tr("Blur (%)"), 0, 100, 1, 0);
    m_fxVignette = new SliderRow(tr("Vignette (%)"), 0, 100, 1, 0);
    m_fxVignette->setToolTip(tr("Darkens the corners"));
    m_fxGrain = new SliderRow(tr("Film grain (%)"), 0, 100, 1, 0);
    for (SliderRow* s : {m_fxDenoise, m_fxSharpen, m_fxBlur, m_fxVignette, m_fxGrain})
        f->addRow(s);

    // ===== Smart =====

    // --- stabilisation ---
    m_stabSection = makeSection(SmartPage, QStringLiteral("stabilise"), tr("Stabilisation"), &f);
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
    m_stabInfo = hint();
    f->addRow(m_stabInfo);

    // --- motion tracking ---
    m_trackSection = makeSection(SmartPage, QStringLiteral("follow"), tr("Motion tracking"), &f);
    m_trackInfo = hint();
    f->addRow(m_trackInfo);
    m_trackStart = new QPushButton(tr("Follow an object…"));
    m_trackStart->setToolTip(tr("Draw a box around something in the picture; this clip then moves with it"));
    m_trackClear = new QPushButton(tr("Stop following"));
    auto* trackRow = new AdaptiveRow;
    trackRow->addWidget(m_trackStart);
    trackRow->addWidget(m_trackClear);
    f->addRow(trackRow);

    // --- faces and number plates ---
    m_redactSection = makeSection(SmartPage, QStringLiteral("redact"), tr("Hide faces and plates"), &f);
    m_redactInfo = hint();
    f->addRow(m_redactInfo);
    m_redactFind = new QPushButton(tr("Find faces and plates"));
    m_redactFind->setToolTip(tr("Looks through the clip on this computer and follows every face and number plate"));
    m_redactClear = new QPushButton(tr("Remove"));
    auto* redactRow = new AdaptiveRow;
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
    m_redactStrength = percent();
    f->addRow(tr("Strength"), m_redactStrength);
    m_redactList = new QListWidget;
    m_redactList->setToolTip(tr("Untick what should stay visible"));
    f->addRow(m_redactList);

    // --- background removal ---
    m_bgSection = makeSection(SmartPage, QStringLiteral("background"), tr("Background"), &f);
    m_bgOn = new QCheckBox(tr("Remove the background"));
    m_bgOn->setToolTip(tr("Keeps the person or object in front and makes the rest transparent"));
    f->addRow(m_bgOn);
    m_bgModel = new QComboBox;
    m_bgModel->addItem(tr("Fast"), QStringLiteral("fast"));
    m_bgModel->addItem(tr("Detailed (slow)"), QStringLiteral("quality"));
    m_bgModel->setToolTip(tr("The detailed model finds hair and fine edges better; the preview uses the fast one"));
    f->addRow(tr("Model"), m_bgModel);
    m_bgSoftness = percent();
    f->addRow(tr("Edge softness"), m_bgSoftness);
    m_bgInfo = hint();
    f->addRow(m_bgInfo);

    m_reframeSection = makeSection(SmartPage, QStringLiteral("reframe"), tr("Vertical video"), &f);
    auto* reframeInfo = hint();
    reframeInfo->setText(tr("Turns the whole video portrait (Reels, TikTok, Shorts) or square; the picture follows the person "
                            "speaking."));
    f->addRow(reframeInfo);
    auto* reframe = new QPushButton(Icons::portrait(), tr("Make it vertical…"));
    f->addRow(reframe);
    connect(reframe, &QPushButton::clicked, this, &PropertiesPanel::reframeRequested);

    // ===== Sound =====

    m_audioSection = makeSection(SoundPage, QStringLiteral("volume"), tr("Sound"), &f);
    m_gain = dspin(-60, 24, 0.5, 1, QStringLiteral(" dB"));
    f->addRow(tr("Volume"), m_gain);
    auto* moreSound = new QPushButton(tr("Sound tools…"));
    moreSound->setToolTip(tr("Opens the Sound workspace: hum, wind and noise removal, equaliser, compressor, ducking"));
    f->addRow(moreSound);
    connect(moreSound, &QPushButton::clicked, this, &PropertiesPanel::soundWorkspaceRequested);
    auto* cutSilences = new QPushButton(Icons::silenceCut(), tr("Cut silences and hesitations…"));
    cutSilences->setToolTip(tr("Removes the pauses and the \"ııı\", \"eee\" of a talk on the main track"));
    f->addRow(cutSilences);
    connect(cutSilences, &QPushButton::clicked, this, &PropertiesPanel::cutSilencesRequested);
    auto* beatCut = new QPushButton(Icons::beat(), tr("Cut to the beat…"));
    beatCut->setToolTip(tr("Moves the cuts between the clips onto the beat of the music"));
    f->addRow(beatCut);
    connect(beatCut, &QPushButton::clicked, this, &PropertiesPanel::beatCutRequested);

    // ===== Info =====

    m_info = new QLabel;
    m_info->setTextFormat(Qt::RichText);
    m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_info->setContentsMargins(6, 0, 6, 0);
    m_pageLayouts[InfoPage]->addWidget(m_info);

    for (QVBoxLayout* l : m_pageLayouts)
        l->addStretch(1);

    // Let the panel be narrow: combo boxes and spin boxes shrink with it.
    for (QComboBox* c : m_pages->findChildren<QComboBox*>()) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(6);
    }
    for (QAbstractSpinBox* sb : m_pages->findChildren<QAbstractSpinBox*>())
        if (sb->buttonSymbols() != QAbstractSpinBox::NoButtons)
            sb->setMinimumWidth(60);
    // Paired X / Y fields: the arrows would leave no room for the value; the
    // wheel and arrow keys still step them.
    for (QDoubleSpinBox* sb : {m_titleX, m_titleY, m_posX, m_posY})
        sb->setButtonSymbols(QAbstractSpinBox::NoButtons);

    // --- wiring ---
    auto titleChanged = [this] { applyTitle(); };
    connect(m_titleText, &QPlainTextEdit::textChanged, this, titleChanged);
    connect(m_titleFont, &QFontComboBox::currentFontChanged, this, titleChanged);
    for (QDoubleSpinBox* sb : {m_titleSize, m_titleOutline, m_titleX, m_titleY, m_titleAnimDuration, m_titleSpacing})
        connect(sb, &QDoubleSpinBox::valueChanged, this, titleChanged);
    connect(m_titleBackOpacity, &QSpinBox::valueChanged, this, titleChanged);
    for (QToolButton* b : {m_titleBold, m_titleItalic})
        connect(b, &QToolButton::toggled, this, titleChanged);
    for (QCheckBox* cb : {m_titleShadow, m_titleUpper})
        connect(cb, &QCheckBox::toggled, this, titleChanged);
    for (QComboBox* c : {m_titleAlign, m_titleAnimIn, m_titleAnimOut, m_titleDecoration})
        connect(c, &QComboBox::currentIndexChanged, this, titleChanged);
    for (QToolButton* b : {m_titleColor, m_titleOutlineColor, m_titleBackColor, m_titleAccent})
        connect(b, &QToolButton::clicked, this, [this, b] {
            const QColor c = QColorDialog::getColor(b->property("color").value<QColor>(), this, tr("Choose a colour"));
            if (c.isValid()) {
                setButtonColor(b, c);
                applyTitle();
            }
        });
    connect(m_templates, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const TitleTemplate* t = findTitleTemplate(item->data(Qt::UserRole).toString());
        if (!t)
            return;
        edit(tr("Title template"), {}, [&](const Clip& c) {
            const TitleData d = applyTitleTemplate(c.title, *t);
            return m_project->timeline()->setTitle(c.id, d);
        });
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

    // Speed.
    connect(m_speed, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        applySpeed(v, m_speedReverse->isChecked(), static_cast<SpeedRampPreset>(m_speedRamp->currentData().toInt()));
    });
    connect(m_speedReverse, &QCheckBox::toggled, this, [this](bool on) {
        applySpeed(m_speed->value(), on, static_cast<SpeedRampPreset>(m_speedRamp->currentData().toInt()));
    });
    connect(m_speedRamp, &QComboBox::currentIndexChanged, this, [this] {
        applySpeed(m_speed->value(), m_speedReverse->isChecked(), static_cast<SpeedRampPreset>(m_speedRamp->currentData().toInt()));
    });
    connect(speedReset, &QToolButton::clicked, this, [this] { applySpeed(1.0, false, SpeedRampPreset::None); });
    auto speedOptions = [this] {
        if (!m_updating && m_speedBlend->currentData().toInt() == static_cast<int>(FrameBlending::AiFlow) &&
            !ensureVisionModels(this, {QStringLiteral("frame-flow")}, tr("Smooth slow motion"))) {
            refresh(); // back to the clip's setting
            return;
        }
        edit(tr("Slow motion options"), {}, [&](const Clip& c) {
            return m_project->timeline()->setSpeedOptions(c.id, static_cast<FrameBlending>(m_speedBlend->currentData().toInt()),
                                                          m_speedPitch->isChecked());
        });
    };
    connect(m_speedBlend, &QComboBox::currentIndexChanged, this, speedOptions);
    connect(m_speedPitch, &QCheckBox::toggled, this, speedOptions);

    // Effects.
    connect(m_looks, &LookGallery::lookChosen, this, [this](const QString& id) {
        edit(id.isEmpty() ? tr("Remove filter") : tr("Apply filter"), {}, [&](const Clip& c) {
            ColorGrade g = c.color;
            g.look = id;
            if (g.lookAmount <= 0.0)
                g.lookAmount = 1.0;
            return m_project->timeline()->setColor(c.id, g);
        });
    });
    connect(m_lookAmount, &SliderRow::valueChanged, this, [this](double v) {
        edit(tr("Filter strength"), QStringLiteral("lookAmount"), [&](const Clip& c) {
            ColorGrade g = c.color;
            g.lookAmount = v / 100.0;
            return m_project->timeline()->setColor(c.id, g);
        });
    });
    connect(m_autoColor, &QPushButton::clicked, this, [this] {
        const Clip* c = m_clip.isNull() ? nullptr : m_project->timeline()->clip(m_clip);
        if (!c || !m_thumbnail)
            return;
        const QImage picture = m_thumbnail(c->mediaId);
        edit(tr("Auto colour and light"), {}, [&](const Clip& c) {
            return m_project->timeline()->setColor(c.id, autoCorrect(picture, c.color));
        });
    });
    connect(m_keyOn, &QCheckBox::toggled, this, [this](bool on) {
        applyEffects(on ? tr("Green screen") : tr("Turn off green screen"), {}, [&](PictureEffects& e) { e.chromaKey.enabled = on; });
    });
    connect(m_keyColor, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_keyColor->property("color").value<QColor>(), this, tr("Screen colour"));
        if (c.isValid())
            applyEffects(tr("Screen colour"), {}, [&](PictureEffects& e) {
                e.chromaKey.color = c;
                e.chromaKey.enabled = true;
            });
    });
    connect(keyGreen, &QToolButton::clicked, this, [this] {
        applyEffects(tr("Screen colour"), {}, [&](PictureEffects& e) {
            e.chromaKey.color = ChromaKey{}.color;
            e.chromaKey.enabled = true;
        });
    });
    connect(keyBlue, &QToolButton::clicked, this, [this] {
        applyEffects(tr("Screen colour"), {}, [&](PictureEffects& e) {
            e.chromaKey.color = QColor(0, 71, 187);
            e.chromaKey.enabled = true;
        });
    });
    connect(m_keyPick, &QPushButton::clicked, this, [this] {
        if (!m_clip.isNull())
            emit keyColorPickRequested(m_clip);
    });
    auto bindEffect = [this](SliderRow* s, const QString& text, const QString& field, auto setter) {
        connect(s, &SliderRow::valueChanged, this, [this, text, field, setter](double v) {
            applyEffects(text, field, [&](PictureEffects& e) { setter(e, v / 100.0); });
        });
    };
    bindEffect(m_keyTolerance, tr("Key tolerance"), QStringLiteral("keyTolerance"), [](PictureEffects& e, double v) { e.chromaKey.tolerance = v; });
    bindEffect(m_keySoftness, tr("Key edge softness"), QStringLiteral("keySoftness"), [](PictureEffects& e, double v) { e.chromaKey.softness = v; });
    bindEffect(m_keySpill, tr("Remove colour spill"), QStringLiteral("keySpill"), [](PictureEffects& e, double v) { e.chromaKey.spill = v; });
    bindEffect(m_fxDenoise, tr("Reduce noise"), QStringLiteral("denoise"), [](PictureEffects& e, double v) { e.denoise = v; });
    bindEffect(m_fxSharpen, tr("Sharpen"), QStringLiteral("sharpen"), [](PictureEffects& e, double v) { e.sharpen = v; });
    bindEffect(m_fxBlur, tr("Blur"), QStringLiteral("blur"), [](PictureEffects& e, double v) { e.blur = v; });
    bindEffect(m_fxVignette, tr("Vignette"), QStringLiteral("vignette"), [](PictureEffects& e, double v) { e.vignette = v; });
    bindEffect(m_fxGrain, tr("Film grain"), QStringLiteral("grain"), [](PictureEffects& e, double v) { e.grain = v; });
    connect(fxReset, &QToolButton::clicked, this, [this] {
        applyEffects(tr("Turn off picture effects"), {}, [](PictureEffects& e) {
            const ChromaKey key = e.chromaKey;
            e = PictureEffects{};
            e.chromaKey = key;
        });
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
    for (QSpinBox* sb : {m_opacity, m_cropLeft, m_cropTop, m_cropRight, m_cropBottom})
        connect(sb, &QSpinBox::valueChanged, this, [this] { applyTransform(); });
    connect(resetTransform, &QToolButton::clicked, this, [this] {
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

CollapsibleSection* PropertiesPanel::makeSection(Page page, const QString& id, const QString& title, QFormLayout** form) {
    auto* s = new CollapsibleSection(id, title);
    *form = s->form();
    m_pageLayouts[page]->addWidget(s);
    return s;
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

void PropertiesPanel::rebuildTemplateGallery() {
    m_templates->clear();
    const QSize size(192, 108);
    for (const TitleTemplate& t : titleTemplates()) {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        QLinearGradient bg(0, 0, size.width(), size.height());
        bg.setColorAt(0, QColor(58, 66, 92));
        bg.setColorAt(1, QColor(28, 30, 42));
        p.fillRect(img.rect(), bg);
        // Shown in the middle of its animation, with its sample text.
        drawTitle(p, size, t.title, ticksFromSeconds(t.seconds / 2), ticksFromSeconds(t.seconds), 1.0);
        p.end();
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(img)), t.name, m_templates);
        item->setData(Qt::UserRole, t.id);
        item->setToolTip(t.name);
    }
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

void PropertiesPanel::showPage(Page page) {
    if (m_tabs->isTabVisible(page))
        m_tabs->setCurrentIndex(page);
}

void PropertiesPanel::setKeyColor(const QUuid& clipId, const QColor& color) {
    if (clipId != m_clip || !color.isValid())
        return;
    applyEffects(tr("Screen colour"), {}, [&](PictureEffects& e) {
        e.chromaKey.color = color;
        e.chromaKey.enabled = true;
    });
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
        t.decoration = static_cast<TitleDecoration>(m_titleDecoration->currentData().toInt());
        t.accentColor = m_titleAccent->property("color").value<QColor>();
        t.letterSpacing = m_titleSpacing->value() / 100.0;
        t.uppercase = m_titleUpper->isChecked();
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

void PropertiesPanel::applySpeed(double speed, bool reverse, SpeedRampPreset ramp) {
    edit(tr("Change speed"), QStringLiteral("speed"), [&](const Clip& c) {
        auto m = m_project->media()->find(c.mediaId);
        return m_project->timeline()->setSpeed(c.id, speed, reverse, speedRampPoints(ramp), m ? m->info.duration : 0);
    });
}

void PropertiesPanel::applyEffects(const QString& text, const QString& field, const std::function<void(PictureEffects&)>& change) {
    edit(text, field, [&](const Clip& c) {
        PictureEffects e = c.effects;
        change(e);
        return m_project->timeline()->setPictureEffects(c.id, e);
    });
}

void PropertiesPanel::focusText() {
    showPage(EditPage);
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
        const double l = m_cropLeft->value() / 100.0, tp = m_cropTop->value() / 100.0;
        const double r = m_cropRight->value() / 100.0, b = m_cropBottom->value() / 100.0;
        if (l > 0 || tp > 0 || r > 0 || b > 0)
            t.crop = QRectF(l, tp, std::max(0.05, 1.0 - l - r), std::max(0.05, 1.0 - tp - b));
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
    refreshTabs();
}

void PropertiesPanel::refreshTabs() {
    m_updating = true;
    const bool any = !m_clip.isNull() || !m_media.isNull();
    int firstVisible = -1;
    for (int p = 0; p < PageCount; ++p) {
        bool visible = false;
        for (CollapsibleSection* s : m_pages->widget(p)->findChildren<CollapsibleSection*>())
            visible |= !s->isHidden();
        if (p == InfoPage)
            visible = any;
        m_tabs->setTabVisible(p, visible);
        if (visible && firstVisible < 0)
            firstVisible = p;
    }
    m_emptyHints[EditPage]->setText(any ? QString() : tr("Select a clip on the timeline to change it, or a file to see its details."));
    for (QLabel* h : m_emptyHints)
        h->setVisible(!h->text().isEmpty());
    if (firstVisible < 0) {
        // Nothing selected: one page with the hint.
        m_tabs->setTabVisible(EditPage, true);
        m_tabs->setCurrentIndex(EditPage);
    } else {
        const int wanted = QSettings().value(QLatin1String(kTabKey), 0).toInt();
        if (wanted >= 0 && wanted < PageCount && m_tabs->isTabVisible(wanted))
            m_tabs->setCurrentIndex(wanted);
        else if (!m_tabs->isTabVisible(m_tabs->currentIndex()))
            m_tabs->setCurrentIndex(firstVisible);
    }
    m_tabs->setVisible(any);
    m_pages->setCurrentIndex(m_tabs->currentIndex());
    m_updating = false;
    updateTabLabels();
}

void PropertiesPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    updateTabLabels();
}

void PropertiesPanel::updateTabLabels() {
    const int room = m_pages->width() > 0 ? m_pages->width() : width();
    for (int i = 0; i < PageCount; ++i)
        m_tabs->setTabText(i, m_tabNames[i]);
    if (m_tabs->sizeHint().width() <= room)
        return;
    for (int i = 0; i < PageCount; ++i)
        if (i != m_tabs->currentIndex())
            m_tabs->setTabText(i, QString());
    if (m_tabs->sizeHint().width() > room)
        m_tabs->setTabText(m_tabs->currentIndex(), QString());
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

    // Header card.
    auto selectedMedia = !m_media.isNull() ? m_project->media()->find(m_media) : nullptr;
    if (clip) {
        const QString trackName = track >= 0 ? tl->tracks()[track].name : QString();
        const QString kind = isTitle      ? tr("Title")
                             : isSubtitle ? tr("Subtitle")
                             : isFreeze   ? tr("Freeze frame")
                             : isStill    ? tr("Picture")
                             : video      ? tr("Video clip")
                                          : tr("Audio clip");
        m_headerKind->setText(QStringLiteral("%1 · %2 · %3").arg(trackName, kind, formatSeconds(clip->duration())));
        QString name = isTitle ? clip->title.text.simplified() : isSubtitle ? clip->text.simplified()
                       : clipMedia ? QFileInfo(clipMedia->path).fileName() : tr("Missing media");
        m_headerName->setText(m_headerName->fontMetrics().elidedText(name, Qt::ElideRight, std::max(80, width() - 40)));
        m_headerName->setToolTip(name);
    } else if (selectedMedia) {
        m_headerKind->setText(tr("File"));
        m_headerName->setText(QFileInfo(selectedMedia->path).fileName());
    }
    m_headerCard->setVisible(clip || selectedMedia);

    auto setSpin = [](QDoubleSpinBox* sb, double v) {
        if (std::abs(sb->value() - v) > 1e-9)
            sb->setValue(v);
    };
    m_titleSection->setVisible(isTitle);
    m_templateSection->setVisible(isTitle);
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
        m_titleDecoration->setCurrentIndex(std::max(0, m_titleDecoration->findData(static_cast<int>(t.decoration))));
        setButtonColor(m_titleAccent, t.accentColor);
        m_titleAccent->setEnabled(t.decoration != TitleDecoration::None);
        setSpin(m_titleSpacing, t.letterSpacing * 100);
        m_titleUpper->setChecked(t.uppercase);
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
        m_stabSection->setModified(s.isActive());
    }
    const bool ai = video && clip->hasMedia();
    // Faces are searched in video frames; a picture can still lose its background.
    m_redactSection->setVisible(ai && !isStill);
    m_bgSection->setVisible(ai);
    m_reframeSection->setVisible(ai);
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
        const auto [mediaIn, mediaOut] = clip->mediaRange();
        const bool partial = r.enabled && clip->kind == ClipKind::Media &&
                             (mediaIn < r.analyzedFrom - tl->frameDuration() || mediaOut > r.analyzedTo + tl->frameDuration());
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
        const Ticks origin = mediaIn;
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
        m_redactSection->setModified(r.isActive());

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
        m_bgSection->setModified(b.enabled);
    }

    const bool follow = video && track != 0 && (clip->hasMedia() || clip->kind == ClipKind::Title);
    m_trackSection->setVisible(follow);
    if (follow) {
        m_trackClear->setEnabled(!clip->motion.isEmpty());
        m_trackInfo->setText(clip->motion.isEmpty()
                                 ? tr("Make this clip move with something in the picture below it, e.g. a name above a person.")
                                 : tr("This clip follows a tracked object (%1 positions).").arg(clip->motion.points.size()));
        m_trackSection->setModified(!clip->motion.isEmpty());
    }

    m_freezeSection->setVisible(isFreeze);
    if (isFreeze) {
        setSpin(m_freezeDuration, ticksToSeconds(clip->duration()));
        m_freezeInfo->setText(tr("Shows the frame at %1 of the source.").arg(formatTimecode(clip->freezeTime, tl->frameRate())));
    }

    m_stillSection->setVisible(isStill);
    if (isStill)
        setSpin(m_stillDuration, ticksToSeconds(clip->duration()));
    const bool loopable = video && clip->kind == ClipKind::Media && clipMedia && clipMedia->info.duration > 0 && !clip->hasSpeedChange();
    m_loopSection->setVisible(loopable);
    if (loopable) {
        m_loop->setChecked(clip->loops());
        m_loopInfo->setVisible(clip->loops());
        m_loopSection->setModified(clip->loops());
    }

    // Speed: video and sound clips with media (not repeating ones).
    const bool speed = clip && clip->kind == ClipKind::Media && (video || audio) && clipMedia && clipMedia->info.duration > 0 && !clip->loops();
    m_speedSection->setVisible(speed);
    if (speed) {
        const TimeRemap& r = clip->remap;
        const bool on = clip->hasSpeedChange();
        setSpin(m_speed, on ? r.speed : 1.0);
        m_speedReverse->setChecked(on && r.reverse);
        const SpeedRampPreset preset = on ? speedRampPresetOf(r.ramp) : SpeedRampPreset::None;
        m_speedRamp->setCurrentIndex(std::max(0, m_speedRamp->findData(static_cast<int>(preset))));
        m_rampView->setRamp(r);
        m_rampView->setVisible(on && !r.ramp.empty());
        m_speedBlend->setCurrentIndex(std::max(0, m_speedBlend->findData(static_cast<int>(r.blending))));
        m_speedPitch->setChecked(r.keepPitch);
        const bool slow = on && (r.speed < 1.0 || !r.ramp.empty());
        m_speedBlend->setEnabled(slow && video);
        m_speedPitch->setEnabled(on && !r.reverse && r.ramp.empty() && r.speed >= 0.25 && r.speed <= 4.0);
        const auto [mediaIn, mediaOut] = clip->mediaRange();
        QString info;
        if (on)
            info = tr("%1 of footage plays in %2.").arg(formatSeconds(mediaOut - mediaIn), formatSeconds(clip->duration()));
        if (on && r.reverse)
            info += QLatin1Char(' ') + tr("Playing backwards can make the preview slower.");
        m_speedInfo->setText(info);
        m_speedInfo->setVisible(!info.isEmpty());
        m_speedSection->setModified(on);
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
        const QRectF crop = t.hasCrop() ? t.crop : QRectF(0, 0, 1, 1);
        m_cropLeft->setValue(qRound(crop.left() * 100));
        m_cropTop->setValue(qRound(crop.top() * 100));
        m_cropRight->setValue(qRound((1.0 - crop.right()) * 100));
        m_cropBottom->setValue(qRound((1.0 - crop.bottom()) * 100));
        m_transformSection->setModified(!t.isIdentity());
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
        m_transitionSection->setModified(!t.isNone());
    }

    m_fadeSection->setVisible(clip && !isSubtitle);
    if (clip && !isSubtitle) {
        const double maxFade = ticksToSeconds(clip->duration());
        m_fadeIn->setMaximum(maxFade);
        m_fadeOut->setMaximum(maxFade);
        setSpin(m_fadeIn, ticksToSeconds(clip->fadeIn));
        setSpin(m_fadeOut, ticksToSeconds(clip->fadeOut));
        m_fadeSection->setModified(clip->fadeIn > 0 || clip->fadeOut > 0);
    }

    // Effects: clips that show a picture from media.
    const bool effects = video && clip->hasMedia();
    m_filterSection->setVisible(effects);
    m_keySection->setVisible(effects);
    m_fxSection->setVisible(effects);
    if (effects) {
        if (m_previewMedia != clip->mediaId && m_thumbnail) {
            const QImage thumb = m_thumbnail(clip->mediaId);
            if (!thumb.isNull()) {
                m_looks->setPreviewImage(thumb);
                m_previewMedia = clip->mediaId;
            }
        }
        m_autoColor->setEnabled(static_cast<bool>(m_thumbnail));
        m_looks->setCurrentLook(clip->color.look);
        m_lookAmount->setValue(clip->color.lookAmount * 100);
        m_lookAmount->setEnabled(clip->color.hasLook());
        m_filterSection->setModified(!clip->color.isIdentity());
        const PictureEffects& e = clip->effects;
        m_keyOn->setChecked(e.chromaKey.enabled);
        setButtonColor(m_keyColor, e.chromaKey.color);
        m_keyTolerance->setValue(e.chromaKey.tolerance * 100);
        m_keySoftness->setValue(e.chromaKey.softness * 100);
        m_keySpill->setValue(e.chromaKey.spill * 100);
        for (SliderRow* s : {m_keyTolerance, m_keySoftness, m_keySpill})
            s->setEnabled(e.chromaKey.enabled);
        m_keySection->setModified(e.chromaKey.enabled);
        m_fxDenoise->setValue(e.denoise * 100);
        m_fxSharpen->setValue(e.sharpen * 100);
        m_fxBlur->setValue(e.blur * 100);
        m_fxVignette->setValue(e.vignette * 100);
        m_fxGrain->setValue(e.grain * 100);
        m_fxSection->setModified(e.denoise > 0 || e.sharpen > 0 || e.blur > 0 || e.vignette > 0 || e.grain > 0);
    }

    m_audioSection->setVisible(audio);
    if (audio) {
        setSpin(m_gain, clip->gainDb);
        m_audioSection->setModified(clip->gainDb != 0.0);
    }
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
        const auto [mediaIn, mediaOut] = clip->mediaRange();
        html += row(tr("In"), formatTimecode(mediaIn, rate));
        html += row(tr("Out"), formatTimecode(mediaOut, rate));
        html += row(tr("Duration"), formatSeconds(clip->duration()));
        html += row(tr("Timeline position"), formatTimecode(clip->timelineStart, m_project->timeline()->frameRate()));
        if (info.video && media->hasKeyframes() && !clip->hasSpeedChange()) {
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

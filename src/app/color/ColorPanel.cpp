#include "ColorPanel.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "color/AutoColor.h"
#include "color/ColorProcessor.h"
#include "color/LookGallery.h"
#include "color/ColorWheelWidget.h"
#include "color/CurvesWidget.h"
#include "color/Lut.h"
#include "color/SliderRow.h"

namespace lumora {

namespace {
const QString kRecentKey = QStringLiteral("color/recentLuts");
constexpr int kMaxRecent = 10;
enum LutItem { NoLut = 0, Browse = 1 };
} // namespace

QStringList ColorPanel::recentLuts() {
    QStringList list = QSettings().value(kRecentKey).toStringList();
    list.erase(std::remove_if(list.begin(), list.end(), [](const QString& p) { return !QFileInfo::exists(p); }),
               list.end());
    return list;
}

void ColorPanel::addRecentLut(const QString& path) {
    QStringList list = QSettings().value(kRecentKey).toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecent)
        list.removeLast();
    QSettings().setValue(kRecentKey, list);
}

ColorPanel::ColorPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    auto* top = new QHBoxLayout;
    m_header = new QLabel;
    m_header->setWordWrap(true);
    QFont hf = m_header->font();
    hf.setBold(true);
    m_header->setFont(hf);
    m_reset = new QToolButton;
    m_reset->setIcon(Icons::reset());
    m_reset->setText(tr("Reset colour"));
    m_reset->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_reset->setToolTip(tr("Reset colour: set every colour setting back to neutral"));
    top->addWidget(m_header, 1);
    top->addWidget(m_reset);
    outer->addLayout(top);

    m_body = new QWidget;
    auto* rl = new QVBoxLayout(m_body);
    rl->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_body);

    auto* filters = new QGroupBox(tr("Filters"));
    auto* fl = new QVBoxLayout(filters);
    m_auto = new QPushButton(Icons::smart(), tr("Auto colour and light"));
    m_auto->setToolTip(tr("Measures the picture and sets exposure, contrast and white balance so it looks natural"));
    fl->addWidget(m_auto);
    m_looks = new LookGallery;
    m_looks->setToolTip(tr("Ready-made looks; all settings below can still be changed"));
    fl->addWidget(m_looks);
    m_lookAmount = new SliderRow(tr("Filter strength (%)"), 0, 100, 1, 0);
    m_lookAmount->setDefault(100);
    fl->addWidget(m_lookAmount);
    rl->addWidget(filters);

    auto* light = new QGroupBox(tr("Light"));
    auto* lightL = new QVBoxLayout(light);
    m_exposure = new SliderRow(tr("Exposure (EV)"), -5, 5, 0.01, 2);
    m_contrast = new SliderRow(tr("Contrast"), -100, 100, 1, 0);
    m_highlights = new SliderRow(tr("Highlights"), -100, 100, 1, 0);
    m_shadows = new SliderRow(tr("Shadows"), -100, 100, 1, 0);
    m_whites = new SliderRow(tr("Whites"), -100, 100, 1, 0);
    m_blacks = new SliderRow(tr("Blacks"), -100, 100, 1, 0);
    for (auto* s : {m_exposure, m_contrast, m_highlights, m_shadows, m_whites, m_blacks})
        lightL->addWidget(s);
    rl->addWidget(light);

    auto* wb = new QGroupBox(tr("White balance"));
    auto* wbL = new QVBoxLayout(wb);
    m_temperature = new SliderRow(tr("Temperature"), -100, 100, 1, 0);
    m_tint = new SliderRow(tr("Tint"), -100, 100, 1, 0);
    wbL->addWidget(m_temperature);
    wbL->addWidget(m_tint);
    rl->addWidget(wb);

    auto* col = new QGroupBox(tr("Colour"));
    auto* colL = new QVBoxLayout(col);
    m_saturation = new SliderRow(tr("Saturation"), -100, 100, 1, 0);
    m_vibrance = new SliderRow(tr("Vibrance"), -100, 100, 1, 0);
    m_vibrance->setToolTip(tr("Like saturation, but leaves colours that are already strong (and skin tones) mostly alone"));
    colL->addWidget(m_saturation);
    colL->addWidget(m_vibrance);
    rl->addWidget(col);

    auto* wheels = new QGroupBox(tr("Colour wheels"));
    wheels->setToolTip(tr("Lift moves the shadows, gamma the mid tones, gain the highlights. Drag a wheel towards a "
                          "colour; the slider below makes the range brighter or darker."));
    auto* wl = new QHBoxLayout(wheels);
    wl->setSpacing(6);
    m_lift = new ColorWheelWidget(tr("Lift"));

    m_gamma = new ColorWheelWidget(tr("Gamma"));

    m_gain = new ColorWheelWidget(tr("Gain"));

    wl->addWidget(m_lift);
    wl->addWidget(m_gamma);
    wl->addWidget(m_gain);
    rl->addWidget(wheels);

    auto* curves = new QGroupBox(tr("Curves"));
    auto* cl = new QVBoxLayout(curves);
    m_curves = new CurvesWidget;
    cl->addWidget(m_curves);
    rl->addWidget(curves);

    auto* hsl = new QGroupBox(tr("HSL mixer"));
    hsl->setToolTip(tr("Change one colour range at a time: its hue, how strong it is and how bright"));
    auto* hl = new QVBoxLayout(hsl);
    auto* bands = new QHBoxLayout;
    bands->setSpacing(3);
    m_hslBands = new QButtonGroup(this);
    const QString bandNames[kHslBands] = {tr("Red"), tr("Orange"), tr("Yellow"), tr("Green"),
                                          tr("Aqua"), tr("Blue"), tr("Purple"), tr("Magenta")};
    for (int i = 0; i < kHslBands; ++i) {
        auto* b = new QToolButton;
        b->setCheckable(true);
        b->setToolTip(bandNames[i]);
        b->setAccessibleName(bandNames[i]);
        b->setFixedSize(22, 22);
        const QColor c = QColor::fromHsvF(hslBandHue(i) / 360.0, 0.75, 0.9);
        b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 2px solid transparent; border-radius: 11px; }"
                                        "QToolButton:checked { border-color: palette(text); }")
                             .arg(c.name()));
        m_hslBands->addButton(b, i);
        bands->addWidget(b);
    }
    bands->addStretch();
    m_hslBands->button(0)->setChecked(true);
    hl->addLayout(bands);
    m_hslHue = new SliderRow(tr("Hue"), -100, 100, 1, 0);
    m_hslSat = new SliderRow(tr("Saturation"), -100, 100, 1, 0);
    m_hslLum = new SliderRow(tr("Luminance"), -100, 100, 1, 0);
    hl->addWidget(m_hslHue);
    hl->addWidget(m_hslSat);
    hl->addWidget(m_hslLum);
    rl->addWidget(hsl);

    auto* lutBox = new QGroupBox(tr("LUT"));
    auto* ll = new QGridLayout(lutBox);
    m_lut = new QComboBox;
    m_lut->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_lut->setMinimumContentsLength(12);
    m_lut->setToolTip(tr("A .cube look-up table (3D or 1D), applied after the settings above"));
    m_lutStatus = new QLabel;
    m_lutStatus->setWordWrap(true);
    m_lutStatus->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    m_lutAmount = new SliderRow(tr("Amount (%)"), 0, 100, 1, 0);
    m_lutAmount->setDefault(100);
    m_saveLut = new QPushButton(Icons::exportFile(), tr("Save look as LUT…"));
    m_saveLut->setToolTip(tr("Write these colour settings as a .cube file, to use the same look on other clips, "
                             "photos or in other programs"));
    ll->addWidget(m_lut, 0, 0);
    ll->addWidget(m_lutStatus, 1, 0);
    ll->addWidget(m_lutAmount, 2, 0);
    ll->addWidget(m_saveLut, 3, 0);
    rl->addWidget(lutBox);
    outer->addStretch();

    auto bind = [this](SliderRow* s, double ColorGrade::*field, const QString& text, const QString& key) {
        connect(s, &SliderRow::valueChanged, this, [this, field, text, key](double v) {
            edit(text, key, [&](ColorGrade& g) { g.*field = v; });
        });
    };
    bind(m_exposure, &ColorGrade::exposure, tr("Exposure"), QStringLiteral("exposure"));
    bind(m_contrast, &ColorGrade::contrast, tr("Contrast"), QStringLiteral("contrast"));
    bind(m_highlights, &ColorGrade::highlights, tr("Highlights"), QStringLiteral("highlights"));
    bind(m_shadows, &ColorGrade::shadows, tr("Shadows"), QStringLiteral("shadows"));
    bind(m_whites, &ColorGrade::whites, tr("Whites"), QStringLiteral("whites"));
    bind(m_blacks, &ColorGrade::blacks, tr("Blacks"), QStringLiteral("blacks"));
    bind(m_temperature, &ColorGrade::temperature, tr("Temperature"), QStringLiteral("temperature"));
    bind(m_tint, &ColorGrade::tint, tr("Tint"), QStringLiteral("tint"));
    bind(m_saturation, &ColorGrade::saturation, tr("Saturation"), QStringLiteral("saturation"));
    bind(m_vibrance, &ColorGrade::vibrance, tr("Vibrance"), QStringLiteral("vibrance"));
    auto bindWheel = [this](ColorWheelWidget* w, ColorWheel ColorGrade::*field, const QString& text, const QString& key) {
        connect(w, &ColorWheelWidget::valueChanged, this, [this, field, text, key](const ColorWheel& v) {
            edit(text, key, [&](ColorGrade& g) { g.*field = v; });
        });
    };
    bindWheel(m_lift, &ColorGrade::lift, tr("Lift"), QStringLiteral("lift"));
    bindWheel(m_gamma, &ColorGrade::gamma, tr("Gamma"), QStringLiteral("gamma"));
    bindWheel(m_gain, &ColorGrade::gain, tr("Gain"), QStringLiteral("gain"));
    connect(m_curves, &CurvesWidget::curveEdited, this, [this](int channel, const ToneCurve& c) {
        edit(tr("Curve"), QStringLiteral("curve%1").arg(channel), [&](ColorGrade& g) {
            ToneCurve* target[4] = {&g.curveMaster, &g.curveRed, &g.curveGreen, &g.curveBlue};
            *target[channel] = c;
        });
    });
    connect(m_hslBands, &QButtonGroup::idClicked, this, &ColorPanel::showHslBand);
    auto bindHsl = [this](SliderRow* s, double HslBand::*field, const QString& key) {
        connect(s, &SliderRow::valueChanged, this, [this, field, key](double v) {
            const int band = m_hslBand;
            edit(tr("HSL"), key + QString::number(band), [&](ColorGrade& g) { g.hsl[band].*field = v; });
        });
    };
    bindHsl(m_hslHue, &HslBand::hue, QStringLiteral("hslHue"));
    bindHsl(m_hslSat, &HslBand::saturation, QStringLiteral("hslSat"));
    bindHsl(m_hslLum, &HslBand::luminance, QStringLiteral("hslLum"));
    connect(m_lutAmount, &SliderRow::valueChanged, this, [this](double v) {
        edit(tr("LUT amount"), QStringLiteral("lutAmount"), [&](ColorGrade& g) { g.lutAmount = v / 100.0; });
    });
    connect(m_lut, &QComboBox::activated, this, [this](int index) {
        const QVariant data = m_lut->itemData(index);
        if (data.typeId() == QMetaType::Int && data.toInt() == Browse) {
            chooseLut();
            return;
        }
        const QString path = data.typeId() == QMetaType::QString ? data.toString() : QString();
        if (!path.isEmpty())
            addRecentLut(path);
        edit(path.isEmpty() ? tr("Remove LUT") : tr("Apply LUT"), QString(), [&](ColorGrade& g) {
            g.lutPath = path;
            if (g.lutAmount <= 0.0)
                g.lutAmount = 1.0;
        });
    });
    connect(m_saveLut, &QPushButton::clicked, this, &ColorPanel::saveLookAsLut);
    connect(m_looks, &LookGallery::lookChosen, this, [this](const QString& id) {
        edit(id.isEmpty() ? tr("Remove filter") : tr("Apply filter"), QString(), [&](ColorGrade& g) {
            g.look = id;
            if (g.lookAmount <= 0.0)
                g.lookAmount = 1.0;
        });
    });
    connect(m_lookAmount, &SliderRow::valueChanged, this, [this](double v) {
        edit(tr("Filter strength"), QStringLiteral("lookAmount"), [&](ColorGrade& g) { g.lookAmount = v / 100.0; });
    });
    connect(m_auto, &QPushButton::clicked, this, [this] {
        edit(tr("Auto colour and light"), QString(), [&](ColorGrade& g) { g = autoCorrect(m_preview, g); });
    });
    connect(m_reset, &QToolButton::clicked, this, [this] {
        edit(tr("Reset colour"), QString(), [](ColorGrade& g) { g = ColorGrade{}; });
    });
    setGrade({});
}

void ColorPanel::setHeader(const QString& text) {
    m_header->setText(text);
    m_header->setVisible(!text.isEmpty());
}

void ColorPanel::setPreviewImage(const QImage& image) {
    m_preview = image;
    m_looks->setPreviewImage(image);
    m_auto->setEnabled(!image.isNull());
}

void ColorPanel::setEditable(bool on) {
    m_body->setEnabled(on);
    m_reset->setEnabled(on);
}

void ColorPanel::edit(const QString& text, const QString& field, const std::function<void(ColorGrade&)>& change) {
    if (m_syncing)
        return;
    ColorGrade g = m_grade;
    change(g);
    if (g == m_grade)
        return;
    m_grade = g;
    emit gradeEdited(g, text, field);
}

void ColorPanel::setGrade(const ColorGrade& g) {
    m_syncing = true;
    m_grade = g;
    m_exposure->setValue(g.exposure);
    m_contrast->setValue(g.contrast);
    m_highlights->setValue(g.highlights);
    m_shadows->setValue(g.shadows);
    m_whites->setValue(g.whites);
    m_blacks->setValue(g.blacks);
    m_temperature->setValue(g.temperature);
    m_tint->setValue(g.tint);
    m_saturation->setValue(g.saturation);
    m_vibrance->setValue(g.vibrance);
    m_lift->setValue(g.lift);
    m_gamma->setValue(g.gamma);
    m_gain->setValue(g.gain);
    m_curves->setCurves(g.curveMaster, g.curveRed, g.curveGreen, g.curveBlue);
    showHslBand(m_hslBand);
    m_lutAmount->setValue(g.lutAmount * 100.0);
    m_lutAmount->setEnabled(!g.lutPath.isEmpty());
    m_looks->setCurrentLook(g.look);
    m_lookAmount->setValue(g.lookAmount * 100.0);
    m_lookAmount->setEnabled(!g.look.isEmpty());
    refreshLutCombo();
    m_syncing = false;
}

void ColorPanel::showHslBand(int band) {
    const bool wasSyncing = m_syncing;
    m_syncing = true;
    m_hslBand = band;
    const HslBand& b = m_grade.hsl[band];
    m_hslHue->setValue(b.hue);
    m_hslSat->setValue(b.saturation);
    m_hslLum->setValue(b.luminance);
    // A dot under the ranges that are changed.
    for (int i = 0; i < kHslBands; ++i)
        m_hslBands->button(i)->setText(m_grade.hsl[i].isIdentity() ? QString() : QStringLiteral("•"));
    m_syncing = wasSyncing;
}

void ColorPanel::refreshLutCombo() {
    const QSignalBlocker b(m_lut);
    m_lut->clear();
    m_lut->addItem(tr("No LUT"), static_cast<int>(NoLut));
    QStringList paths = recentLuts();
    if (!m_grade.lutPath.isEmpty() && !paths.contains(m_grade.lutPath))
        paths.prepend(m_grade.lutPath);
    for (const QString& p : paths) {
        m_lut->addItem(QFileInfo(p).completeBaseName(), p);
        m_lut->setItemData(m_lut->count() - 1, QDir::toNativeSeparators(p), Qt::ToolTipRole);
    }
    m_lut->insertSeparator(m_lut->count());
    m_lut->addItem(tr("Browse…"), static_cast<int>(Browse));
    m_lut->setCurrentIndex(m_grade.lutPath.isEmpty() ? 0 : m_lut->findData(m_grade.lutPath));

    if (m_grade.lutPath.isEmpty()) {
        m_lutStatus->clear();
        m_lutStatus->hide();
        return;
    }
    QString err;
    const auto lut = luts::get(m_grade.lutPath, &err);
    if (!lut)
        m_lutStatus->setText(tr("Cannot read this LUT: %1").arg(err));
    else
        m_lutStatus->setText(lut->is3D() ? tr("%1 · 3D, %2 points per axis").arg(lut->title()).arg(lut->size())
                                         : tr("%1 · 1D, %2 points").arg(lut->title()).arg(lut->size()));
    m_lutStatus->show();
}

void ColorPanel::chooseLut() {
    QSettings s;
    const QString dir = s.value(QStringLiteral("dirs/luts")).toString();
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Choose a LUT"), dir, tr("Cube LUT (*.cube);;All files (*)"));
    if (path.isEmpty()) {
        refreshLutCombo();
        return;
    }
    s.setValue(QStringLiteral("dirs/luts"), QFileInfo(path).absolutePath());
    QString err;
    if (!luts::get(path, &err)) {
        QMessageBox::warning(this, tr("LUT"), tr("Cannot read %1:\n%2").arg(QDir::toNativeSeparators(path), err));
        refreshLutCombo();
        return;
    }
    addRecentLut(path);
    edit(tr("Apply LUT"), QString(), [&](ColorGrade& g) {
        g.lutPath = path;
        if (g.lutAmount <= 0.0)
            g.lutAmount = 1.0;
    });
    refreshLutCombo();
}

void ColorPanel::saveLookAsLut() {
    QSettings s;
    const QString dir = s.value(QStringLiteral("dirs/luts")).toString();
    QString path = QFileDialog::getSaveFileName(this, tr("Save look as LUT"),
                                                QDir(dir).filePath(tr("My look") + QStringLiteral(".cube")),
                                                tr("Cube LUT (*.cube)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".cube"), Qt::CaseInsensitive))
        path += QStringLiteral(".cube");
    s.setValue(QStringLiteral("dirs/luts"), QFileInfo(path).absolutePath());
    QString err;
    // 33 points per axis is the size most programs and cameras expect.
    if (!ColorProcessor(m_grade).bake(33, QFileInfo(path).completeBaseName()).save(path, &err)) {
        QMessageBox::warning(this, tr("Save look as LUT"), tr("Cannot write %1:\n%2").arg(QDir::toNativeSeparators(path), err));
        return;
    }
    addRecentLut(path);
    refreshLutCombo();
}

} // namespace lumora

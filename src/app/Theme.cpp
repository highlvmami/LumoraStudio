#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHash>
#include <QPalette>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

namespace lumora {

namespace {

ThemeKind g_kind = ThemeKind::Dark;
ThemeColors g_colors;

// "Night": blue-violet tinted surfaces with the warm sunset colours of the logo.
ThemeColors nightColors() {
    ThemeColors c;
    c.canvas = QColor(0x0b, 0x0c, 0x13);
    c.panel = QColor(0x14, 0x16, 0x21);
    c.raised = QColor(0x1d, 0x20, 0x30);
    c.hover = QColor(0x29, 0x2d, 0x42);
    c.border = QColor(0x26, 0x2a, 0x3d);
    c.text = QColor(0xe8, 0xe9, 0xf3);
    c.textDim = QColor(0x95, 0x9a, 0xb6);
    c.textFaint = QColor(0x5d, 0x62, 0x7e);
    c.accent = QColor(0xff, 0xb4, 0x54);
    c.accentAlt = QColor(0xf0, 0x56, 0x8c);
    c.accentCool = QColor(0x7c, 0x7e, 0xf6);
    c.onAccent = QColor(0x1c, 0x12, 0x08);
    c.videoClip = QColor(0x58, 0x5f, 0xe0);
    c.audioClip = QColor(0x1f, 0xae, 0x95);
    c.titleClip = QColor(0xcf, 0x55, 0x9a);
    c.playhead = c.accent;
    c.success = QColor(0x4c, 0xd6, 0x9b);
    c.warning = QColor(0xff, 0xc8, 0x57);
    c.danger = QColor(0xff, 0x5c, 0x7a);
    c.viewer = QColor(0x07, 0x08, 0x0d);
    return c;
}

// "Day": warm paper surfaces, the same brand colours a little deeper.
ThemeColors dayColors() {
    ThemeColors c;
    c.canvas = QColor(0xe4, 0xe1, 0xda);
    c.panel = QColor(0xf7, 0xf5, 0xf1);
    c.raised = QColor(0xff, 0xff, 0xff);
    c.hover = QColor(0xec, 0xe9, 0xe2);
    c.border = QColor(0xd8, 0xd4, 0xcb);
    c.text = QColor(0x1c, 0x1d, 0x29);
    c.textDim = QColor(0x5d, 0x60, 0x76);
    c.textFaint = QColor(0x93, 0x95, 0xa8);
    c.accent = QColor(0xe8, 0x86, 0x0c);
    c.accentAlt = QColor(0xd9, 0x3b, 0x78);
    c.accentCool = QColor(0x54, 0x57, 0xe0);
    c.onAccent = QColor(0xff, 0xff, 0xff);
    c.videoClip = QColor(0x5a, 0x61, 0xe2);
    c.audioClip = QColor(0x1c, 0x9e, 0x88);
    c.titleClip = QColor(0xc8, 0x4c, 0x90);
    c.playhead = c.accent;
    c.success = QColor(0x1f, 0x9d, 0x6b);
    c.warning = QColor(0xb7, 0x7d, 0x00);
    c.danger = QColor(0xd9, 0x34, 0x4f);
    c.viewer = QColor(0x1b, 0x1c, 0x25);
    return c;
}

QString rgba(const QColor& c, double alpha) {
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// The style sheet uses @name for the colours above, plus a few derived ones.
const char* const kStyleSheet = R"(
QMainWindow, QMainWindow > QWidget#centralHost { background: @canvas; }
QMainWindow::separator { background: @canvas; width: 6px; height: 6px; }
QMainWindow::separator:hover { background: @accentSoft; }
QToolTip { background: @raised; color: @text; border: 1px solid @border; border-radius: 6px; padding: 4px 8px; }

/* ---- Top bar ---- */
QMenuBar { background: @canvas; color: @textDim; padding: 7px 6px 7px 6px; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: @raised; color: @text; }
QMenuBar::item:pressed { background: @hover; color: @text; }
QMenu { background: @raised; color: @text; border: 1px solid @border; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: @accentSoft; color: @text; }
QMenu::item:disabled { color: @textFaint; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QMenu::indicator { width: 14px; height: 14px; margin-left: 6px; }

QToolBar { background: transparent; border: none; spacing: 2px; }
QToolBar#workspaceToolBar { background: @canvas; padding: 6px 10px; }
QWidget#workspaceBar QToolButton { padding: 5px; border-radius: 8px; border: none; }
QWidget#workspaceBar QToolButton:hover { background: @raised; }
QWidget#workspaceBar QToolButton#primaryAction {
    color: @onAccent; font-weight: 600; padding: 6px 16px; border-radius: 16px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent, stop:1 @accentAlt); }
QWidget#workspaceBar QToolButton#primaryAction:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accentHi, stop:1 @accentAltHi); }
QWidget#workspaceBar QToolButton#primaryAction:disabled { background: @raised; color: @textFaint; }
QLabel#brandMark { color: @text; font-size: 15px; }

QFrame#workspaceSwitcher { background: @panel; border: 1px solid @border; border-radius: 12px; }
QTabBar#workspaceTabs { background: transparent; }
QTabBar#workspaceTabs::tab {
    background: transparent; color: @textDim; font-weight: 600;
    padding: 5px 18px; margin: 3px 2px; border-radius: 9px; border: none; }
QTabBar#workspaceTabs::tab:hover { background: @raised; color: @text; }
QTabBar#workspaceTabs::tab:selected {
    color: @onAccent;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent, stop:1 @accentAlt); }
QTabBar#workspaceTabs::tab:disabled { color: @textFaint; }

/* ---- Panels ---- */
QWidget#panelSurface { background: @panel; border: 1px solid @border; border-radius: 12px; }
QDockWidget { color: @textDim; }
QWidget#dockTitle { background: transparent; }
QLabel#dockTitleText { color: @textDim; font-size: 11px; font-weight: 600; }
QToolButton#dockTitleButton { background: transparent; border: none; border-radius: 6px; padding: 2px; }
QToolButton#dockTitleButton:hover { background: @raised; }

QStatusBar { background: @canvas; color: @textFaint; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @textFaint; padding: 0 8px; }

QSplitter::handle { background: @canvas; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }
QSplitter::handle:hover { background: @accentSoft; }

QTabBar::tab { background: transparent; color: @textDim; padding: 5px 12px; border: none;
               border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }
QTabWidget::pane { border: 1px solid @border; border-radius: 8px; top: -1px; }

/* ---- Properties panel ---- */
QTabBar#propertyTabs { background: transparent; }
QTabBar#propertyTabs::tab { background: transparent; color: @textDim; padding: 5px 7px; margin: 2px 1px;
    border: 1px solid transparent; border-radius: 8px; font-weight: 600; }
QTabBar#propertyTabs::tab:hover { background: @raised; color: @text; }
QTabBar#propertyTabs::tab:selected { background: @accentSoft; border-color: @accentLine; color: @text; }
QWidget#propertySection { background: @groupFill; border-radius: 10px; }
QToolButton#sectionHeader { font-weight: 600; text-align: left; border: none; border-radius: 8px; padding: 6px 4px; }
QToolButton#sectionHeader:hover { background: @raised; }
QToolButton#sectionHeader:checked { background: transparent; border: none; }
QToolButton#sectionReset { border: none; padding: 2px; }
QLabel#sectionDot { color: @accent; font-size: 16px; }
QLabel#propertyHint { color: @textFaint; }
QFrame#propertyHeader { background: @raised; border: 1px solid @border; border-radius: 10px; }

QGroupBox { font-weight: 600; margin-top: 20px; padding-top: 4px; border: none; border-radius: 10px; background: @groupFill; }
QGroupBox::title { subcontrol-origin: margin; left: 4px; padding: 0 4px 2px 4px; color: @text; }

QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
QAbstractScrollArea { border: none; }
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: @border; }

/* ---- Lists ---- */
QListView, QTreeView, QTableView { background: transparent; color: @text; border: none; outline: none;
    selection-background-color: @accentSoft; selection-color: @text; }
QListView::item { border-radius: 8px; padding: 2px; color: @text; }
QListView::item:hover { background: @raised; }
QListView::item:selected { background: @accentSoft; color: @text; }
QTextBrowser, QTextEdit, QPlainTextEdit { background: @raised; color: @text; border: 1px solid @border;
    border-radius: 8px; selection-background-color: @accent; selection-color: @onAccent; }

/* ---- Buttons ---- */
QPushButton { background: @raised; color: @text; border: 1px solid @border; border-radius: 8px; padding: 5px 14px; }
QPushButton:hover { background: @hover; }
QPushButton:pressed { background: @border; }
QPushButton:disabled { color: @textFaint; background: @panel; }
QPushButton[primary="true"] { color: @onAccent; border: none; font-weight: 600;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent, stop:1 @accentAlt); }
QPushButton[primary="true"]:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accentHi, stop:1 @accentAltHi); }
QPushButton[primary="true"]:disabled { background: @raised; color: @textFaint; }

QCheckBox, QRadioButton { spacing: 8px; color: @text; }
QCheckBox:disabled, QRadioButton:disabled { color: @textFaint; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; border: 1.5px solid @textFaint; background: @raised; }
QCheckBox::indicator { border-radius: 5px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { border-color: @accent; background: @accent; image: url(@check); }
QRadioButton::indicator:checked { border-color: @accent;
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,
        stop:0 @onAccent, stop:0.32 @onAccent, stop:0.42 @accent, stop:1 @accent); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: @border; background: @panel; }

QToolButton { color: @text; background: transparent; border: 1px solid transparent; border-radius: 7px; padding: 3px; }
QToolButton:hover { background: @raised; border-color: @border; }
QToolButton:pressed { background: @hover; }
QToolButton:checked { background: @accentSoft; border-color: @accentLine; color: @text; }
QToolButton:disabled { color: @textFaint; }
QToolButton#playButton { background: @accent; border: none; border-radius: 17px; padding: 6px; }
QToolButton#playButton:hover { background: @accentHi; }
QToolButton#monitorMode { color: @textDim; padding: 4px 12px; border-radius: 8px; font-weight: 600; }
QToolButton#monitorMode:checked { background: @raised; color: @text; border-color: @border; }
QLabel#timecode { color: @text; background: @canvas; border: 1px solid @border; border-radius: 8px; padding: 3px 10px; }

/* ---- Fields ---- */
QLineEdit, QAbstractSpinBox, QComboBox {
    background: @raised; color: @text; border: 1px solid @border; border-radius: 7px;
    padding: 3px 8px; min-height: 18px; selection-background-color: @accent; selection-color: @onAccent; }
QLineEdit:hover, QAbstractSpinBox:hover, QComboBox:hover { border-color: @hover; }
QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: @accent; }
QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled { color: @textFaint; background: @panel; }
QAbstractSpinBox { padding-right: 20px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    subcontrol-origin: border; width: 18px; border: none; background: transparent; }
QAbstractSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 7px; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 7px; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @hover; }
QAbstractSpinBox::up-arrow { image: url(@arrowUp); width: 8px; height: 8px; }
QAbstractSpinBox::down-arrow { image: url(@arrowDown); width: 8px; height: 8px; }
QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::up-arrow:off { image: url(@arrowUpOff); }
QAbstractSpinBox::down-arrow:disabled, QAbstractSpinBox::down-arrow:off { image: url(@arrowDownOff); }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 22px; border: none; }
QComboBox::down-arrow { image: url(@arrowDown); width: 9px; height: 9px; }
QComboBox::down-arrow:disabled { image: url(@arrowDownOff); }
QComboBox QAbstractItemView { background: @raised; color: @text; border: 1px solid @border; border-radius: 8px;
    padding: 4px; outline: none; selection-background-color: @accentSoft; selection-color: @text; }

/* ---- Sliders ---- */
QSlider::groove:horizontal { height: 4px; background: @hover; border-radius: 2px; }
QSlider::sub-page:horizontal { border-radius: 2px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent, stop:1 @accentAlt); }
QSlider::sub-page:horizontal:disabled { background: @border; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -6px 0; border-radius: 8px;
    background: @handle; border: 2px solid @accent; }
QSlider::handle:horizontal:hover { background: @accent; }
QSlider::handle:horizontal:disabled { border-color: @border; background: @panel; }

/* ---- Scroll bars ---- */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: @hover; border-radius: 3px; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::handle:hover { background: @textFaint; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QProgressBar { background: @raised; border: 1px solid @border; border-radius: 7px; text-align: center; color: @text; min-height: 14px; }
QProgressBar::chunk { border-radius: 6px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @accent, stop:1 @accentAlt); }

QDialog { background: @panel; }
QDialogButtonBox QPushButton { min-width: 80px; }
)";

QString arrowFile(const QString& name) { return QStringLiteral(":/theme/%1.png").arg(name); }

QString buildStyleSheet(const ThemeColors& c, bool dark) {
    QHash<QString, QString> v{
        {QStringLiteral("canvas"), c.canvas.name()},
        {QStringLiteral("panel"), c.panel.name()},
        {QStringLiteral("raised"), c.raised.name()},
        {QStringLiteral("hover"), c.hover.name()},
        {QStringLiteral("border"), c.border.name()},
        {QStringLiteral("text"), c.text.name()},
        {QStringLiteral("textDim"), c.textDim.name()},
        {QStringLiteral("textFaint"), c.textFaint.name()},
        {QStringLiteral("accent"), c.accent.name()},
        {QStringLiteral("accentAlt"), c.accentAlt.name()},
        {QStringLiteral("accentHi"), c.accent.lighter(112).name()},
        {QStringLiteral("accentAltHi"), c.accentAlt.lighter(112).name()},
        {QStringLiteral("accentSoft"), rgba(c.accent, dark ? 0.16 : 0.20)},
        {QStringLiteral("accentLine"), rgba(c.accent, 0.55)},
        {QStringLiteral("onAccent"), c.onAccent.name()},
        {QStringLiteral("groupFill"), rgba(c.raised, dark ? 0.45 : 0.6)},
        {QStringLiteral("check"), arrowFile(dark ? QStringLiteral("check-dark") : QStringLiteral("check-light"))},
        {QStringLiteral("handle"), dark ? c.text.name() : QStringLiteral("#ffffff")},
        {QStringLiteral("arrowUp"), arrowFile(dark ? QStringLiteral("up-dark") : QStringLiteral("up-light"))},
        {QStringLiteral("arrowDown"), arrowFile(dark ? QStringLiteral("down-dark") : QStringLiteral("down-light"))},
        {QStringLiteral("arrowUpOff"), arrowFile(QStringLiteral("up-off"))},
        {QStringLiteral("arrowDownOff"), arrowFile(QStringLiteral("down-off"))},
    };
    QString css = QString::fromUtf8(kStyleSheet);
    static const QRegularExpression token(QStringLiteral("@([A-Za-z]+)"));
    QString out;
    out.reserve(css.size() + 1024);
    qsizetype last = 0;
    for (auto it = token.globalMatch(css); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(css).mid(last, m.capturedStart() - last);
        out += v.value(m.captured(1), m.captured(0));
        last = m.capturedEnd();
    }
    out += QStringView(css).mid(last);
    return out;
}

void loadFonts() {
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    for (const char* f : {"Regular", "Medium", "SemiBold", "Bold"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Inter-%1.ttf").arg(QLatin1String(f)));
}

} // namespace

void applyTheme(ThemeKind kind) {
    g_kind = kind;
    g_colors = kind == ThemeKind::Dark ? nightColors() : dayColors();
    const ThemeColors& c = g_colors;
    const bool dark = kind == ThemeKind::Dark;

    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    loadFonts();
    QFont font(QStringLiteral("Inter"));
    font.setPointSizeF(9.5);
    font.setHintingPreference(QFont::PreferNoHinting);
    QApplication::setFont(font);

    QPalette p;
    p.setColor(QPalette::Window, c.panel);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.raised);
    p.setColor(QPalette::AlternateBase, c.hover);
    p.setColor(QPalette::ToolTipBase, c.raised);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.textFaint);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.raised);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, dark ? QColor(255, 255, 255) : c.text);
    p.setColor(QPalette::Light, c.hover.lighter(115));
    p.setColor(QPalette::Midlight, c.hover);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.canvas);
    p.setColor(QPalette::Shadow, QColor(0, 0, 0));
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.onAccent);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::LinkVisited, c.accentAlt);
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.textFaint);
    p.setColor(QPalette::Disabled, QPalette::Highlight, c.border);
    QApplication::setPalette(p);
    qApp->setStyleSheet(buildStyleSheet(c, dark));
}

ThemeKind themeFromString(const QString& s) { return s == QLatin1String("light") ? ThemeKind::Light : ThemeKind::Dark; }
QString themeToString(ThemeKind k) { return k == ThemeKind::Light ? QStringLiteral("light") : QStringLiteral("dark"); }

ThemeKind currentTheme() { return g_kind; }

const ThemeColors& themeColors() {
    if (!g_colors.panel.isValid())
        g_colors = nightColors();
    return g_colors;
}

void markPrimary(QWidget* button) {
    button->setProperty("primary", true);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void makePanelSurface(QWidget* w) {
    w->setObjectName(QStringLiteral("panelSurface"));
    w->setAttribute(Qt::WA_StyledBackground, true);
}

} // namespace lumora

#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace lumora {

void applyTheme(ThemeKind kind) {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    const QColor accent(236, 72, 153);
    if (kind == ThemeKind::Dark) {
        p.setColor(QPalette::Window, QColor(37, 37, 41));
        p.setColor(QPalette::WindowText, QColor(225, 225, 230));
        p.setColor(QPalette::Base, QColor(27, 27, 30));
        p.setColor(QPalette::AlternateBase, QColor(32, 32, 36));
        p.setColor(QPalette::ToolTipBase, QColor(50, 50, 56));
        p.setColor(QPalette::ToolTipText, QColor(235, 235, 240));
        p.setColor(QPalette::PlaceholderText, QColor(139, 139, 149));
        p.setColor(QPalette::Text, QColor(225, 225, 230));
        p.setColor(QPalette::Button, QColor(48, 48, 54));
        p.setColor(QPalette::ButtonText, QColor(225, 225, 230));
        p.setColor(QPalette::BrightText, QColor(255, 255, 255));
        p.setColor(QPalette::Light, QColor(70, 70, 78));
        p.setColor(QPalette::Midlight, QColor(58, 58, 64));
        p.setColor(QPalette::Mid, QColor(64, 64, 72));
        p.setColor(QPalette::Dark, QColor(20, 20, 22));
        p.setColor(QPalette::Shadow, QColor(0, 0, 0));
        p.setColor(QPalette::Highlight, accent.darker(115));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(244, 114, 182));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 110, 118));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(110, 110, 118));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 110, 118));
    } else {
        p = QApplication::style()->standardPalette();
        p.setColor(QPalette::Window, QColor(243, 243, 246));
        p.setColor(QPalette::Base, QColor(255, 255, 255));
        p.setColor(QPalette::AlternateBase, QColor(234, 234, 239));
        p.setColor(QPalette::PlaceholderText, QColor(110, 110, 120));
        p.setColor(QPalette::Highlight, accent);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::BrightText, QColor(30, 30, 35));
    }
    QApplication::setPalette(p);
    qApp->setStyleSheet(QStringLiteral(
        "QDockWidget::title { padding: 4px 8px; }"
        "QTabBar#workspaceTabs { font-weight: 600; }"
        "QTabBar#workspaceTabs::tab { padding: 6px 18px; }"
        "QGroupBox { font-weight: 600; margin-top: 14px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; padding: 0 4px; }"));
}

ThemeKind themeFromString(const QString& s) { return s == QLatin1String("light") ? ThemeKind::Light : ThemeKind::Dark; }
QString themeToString(ThemeKind k) { return k == ThemeKind::Light ? QStringLiteral("light") : QStringLiteral("dark"); }

} // namespace lumora

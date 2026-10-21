#include "panels/HelpPage.h"

#include "Theme.h"

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace lumora {

namespace {

constexpr int kIdRole = Qt::UserRole;

} // namespace

HelpPage::HelpPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* reading = new QWidget;
    makePanelSurface(reading);
    auto* readingLayout = new QVBoxLayout(reading);
    readingLayout->setContentsMargins(4, 4, 4, 4);
    m_page = new QTextBrowser;
    m_page->setObjectName(QStringLiteral("helpPage"));
    m_page->setOpenLinks(false);
    m_page->document()->setDocumentMargin(28);
    connect(m_page, &QTextBrowser::anchorClicked, this, &HelpPage::openLink);
    readingLayout->addWidget(m_page);
    layout->addWidget(reading, 1);

    auto* side = new QWidget;
    makePanelSurface(side);
    side->setFixedWidth(250);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(10, 12, 10, 10);
    sideLayout->setSpacing(8);
    auto* heading = new QLabel(tr("Sections"));
    heading->setObjectName(QStringLiteral("dockTitleText"));
    sideLayout->addWidget(heading);
    m_menu = new QListWidget;
    m_menu->setObjectName(QStringLiteral("helpMenu"));
    m_menu->setSpacing(1);
    m_menu->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (const QString& id : sectionIds()) {
        auto* item = new QListWidgetItem(sectionTitle(id), m_menu);
        item->setData(kIdRole, id);
        item->setSizeHint(QSize(0, 32));
    }
    connect(m_menu, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item)
            showSection(item->data(kIdRole).toString());
    });
    sideLayout->addWidget(m_menu, 1);
    layout->addWidget(side);

    applyStyle();
    showWelcome();
}

QStringList HelpPage::sectionIds() {
    return {QStringLiteral("start"),  QStringLiteral("edit"),      QStringLiteral("titles"), QStringLiteral("stickers"),
            QStringLiteral("subtitles"), QStringLiteral("smart"), QStringLiteral("color"),  QStringLiteral("audio"),
            QStringLiteral("photo"),  QStringLiteral("export"),    QStringLiteral("view"),   QStringLiteral("shortcuts")};
}

QString HelpPage::sectionTitle(const QString& id) const {
    if (id == QLatin1String("start"))
        return tr("Getting started");
    if (id == QLatin1String("edit"))
        return tr("Editing video");
    if (id == QLatin1String("titles"))
        return tr("Titles, transitions and fades");
    if (id == QLatin1String("stickers"))
        return tr("Stickers");
    if (id == QLatin1String("subtitles"))
        return tr("Subtitles");
    if (id == QLatin1String("smart"))
        return tr("Smart video tools");
    if (id == QLatin1String("color"))
        return tr("Colour");
    if (id == QLatin1String("audio"))
        return tr("Sound");
    if (id == QLatin1String("photo"))
        return tr("Photos");
    if (id == QLatin1String("export"))
        return tr("Exporting");
    if (id == QLatin1String("view"))
        return tr("Full screen, panels and settings");
    if (id == QLatin1String("shortcuts"))
        return tr("Keyboard shortcuts");
    return id;
}

QString HelpPage::currentSection() const {
    const QListWidgetItem* item = m_menu->currentItem();
    return item ? item->data(kIdRole).toString() : QString();
}

void HelpPage::setShortcutsProvider(std::function<QString()> provider) {
    m_shortcuts = std::move(provider);
}

QString HelpPage::pageHtml(const QString& id) const {
    if (id == QLatin1String("shortcuts"))
        return QStringLiteral("<h1>%1</h1><p>%2</p>%3")
            .arg(sectionTitle(id).toHtmlEscaped(),
                 tr("The same keys are listed in Help → Keyboard shortcuts. Shortcuts work while the main window has the focus.")
                     .toHtmlEscaped(),
                 m_shortcuts ? m_shortcuts() : QString());
    // The pages follow the interface language; a language without its own
    // pages falls back to English.
    //: Folder of the "How to use" pages for this language (resources/help/<code>).
    const QString lang = tr("en", "help language");
    for (const QString& l : {lang, QStringLiteral("en")}) {
        QFile f(QStringLiteral(":/help/%1/%2.html").arg(l, id));
        if (f.open(QIODevice::ReadOnly))
            return QString::fromUtf8(f.readAll());
    }
    return QStringLiteral("<h1>%1</h1>").arg(sectionTitle(id).toHtmlEscaped());
}

void HelpPage::showWelcome() {
    {
        const QSignalBlocker b(m_menu);
        m_menu->setCurrentItem(nullptr);
        m_menu->clearSelection();
    }
    m_page->setHtml(QStringLiteral("<div class='welcome'><h1>%1</h1><p class='lead'>%2</p><p>%3</p></div>")
                        .arg(tr("How to use LumoraStudio").toHtmlEscaped(),
                             tr("Pick a section on the right.").toHtmlEscaped(),
                             tr("Each section explains one part of the program step by step. The menu stays where it is, "
                                "so you can jump between sections while you read. Go back to Edit, Color, Audio or Photo "
                                "with the tabs at the top whenever you like.")
                                 .toHtmlEscaped()));
}

void HelpPage::showSection(const QString& id) {
    for (int i = 0; i < m_menu->count(); ++i) {
        QListWidgetItem* item = m_menu->item(i);
        if (item->data(kIdRole).toString() == id && m_menu->currentItem() != item) {
            const QSignalBlocker b(m_menu);
            m_menu->setCurrentItem(item);
        }
    }
    m_page->setHtml(pageHtml(id));
    m_page->verticalScrollBar()->setValue(0);
}

void HelpPage::openLink(const QUrl& url) {
    // Links between pages are written as "section:<id>".
    if (url.scheme() == QLatin1String("section"))
        showSection(url.path());
    else if (url.scheme() == QLatin1String("https"))
        QDesktopServices::openUrl(url);
}

void HelpPage::applyStyle() {
    const ThemeColors& c = themeColors();
    m_page->document()->setDefaultStyleSheet(
        QStringLiteral("h1 { font-size: 22px; font-weight: 700; color: %1; margin-bottom: 6px; }"
                       "h2 { font-size: 16px; font-weight: 600; color: %1; margin-top: 18px; margin-bottom: 4px; }"
                       "p, li { line-height: 145%; color: %1; }"
                       "p.lead { font-size: 15px; color: %2; }"
                       "p.note { color: %3; }"
                       "a { color: %4; text-decoration: none; }"
                       "kbd, .key { font-weight: 600; color: %4; }"
                       "b { color: %1; }"
                       "td { padding: 3px 14px 3px 0; color: %1; }")
            .arg(c.text.name(), c.textDim.name(), c.textDim.name(), c.accent.name()));
}

} // namespace lumora

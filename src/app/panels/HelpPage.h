#pragma once

#include <QWidget>

#include <functional>

class QListWidget;
class QTextBrowser;
class QUrl;

namespace lumora {

// The "How to use" workspace: a booklet. The section menu stays on the left;
// picking a section shows its page in the reading area beside it. The pages
// are HTML files in resources/help/<language>/<section>.html.
class HelpPage : public QWidget {
    Q_OBJECT
public:
    explicit HelpPage(QWidget* parent = nullptr);

    // Section ids, in menu order.
    static QStringList sectionIds();

    void showSection(const QString& id);
    QString currentSection() const;
    // Back to the "pick a section" page.
    void showWelcome();

    // The keyboard shortcuts page is built from the live actions.
    void setShortcutsProvider(std::function<QString()> provider);

private:
    QString sectionTitle(const QString& id) const;
    QString pageHtml(const QString& id) const;
    void openLink(const QUrl& url);
    void applyStyle();

    QTextBrowser* m_page;
    QListWidget* m_menu;
    std::function<QString()> m_shortcuts;
};

} // namespace lumora

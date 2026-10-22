#include "TitleTemplates.h"

#include <QCoreApplication>

namespace lumora {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("lumora::titles", text); }

std::vector<TitleTemplate> makeTemplates() {
    std::vector<TitleTemplate> out;
    auto add = [&](const char* id, const char* name, const char* text, double seconds, auto setup) {
        TitleTemplate t;
        t.id = QString::fromLatin1(id);
        t.name = tr(name);
        t.title.text = tr(text);
        t.seconds = seconds;
        setup(t.title);
        out.push_back(t);
    };
    add("headline", QT_TRANSLATE_NOOP("lumora::titles", "Headline"), QT_TRANSLATE_NOOP("lumora::titles", "Big moment"), 4,
        [](TitleData& d) {
            d.size = 0.12;
            d.y = 0.5;
            d.animationIn = TitleAnimation::Zoom;
            d.animationOut = TitleAnimation::Fade;
        });
    add("lowerThird", QT_TRANSLATE_NOOP("lumora::titles", "Lower third"), QT_TRANSLATE_NOOP("lumora::titles", "Name Surname\nTitle"), 5,
        [](TitleData& d) {
            d.size = 0.05;
            d.align = 0;
            d.x = 0.24;
            d.y = 0.8;
            d.shadow = false;
            d.backgroundColor = QColor(15, 15, 20);
            d.backgroundOpacity = 0.7;
            d.decoration = TitleDecoration::Bar;
            d.animationIn = TitleAnimation::SlideLeft;
            d.animationOut = TitleAnimation::SlideLeft;
        });
    add("news", QT_TRANSLATE_NOOP("lumora::titles", "News band"), QT_TRANSLATE_NOOP("lumora::titles", "BREAKING: something happened"), 6,
        [](TitleData& d) {
            d.size = 0.05;
            d.align = 0;
            d.x = 0.3;
            d.y = 0.88;
            d.shadow = false;
            d.backgroundColor = QColor(178, 24, 36);
            d.backgroundOpacity = 0.95;
            d.accentColor = QColor(255, 255, 255);
            d.decoration = TitleDecoration::Band;
            d.animationIn = TitleAnimation::SlideLeft;
        });
    add("cinema", QT_TRANSLATE_NOOP("lumora::titles", "Cinema"), QT_TRANSLATE_NOOP("lumora::titles", "A film by"), 5,
        [](TitleData& d) {
            d.size = 0.045;
            d.bold = false;
            d.uppercase = true;
            d.letterSpacing = 0.35;
            d.y = 0.5;
            d.shadow = false;
            d.animationIn = TitleAnimation::Blur;
            d.animationOut = TitleAnimation::Blur;
            d.animationDuration = kTicksPerSecond;
        });
    add("chapter", QT_TRANSLATE_NOOP("lumora::titles", "Chapter"), QT_TRANSLATE_NOOP("lumora::titles", "Chapter 1"), 4,
        [](TitleData& d) {
            d.size = 0.08;
            d.y = 0.45;
            d.decoration = TitleDecoration::Underline;
            d.animationIn = TitleAnimation::SlideUp;
        });
    add("quote", QT_TRANSLATE_NOOP("lumora::titles", "Quote"), QT_TRANSLATE_NOOP("lumora::titles", "“A good quote”"), 5,
        [](TitleData& d) {
            d.size = 0.06;
            d.italic = true;
            d.bold = false;
            d.y = 0.5;
            d.decoration = TitleDecoration::Brackets;
            d.accentColor = QColor(255, 255, 255);
        });
    add("social", QT_TRANSLATE_NOOP("lumora::titles", "Social tag"), QT_TRANSLATE_NOOP("lumora::titles", "@username"), 4,
        [](TitleData& d) {
            d.size = 0.05;
            d.y = 0.85;
            d.color = QColor(20, 20, 24);
            d.shadow = false;
            d.backgroundColor = QColor(255, 196, 64);
            d.backgroundOpacity = 1.0;
            d.animationIn = TitleAnimation::Pop;
            d.animationOut = TitleAnimation::Pop;
        });
    add("neon", QT_TRANSLATE_NOOP("lumora::titles", "Neon"), QT_TRANSLATE_NOOP("lumora::titles", "Tonight"), 4,
        [](TitleData& d) {
            d.size = 0.1;
            d.y = 0.5;
            d.color = QColor(255, 240, 250);
            d.outlineColor = QColor(255, 40, 160);
            d.outlineWidth = 0.08;
            d.shadow = true;
            d.animationIn = TitleAnimation::Pop;
        });
    add("framed", QT_TRANSLATE_NOOP("lumora::titles", "Framed"), QT_TRANSLATE_NOOP("lumora::titles", "Summer 2026"), 4,
        [](TitleData& d) {
            d.size = 0.07;
            d.y = 0.5;
            d.uppercase = true;
            d.letterSpacing = 0.12;
            d.decoration = TitleDecoration::Frame;
            d.accentColor = QColor(255, 255, 255);
        });
    add("typewriter", QT_TRANSLATE_NOOP("lumora::titles", "Typewriter"), QT_TRANSLATE_NOOP("lumora::titles", "Once upon a time..."), 5,
        [](TitleData& d) {
            d.fontFamily = QStringLiteral("Courier New");
            d.size = 0.055;
            d.bold = false;
            d.y = 0.5;
            d.animationIn = TitleAnimation::Typewriter;
            d.animationDuration = 2 * kTicksPerSecond;
        });
    add("minimal", QT_TRANSLATE_NOOP("lumora::titles", "Minimal"), QT_TRANSLATE_NOOP("lumora::titles", "simple and clean"), 4,
        [](TitleData& d) {
            d.size = 0.05;
            d.bold = false;
            d.shadow = false;
            d.letterSpacing = 0.1;
            d.y = 0.5;
        });
    add("credits", QT_TRANSLATE_NOOP("lumora::titles", "Thank you"), QT_TRANSLATE_NOOP("lumora::titles", "Thanks for watching"), 5,
        [](TitleData& d) {
            d.size = 0.07;
            d.y = 0.5;
            d.decoration = TitleDecoration::Underline;
            d.animationIn = TitleAnimation::Fade;
            d.animationOut = TitleAnimation::Fade;
            d.animationDuration = kTicksPerSecond;
        });
    return out;
}

} // namespace

const std::vector<TitleTemplate>& titleTemplates() {
    static const std::vector<TitleTemplate> all = makeTemplates();
    return all;
}

const TitleTemplate* findTitleTemplate(const QString& id) {
    for (const auto& t : titleTemplates())
        if (t.id == id)
            return &t;
    return nullptr;
}

TitleData applyTitleTemplate(const TitleData& title, const TitleTemplate& t) {
    TitleData d = t.title;
    if (!title.text.isEmpty())
        d.text = title.text;
    return d;
}

} // namespace lumora

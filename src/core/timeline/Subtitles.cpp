#include "Subtitles.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringConverter>
#include <QStringDecoder>
#include <algorithm>

namespace lumora {

namespace {
QString colorToJson(const QColor& c) { return c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb); }
QColor colorFromJson(const QJsonValue& v, const QColor& fallback) {
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}
} // namespace

QJsonObject SubtitleStyle::toJson() const {
    QJsonObject o{{"size", size},
                  {"bold", bold},
                  {"color", colorToJson(color)},
                  {"outlineColor", colorToJson(outlineColor)},
                  {"outlineWidth", outlineWidth},
                  {"backgroundOpacity", backgroundOpacity},
                  {"y", y}};
    if (!fontFamily.isEmpty())
        o["font"] = fontFamily;
    return o;
}

SubtitleStyle SubtitleStyle::fromJson(const QJsonObject& o) {
    SubtitleStyle s;
    s.fontFamily = o.value("font").toString();
    s.size = std::clamp(o.value("size").toDouble(s.size), 0.01, 0.3);
    s.bold = o.value("bold").toBool(s.bold);
    s.color = colorFromJson(o.value("color"), s.color);
    s.outlineColor = colorFromJson(o.value("outlineColor"), s.outlineColor);
    s.outlineWidth = std::clamp(o.value("outlineWidth").toDouble(s.outlineWidth), 0.0, 0.5);
    s.backgroundOpacity = std::clamp(o.value("backgroundOpacity").toDouble(s.backgroundOpacity), 0.0, 1.0);
    s.y = std::clamp(o.value("y").toDouble(s.y), 0.05, 1.0);
    return s;
}

namespace subtitles {

namespace {

// hh:mm:ss,mmm / hh:mm:ss.mmm / mm:ss.mmm
bool parseTime(const QString& s, Ticks* out) {
    static const QRegularExpression re(QStringLiteral(R"(^\s*(?:(\d+):)?(\d{1,2}):(\d{1,2})[,.](\d{1,3})\s*$)"));
    const auto m = re.match(s);
    if (!m.hasMatch())
        return false;
    const qint64 h = m.captured(1).isEmpty() ? 0 : m.captured(1).toLongLong();
    const qint64 min = m.captured(2).toLongLong();
    const qint64 sec = m.captured(3).toLongLong();
    QString frac = m.captured(4);
    while (frac.size() < 3)
        frac += QLatin1Char('0');
    const qint64 ms = frac.toLongLong();
    *out = ((h * 3600 + min * 60 + sec) * 1000 + ms) * (kTicksPerSecond / 1000);
    return true;
}

QString cleanText(QString t) {
    static const QRegularExpression tags(QStringLiteral(R"(<[^>]*>)"));
    static const QRegularExpression ass(QStringLiteral(R"(\{\\[^}]*\})"));
    t.remove(tags);
    t.remove(ass);
    t.replace(QLatin1String("&amp;"), QLatin1String("&"));
    t.replace(QLatin1String("&lt;"), QLatin1String("<"));
    t.replace(QLatin1String("&gt;"), QLatin1String(">"));
    t.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    QStringList lines = t.split(QLatin1Char('\n'));
    for (auto& l : lines)
        l = l.trimmed();
    lines.removeAll(QString());
    return lines.join(QLatin1Char('\n'));
}

} // namespace

std::vector<SubtitleCue> parse(const QString& input, QString* error) {
    QString text = input;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    const QStringList lines = text.split(QLatin1Char('\n'));
    std::vector<SubtitleCue> cues;
    static const QRegularExpression arrow(QStringLiteral(R"(\s*-->\s*)"));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        if (!line.contains(QLatin1String("-->")))
            continue;
        const QStringList parts = line.split(arrow);
        if (parts.size() < 2)
            continue;
        // VTT allows cue settings after the end time ("00:01.000 align:start").
        const QString endPart = parts[1].section(QLatin1Char(' '), 0, 0, QString::SectionSkipEmpty);
        SubtitleCue c;
        if (!parseTime(parts[0], &c.start) || !parseTime(endPart, &c.end))
            continue;
        QStringList body;
        int j = i + 1;
        for (; j < lines.size() && !lines[j].trimmed().isEmpty(); ++j) {
            if (lines[j].contains(QLatin1String("-->")))
                break;
            body << lines[j];
        }
        // An SRT index line right before the next cue belongs to that cue.
        static const QRegularExpression digits(QStringLiteral(R"(^\s*\d+\s*$)"));
        if (j < lines.size() && !body.isEmpty() && digits.match(body.last()).hasMatch())
            body.removeLast();
        i = j - 1;
        c.text = cleanText(body.join(QLatin1Char('\n')));
        if (c.end > c.start && !c.text.isEmpty())
            cues.push_back(c);
    }
    std::stable_sort(cues.begin(), cues.end(), [](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });
    if (cues.empty() && error)
        *error = SubtitleText::tr("No subtitles found. Only SubRip (.srt) and WebVTT (.vtt) files can be read.");
    return cues;
}

std::vector<SubtitleCue> readFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    const QByteArray data = f.readAll();
    QString text;
    if (auto enc = QStringConverter::encodingForData(data)) {
        QStringDecoder dec(*enc);
        text = dec.decode(data);
    } else {
        QStringDecoder utf8(QStringConverter::Utf8);
        text = utf8.decode(data);
        if (utf8.hasError()) {
            // Old Turkish .srt files are often Windows-1254.
            QStringDecoder decoder("windows-1254");
            text = decoder.isValid() ? QString(decoder.decode(data)) : QString::fromLatin1(data);
        }
    }
    return parse(text, error);
}

QString formatTime(Ticks t, Format format) {
    const qint64 ms = std::max<qint64>(0, (t + kTicksPerSecond / 2000) / (kTicksPerSecond / 1000));
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(format == Format::Srt ? QLatin1Char(',') : QLatin1Char('.'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString write(const std::vector<SubtitleCue>& cues, Format format) {
    QString out;
    if (format == Format::Vtt)
        out += QStringLiteral("WEBVTT\n\n");
    int n = 1;
    for (const auto& c : cues) {
        if (format == Format::Srt)
            out += QString::number(n++) + QLatin1Char('\n');
        out += formatTime(c.start, format) + QStringLiteral(" --> ") + formatTime(c.end, format) + QLatin1Char('\n');
        QString t = c.text;
        if (format == Format::Vtt) {
            t.replace(QLatin1Char('&'), QLatin1String("&amp;"));
            t.replace(QLatin1Char('<'), QLatin1String("&lt;"));
        }
        out += t + QStringLiteral("\n\n");
    }
    return out;
}

bool writeFile(const QString& path, const std::vector<SubtitleCue>& cues, Format format, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(write(cues, format).toUtf8());
    return true;
}

QString isoLanguage(const QString& in) {
    const QString s = in.trimmed().toLower();
    struct L {
        const char* iso;
        const char* a;
        const char* b;
    };
    static const L table[] = {
        {"tur", "tr", "türkçe"},   {"eng", "en", "english"},  {"deu", "de", "deutsch"}, {"fra", "fr", "français"},
        {"spa", "es", "español"},  {"ita", "it", "italiano"}, {"ara", "ar", "arabic"},  {"rus", "ru", "russian"},
        {"jpn", "ja", "japanese"}, {"kor", "ko", "korean"},   {"zho", "zh", "chinese"}, {"por", "pt", "português"},
        {"nld", "nl", "nederlands"}, {"aze", "az", "azərbaycan"}, {"kur", "ku", "kurdî"},
    };
    for (const auto& l : table)
        if (s == QLatin1String(l.iso) || s == QLatin1String(l.a) || s == QString::fromUtf8(l.b))
            return QString::fromLatin1(l.iso);
    if (s == QLatin1String("ger"))
        return QStringLiteral("deu");
    if (s == QLatin1String("fre"))
        return QStringLiteral("fra");
    if (s.size() == 3)
        return s;
    return QStringLiteral("und");
}

} // namespace subtitles

} // namespace lumora

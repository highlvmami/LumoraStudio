#pragma once

#include <QColor>
#include <QCoreApplication>
#include <QJsonObject>
#include <QString>
#include <vector>

#include "TimeBase.h"

namespace lumora {

// How the cues of one subtitle track look when drawn into the picture (the
// preview always, an export only when burning in). Sizes are fractions of
// the frame height.
struct SubtitleStyle {
    QString fontFamily;     // empty = default sans serif
    double size = 0.055;
    bool bold = false;
    QColor color = QColor(255, 255, 255);
    QColor outlineColor = QColor(0, 0, 0);
    double outlineWidth = 0.12; // fraction of the font size
    double backgroundOpacity = 0.0; // box behind the text, 0 .. 1
    double y = 0.92;        // bottom of the text block, fraction of the frame height

    bool operator==(const SubtitleStyle&) const = default;
    QJsonObject toJson() const;
    static SubtitleStyle fromJson(const QJsonObject& o);
};

struct SubtitleCue {
    Ticks start = 0;
    Ticks end = 0;
    QString text; // lines separated by \n, plain text

    bool operator==(const SubtitleCue&) const = default;
};

namespace subtitles {

enum class Format { Srt, Vtt };

// Reads SubRip (.srt) or WebVTT (.vtt) text; the format is detected from the
// content. Simple markup (<i>, <b>, {\an8} …) is removed. Cues come back
// sorted by start time.
std::vector<SubtitleCue> parse(const QString& text, QString* error = nullptr);
// Reads a file (UTF-8, UTF-16 with BOM, or Windows-1254 / Latin-1 as a
// fallback for old Turkish files).
std::vector<SubtitleCue> readFile(const QString& path, QString* error = nullptr);
QString write(const std::vector<SubtitleCue>& cues, Format format);
bool writeFile(const QString& path, const std::vector<SubtitleCue>& cues, Format format, QString* error = nullptr);

// "00:01:02,345" (SRT) / "00:01:02.345" (VTT).
QString formatTime(Ticks t, Format format);

// ISO 639-2 code for a language, e.g. "tur", from a code or a name.
QString isoLanguage(const QString& codeOrName);

} // namespace subtitles

// Translatable strings of the subtitle reader.
class SubtitleText {
    Q_DECLARE_TR_FUNCTIONS(SubtitleText)
};

} // namespace lumora

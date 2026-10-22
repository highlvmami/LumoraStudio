#include "TitleRenderer.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <memory>

namespace lumora {

namespace {

double smoothstep(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

double easeOutBack(double x) {
    x = std::clamp(x, 0.0, 1.0);
    constexpr double c1 = 1.70158, c3 = c1 + 1;
    return 1 + c3 * std::pow(x - 1, 3) + c1 * std::pow(x - 1, 2);
}

struct AnimState {
    double opacity = 1.0;
    double offsetY = 0.0; // fraction of frame height
    double scale = 1.0;
    double reveal = 1.0;  // typewriter: fraction of characters shown
    double offsetX = 0.0; // fraction of frame width
    double blur = 0.0;    // fraction of the font size
};

void applyAnimation(AnimState& s, TitleAnimation a, double p) {
    switch (a) {
    case TitleAnimation::None: break;
    case TitleAnimation::Fade: s.opacity *= smoothstep(p); break;
    case TitleAnimation::SlideUp:
        s.opacity *= smoothstep(p);
        s.offsetY += (1.0 - smoothstep(p)) * 0.06;
        break;
    case TitleAnimation::Pop:
        s.opacity *= smoothstep(std::min(1.0, p * 2));
        s.scale *= 0.6 + 0.4 * easeOutBack(p);
        break;
    case TitleAnimation::Typewriter: s.reveal = std::min(s.reveal, p); break;
    case TitleAnimation::SlideLeft:
        s.opacity *= smoothstep(p);
        s.offsetX += (1.0 - smoothstep(p)) * 0.08;
        break;
    case TitleAnimation::Zoom:
        s.opacity *= smoothstep(p);
        s.scale *= 1.0 + 0.6 * (1.0 - smoothstep(p));
        break;
    case TitleAnimation::Blur:
        s.opacity *= smoothstep(std::min(1.0, p * 1.5));
        s.blur += 0.6 * (1.0 - smoothstep(p));
        break;
    }
}

// Soft drop shadow: the text drawn into a small image and scaled up smoothly,
// which blurs it cheaply and works on every painter backend.
void drawSoftShadow(QPainter& p, const QPainterPath& path, double fontPx, double opacity) {
    const QRectF bounds = path.boundingRect().adjusted(-fontPx, -fontPx, fontPx, fontPx);
    const double factor = 0.25;
    const QSize small(std::max(1, static_cast<int>(bounds.width() * factor)),
                      std::max(1, static_cast<int>(bounds.height() * factor)));
    QImage img(small, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter sp(&img);
        sp.setRenderHint(QPainter::Antialiasing);
        sp.scale(factor, factor);
        sp.translate(-bounds.topLeft());
        sp.fillPath(path, QColor(0, 0, 0, 200));
    }
    const double off = fontPx * 0.05;
    p.save();
    p.setOpacity(p.opacity() * opacity);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(bounds.translated(off, off), img);
    p.restore();
}

} // namespace

void drawTitle(QPainter& p, QSize frame, const TitleData& t, Ticks local, Ticks duration, double opacity) {
    if (t.text.isEmpty() || frame.isEmpty() || opacity <= 0.0)
        return;
    AnimState s;
    const double animLen = std::max<double>(1, std::min<Ticks>(t.animationDuration, duration / 2));
    if (t.animationDuration > 0) {
        applyAnimation(s, t.animationIn, local / animLen);
        TitleAnimation out = t.animationOut == TitleAnimation::Typewriter ? TitleAnimation::Fade : t.animationOut;
        applyAnimation(s, out, (duration - local) / animLen);
    }
    s.opacity *= opacity;
    if (s.opacity <= 0.001)
        return;

    const double fontPx = std::max(1.0, t.size * frame.height());
    QFont font = t.fontFamily.isEmpty() ? QFont() : QFont(t.fontFamily);
    if (t.fontFamily.isEmpty())
        font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(static_cast<int>(std::lround(fontPx)));
    font.setBold(t.bold);
    font.setItalic(t.italic);
    if (t.letterSpacing != 0.0)
        font.setLetterSpacing(QFont::AbsoluteSpacing, t.letterSpacing * fontPx);
    const QFontMetricsF fm(font);

    const QString full = t.uppercase ? t.text.toUpper() : t.text;
    QString text = full;
    if (s.reveal < 1.0)
        text = text.left(static_cast<int>(std::ceil(s.reveal * text.size())));
    const QStringList lines = full.split(QLatin1Char('\n'));
    const QStringList shown = text.split(QLatin1Char('\n'));
    // Lay out with the full text so the block does not move while it types.
    double blockW = 0;
    for (const auto& l : lines)
        blockW = std::max(blockW, fm.horizontalAdvance(l));
    const double lineH = fm.lineSpacing();
    const double blockH = lineH * lines.size();

    QPainterPath path;
    for (int i = 0; i < shown.size(); ++i) {
        const double fullW = fm.horizontalAdvance(lines.value(i));
        double x = 0;
        if (t.align == 1)
            x = (blockW - fullW) / 2;
        else if (t.align == 2)
            x = blockW - fullW;
        path.addText(x, i * lineH + fm.ascent(), font, shown[i]);
    }

    // With a blur animation the title is drawn into a picture first.
    const double blurPx = s.blur * fontPx;
    QImage layer;
    QPainter* target = &p;
    std::unique_ptr<QPainter> layerPainter;
    if (blurPx > 0.5) {
        layer = QImage(frame, QImage::Format_ARGB32_Premultiplied);
        layer.fill(Qt::transparent);
        layerPainter = std::make_unique<QPainter>(&layer);
        target = layerPainter.get();
    }
    QPainter& d = *target;
    d.save();
    d.setRenderHint(QPainter::Antialiasing);
    d.setRenderHint(QPainter::TextAntialiasing);
    const QPointF centre((t.x + s.offsetX) * frame.width(), (t.y + s.offsetY) * frame.height());
    d.translate(centre);
    d.scale(s.scale, s.scale);
    d.translate(-blockW / 2, -blockH / 2);
    if (!layerPainter)
        d.setOpacity(d.opacity() * s.opacity);

    const double pad = fontPx * 0.3;
    const QRectF block(0, 0, blockW, blockH);
    QColor accent = t.accentColor;
    switch (t.decoration) {
    case TitleDecoration::None: break;
    case TitleDecoration::Band: {
        // Across the whole frame, behind the text.
        QColor band = t.backgroundOpacity > 0 ? t.backgroundColor : accent;
        if (t.backgroundOpacity > 0)
            band.setAlphaF(std::clamp(t.backgroundOpacity, 0.0, 1.0));
        const double left = -(centre.x() / s.scale - blockW / 2) - 2;
        d.fillRect(QRectF(left, -pad * 0.6, frame.width() / s.scale + 4, blockH + 1.2 * pad), band);
        d.fillRect(QRectF(left, -pad * 0.6, frame.width() / s.scale + 4, fontPx * 0.08), accent);
        break;
    }
    case TitleDecoration::Bar:
        d.fillRect(QRectF(-pad * 1.6, -pad * 0.4, fontPx * 0.12, blockH + 0.8 * pad), accent);
        break;
    case TitleDecoration::Underline:
        d.fillRect(QRectF(t.align == 1 ? blockW * 0.25 : t.align == 2 ? blockW * 0.5 : 0, blockH + pad * 0.2,
                          blockW * 0.5, fontPx * 0.08),
                   accent);
        break;
    case TitleDecoration::Frame: {
        d.setPen(QPen(accent, fontPx * 0.05));
        d.setBrush(Qt::NoBrush);
        d.drawRect(block.adjusted(-pad * 1.5, -pad, pad * 1.5, pad));
        break;
    }
    case TitleDecoration::Brackets: {
        d.setPen(QPen(accent, fontPx * 0.06, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        const QRectF r = block.adjusted(-pad * 1.5, -pad, pad * 1.5, pad);
        const double arm = std::min(r.width(), r.height()) * 0.3;
        d.drawPolyline(QPolygonF{QPointF(r.left(), r.top() + arm), r.topLeft(), QPointF(r.left() + arm, r.top())});
        d.drawPolyline(QPolygonF{QPointF(r.right() - arm, r.top()), r.topRight(), QPointF(r.right(), r.top() + arm)});
        d.drawPolyline(QPolygonF{QPointF(r.left(), r.bottom() - arm), r.bottomLeft(), QPointF(r.left() + arm, r.bottom())});
        d.drawPolyline(QPolygonF{QPointF(r.right() - arm, r.bottom()), r.bottomRight(), QPointF(r.right(), r.bottom() - arm)});
        break;
    }
    }
    if (t.backgroundOpacity > 0 && t.decoration != TitleDecoration::Band) {
        QColor bg = t.backgroundColor;
        bg.setAlphaF(std::clamp(t.backgroundOpacity, 0.0, 1.0));
        d.setPen(Qt::NoPen);
        d.setBrush(bg);
        d.drawRoundedRect(QRectF(-pad, -pad * 0.6, blockW + 2 * pad, blockH + 1.2 * pad), pad * 0.6, pad * 0.6);
    }
    if (t.shadow)
        drawSoftShadow(d, path, fontPx, 1.0);
    if (t.outlineWidth > 0) {
        QPen pen(t.outlineColor, 2.0 * t.outlineWidth * fontPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        d.strokePath(path, pen);
    }
    d.fillPath(path, t.color);
    d.restore();
    if (layerPainter) {
        layerPainter.reset();
        // Cheap blur: down and up again.
        const double factor = std::clamp(blurPx / 2.0, 1.0, 48.0);
        const QSize small(std::max(1, static_cast<int>(layer.width() / factor)), std::max(1, static_cast<int>(layer.height() / factor)));
        layer = layer.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .scaled(layer.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.save();
        p.setOpacity(p.opacity() * s.opacity);
        p.drawImage(0, 0, layer);
        p.restore();
    }
}

void drawSubtitle(QPainter& p, QSize frame, const SubtitleStyle& style, const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty() || frame.isEmpty())
        return;
    const double px = std::max(4.0, style.size * frame.height());
    QFont font(style.fontFamily.isEmpty() ? QStringLiteral("Sans Serif") : style.fontFamily);
    if (style.fontFamily.isEmpty())
        font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(static_cast<int>(std::lround(px)));
    font.setBold(style.bold);
    const QFontMetricsF fm(font);
    // Wrap each line to the available width.
    const double maxWidth = frame.width() * 0.9;
    QStringList lines;
    for (const QString& para : t.split(QLatin1Char('\n'))) {
        QString line;
        for (const QString& word : para.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString candidate = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            if (!line.isEmpty() && fm.horizontalAdvance(candidate) > maxWidth) {
                lines << line;
                line = word;
            } else {
                line = candidate;
            }
        }
        if (!line.isEmpty())
            lines << line;
    }
    const double lineHeight = fm.lineSpacing();
    const double bottom = style.y * frame.height();
    double y = bottom - lineHeight * lines.size() + fm.ascent();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (const QString& line : lines) {
        const double w = fm.horizontalAdvance(line);
        const double x = (frame.width() - w) / 2;
        if (style.backgroundOpacity > 0.0) {
            QColor box(0, 0, 0);
            box.setAlphaF(std::clamp(style.backgroundOpacity, 0.0, 1.0));
            const double pad = px * 0.25;
            p.fillRect(QRectF(x - pad, y - fm.ascent() - pad * 0.3, w + 2 * pad, lineHeight + pad * 0.6), box);
        }
        QPainterPath path;
        path.addText(QPointF(x, y), font, line);
        if (style.outlineWidth > 0.0) {
            QPen pen(style.outlineColor, style.outlineWidth * px, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.strokePath(path, pen);
        }
        p.fillPath(path, style.color);
        y += lineHeight;
    }
    p.restore();
}

} // namespace lumora

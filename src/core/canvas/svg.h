// A canvas as an SVG picture (part of the canvas module, canvas.h): the canvas view's "Export as
// SVG / PNG" and the HTML exports' picture of a canvas. Cards as rounded rectangles in their
// colours, text cards' Markdown as plain lines (headings bold, inline marks taken off, wrapped,
// cut at the card's bottom), file cards with their name (a picture's data URL and a note's start
// when the caller gives them), link cards with their address, groups as frames with their labels,
// edges as curves with arrowheads and labels. Everything from the canvas is escaped; colours are
// only the presets or validated hex; pictures only data: URLs of picture formats — nothing
// refers outside the file.
#pragma once

#include "canvas/canvas.h"

#include <QSizeF>

#include <functional>

namespace brulion::canvas {

struct SvgOptions {
    bool dark = false;
    /// Room around the cards.
    double padding = 40;
    /// The picture's largest width or height in pixels (a huge canvas is scaled down to it).
    double maxPixels = 8192;
    QString fontFamily = QStringLiteral("sans-serif");
    /// A picture file's data URL ("data:image/png;base64,…"; "" for none): shown in its card.
    std::function<QString(const QString &path)> pictureDataUrl;
    /// The Markdown a note card shows (its page, or its section for a subpath; "" for none).
    std::function<QString(const QString &path, const QString &subpath)> noteText;
};

/// The SVG file (UTF-8 text). An empty canvas is a small empty picture.
QString toSvg(const Canvas &c, const SvgOptions &options = {});
/// The picture's size in pixels toSvg() writes (its width and height attributes).
QSizeF svgSize(const Canvas &c, const SvgOptions &options = {});

/// The lines a card shows of Markdown text (inline marks taken off, list markers as "•", a
/// heading's level in `headingLevels`), wrapped at `charsPerLine`, at most `maxLines`.
QStringList plainLines(const QString &markdown, int charsPerLine, int maxLines, QList<int> *headingLevels = nullptr);

} // namespace brulion::canvas

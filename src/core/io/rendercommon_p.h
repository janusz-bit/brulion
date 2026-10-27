// What the renderers of the exporters share (HtmlRenderer — htmlrender_p.h —, PandocRenderer —
// pandocrender_p.h): inline "atoms" (spans replaced by rendered output) with CommonMark-like
// emphasis between them, code spans and comments, and block helpers (list markers, table cells,
// callout titles, list items that are one fenced block). Internal to the io module.
#pragma once

#include "types.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>
#include <optional>
#include <utility>

namespace brulion::io::detail {

/// A span of inline text [from, to) replaced by `out` (already rendered; empty hides it).
struct InlineAtom {
    qsizetype from = 0;
    qsizetype to = 0;
    QString out;
};

/// Opening and closing markup of an emphasis: `ch` is '*' or '_' (`count` 1 = emphasis, 2 =
/// strong), '~' (strikethrough) or '=' (highlight).
using EmphasisMarks = std::function<std::pair<QString, QString>(QChar ch, int count)>;
/// Output of plain text between atoms (also unmatched delimiters).
using TextOutput = std::function<QString(QStringView text)>;

/// `text` with `atoms` (any order, overlapping ones dropped: the earliest and longest wins)
/// replaced and emphasis delimiter runs (`*`, `_`, `~~`, `==`) between them paired.
QString renderAtoms(QStringView text, QList<InlineAtom> atoms, const EmphasisMarks &marks, const TextOutput &plain);

/// Code spans and comments of `text` (blanked in `masked`, markdown::maskInlineCode): a code span
/// becomes `code(content)` (line breaks as spaces, one padding space stripped), `%%…%%` and
/// `<!--…-->` comments hidden atoms.
void codeAndCommentAtoms(QStringView text, QStringView masked, QList<InlineAtom> &atoms,
                         const std::function<QString(const QString &content)> &code);

/// ASCII punctuation (what a backslash can escape).
bool isAsciiPunct(QChar c);
/// "scheme:" at the start of a link target.
bool hasScheme(QStringView target);
/// http, https and mailto URLs — the only external links exports make clickable.
bool safeExternal(QStringView url);
/// An http: or https: URL with a host — a picture from the internet when a picture (`![](…)`,
/// `![[…]]`) names it (docs/SYNTAX.md §2; the app's webpicture::isWebUrl, without QtNetwork).
bool isWebPictureTarget(QStringView target);

enum class MediaKind { Image, Audio, Video, Other };
/// The kind of an attachment by its file name (pictures as the app shows them).
MediaKind mediaKind(const QString &path);
/// A link target with a non-Markdown extension.
bool isAttachmentTarget(const QString &target);
/// What a link shows when it cannot be one (inside another link): its label, else its target.
QString visibleText(const Link &link, QStringView source);

/// A numbered list item ("1." / "1)").
bool isOrderedItem(const Block &b);
/// Split a GFM table row into cells (`\|` is not a separator).
QStringList tableCells(QStringView row);
/// The callout title: the first text line without `[!type]±`, else the type capitalised.
QString calloutTitle(const Block &b);
/// A list item's text that is one fenced block, as the editor shows it (a code, calc, query or
/// math block; editor/blocktext.h parseFence): the language (lower case) and the lines between.
std::optional<std::pair<QString, QString>> itemFence(const QString &text);
/// Fenced math (```math, latex, tex — docs/SYNTAX.md §11).
bool isMathFence(const QString &lang);

} // namespace brulion::io::detail

// Coloured highlights (docs/SYNTAX.md §11 "Coloured highlights"): next to `==text==`, the HTML
// that Obsidian's Highlightr plugin writes — `<mark style="background: #FF5582A6;">text</mark>`,
// `<mark class="hltr-red">text</mark>` — and a bare `<mark>text</mark>`. Obsidian, GitHub, Pandoc
// and browsers show it without Brulion. A colour is never passed on as written: it is mapped to the
// nearest of a fixed palette (HighlightColor), so no CSS of a note reaches the UI or an export.
// Part of the markdown module — see markdown/markdown.h for the module overview.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::markdown {

/// The highlight palette (Highlightr's default colours). Default is `==text==` / a `<mark>`
/// without a colour: the theme's highlight.
enum class HighlightColor { Default, Red, Orange, Yellow, Green, Cyan, Blue, Purple, Pink, Grey };
inline constexpr int HighlightColorCount = 10;

/// "default", "red", "orange", "yellow", "green", "cyan", "blue", "purple", "pink", "grey".
QString highlightColorName(HighlightColor color);
/// Inverse of highlightColorName() (case-insensitive; also "gray"); nullopt for anything else.
std::optional<HighlightColor> parseHighlightColorName(QStringView name);
/// The colour Brulion writes into the note (Highlightr's default, "#FF5582A6"); "" for Default.
QString highlightWrittenColor(HighlightColor color);
/// The colour the app draws a highlight with (a translucent "#AARRGGBB" that reads on light and
/// dark paper); "" for Default (the theme's own, Palette::highlightBackground).
QString highlightTint(HighlightColor color);

/// The palette colour nearest to a CSS declaration list (`style="…"`): the value of its
/// `background-color` or `background` (the first colour in it: #rgb, #rgba, #rrggbb, #rrggbbaa,
/// rgb()/rgba(), a CSS colour name) — Highlightr's own colours exactly, others by hue (grey when
/// barely coloured). Default when there is no colour.
HighlightColor highlightColorFromStyle(QStringView style);
/// The colour of a `class` attribute: Highlightr's `hltr-<name>` classes; Default otherwise.
HighlightColor highlightColorFromClass(QStringView classes);

/// A `<mark …>` or `</mark>` tag (an HTML tag as CommonMark reads one, on one line, at most 512
/// characters; tag name in any case; attributes quoted or not).
struct MarkTag {
    qsizetype from = 0;
    qsizetype to = 0;
    bool closing = false;
    HighlightColor color = HighlightColor::Default; ///< opening tags: style, else class
};
/// The mark tag that starts at `at` (`text[at]` is '<'), or nullopt.
std::optional<MarkTag> markTagAt(QStringView text, qsizetype at);

/// An opening tag and the closing tag it pairs with.
struct MarkPair {
    MarkTag open;
    MarkTag close;
};
/// The `<mark>` … `</mark>` pairs of inline Markdown, in order of their opening tags: each closing
/// tag closes the nearest open one (nesting allowed); unpaired tags are left out (shown as text).
/// `masked`: the text with code spans and comments blanked (maskInlineCode()), so a tag in code
/// is none. Linear time.
QList<MarkPair> findMarkPairs(QStringView masked);

/// The opening tag Brulion writes for `color`: `<mark style="background: #FF5582A6;">` (Highlightr's
/// form); "" for Default (which is written `==`).
QString markOpenTag(HighlightColor color);

/// One highlight of a text: `==…==` or a `<mark>` pair.
struct HighlightSpan {
    qsizetype from = 0;        ///< the opening marker
    qsizetype contentFrom = 0; ///< after it
    qsizetype contentTo = 0;   ///< the closing marker
    qsizetype to = 0;          ///< after it
    bool html = false;         ///< a `<mark>` pair (else `==`)
    HighlightColor color = HighlightColor::Default;
};
/// Every highlight of inline Markdown `text` (`==` paired by the emphasis rules the renderer uses,
/// `<mark>` pairs by findMarkPairs(); not in code spans or comments), ordered by `from`.
QList<HighlightSpan> findHighlights(QStringView text);

/// The result of setHighlight(): the new text and the selection to show (the highlighted text).
struct HighlightEdit {
    QString text;
    qsizetype from = 0;
    qsizetype to = 0;
    bool changed = false;
    /// Nothing changed because the selection cannot be highlighted: it spans several lines, cuts
    /// a highlight's marker, or the caret is inside a marker.
    bool refused = false;
};
/// The editor's "Highlight…" (a pure edit of a block's text; [from, to) the selection):
/// * a selection inside a highlight's text (or exactly covering one, markers included) changes
///   that highlight: `color` recolours it (Default: `==…==`, others `<mark style="…">…</mark>`),
///   nullopt removes its markers;
/// * else, with a colour, the selection (without blanks at its ends; an empty one: the caret,
///   markers with nothing between) is wrapped, highlights wholly inside it unwrapped first; a
///   selection over several lines or one cutting a highlight's marker is refused (unchanged);
/// * nullopt with no highlight around: unchanged.
/// The rest of the text stays byte-for-byte.
HighlightEdit setHighlight(const QString &text, qsizetype from, qsizetype to, std::optional<HighlightColor> color);

} // namespace brulion::markdown

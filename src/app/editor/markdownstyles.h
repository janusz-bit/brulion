// Markdown styles — the rules of the edited text's syntax colours as a pure function
// (docs/ATRAMENT.md "The highlighter as a pure function"): one line of raw Markdown (a block's
// body as edited, a line of a whole file in source mode, a line of code), the state the line before
// it left and the mode in; style operations out — exactly the formats MarkdownHighlighter sets and
// merges, in its order — with the line's prose for spell checking, its reminder chips and the state
// the next line starts in. QtCore only (no QTextCharFormat, QColor or QFont): MarkdownHighlighter (a
// QSyntaxHighlighter) applies the operations with its QTextCharFormats, built from the same table
// (styleAttributes(), colorRolePalette()); the block editor drawn by Atrament flattens them into
// runs of attributes (flattenStyles()) for its text input. One set of rules, tested once
// (tests/app/editor/tst_editor_markdownstyles.cpp).
//
//   StyleOptions options;                           // mode Inline, Source or Code
//   LineStyles styles;                              // reused: its lists keep their capacity
//   int state = 0;                                  // what the first line starts in
//   for (int i = 0; i < lines.size(); ++i) {
//       options.firstLine = i == 0;
//       styleMarkdownLine(lines[i], state, options, &styles);
//       for (const StyleOp &op : styles.ops)        // a QTextDocument: set or merge in order
//           …;
//       const QList<StyleRun> runs = flattenStyles(styles.ops, int(lines[i].size()));  // flat spans
//       state = styles.state;
//   }
//
// Modes (MarkdownHighlighter's `mode`): Inline — one block body as edited (no state across lines);
// Source — a whole file: fenced code (``` and ~~~; a ```luau / ```lua fence coloured as Luau, the
// lexer's state kept in the line state), the frontmatter (a `---` first line), %% comments and
// display formulas between `$$` lines run over lines, and list markers are syntax; Code — a code
// block's content: whole-line comments (`#`, `//`) only, or Luau (syntax/luau.h) when `luau` is set.
//
// The operations are QSyntaxHighlighter::setFormat (merge false: the range takes the style's format,
// whatever it had) and a merge (merge true: the style's attributes over what the range has, as
// QTextCharFormat::merge — what the style does not set stays); later ones win. The style names the
// format (markdownStyleName(): "heading", "marker", … "luau.comment"); `modifier` and `tint` are the
// few ad hoc variants: a callout's title (Muted) and a task keyword (Marker) at DemiBold, a heading's
// `#` marks (Marker over the heading's format), a coloured `<mark>`'s palette tint (Highlight).
#pragma once

#include "dates/names.h"
#include "markdown/highlight.h"

#include <QDate>
#include <QLatin1StringView>
#include <QList>
#include <QString>

namespace brulion::editor {

/// A reminder token of a line (positions in the line): MarkdownHighlighter's `reminderChips`.
struct LineChip {
    int from = 0;
    int to = 0;
    bool alarm = false;
    QString label; ///< markdown::reminderChipLabel(): "⏰ jutro 18:00"
    bool operator==(const LineChip &other) const = default;
};

/// The part of a line that is prose for spell checking ([from, to) of the line, `to` −1: its
/// end; `from` −1: none — fences, code, the frontmatter, formulas and comments over lines in
/// Source mode, everything in Code mode); `lineStart`: it starts the line (spell::findWords).
struct SpellRange {
    int from = 0;
    int to = -1;
    bool lineStart = true;
    bool operator==(const SpellRange &other) const = default;
};

/// A format of the highlighter (MarkdownHighlighter::rebuildFormats(): one QTextCharFormat each).
enum class MarkdownStyle : quint8 {
    Heading,
    Marker, ///< list / quote / heading marks, brackets, emphasis markers, checkboxes
    Link,
    BlockRef,
    Url,
    Tag,
    Code,
    Fence, ///< code fences, the frontmatter's `---`, inline code's backticks
    Bold,
    Italic,
    Strike,
    Highlight,
    Muted,
    Faint,
    BlockId,
    Property,
    Comment,
    Due,
    Scheduled,
    Start,
    Reminder,
    Alarm,
    Recurrence,
    Done,
    Priority,
    LuauKeyword,
    LuauConstant,
    LuauBuiltin,
    LuauApi,
    LuauFunction,
    LuauNumber,
    LuauString,
    LuauInterpolation,
    LuauComment,
};
inline constexpr int MarkdownStyleCount = int(MarkdownStyle::LuauComment) + 1;

/// "heading", "marker", "link", "blockRef", "url", "tag", "code", "fence", "bold", "italic",
/// "strike", "highlight", "muted", "faint", "blockId", "property", "comment", "due", "scheduled",
/// "start", "reminder", "alarm", "recurrence", "done", "priority", "luau.keyword", "luau.constant",
/// "luau.builtin", "luau.api", "luau.function", "luau.number", "luau.string", "luau.interpolation",
/// "luau.comment": the format's name (the property tests read back from MarkdownHighlighter).
QLatin1StringView markdownStyleName(MarkdownStyle style);

/// A variant of a style's format.
enum class StyleModifier : quint8 {
    None,
    DemiBold,  ///< at DemiBold weight (a callout's title: Muted; a task keyword: Marker)
    OnHeading, ///< over the heading's format (a heading's `#` marks: Marker — DemiBold, the marker's colour)
};

/// One operation of a line's styles: [from, from + length) of the line.
struct StyleOp {
    int from = 0;
    int length = 0;
    MarkdownStyle style = MarkdownStyle::Heading;
    bool merge = false; ///< false: set (the range takes the format); true: merged over the range's
    StyleModifier modifier = StyleModifier::None;
    /// Highlight of a coloured `<mark>`: its background is markdown::highlightTint(tint).
    markdown::HighlightColor tint = markdown::HighlightColor::Default;
    bool operator==(const StyleOp &other) const = default;
};

enum class MarkdownMode : quint8 { Inline, Source, Code };

struct StyleOptions {
    MarkdownMode mode = MarkdownMode::Inline;
    /// Code mode: the language is Luau (syntax::isLuauLanguage(): "luau", "lua").
    bool luau = false;
    /// Source mode: the text's first line (a `---` there opens the frontmatter).
    bool firstLine = false;
    /// Reminder tokens (⏰ / ⏰!) give LineStyles::chips, labelled in `chipLocale` relative to
    /// `chipToday` (invalid: absolute dates). MarkdownHighlighter: only in its "inline" mode.
    bool reminderChips = false;
    dates::UiLocale chipLocale = dates::UiLocale::Polish;
    QDate chipToday;
};

/// What styleMarkdownLine() finds on a line.
struct LineStyles {
    QList<StyleOp> ops; ///< in order; none of length 0, all inside the line
    /// The state the next line starts in (an opaque number; 0 also for a line ending in none).
    int state = 0;
    SpellRange spell;
    QList<LineChip> chips; ///< in order (StyleOptions::reminderChips)
};

/// The styles of `line` (no line break in it), which starts in `previousState` (the state the line
/// before ended in; 0 or −1 for the first line), into `out` (overwritten; its lists keep their
/// capacity for the next line).
void styleMarkdownLine(const QString &line, int previousState, const StyleOptions &options, LineStyles *out);
LineStyles styleMarkdownLine(const QString &line, int previousState, const StyleOptions &options);

/// A foreground colour, by the palette key MarkdownHighlighter takes it from (colorRolePalette()).
enum class ColorRole : quint8 {
    None, ///< the text's own colour
    Heading,
    Marker,
    Link,
    Url,
    Tag,
    Code,
    Muted,
    Faint,
    Due,
    Scheduled,
    Reminder,
    Alarm,
    Recurrence,
    Done,
    Priority,
    Keyword,  ///< Luau keywords and the braces of an interpolation
    Constant, ///< Luau's nil, true, false
    Builtin,  ///< Luau's standard library and `b`, the fields of `b`
    Function, ///< the name a Luau `function` statement defines
    Number,
    String,
};
enum class BackgroundRole : quint8 {
    None,
    Code,      ///< "codeBackground"
    Highlight, ///< "highlightBackground"
    Tint,      ///< a coloured `<mark>`'s: StyleAttributes::tint
};
enum class FontWeight : quint8 { Normal, DemiBold, Bold };

/// Where MarkdownHighlighter takes a role's colour from: the palette's `key`, else its
/// `fallbackKey` (when not empty), else the colour string `fallback` ("#rrggbb" or "#aarrggbb").
struct PaletteColor {
    QLatin1StringView key;
    QLatin1StringView fallbackKey;
    QLatin1StringView fallback;
};
/// `reminderChips` (MarkdownHighlighter::reminderChips): Reminder and Alarm take the chip's colours
/// ("reminderChip", "alarmChip") first. None: an empty key.
PaletteColor colorRolePalette(ColorRole role, bool reminderChips = false);
/// Tint and None: an empty key (a tint is markdown::highlightTint()).
PaletteColor backgroundRolePalette(BackgroundRole role);

/// What a format sets (an attribute at its default is one it leaves alone when merged).
struct StyleAttributes {
    ColorRole color = ColorRole::None;
    BackgroundRole background = BackgroundRole::None;
    markdown::HighlightColor tint = markdown::HighlightColor::Default; ///< background Tint's colour
    FontWeight weight = FontWeight::Normal;
    bool italic = false;
    bool strike = false;
    bool underline = false;
    bool monospace = false; ///< the editor's monospace family (MarkdownHighlighter::monoFamily)
    bool operator==(const StyleAttributes &other) const = default;

    /// `other` over these, as QTextCharFormat::merge: what it sets wins, the rest stays.
    void merge(const StyleAttributes &other);
    /// The tint's colour string (markdown::highlightTint()); "" without one.
    QString tintColor() const;
};

/// The attributes of a style's format (MarkdownHighlighter builds its formats from these).
StyleAttributes styleAttributes(MarkdownStyle style);
/// The attributes of one operation's format: its style's with its modifier and tint.
StyleAttributes styleAttributes(const StyleOp &op);

/// A stretch of a line with one look: what the operations over it leave.
struct StyleRun {
    int from = 0;
    int length = 0;
    MarkdownStyle style = MarkdownStyle::Heading; ///< the last operation's (the format's name)
    StyleAttributes attributes;
    bool operator==(const StyleRun &other) const = default;
};

/// `ops` (one line's, in order) flattened: the runs of characters some operation covers, in order,
/// never overlapping, neighbours with the same style and attributes joined; characters no operation
/// covers are in none. `lineLength`: operations are cut at the line's end.
QList<StyleRun> flattenStyles(const QList<StyleOp> &ops, int lineLength);

} // namespace brulion::editor

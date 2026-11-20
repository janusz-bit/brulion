// Luau syntax colours (docs/UI.md "The block editor": ```luau blocks; docs/EXTENSIONS.md): a lexer
// that splits one line of Luau into coloured spans, carrying what a line leaves open — a long
// string `[[…]]` / `[=[…]=]`, a long comment `--[[…]]`, a backtick string continued by `\` — to
// the next line as a small state. Pure functions, no Qt GUI: the editor's MarkdownHighlighter
// paints the spans, tests check them. Part of the syntax module.
//
//   LuauState state;                                   // the first line's
//   for (const QString &line : lines) {
//       const QList<LuauSpan> spans = lexLuauLine(line, &state);   // state: what the next line starts in
//       …
//   }
//
// What is a span: keywords (`local`, `function`, `if` … `continue`), the constants `nil`, `true`,
// `false`, the standard library's globals (`string`, `table`, `print`, `pairs` …) and Brulion's
// `b`, a field of `b` (`b.command`: "command"), the name a `function` statement defines, numbers
// (`42`, `0x1F`, `0b1010`, `1_000`, `2.5e-3`), strings (quoted, long, backtick — the `{…}` of an
// interpolation is code again, its braces their own span), and comments (`--` to the end of the
// line, long comments). Everything else (names, operators, punctuation) has no span. Malformed
// code never fails: an unclosed quote runs to the end of its line, an unclosed long string or
// comment to the end of the text, nesting deeper than 32 braces of interpolation is a string.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::syntax {

enum class LuauToken : quint8 {
    Keyword,
    Constant, ///< nil, true, false
    Builtin, ///< a global of the standard library, and `b`
    Api, ///< a field of `b`: the "command" of b.command, the "ui" of b.ui.text
    Function, ///< the name a `function` statement defines (`local function walk`, `function M.go`)
    Number,
    String,
    Interpolation, ///< the braces of `{…}` inside a backtick string
    Comment,
};

struct LuauSpan {
    int from = 0;
    int length = 0;
    LuauToken kind = LuauToken::Keyword;
    bool operator==(const LuauSpan &other) const = default;
};

/// What a line leaves open for the next one.
struct LuauState {
    enum Kind : quint8 {
        Code = 0,
        LongString = 1, ///< inside [[ … ]] or [=…=[ … ]=…=]
        LongComment = 2, ///< inside --[[ … ]]
        Backtick = 3, ///< a `…` string whose line ended with a backslash
    };
    Kind kind = Code;
    int level = 0; ///< the number of `=` of the long bracket (0 … 255)

    bool operator==(const LuauState &other) const = default;
    /// The state as a small number (0 … 1023), and back (QSyntaxHighlighter keeps an int per line).
    int pack() const { return int(kind) | (qBound(0, level, 255) << 2); }
    static LuauState unpack(int packed)
    {
        LuauState s;
        s.kind = Kind(packed & 0x3);
        s.level = (packed >> 2) & 0xff;
        return s;
    }
};

/// The coloured spans of `line` (no line break in it), in order and never overlapping; `state` is
/// what the line starts in and, on return, what the next one starts in.
QList<LuauSpan> lexLuauLine(QStringView line, LuauState *state);

/// The spans of a whole text (lines separated by \n, \r\n or \r), positions in the text.
QList<LuauSpan> lexLuau(QStringView text);

/// "keyword", "constant", "builtin", "api", "function", "number", "string", "interpolation",
/// "comment": a span's kind as the highlighter's palette names its colour.
QString luauTokenName(LuauToken kind);

/// True for a fence language that is Luau code: "luau", "lua" (any case).
bool isLuauLanguage(QStringView language);

} // namespace brulion::syntax

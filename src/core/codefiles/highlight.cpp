#include "codefiles/highlight.h"

#include "codefiles/highlight_p.h"
#include "syntax/luau.h"

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

namespace {

using detail::LanguageSpec;
using Lexer = LanguageSpec::Lexer;

/// CodeState::kind.
enum StateKind : quint8 {
    Plain = 0,
    BlockComment = 1, ///< data: the nesting depth
    TripleString = 2, ///< data: the quote
    Backtick = 3,     ///< data: 1 with backslash escapes
    CppRaw = 4,       ///< data: the delimiter's length
    RustRaw = 5,      ///< data: the number of "#"
    NixString = 6,
    LuauOpen = 7, ///< data: syntax::LuauState::pack()
    MarkupComment = 8,
    MarkupTag = 9, ///< inside a tag, among its attributes
    MarkdownFence = 10, ///< data: the fence's length, | 0x100 for "~"
    CData = 11,
};

constexpr qsizetype MaxLexedLength = 10000;

struct Out {
    QList<CodeSpan> spans;
    void add(qsizetype from, qsizetype to, CodeToken kind)
    {
        if (to <= from)
            return;
        if (!spans.isEmpty()) {
            CodeSpan &last = spans.last();
            if (last.kind == kind && last.from + last.length == from) {
                last.length += int(to - from);
                return;
            }
        }
        spans.append({int(from), int(to - from), kind});
    }
};

bool startsAt(QStringView line, qsizetype i, QStringView what)
{
    return !what.isEmpty() && i + what.size() <= line.size() && line.sliced(i, what.size()) == what;
}

bool isIdentStart(QChar c)
{
    return c.isLetter() || c == u'_';
}

bool isIdentChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'_' || c.isMark();
}

qsizetype skipSpaces(QStringView line, qsizetype i)
{
    while (i < line.size() && line.at(i).isSpace())
        ++i;
    return i;
}

bool onlySpacesBefore(QStringView line, qsizetype i)
{
    for (qsizetype k = 0; k < i; ++k) {
        if (!line.at(k).isSpace())
            return false;
    }
    return true;
}

qsizetype identEnd(QStringView line, qsizetype i, bool dashes)
{
    const qsizetype n = line.size();
    qsizetype j = i;
    while (j < n && (isIdentChar(line.at(j)) || (dashes && line.at(j) == u'-' && j + 1 < n && isIdentChar(line.at(j + 1)))))
        ++j;
    return j;
}

qsizetype numberEnd(QStringView line, qsizetype i)
{
    const qsizetype n = line.size();
    qsizetype j = i;
    const auto isHex = [](QChar c) { return c.isDigit() || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F'); };
    if (line.at(j) == u'0' && j + 1 < n && QStringView(u"xXbBoO").contains(line.at(j + 1))) {
        j += 2;
        while (j < n && (isHex(line.at(j)) || line.at(j) == u'_' || line.at(j) == u'\''))
            ++j;
    } else {
        bool dot = false;
        while (j < n) {
            const QChar c = line.at(j);
            if (c.isDigit() || c == u'_' || (c == u'\'' && j + 1 < n && line.at(j + 1).isDigit())) {
                ++j;
            } else if (c == u'.' && !dot && j + 1 < n && line.at(j + 1).isDigit()) {
                dot = true;
                ++j;
            } else if ((c == u'e' || c == u'E') && j + 1 < n
                       && (line.at(j + 1).isDigit()
                           || ((line.at(j + 1) == u'+' || line.at(j + 1) == u'-') && j + 2 < n && line.at(j + 2).isDigit()))) {
                j += 2;
            } else {
                break;
            }
        }
    }
    // A suffix: 10u, 1.5f, 42i64, 3n, 12px (CSS units read as part of the number).
    while (j < n && (line.at(j).isLetterOrNumber() || line.at(j) == u'_' || line.at(j) == u'%'))
        ++j;
    return j;
}

/// After the opening quote: where the string ends (after its closing quote), or the line's end.
qsizetype stringEnd(QStringView line, qsizetype from, QChar quote, bool escapes, bool doubled)
{
    const qsizetype n = line.size();
    for (qsizetype j = from; j < n; ++j) {
        const QChar c = line.at(j);
        if (escapes && c == u'\\') {
            ++j;
            continue;
        }
        if (c == quote) {
            if (doubled && j + 1 < n && line.at(j + 1) == quote) {
                ++j;
                continue;
            }
            return j + 1;
        }
    }
    return n;
}

/// After an opening quote: the position after its closing one, -1 when the line has none.
qsizetype closingQuote(QStringView line, qsizetype from, QChar quote, bool escapes)
{
    for (qsizetype j = from; j < line.size(); ++j) {
        if (escapes && line.at(j) == u'\\') {
            ++j;
            continue;
        }
        if (line.at(j) == quote)
            return j + 1;
    }
    return -1;
}

/// Continues what `state` holds open from `from`: where it ends on this line (the line's end
/// when it stays open; `state` Plain once it closes).
qsizetype continueOpen(const LanguageSpec &s, QStringView line, qsizetype from, CodeState *state)
{
    const qsizetype n = line.size();
    switch (state->kind) {
    case BlockComment:
    case MarkupComment: {
        int depth = std::max<int>(1, state->data);
        const QStringView open = s.blockOpen;
        const QStringView close = s.blockClose;
        for (qsizetype j = from; j < n;) {
            if (startsAt(line, j, close)) {
                j += close.size();
                if (--depth == 0) {
                    *state = {};
                    return j;
                }
            } else if (s.nestedBlocks && startsAt(line, j, open)) {
                j += open.size();
                ++depth;
            } else {
                ++j;
            }
        }
        state->data = quint16(std::min(depth, 255));
        return n;
    }
    case TripleString: {
        const QChar q(char16_t(state->data));
        const QString close(3, q);
        for (qsizetype j = from; j < n; ++j) {
            if (line.at(j) == u'\\') {
                ++j;
                continue;
            }
            if (startsAt(line, j, close)) {
                *state = {};
                return j + 3;
            }
        }
        return n;
    }
    case Backtick: {
        const qsizetype end = closingQuote(line, from, u'`', state->data != 0);
        if (end < 0)
            return n;
        *state = {};
        return end;
    }
    case CppRaw: {
        const int len = state->data;
        for (qsizetype j = from; j < n; ++j) {
            if (line.at(j) != u')' || j + 1 + len >= n || line.at(j + 1 + len) != u'"')
                continue;
            bool ok = true;
            for (qsizetype k = j + 1; k < j + 1 + len && ok; ++k)
                ok = !line.at(k).isSpace() && line.at(k) != u'(' && line.at(k) != u')' && line.at(k) != u'\\';
            if (ok) {
                *state = {};
                return j + 2 + len;
            }
        }
        return n;
    }
    case RustRaw: {
        const int hashes = state->data;
        for (qsizetype j = from; j < n; ++j) {
            if (line.at(j) != u'"')
                continue;
            qsizetype k = j + 1;
            while (k < n && k - j - 1 < hashes && line.at(k) == u'#')
                ++k;
            if (k - j - 1 == hashes) {
                *state = {};
                return k;
            }
        }
        return n;
    }
    case NixString:
        for (qsizetype j = from; j + 1 < n; ++j) {
            if (line.at(j) != u'\'' || line.at(j + 1) != u'\'')
                continue;
            if (j + 2 < n && QStringView(u"$'\\").contains(line.at(j + 2))) {
                j += 2;
                continue;
            }
            *state = {};
            return j + 2;
        }
        return n;
    case CData: {
        const qsizetype end = line.indexOf(u"]]>", from);
        if (end < 0)
            return n;
        *state = {};
        return end + 3;
    }
    default: *state = {}; return from;
    }
}

CodeToken openKind(quint8 kind)
{
    return kind == BlockComment || kind == MarkupComment ? CodeToken::Comment : CodeToken::String;
}

/// $name, ${…}, $(…), $1, $@: where a variable starting at `i` ends (i + 1: none).
qsizetype variableEnd(QStringView line, qsizetype i)
{
    const qsizetype n = line.size();
    if (i + 1 >= n)
        return i + 1;
    const QChar c = line.at(i + 1);
    if (c == u'{') {
        const qsizetype close = line.indexOf(u'}', i + 2);
        return close < 0 ? n : close + 1;
    }
    if (c == u'(') {
        // $(VAR) of a Makefile; a shell's $(command …): only its "$(".
        qsizetype j = i + 2;
        while (j < n && j - i < 48 && (isIdentChar(line.at(j)) || line.at(j) == u'.' || line.at(j) == u'-'))
            ++j;
        return j < n && line.at(j) == u')' ? j + 1 : i + 2;
    }
    if (isIdentChar(c))
        return identEnd(line, i + 1, false);
    if (QStringView(u"@#?$!*-").contains(c))
        return i + 2;
    return i + 1;
}

bool allCaps(QStringView word)
{
    bool letter = false;
    for (const QChar c : word) {
        if (c.isLower())
            return false;
        letter = letter || c.isLetter();
    }
    return letter && word.size() >= 2;
}

// ---- The generic lexer ----------------------------------------------------------------------------

void lexCode(const LanguageSpec &s, QStringView line, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    if (state->kind != Plain) {
        const quint8 kind = state->kind;
        i = continueOpen(s, line, 0, state);
        out.add(0, i, openKind(kind));
    }
    QString lastKeyword;
    while (i < n) {
        const QChar c = line.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        // Comments.
        bool comment = false;
        for (const QString &start : s.lineComments) {
            if (!startsAt(line, i, start))
                continue;
            if (s.hashCommentAtWordStart && start == u"#" && i > 0 && !line.at(i - 1).isSpace())
                continue;
            comment = true;
            break;
        }
        if (comment) {
            out.add(i, n, CodeToken::Comment);
            return;
        }
        if (startsAt(line, i, s.blockOpen)) {
            *state = {BlockComment, 1};
            const qsizetype end = continueOpen(s, line, i + s.blockOpen.size(), state);
            out.add(i, end, CodeToken::Comment);
            i = end;
            continue;
        }
        // The preprocessor, attributes, decorators.
        if (s.hashPreprocessor && c == u'#' && onlySpacesBefore(line, i)) {
            qsizetype j = skipSpaces(line, i + 1);
            const qsizetype wordStart = j;
            j = identEnd(line, j, false);
            out.add(i, j, CodeToken::Preprocessor);
            const QStringView directive = line.sliced(wordStart, j - wordStart);
            if (directive == u"include" || directive == u"import") {
                const qsizetype k = skipSpaces(line, j);
                if (k < n && (line.at(k) == u'<' || line.at(k) == u'"')) {
                    const qsizetype close = line.indexOf(line.at(k) == u'<' ? u'>' : u'"', k + 1);
                    const qsizetype end = close < 0 ? n : close + 1;
                    out.add(k, end, CodeToken::String);
                    j = end;
                }
            }
            i = j;
            continue;
        }
        if (s.rustAttributes && c == u'#' && i + 1 < n
            && (line.at(i + 1) == u'[' || (line.at(i + 1) == u'!' && i + 2 < n && line.at(i + 2) == u'['))) {
            const qsizetype close = line.indexOf(u']', i);
            const qsizetype end = close < 0 ? n : close + 1;
            out.add(i, end, CodeToken::Preprocessor);
            i = end;
            continue;
        }
        if (s.decorators && c == u'@' && i + 1 < n && isIdentStart(line.at(i + 1))) {
            qsizetype j = identEnd(line, i + 1, false);
            while (j + 1 < n && line.at(j) == u'.' && isIdentStart(line.at(j + 1)))
                j = identEnd(line, j + 1, false);
            out.add(i, j, CodeToken::Preprocessor);
            i = j;
            continue;
        }
        // Strings.
        if (s.tripleQuotes && s.quotes.contains(c) && startsAt(line, i, QString(3, c))) {
            *state = {TripleString, c.unicode()};
            const qsizetype end = continueOpen(s, line, i + 3, state);
            out.add(i, end, CodeToken::String);
            i = end;
            continue;
        }
        if (s.backtickStrings && c == u'`') {
            const qsizetype close = closingQuote(line, i + 1, u'`', s.backtickEscapes);
            if (close < 0)
                *state = {Backtick, quint16(s.backtickEscapes ? 1 : 0)};
            const qsizetype end = close < 0 ? n : close;
            out.add(i, end, CodeToken::String);
            i = end;
            continue;
        }
        if (s.nixStrings && c == u'\'' && i + 1 < n && line.at(i + 1) == u'\'') {
            *state = {NixString, 0};
            const qsizetype end = continueOpen(s, line, i + 2, state);
            out.add(i, end, CodeToken::String);
            i = end;
            continue;
        }
        if (s.quotes.contains(c)) {
            if (s.lifetimes && c == u'\'') {
                // 'a' or '\n' is a character; 'a without its quote a lifetime or a label.
                const bool character = (i + 1 < n && line.at(i + 1) == u'\\')
                                       || (i + 2 < n && line.at(i + 2) == u'\'')
                                       || (i + 3 < n && line.at(i + 1).isHighSurrogate() && line.at(i + 3) == u'\'');
                if (!character) {
                    const qsizetype j = identEnd(line, i + 1, false);
                    out.add(i, j, CodeToken::Variable);
                    i = std::max(j, i + 1);
                    continue;
                }
            }
            const qsizetype end = stringEnd(line, i + 1, c, true, s.doubledQuoteEscapes);
            out.add(i, end, CodeToken::String);
            i = end;
            continue;
        }
        // Variables and commands.
        if (s.variables && c == u'$') {
            const qsizetype end = variableEnd(line, i);
            if (end > i + 1)
                out.add(i, end, CodeToken::Variable);
            i = end;
            continue;
        }
        if (s.sigils && (c == u'$' || c == u'@' || c == u'%') && i + 1 < n && isIdentStart(line.at(i + 1))
            && (c == u'$' || i == 0 || !isIdentChar(line.at(i - 1)))) {
            const qsizetype end = identEnd(line, i + 1, false);
            out.add(i, end, CodeToken::Variable);
            i = end;
            continue;
        }
        if (s.backslashCommands && c == u'\\') {
            qsizetype j = i + 1;
            while (j < n && line.at(j).isLetter())
                ++j;
            if (j == i + 1 && j < n)
                ++j;
            out.add(i, j, CodeToken::Keyword);
            i = j;
            continue;
        }
        // Numbers.
        if (c.isDigit() || (c == u'.' && i + 1 < n && line.at(i + 1).isDigit())) {
            const qsizetype end = numberEnd(line, i);
            out.add(i, end, CodeToken::Number);
            i = end;
            continue;
        }
        // Names.
        if (isIdentStart(c)) {
            const qsizetype j = identEnd(line, i, s.dashInNames);
            const QStringView word = line.sliced(i, j - i);
            // Raw strings: R"x(…)x" (and its u8R, uR, UR, LR forms), r#"…"#, br"…".
            if (s.cppRawStrings && j < n && line.at(j) == u'"'
                && (word == u"R" || word == u"u8R" || word == u"uR" || word == u"UR" || word == u"LR")) {
                const qsizetype paren = line.indexOf(u'(', j + 1);
                if (paren >= 0 && paren - j - 1 <= 16) {
                    *state = {CppRaw, quint16(paren - j - 1)};
                    const qsizetype end = continueOpen(s, line, paren + 1, state);
                    out.add(i, end, CodeToken::String);
                    i = end;
                    continue;
                }
            }
            if (s.rustRawStrings && (word == u"r" || word == u"br") && j < n && (line.at(j) == u'#' || line.at(j) == u'"')) {
                qsizetype k = j;
                while (k < n && line.at(k) == u'#')
                    ++k;
                if (k < n && line.at(k) == u'"') {
                    *state = {RustRaw, quint16(k - j)};
                    const qsizetype end = continueOpen(s, line, k + 1, state);
                    out.add(i, end, CodeToken::String);
                    i = end;
                    continue;
                }
            }
            // A string's prefix (f"…", r'…', b"…", u8"…", L"…"): part of the string.
            if (j < n && s.quotes.contains(line.at(j)) && word.size() <= 2) {
                bool prefix = true;
                for (const QChar p : word)
                    prefix = prefix && QStringView(u"rRbBfFuUL8").contains(p);
                if (prefix) {
                    out.add(i, j, CodeToken::String);
                    i = j;
                    continue;
                }
            }
            const QString key = s.caseInsensitive ? word.toString().toLower() : word.toString();
            std::optional<CodeToken> kind;
            const QString definer = lastKeyword;
            lastKeyword.clear();
            if (s.keywords.contains(key)) {
                kind = CodeToken::Keyword;
                lastKeyword = key;
            } else if (s.colonProperties && j < n && line.at(j) == u':' && !(j + 1 < n && line.at(j + 1) == u':')) {
                kind = CodeToken::Property;
            } else if (s.types.contains(key)) {
                kind = CodeToken::Type;
            } else if (s.constants.contains(key)) {
                kind = CodeToken::Constant;
            } else if (s.builtins.contains(key)) {
                kind = CodeToken::Builtin;
            } else if (!definer.isEmpty() && s.functionDefiners.contains(definer)) {
                kind = CodeToken::Function;
            } else if (!definer.isEmpty() && s.typeDefiners.contains(definer)) {
                kind = CodeToken::Type;
            } else if (const qsizetype next = skipSpaces(line, j); next < n && line.at(next) == u'(') {
                kind = CodeToken::Function;
            } else if (s.upperConstants && allCaps(word)) {
                kind = CodeToken::Constant;
            } else if (s.capitalTypes && word.at(0).isUpper()) {
                kind = CodeToken::Type;
            }
            if (kind)
                out.add(i, j, *kind);
            i = j;
            continue;
        }
        ++i;
    }
}

// ---- Markup ---------------------------------------------------------------------------------------

qsizetype lexTagBody(QStringView line, qsizetype i, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    bool afterEquals = false;
    while (i < n) {
        const QChar c = line.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c == u'>') {
            out.add(i, i + 1, CodeToken::Tag);
            *state = {};
            return i + 1;
        }
        if ((c == u'/' || c == u'?') && i + 1 < n && line.at(i + 1) == u'>') {
            out.add(i, i + 2, CodeToken::Tag);
            *state = {};
            return i + 2;
        }
        if (c == u'"' || c == u'\'') {
            const qsizetype end = stringEnd(line, i + 1, c, false, false);
            out.add(i, end, CodeToken::String);
            i = end;
            afterEquals = false;
            continue;
        }
        if (c == u'=') {
            afterEquals = true;
            ++i;
            continue;
        }
        qsizetype j = i;
        while (j < n && !line.at(j).isSpace() && !QStringView(u"=>\"'").contains(line.at(j))
               && !(line.at(j) == u'/' && j + 1 < n && line.at(j + 1) == u'>'))
            ++j;
        if (j == i)
            j = i + 1;
        out.add(i, j, afterEquals ? CodeToken::String : CodeToken::Attribute);
        afterEquals = false;
        i = j;
    }
    return n;
}

void lexMarkup(const LanguageSpec &s, QStringView line, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    if (state->kind == MarkupComment || state->kind == CData) {
        const quint8 kind = state->kind;
        i = continueOpen(s, line, 0, state);
        out.add(0, i, kind == CData ? CodeToken::String : CodeToken::Comment);
    } else if (state->kind == MarkupTag) {
        i = lexTagBody(line, 0, state, out);
    }
    while (i < n) {
        const QChar c = line.at(i);
        if (c == u'<') {
            if (startsAt(line, i, u"<!--")) {
                *state = {MarkupComment, 1};
                const qsizetype end = continueOpen(s, line, i + 4, state);
                out.add(i, end, CodeToken::Comment);
                i = end;
                continue;
            }
            if (startsAt(line, i, u"<![CDATA[")) {
                *state = {CData, 0};
                const qsizetype end = continueOpen(s, line, i + 9, state);
                out.add(i, end, CodeToken::String);
                i = end;
                continue;
            }
            if (i + 1 < n && (line.at(i + 1) == u'!' || line.at(i + 1) == u'?')) {
                const qsizetype close = line.indexOf(u'>', i);
                const qsizetype end = close < 0 ? n : close + 1;
                out.add(i, end, CodeToken::Preprocessor);
                i = end;
                continue;
            }
            qsizetype j = i + 1;
            if (j < n && line.at(j) == u'/')
                ++j;
            const qsizetype nameStart = j;
            while (j < n && (isIdentChar(line.at(j)) || QStringView(u":.-").contains(line.at(j))))
                ++j;
            if (j > nameStart) {
                out.add(i, j, CodeToken::Tag);
                *state = {MarkupTag, 0};
                i = lexTagBody(line, j, state, out);
                continue;
            }
            ++i;
            continue;
        }
        if (c == u'&') {
            qsizetype j = i + 1;
            while (j < n && j - i <= 12 && (line.at(j).isLetterOrNumber() || line.at(j) == u'#'))
                ++j;
            if (j < n && j > i + 1 && line.at(j) == u';') {
                out.add(i, j + 1, CodeToken::Constant);
                i = j + 1;
                continue;
            }
        }
        ++i;
    }
}

// ---- Data: JSON, YAML, INI / TOML ------------------------------------------------------------------

/// A scalar (`word`) of YAML or INI: a constant, a number or nothing.
std::optional<CodeToken> scalarKind(const LanguageSpec &s, QStringView word)
{
    if (word.isEmpty())
        return std::nullopt;
    if (s.constants.contains(word.toString()))
        return CodeToken::Constant;
    bool ok = false;
    (void)word.toDouble(&ok);
    if (ok || ((word.startsWith(u"0x") || word.startsWith(u"0o") || word.startsWith(u"0b")) && word.size() > 2))
        return CodeToken::Number;
    return std::nullopt;
}

/// The values of YAML and INI from `i`: strings, comments, constants, numbers, anchors.
void lexValues(const LanguageSpec &s, QStringView line, qsizetype i, CodeState *state, Out &out, bool yaml)
{
    const qsizetype n = line.size();
    while (i < n) {
        const QChar c = line.at(i);
        if (c.isSpace() || QStringView(u",[]{}=").contains(c)) {
            ++i;
            continue;
        }
        if ((c == u'#' || (!yaml && c == u';')) && (i == 0 || line.at(i - 1).isSpace())) {
            out.add(i, n, CodeToken::Comment);
            return;
        }
        if (!yaml && s.tripleQuotes && (c == u'"' || c == u'\'') && startsAt(line, i, QString(3, c))) {
            *state = {TripleString, c.unicode()};
            const qsizetype end = continueOpen(s, line, i + 3, state);
            out.add(i, end, CodeToken::String);
            i = end;
            continue;
        }
        if (c == u'"' || c == u'\'') {
            const qsizetype end = stringEnd(line, i + 1, c, c == u'"', c == u'\'' && yaml);
            out.add(i, end, CodeToken::String);
            i = end;
            continue;
        }
        if (yaml && (c == u'&' || c == u'*' || c == u'!')) {
            qsizetype j = i + 1;
            while (j < n && !line.at(j).isSpace() && !QStringView(u",[]{}").contains(line.at(j)))
                ++j;
            out.add(i, j, c == u'!' ? CodeToken::Type : CodeToken::Variable);
            i = j;
            continue;
        }
        if (yaml && (c == u'|' || c == u'>') && skipSpaces(line, i + 1) >= n) {
            out.add(i, i + 1, CodeToken::Keyword);
            return;
        }
        qsizetype j = i;
        while (j < n && !line.at(j).isSpace() && !QStringView(u",[]{}").contains(line.at(j)))
            ++j;
        if (const std::optional<CodeToken> kind = scalarKind(s, line.sliced(i, j - i)))
            out.add(i, j, *kind);
        i = j;
    }
}

void lexJson(const LanguageSpec &s, QStringView line, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    if (state->kind == BlockComment) {
        i = continueOpen(s, line, 0, state);
        out.add(0, i, CodeToken::Comment);
    }
    while (i < n) {
        const QChar c = line.at(i);
        if (startsAt(line, i, u"//")) {
            out.add(i, n, CodeToken::Comment);
            return;
        }
        if (startsAt(line, i, u"/*")) {
            *state = {BlockComment, 1};
            const qsizetype end = continueOpen(s, line, i + 2, state);
            out.add(i, end, CodeToken::Comment);
            i = end;
            continue;
        }
        if (c == u'"') {
            const qsizetype end = stringEnd(line, i + 1, u'"', true, false);
            const qsizetype next = skipSpaces(line, end);
            out.add(i, end, next < n && line.at(next) == u':' ? CodeToken::Property : CodeToken::String);
            i = end;
            continue;
        }
        if (c.isDigit() || (c == u'-' && i + 1 < n && line.at(i + 1).isDigit())) {
            const qsizetype end = numberEnd(line, c == u'-' ? i + 1 : i);
            out.add(i, end, CodeToken::Number);
            i = end;
            continue;
        }
        if (c.isLetter()) {
            const qsizetype j = identEnd(line, i, false);
            if (s.constants.contains(line.sliced(i, j - i).toString()))
                out.add(i, j, CodeToken::Constant);
            i = j;
            continue;
        }
        ++i;
    }
}

void lexYaml(const LanguageSpec &s, QStringView line, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    qsizetype i = skipSpaces(line, 0);
    if (i >= n)
        return;
    if (line.at(i) == u'#') {
        out.add(i, n, CodeToken::Comment);
        return;
    }
    if (i == 0 && (line.startsWith(u"---") || line.startsWith(u"..."))) {
        out.add(0, 3, CodeToken::Keyword);
        lexValues(s, line, 3, state, out, true);
        return;
    }
    while (i + 1 < n && line.at(i) == u'-' && line.at(i + 1) == u' ') {
        out.add(i, i + 1, CodeToken::Keyword);
        i = skipSpaces(line, i + 1);
    }
    if (i < n && line.at(i) == u'-' && i + 1 == n) {
        out.add(i, n, CodeToken::Keyword);
        return;
    }
    // A key: up to ": " (or ":" at the line's end), quoted or not.
    qsizetype keyEnd = -1;
    qsizetype valueStart = i;
    if (i < n && (line.at(i) == u'"' || line.at(i) == u'\'')) {
        const qsizetype end = stringEnd(line, i + 1, line.at(i), line.at(i) == u'"', false);
        const qsizetype after = skipSpaces(line, end);
        if (after < n && line.at(after) == u':' && (after + 1 == n || line.at(after + 1).isSpace())) {
            keyEnd = end;
            valueStart = after + 1;
        }
    } else {
        for (qsizetype k = i; k < n; ++k) {
            const QChar c = line.at(k);
            if (c == u':' && (k + 1 == n || line.at(k + 1).isSpace())) {
                keyEnd = k;
                valueStart = k + 1;
                break;
            }
            if ((c == u'#' && k > i && line.at(k - 1).isSpace()) || QStringView(u"\"'{[").contains(c))
                break;
        }
    }
    if (keyEnd > i)
        out.add(i, keyEnd, CodeToken::Property);
    lexValues(s, line, valueStart, state, out, true);
}

void lexIni(const LanguageSpec &s, QStringView line, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    if (state->kind == TripleString) {
        i = continueOpen(s, line, 0, state);
        out.add(0, i, CodeToken::String);
        if (state->kind != Plain)
            return;
        lexValues(s, line, i, state, out, false);
        return;
    }
    i = skipSpaces(line, 0);
    if (i >= n)
        return;
    const QChar c = line.at(i);
    if (c == u'#' || c == u';') {
        out.add(i, n, CodeToken::Comment);
        return;
    }
    if (c == u'[') {
        qsizetype close = line.lastIndexOf(u']');
        if (close < i)
            close = n - 1;
        out.add(i, close + 1, CodeToken::Tag);
        lexValues(s, line, close + 1, state, out, false);
        return;
    }
    qsizetype eq = -1;
    for (qsizetype k = i; k < n; ++k) {
        if (line.at(k) == u'=' || line.at(k) == u':') {
            eq = k;
            break;
        }
        if (line.at(k) == u'"' || line.at(k) == u'\'')
            break;
    }
    if (eq > i) {
        qsizetype keyEnd = eq;
        while (keyEnd > i && line.at(keyEnd - 1).isSpace())
            --keyEnd;
        out.add(i, keyEnd, CodeToken::Property);
        lexValues(s, line, eq + 1, state, out, false);
        return;
    }
    lexValues(s, line, i, state, out, false);
}

// ---- Markdown, diffs, .gitignore, CSS --------------------------------------------------------------

void lexMarkdownInline(QStringView line, qsizetype i, Out &out)
{
    const qsizetype n = line.size();
    while (i < n) {
        const QChar c = line.at(i);
        if (c == u'`') {
            qsizetype run = i;
            while (run < n && line.at(run) == u'`')
                ++run;
            const QString ticks = line.sliced(i, run - i).toString();
            const qsizetype close = line.indexOf(ticks, run);
            if (close >= 0) {
                out.add(i, close + ticks.size(), CodeToken::String);
                i = close + ticks.size();
                continue;
            }
            i = run;
            continue;
        }
        if (c == u']' && i + 1 < n && line.at(i + 1) == u'(') {
            const qsizetype close = line.indexOf(u')', i + 2);
            if (close > i + 2) {
                out.add(i + 2, close, CodeToken::Attribute);
                i = close + 1;
                continue;
            }
        }
        if (c == u'<' && (startsAt(line, i, u"<http://") || startsAt(line, i, u"<https://"))) {
            const qsizetype close = line.indexOf(u'>', i);
            if (close > i) {
                out.add(i + 1, close, CodeToken::Attribute);
                i = close + 1;
                continue;
            }
        }
        ++i;
    }
}

void lexMarkdown(QStringView line, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    qsizetype i = 0;
    while (i < n && i < 3 && line.at(i) == u' ')
        ++i;
    const bool fence = i + 2 < n && (line.at(i) == u'`' || line.at(i) == u'~') && line.at(i + 1) == line.at(i)
                       && line.at(i + 2) == line.at(i);
    if (state->kind == MarkdownFence) {
        const QChar f = (state->data & 0x100) ? u'~' : u'`';
        const int len = state->data & 0xff;
        qsizetype run = i;
        while (run < n && line.at(run) == f)
            ++run;
        if (fence && line.at(i) == f && run - i >= len && skipSpaces(line, run) >= n) {
            out.add(0, n, CodeToken::Comment);
            *state = {};
        } else {
            out.add(0, n, CodeToken::String);
        }
        return;
    }
    if (fence) {
        qsizetype run = i;
        while (run < n && line.at(run) == line.at(i))
            ++run;
        *state = {MarkdownFence, quint16(std::min<qsizetype>(run - i, 255) | (line.at(i) == u'~' ? 0x100 : 0))};
        out.add(0, n, CodeToken::Comment);
        return;
    }
    if (i < n && line.at(i) == u'#') {
        qsizetype run = i;
        while (run < n && line.at(run) == u'#')
            ++run;
        if (run - i <= 6 && (run == n || line.at(run) == u' ')) {
            out.add(i, n, CodeToken::Tag);
            return;
        }
    }
    if (i < n && line.at(i) == u'>') {
        out.add(i, i + 1, CodeToken::Keyword);
        ++i;
    }
    i = skipSpaces(line, i);
    if (i + 1 < n && QStringView(u"-*+").contains(line.at(i)) && line.at(i + 1) == u' ') {
        out.add(i, i + 1, CodeToken::Keyword);
        i += 2;
    } else {
        qsizetype d = i;
        while (d < n && line.at(d).isDigit())
            ++d;
        if (d > i && d + 1 < n && (line.at(d) == u'.' || line.at(d) == u')') && line.at(d + 1) == u' ') {
            out.add(i, d + 1, CodeToken::Keyword);
            i = d + 2;
        }
    }
    lexMarkdownInline(line, i, out);
}

void lexDiff(QStringView line, Out &out)
{
    const qsizetype n = line.size();
    if (n == 0)
        return;
    if (line.startsWith(u"diff ") || line.startsWith(u"index ") || line.startsWith(u"--- ") || line.startsWith(u"+++ ")
        || line.startsWith(u"new file") || line.startsWith(u"deleted file") || line.startsWith(u"similarity ")
        || line.startsWith(u"rename ") || line.startsWith(u"old mode") || line.startsWith(u"new mode")) {
        out.add(0, n, CodeToken::Keyword);
        return;
    }
    if (line.startsWith(u"@@")) {
        const qsizetype close = line.indexOf(u"@@", 2);
        out.add(0, close < 0 ? n : close + 2, CodeToken::Function);
        return;
    }
    if (line.at(0) == u'+')
        out.add(0, n, CodeToken::Inserted);
    else if (line.at(0) == u'-')
        out.add(0, n, CodeToken::Deleted);
}

void lexGitignore(QStringView line, Out &out)
{
    const qsizetype i = skipSpaces(line, 0);
    if (i < line.size() && line.at(i) == u'#')
        out.add(i, line.size(), CodeToken::Comment);
    else if (i < line.size() && line.at(i) == u'!')
        out.add(i, i + 1, CodeToken::Keyword);
}

void lexCss(const LanguageSpec &s, QStringView language, QStringView line, CodeState *state, Out &out)
{
    const qsizetype n = line.size();
    const bool slashComments = language != u"css";
    qsizetype i = 0;
    if (state->kind == BlockComment) {
        i = continueOpen(s, line, 0, state);
        out.add(0, i, CodeToken::Comment);
    }
    const auto isHex = [](QChar c) { return c.isDigit() || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F'); };
    while (i < n) {
        const QChar c = line.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (startsAt(line, i, u"/*")) {
            *state = {BlockComment, 1};
            const qsizetype end = continueOpen(s, line, i + 2, state);
            out.add(i, end, CodeToken::Comment);
            i = end;
            continue;
        }
        if (slashComments && startsAt(line, i, u"//")) {
            out.add(i, n, CodeToken::Comment);
            return;
        }
        if (c == u'"' || c == u'\'') {
            const qsizetype end = stringEnd(line, i + 1, c, true, false);
            out.add(i, end, CodeToken::String);
            i = end;
            continue;
        }
        if ((c == u'@' || c == u'$') && i + 1 < n && (isIdentStart(line.at(i + 1)) || line.at(i + 1) == u'-')) {
            const qsizetype end = identEnd(line, i + 1, true);
            const bool rule = c == u'@' && (language != u"less" || end >= n || line.at(skipSpaces(line, end)) != u':');
            out.add(i, end, rule ? CodeToken::Keyword : CodeToken::Variable);
            i = end;
            continue;
        }
        if (c == u'!' && startsAt(line, i, u"!important")) {
            out.add(i, i + 10, CodeToken::Keyword);
            i += 10;
            continue;
        }
        if (c == u'#') {
            qsizetype j = i + 1;
            while (j < n && isHex(line.at(j)))
                ++j;
            const qsizetype len = j - i - 1;
            if ((len == 3 || len == 4 || len == 6 || len == 8) && (j >= n || !isIdentChar(line.at(j)))) {
                out.add(i, j, CodeToken::Number);
                i = j;
                continue;
            }
            const qsizetype end = identEnd(line, i + 1, true);
            out.add(i, end, CodeToken::Function);
            i = std::max(end, i + 1);
            continue;
        }
        if (c == u'.' && i + 1 < n && isIdentStart(line.at(i + 1))) {
            const qsizetype end = identEnd(line, i + 1, true);
            out.add(i, end, CodeToken::Type);
            i = end;
            continue;
        }
        if (c.isDigit() || (c == u'.' && i + 1 < n && line.at(i + 1).isDigit())
            || (c == u'-' && i + 1 < n && line.at(i + 1).isDigit())) {
            const qsizetype end = numberEnd(line, c == u'-' ? i + 1 : i);
            out.add(i, end, CodeToken::Number);
            i = end;
            continue;
        }
        if (isIdentStart(c) || (c == u'-' && i + 1 < n && (isIdentStart(line.at(i + 1)) || line.at(i + 1) == u'-'))) {
            qsizetype j = i;
            while (j < n && line.at(j) == u'-')
                ++j;
            j = identEnd(line, j, true);
            const QStringView word = line.sliced(i, j - i);
            const qsizetype next = skipSpaces(line, j);
            const bool colon = next < n && line.at(next) == u':' && !(next + 1 < n && line.at(next + 1) == u':');
            const bool property = colon && (next + 1 >= n || line.at(next + 1).isSpace() || line.at(next + 1).isDigit()
                                            || QStringView(u"#\"'-$@").contains(line.at(next + 1)));
            if (word.startsWith(u"--"))
                out.add(i, j, CodeToken::Variable);
            else if (property)
                out.add(i, j, CodeToken::Property);
            else if (next < n && line.at(next) == u'(')
                out.add(i, j, CodeToken::Function);
            else if (s.constants.contains(word.toString()))
                out.add(i, j, CodeToken::Constant);
            i = j;
            continue;
        }
        ++i;
    }
}

void lexLuau(QStringView line, CodeState *state, Out &out)
{
    syntax::LuauState luau = state->kind == LuauOpen ? syntax::LuauState::unpack(state->data) : syntax::LuauState{};
    for (const syntax::LuauSpan &s : syntax::lexLuauLine(line, &luau)) {
        CodeToken kind = CodeToken::Keyword;
        switch (s.kind) {
        case syntax::LuauToken::Keyword: kind = CodeToken::Keyword; break;
        case syntax::LuauToken::Constant: kind = CodeToken::Constant; break;
        case syntax::LuauToken::Builtin: kind = CodeToken::Builtin; break;
        case syntax::LuauToken::Api:
        case syntax::LuauToken::Function: kind = CodeToken::Function; break;
        case syntax::LuauToken::Number: kind = CodeToken::Number; break;
        case syntax::LuauToken::String:
        case syntax::LuauToken::Interpolation: kind = CodeToken::String; break;
        case syntax::LuauToken::Comment: kind = CodeToken::Comment; break;
        }
        out.add(s.from, s.from + s.length, kind);
    }
    *state = luau.kind == syntax::LuauState::Code ? CodeState{} : CodeState{LuauOpen, quint16(luau.pack())};
}

} // namespace

bool hasSyntax(QStringView language)
{
    return !language.isEmpty() && detail::languageSpec(language) != nullptr;
}

QList<CodeSpan> lexCodeLine(QStringView language, QStringView line, CodeState *state)
{
    Out out;
    CodeState none;
    if (!state)
        state = &none;
    const LanguageSpec *spec = language.isEmpty() ? nullptr : detail::languageSpec(language);
    if (!spec)
        return out.spans;
    const QStringView lexed = line.first(std::min(line.size(), MaxLexedLength));
    switch (spec->lexer) {
    case Lexer::Code: lexCode(*spec, lexed, state, out); break;
    case Lexer::Markup: lexMarkup(*spec, lexed, state, out); break;
    case Lexer::Json: lexJson(*spec, lexed, state, out); break;
    case Lexer::Yaml: lexYaml(*spec, lexed, state, out); break;
    case Lexer::Ini: lexIni(*spec, lexed, state, out); break;
    case Lexer::Markdown: lexMarkdown(lexed, state, out); break;
    case Lexer::Diff: lexDiff(lexed, out); break;
    case Lexer::Gitignore: lexGitignore(lexed, out); break;
    case Lexer::Css: lexCss(*spec, language, lexed, state, out); break;
    case Lexer::Luau: lexLuau(lexed, state, out); break;
    }
    return out.spans;
}

QString codeTokenName(CodeToken kind)
{
    switch (kind) {
    case CodeToken::Keyword: return u"keyword"_s;
    case CodeToken::Type: return u"type"_s;
    case CodeToken::Constant: return u"constant"_s;
    case CodeToken::Builtin: return u"builtin"_s;
    case CodeToken::Function: return u"function"_s;
    case CodeToken::Number: return u"number"_s;
    case CodeToken::String: return u"string"_s;
    case CodeToken::Comment: return u"comment"_s;
    case CodeToken::Preprocessor: return u"preprocessor"_s;
    case CodeToken::Tag: return u"tag"_s;
    case CodeToken::Attribute: return u"attribute"_s;
    case CodeToken::Property: return u"property"_s;
    case CodeToken::Variable: return u"variable"_s;
    case CodeToken::Inserted: return u"inserted"_s;
    case CodeToken::Deleted: return u"deleted"_s;
    }
    return {};
}

} // namespace brulion::codefiles

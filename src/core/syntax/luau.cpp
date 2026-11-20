#include "syntax/luau.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::syntax {

namespace {

constexpr int kMaxInterpolation = 32; // braces of `{…}` inside `…{…}…` strings, nested

bool isIdentStart(QChar c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_'; }
bool isIdentPart(QChar c) { return isIdentStart(c) || (c >= u'0' && c <= u'9'); }
bool isDigit(QChar c) { return c >= u'0' && c <= u'9'; }
bool isHexDigit(QChar c) { return isDigit(c) || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F'); }

const QSet<QString> &keywords()
{
    static const QSet<QString> set{u"and"_s,   u"break"_s,  u"do"_s,     u"else"_s,  u"elseif"_s, u"end"_s,
                                   u"for"_s,   u"function"_s, u"if"_s,   u"in"_s,    u"local"_s,  u"not"_s,
                                   u"or"_s,    u"repeat"_s, u"return"_s, u"then"_s,  u"until"_s,  u"while"_s,
                                   u"continue"_s};
    return set;
}

const QSet<QString> &builtins()
{
    // Luau's globals in Brulion's sandbox (docs/EXTENSIONS.md "Luau in Brulion"), and `b`.
    static const QSet<QString> set{
            u"b"_s,         u"string"_s,       u"table"_s,    u"math"_s,     u"utf8"_s,      u"bit32"_s,
            u"buffer"_s,    u"vector"_s,       u"coroutine"_s, u"os"_s,      u"debug"_s,     u"print"_s,
            u"assert"_s,    u"error"_s,        u"getmetatable"_s, u"setmetatable"_s, u"ipairs"_s, u"pairs"_s,
            u"next"_s,      u"pcall"_s,        u"xpcall"_s,   u"rawequal"_s, u"rawget"_s,    u"rawset"_s,
            u"rawlen"_s,    u"select"_s,       u"tonumber"_s, u"tostring"_s, u"type"_s,      u"typeof"_s,
            u"unpack"_s,    u"gcinfo"_s,       u"newproxy"_s, u"self"_s};
    return set;
}

/// The length of a long bracket's opening `[==[` at `at` (its `=` count in *level), 0 when none.
int longBracketOpen(QStringView s, qsizetype at, int *level)
{
    if (at >= s.size() || s[at] != u'[')
        return 0;
    qsizetype i = at + 1;
    while (i < s.size() && s[i] == u'=')
        ++i;
    if (i >= s.size() || s[i] != u'[')
        return 0;
    *level = int(std::min<qsizetype>(i - at - 1, 255));
    return int(i - at + 1);
}

/// The end (after its last `]`) of the long bracket of `level` closing at or after `from`; -1 when
/// it does not close on this line.
qsizetype longBracketClose(QStringView s, qsizetype from, int level)
{
    for (qsizetype i = s.indexOf(u']', from); i >= 0; i = s.indexOf(u']', i + 1)) {
        qsizetype j = i + 1;
        int n = 0;
        while (j < s.size() && s[j] == u'=' && n < level) {
            ++j;
            ++n;
        }
        if (n == level && j < s.size() && s[j] == u']')
            return j + 1;
    }
    return -1;
}

class Lexer {
public:
    Lexer(QStringView line, LuauState *state) : m_s(line), m_state(state) { }

    QList<LuauSpan> run()
    {
        qsizetype pos = 0;
        const LuauState start = *m_state;
        *m_state = LuauState{};
        switch (start.kind) {
        case LuauState::LongString:
        case LuauState::LongComment: {
            const qsizetype end = longBracketClose(m_s, 0, start.level);
            const LuauToken kind = start.kind == LuauState::LongString ? LuauToken::String : LuauToken::Comment;
            if (end < 0) {
                add(0, m_s.size(), kind);
                *m_state = start;
                return m_spans;
            }
            add(0, end, kind);
            pos = end;
            break;
        }
        case LuauState::Backtick:
            pos = backtick(0, 0, true);
            break;
        case LuauState::Code: break;
        }
        code(pos, 0, false);
        return m_spans;
    }

private:
    void add(qsizetype from, qsizetype to, LuauToken kind)
    {
        if (to <= from)
            return;
        // Adjacent spans of one kind are one (a string around an escaped brace).
        if (!m_spans.isEmpty() && m_spans.last().kind == kind && m_spans.last().from + m_spans.last().length == from) {
            m_spans.last().length += int(to - from);
            return;
        }
        m_spans.append({int(from), int(to - from), kind});
    }

    QChar at(qsizetype i) const { return i < m_s.size() ? m_s[i] : QChar(); }

    /// The previous non-blank character before `i` ("" at the start).
    QChar before(qsizetype i) const
    {
        while (i > 0 && (m_s[i - 1] == u' ' || m_s[i - 1] == u'\t'))
            --i;
        return i > 0 ? m_s[i - 1] : QChar();
    }

    /// The name at `i` is a field: after a `.` (not the `..` of a concatenation) or a `:`.
    bool isField(qsizetype i) const
    {
        while (i > 0 && (m_s[i - 1] == u' ' || m_s[i - 1] == u'\t'))
            --i;
        if (i == 0)
            return false;
        if (m_s[i - 1] == u':')
            return true;
        return m_s[i - 1] == u'.' && (i < 2 || m_s[i - 2] != u'.');
    }

    /// A quoted string from its quote at `pos`: its end (after the closing quote, else the line's).
    qsizetype quoted(qsizetype pos)
    {
        const QChar quote = m_s[pos];
        qsizetype i = pos + 1;
        while (i < m_s.size()) {
            if (m_s[i] == u'\\') {
                i += 2;
                continue;
            }
            if (m_s[i] == quote) {
                ++i;
                break;
            }
            ++i;
        }
        i = std::min(i, m_s.size());
        add(pos, i, LuauToken::String);
        return i;
    }

    /// A backtick string from `pos` (its opening backtick, or the line's start when `continued`):
    /// its end. `{…}` inside is code again (`depth` interpolations deep).
    qsizetype backtick(qsizetype pos, int depth, bool continued)
    {
        qsizetype i = continued ? pos : pos + 1;
        qsizetype runStart = pos;
        while (i < m_s.size()) {
            const QChar c = m_s[i];
            if (c == u'\\') {
                if (i + 1 >= m_s.size()) { // a line break in the string: it goes on
                    add(runStart, m_s.size(), LuauToken::String);
                    if (depth == 0)
                        *m_state = LuauState{LuauState::Backtick, 0};
                    return m_s.size();
                }
                i += 2;
                continue;
            }
            if (c == u'`') {
                add(runStart, i + 1, LuauToken::String);
                return i + 1;
            }
            if (c == u'{' && depth < kMaxInterpolation) {
                add(runStart, i, LuauToken::String);
                add(i, i + 1, LuauToken::Interpolation);
                const qsizetype close = code(i + 1, depth + 1, true);
                if (close >= m_s.size())
                    return m_s.size(); // the line ended inside the interpolation
                add(close, close + 1, LuauToken::Interpolation);
                i = close + 1;
                runStart = i;
                continue;
            }
            ++i;
        }
        add(runStart, m_s.size(), LuauToken::String);
        return m_s.size();
    }

    /// A number from `pos`: its end.
    qsizetype number(qsizetype pos)
    {
        qsizetype i = pos;
        if (m_s[i] == u'0' && (at(i + 1) == u'x' || at(i + 1) == u'X')) {
            i += 2;
            while (i < m_s.size() && (isHexDigit(m_s[i]) || m_s[i] == u'_'))
                ++i;
        } else if (m_s[i] == u'0' && (at(i + 1) == u'b' || at(i + 1) == u'B')) {
            i += 2;
            while (i < m_s.size() && (m_s[i] == u'0' || m_s[i] == u'1' || m_s[i] == u'_'))
                ++i;
        } else {
            while (i < m_s.size() && (isDigit(m_s[i]) || m_s[i] == u'_'))
                ++i;
            if (at(i) == u'.' && at(i + 1) != u'.') {
                ++i;
                while (i < m_s.size() && (isDigit(m_s[i]) || m_s[i] == u'_'))
                    ++i;
            }
            if ((at(i) == u'e' || at(i) == u'E')
                && (isDigit(at(i + 1)) || ((at(i + 1) == u'+' || at(i + 1) == u'-') && isDigit(at(i + 2))))) {
                i += 2;
                while (i < m_s.size() && isDigit(m_s[i]))
                    ++i;
            }
        }
        add(pos, i, LuauToken::Number);
        return i;
    }

    /// Code from `pos`; inside an interpolation (`closing`) it stops at the `}` that closes it and
    /// returns its position, else at the line's end.
    qsizetype code(qsizetype pos, int depth, bool closing)
    {
        int braces = 0;
        bool afterFunction = false; // the name a `function` statement defines comes next
        bool afterB = false; // a field of `b` comes next (after its `.`)
        qsizetype i = pos;
        while (i < m_s.size()) {
            const QChar c = m_s[i];
            if (c == u' ' || c == u'\t') {
                ++i;
                continue;
            }
            if (c == u'-' && at(i + 1) == u'-') {
                int level = 0;
                if (const int open = longBracketOpen(m_s, i + 2, &level)) {
                    const qsizetype end = longBracketClose(m_s, i + 2 + open, level);
                    if (end < 0) {
                        add(i, m_s.size(), LuauToken::Comment);
                        if (depth == 0)
                            *m_state = LuauState{LuauState::LongComment, level};
                        return m_s.size();
                    }
                    add(i, end, LuauToken::Comment);
                    i = end;
                    continue;
                }
                add(i, m_s.size(), LuauToken::Comment);
                return m_s.size();
            }
            if (c == u'[') {
                int level = 0;
                if (const int open = longBracketOpen(m_s, i, &level)) {
                    const qsizetype end = longBracketClose(m_s, i + open, level);
                    if (end < 0) {
                        add(i, m_s.size(), LuauToken::String);
                        if (depth == 0)
                            *m_state = LuauState{LuauState::LongString, level};
                        return m_s.size();
                    }
                    add(i, end, LuauToken::String);
                    i = end;
                    continue;
                }
                ++i;
                afterFunction = afterB = false;
                continue;
            }
            if (c == u'"' || c == u'\'') {
                i = quoted(i);
                afterFunction = afterB = false;
                continue;
            }
            if (c == u'`') {
                i = backtick(i, depth, false);
                afterFunction = afterB = false;
                continue;
            }
            if (isDigit(c) || (c == u'.' && isDigit(at(i + 1)) && before(i) != u'.')) {
                i = number(i);
                afterFunction = afterB = false;
                continue;
            }
            if (isIdentStart(c)) {
                qsizetype j = i + 1;
                while (j < m_s.size() && isIdentPart(m_s[j]))
                    ++j;
                const QString word = m_s.sliced(i, j - i).toString();
                const bool field = isField(i);
                if (afterFunction) {
                    // The name and its fields: `function M.walk`, `function obj:method`.
                    qsizetype end = j;
                    while ((at(end) == u'.' || at(end) == u':') && isIdentStart(at(end + 1))) {
                        end += 1;
                        while (end < m_s.size() && isIdentPart(m_s[end]))
                            ++end;
                    }
                    add(i, end, LuauToken::Function);
                    i = end;
                    afterFunction = afterB = false;
                    continue;
                }
                if (field) {
                    if (afterB)
                        add(i, j, LuauToken::Api);
                } else if (keywords().contains(word)) {
                    add(i, j, LuauToken::Keyword);
                    afterFunction = word == u"function";
                    afterB = false;
                    i = j;
                    continue;
                } else if (word == u"nil" || word == u"true" || word == u"false") {
                    add(i, j, LuauToken::Constant);
                } else if (builtins().contains(word)) {
                    add(i, j, LuauToken::Builtin);
                }
                // `b.ui.text`: every field of the chain after `b` is the API's.
                afterB = (word == u"b" && !field) || (field && afterB);
                afterFunction = false;
                i = j;
                continue;
            }
            if (c == u'.' || c == u':') {
                // b.command: the dot keeps `afterB`; `..` (concatenation) ends it.
                if (at(i + 1) == u'.') {
                    afterB = false;
                    i += 2;
                    continue;
                }
                ++i;
                continue;
            }
            if (closing) {
                if (c == u'{') {
                    ++braces;
                } else if (c == u'}') {
                    if (braces == 0)
                        return i;
                    --braces;
                }
            }
            afterFunction = false;
            afterB = false;
            ++i;
        }
        return m_s.size();
    }

    QStringView m_s;
    LuauState *m_state;
    QList<LuauSpan> m_spans;
};

} // namespace

QList<LuauSpan> lexLuauLine(QStringView line, LuauState *state)
{
    LuauState scratch;
    return Lexer(line, state ? state : &scratch).run();
}

QList<LuauSpan> lexLuau(QStringView text)
{
    QList<LuauSpan> out;
    LuauState state;
    qsizetype start = 0;
    while (start <= text.size()) {
        qsizetype end = start;
        while (end < text.size() && text[end] != u'\n' && text[end] != u'\r')
            ++end;
        for (LuauSpan span : lexLuauLine(text.sliced(start, end - start), &state)) {
            span.from += int(start);
            out.append(span);
        }
        if (end >= text.size())
            break;
        start = end + (text[end] == u'\r' && end + 1 < text.size() && text[end + 1] == u'\n' ? 2 : 1);
    }
    return out;
}

QString luauTokenName(LuauToken kind)
{
    switch (kind) {
    case LuauToken::Keyword: return u"keyword"_s;
    case LuauToken::Constant: return u"constant"_s;
    case LuauToken::Builtin: return u"builtin"_s;
    case LuauToken::Api: return u"api"_s;
    case LuauToken::Function: return u"function"_s;
    case LuauToken::Number: return u"number"_s;
    case LuauToken::String: return u"string"_s;
    case LuauToken::Interpolation: return u"interpolation"_s;
    case LuauToken::Comment: return u"comment"_s;
    }
    return u"keyword"_s;
}

bool isLuauLanguage(QStringView language)
{
    return language.compare(u"luau", Qt::CaseInsensitive) == 0 || language.compare(u"lua", Qt::CaseInsensitive) == 0;
}

} // namespace brulion::syntax

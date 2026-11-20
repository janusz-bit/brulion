// Motions, text objects and searches of the Vim engine (vim.h). The word, paragraph and bracket
// motions follow Vim's own algorithms (fwd_word, bck_word, end_word, bckend_word, findpar,
// findmatch in Vim's search.c / textobject.c) over the buffer's lines, so counts, empty lines and
// line ends behave as in Vim.
#include "vim.h"
#include "vim_p.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::vim {

using namespace detail;

// --- Characters --------------------------------------------------------------------------------

int charClass(QChar c)
{
    if (c == u' ' || c == u'\t' || c == QChar(0x00A0) || c.isSpace())
        return 0;
    if (c.isLetterOrNumber() || c == u'_' || c.isMark() || c.isSurrogate())
        return 2;
    return 1;
}

int nextBoundary(const QString &text, int i)
{
    const int n = int(text.size());
    if (i >= n)
        return n;
    ++i;
    while (i < n
           && (text[i].isLowSurrogate() || text[i].isMark() || text[i] == QChar(0x200D)
               || (text[i - 1] == QChar(0x200D)) || (text[i].unicode() >= 0xFE00 && text[i].unicode() <= 0xFE0F)))
        ++i;
    return i;
}

int previousBoundary(const QString &text, int i)
{
    if (i <= 0)
        return 0;
    i = std::min(i, int(text.size()));
    --i;
    while (i > 0
           && (text[i].isLowSurrogate() || text[i].isMark() || text[i] == QChar(0x200D) || text[i - 1] == QChar(0x200D)
               || (text[i].unicode() >= 0xFE00 && text[i].unicode() <= 0xFE0F)))
        --i;
    return i;
}

QString indentationOf(const QString &line)
{
    int i = 0;
    while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
        ++i;
    return line.left(i);
}

namespace {

bool isBlankLine(const QString &s)
{
    for (const QChar c : s)
        if (c != u' ' && c != u'\t')
            return false;
    return true;
}

/// Walks the buffer's characters as Vim's inc() / dec() do: every character of a line, then the
/// line's end (its "NUL": column == length), then the next line.
class Walker {
public:
    Walker(const Buffer *b, Position p) : m_b(b), m_p(p) { load(); }
    Position pos() const { return m_p; }
    void set(Position p)
    {
        const bool other = p.line != m_p.line;
        m_p = p;
        if (other)
            load();
    }
    /// 0 moved on the line, 2 moved onto the line's end, 1 to the next line, -1 at the end.
    int inc()
    {
        if (m_p.column < m_text.size()) {
            m_p.column = nextBoundary(m_text, m_p.column);
            return m_p.column < m_text.size() ? 0 : 2;
        }
        if (m_p.line + 1 < m_b->lineCount()) {
            ++m_p.line;
            m_p.column = 0;
            load();
            return 1;
        }
        return -1;
    }
    /// 0 moved on the line, 1 to the previous line's end, -1 at the start.
    int dec()
    {
        if (m_p.column > 0) {
            m_p.column = previousBoundary(m_text, m_p.column);
            return 0;
        }
        if (m_p.line > 0) {
            --m_p.line;
            load();
            m_p.column = int(m_text.size());
            return 1;
        }
        return -1;
    }
    QChar ch() const { return m_p.column < m_text.size() ? m_text[m_p.column] : QChar(); }
    int cls(bool big) const
    {
        if (m_p.column >= m_text.size())
            return 0;
        const int c = charClass(m_text[m_p.column]);
        return big && c != 0 ? 1 : c;
    }
    bool lineEmpty() const { return m_text.isEmpty(); }
    const QString &text() const { return m_text; }

private:
    void load() { m_text = m_b->line(m_p.line); }
    const Buffer *m_b;
    Position m_p;
    QString m_text;
};

/// skip_chars(): while the class is `cls` move; true when the buffer's edge stopped it.
bool skipChars(Walker &w, int cls, bool forward, bool big)
{
    while (w.cls(big) == cls)
        if ((forward ? w.inc() : w.dec()) == -1)
            return true;
    return false;
}

/// fwd_word(): `eol` (an operator waits) stops at the end of the line on the last count.
bool forwardWord(Walker &w, int count, bool big, bool eol, const Buffer *b)
{
    while (--count >= 0) {
        const int sclass = w.cls(big);
        const bool lastLine = w.pos().line == b->lineCount() - 1;
        int i = w.inc();
        if (i == -1 || (i >= 1 && lastLine)) // started at the last character of the buffer
            return false;
        if (i >= 1 && eol && count == 0) // started at the last character of the line
            return true;
        if (sclass != 0) {
            while (w.cls(big) == sclass) {
                i = w.inc();
                if (i == -1 || (i >= 1 && eol && count == 0))
                    return true;
            }
        }
        while (w.cls(big) == 0) {
            if (w.pos().column == 0 && w.lineEmpty()) // an empty line is a word
                break;
            i = w.inc();
            if (i == -1 || (i >= 1 && eol && count == 0))
                return true;
        }
    }
    return true;
}

bool backWord(Walker &w, int count, bool big, bool stop)
{
    while (--count >= 0) {
        const int sclass = w.cls(big);
        if (w.dec() == -1)
            return false;
        bool finished = false;
        if (!stop || sclass == w.cls(big) || sclass == 0) {
            while (w.cls(big) == 0) {
                if (w.pos().column == 0 && w.lineEmpty()) {
                    finished = true;
                    break;
                }
                if (w.dec() == -1)
                    return true;
            }
            if (!finished && skipChars(w, w.cls(big), false, big))
                return true;
        }
        if (!finished)
            w.inc();
        stop = false;
    }
    return true;
}

bool endWord(Walker &w, int count, bool big, bool stop, bool empty)
{
    while (--count >= 0) {
        const int sclass = w.cls(big);
        if (w.inc() == -1)
            return false;
        bool finished = false;
        if (w.cls(big) == sclass && sclass != 0) {
            if (skipChars(w, sclass, true, big))
                return false;
        } else if (!stop || sclass == 0) {
            while (w.cls(big) == 0) {
                if (w.pos().column == 0 && w.lineEmpty() && empty) {
                    finished = true;
                    break;
                }
                if (w.inc() == -1)
                    return false;
            }
            if (!finished && skipChars(w, w.cls(big), true, big))
                return false;
        }
        if (!finished)
            w.dec();
        stop = false;
    }
    return true;
}

bool backEndWord(Walker &w, int count, bool big)
{
    while (--count >= 0) {
        const int sclass = w.cls(big);
        if (w.dec() == -1)
            return false;
        if (sclass != 0) {
            while (w.cls(big) == sclass)
                if (w.dec() == -1)
                    return true;
        }
        while (w.cls(big) == 0) {
            if (w.pos().column == 0 && w.lineEmpty())
                break;
            if (w.dec() == -1)
                return true;
        }
    }
    return true;
}

QChar matchingBracket(QChar c, bool &forward)
{
    switch (c.unicode()) {
    case '(': forward = true; return u')';
    case '[': forward = true; return u']';
    case '{': forward = true; return u'}';
    case ')': forward = false; return u'(';
    case ']': forward = false; return u'[';
    case '}': forward = false; return u'{';
    default: return {};
    }
}

/// Smart case: a pattern with a capital letter matches its case, else any case.
Qt::CaseSensitivity caseFor(const QString &pattern)
{
    for (const QChar c : pattern)
        if (c.isUpper())
            return Qt::CaseSensitive;
    return Qt::CaseInsensitive;
}

bool wholeWordAt(const QString &s, int at, int length)
{
    if (at > 0 && charClass(s[at - 1]) == 2)
        return false;
    if (at + length < s.size() && charClass(s[at + length]) == 2)
        return false;
    return true;
}

/// The first match of `pattern` in `s` at or after `from` (-1 none).
int findIn(const QString &s, const QString &pattern, int from, Qt::CaseSensitivity cs, bool wholeWord)
{
    int at = from;
    while (at <= s.size()) {
        at = int(s.indexOf(pattern, at, cs));
        if (at < 0)
            return -1;
        if (!wholeWord || wholeWordAt(s, at, int(pattern.size())))
            return at;
        ++at;
    }
    return -1;
}

/// The last match of `pattern` in `s` starting before `before` (-1 none).
int findLastIn(const QString &s, const QString &pattern, int before, Qt::CaseSensitivity cs, bool wholeWord)
{
    int at = before - 1;
    while (at >= 0) {
        at = int(s.lastIndexOf(pattern, at, cs));
        if (at < 0)
            return -1;
        if (!wholeWord || wholeWordAt(s, at, int(pattern.size())))
            return at;
        --at;
    }
    return -1;
}

} // namespace

// --- Buffer defaults ---------------------------------------------------------------------------

Register Buffer::linesRegister(int first, int last) const
{
    QStringList lines;
    for (int i = first; i <= last; ++i)
        lines.append(line(i));
    Register r;
    r.text = lines.join(u'\n');
    r.linewise = true;
    return r;
}

QVariant Buffer::anchor(Position p) const
{
    return QVariant::fromValue(QList<int>{p.line, p.column});
}

Position Buffer::anchorPosition(const QVariant &anchor) const
{
    const QList<int> l = anchor.value<QList<int>>();
    if (l.size() != 2)
        return {-1, 0};
    return {std::min(l[0], lineCount() - 1), l[1]};
}

// --- Motions -----------------------------------------------------------------------------------

Engine::Motion Engine::motion(const QString &keys, const QString &arg, int count, bool hasCount, bool forOperator)
{
    Motion m;
    const Position cur = m_buffer->cursor();
    const int n = std::max(1, count);
    m.to = cur;
    const auto lineMotion = [&](int line) {
        m.to = Position{std::clamp(line, 0, lastLine()), 0};
        m.kind = MotionKind::Linewise;
        m.valid = true;
    };

    if (keys == u"h" || keys == tok(Backspace) || keys == tok(Left) || keys == tok(ctrl('H'))) {
        if (cur.column == 0)
            return m; // (valid false: Vim beeps; an operator does nothing)
        Position p = cur;
        for (int i = 0; i < n && p.column > 0; ++i)
            p = prevChar(p);
        m.to = p;
        m.valid = true;
        return m;
    }
    if (keys == u"l" || keys == u" " || keys == tok(Right)) {
        const int len = lineLength(cur.line);
        Position p = cur;
        for (int i = 0; i < n; ++i) {
            const Position next = afterChar(p);
            if (next.column >= len && !forOperator) // the caret stays on the last character
                break;
            p = next;
            if (p.column >= len)
                break;
        }
        if (p == cur && !(forOperator && len > 0 && cur.column < len))
            return m;
        m.to = p;
        m.valid = true;
        return m;
    }
    if (keys == u"j" || keys == tok(Down) || keys == u"gj" || keys == tok(ctrl('N')) || keys == tok(ctrl('J'))) {
        if (cur.line + n > lastLine() && cur.line == lastLine())
            return m;
        m.to = Position{std::min(cur.line + n, lastLine()), cur.column};
        m.kind = MotionKind::Linewise;
        m.keepWant = true;
        m.valid = true;
        return m;
    }
    if (keys == u"k" || keys == tok(Up) || keys == u"gk" || keys == tok(ctrl('P'))) {
        if (cur.line == 0)
            return m;
        m.to = Position{std::max(cur.line - n, 0), cur.column};
        m.kind = MotionKind::Linewise;
        m.keepWant = true;
        m.valid = true;
        return m;
    }
    if (keys == u"+" || keys == tok(Enter) || keys == u"_" || keys == u"-") {
        const int target = keys == u"-" ? cur.line - n : keys == u"_" ? cur.line + n - 1 : cur.line + n;
        if (target < 0 || target > lastLine())
            return m;
        m.to = firstNonBlank(target);
        m.kind = MotionKind::Linewise;
        m.valid = true;
        return m;
    }
    if (keys == u"0" || keys == tok(Home) || keys == u"g0") {
        m.to = Position{cur.line, 0};
        m.valid = true;
        return m;
    }
    if (keys == u"^") {
        m.to = firstNonBlank(cur.line);
        m.valid = true;
        return m;
    }
    if (keys == u"$" || keys == tok(End) || keys == u"g$") {
        const int line = std::min(cur.line + n - 1, lastLine());
        const int len = lineLength(line);
        m.to = Position{line, len == 0 ? 0 : previousBoundary(lineText(line), len)};
        m.kind = MotionKind::Inclusive;
        m.wantEnd = true;
        m.valid = true;
        if (len == 0 && forOperator)
            m.kind = MotionKind::Exclusive;
        return m;
    }
    if (keys == u"g_") {
        const int line = std::min(cur.line + n - 1, lastLine());
        const QString s = lineText(line);
        int end = int(s.size());
        while (end > 0 && (s[end - 1] == u' ' || s[end - 1] == u'\t'))
            --end;
        m.to = Position{line, end == 0 ? 0 : previousBoundary(s, end)};
        m.kind = MotionKind::Inclusive;
        m.valid = true;
        return m;
    }
    if (keys == u"|") {
        const QString s = lineText(cur.line);
        m.to = Position{cur.line, std::min(n - 1, std::max(0, int(s.size()) - 1))};
        m.valid = true;
        return m;
    }
    if (keys == u"gg") {
        lineMotion(hasCount ? n - 1 : 0);
        m.to = firstNonBlank(m.to.line);
        m.jump = true;
        return m;
    }
    if (keys == u"G") {
        lineMotion(hasCount ? n - 1 : lastLine());
        m.to = firstNonBlank(m.to.line);
        m.jump = true;
        return m;
    }
    if (keys == u"w" || keys == u"W" || keys == u"e" || keys == u"E" || keys == u"b" || keys == u"B" || keys == u"ge"
        || keys == u"gE" || keys == u"ce" || keys == u"cE") {
        const bool big = keys.back().isUpper();
        Walker w(m_buffer, cur);
        bool ok = true;
        if (keys == u"w" || keys == u"W") {
            ok = forwardWord(w, n, big, forOperator, m_buffer);
            m.kind = MotionKind::Exclusive;
        } else if (keys == u"e" || keys == u"E" || keys == u"ce" || keys == u"cE") {
            // (ce: cw on a word — end_word() with `stop`: a word's last character stays.)
            ok = endWord(w, n, big, keys[0] == u'c', false);
            m.kind = MotionKind::Inclusive;
        } else if (keys == u"b" || keys == u"B") {
            ok = backWord(w, n, big, false);
            m.kind = MotionKind::Exclusive;
        } else {
            ok = backEndWord(w, n, big);
            m.kind = MotionKind::Inclusive;
        }
        Position p = w.pos();
        if (!forOperator) {
            // The caret never rests on a line's end (an empty line's column 0 is no end).
            if (p.column > 0 && p.column >= lineLength(p.line))
                p.column = previousBoundary(lineText(p.line), p.column);
        } else if ((keys == u"w" || keys == u"W") && p.column > 0 && p.column >= lineLength(p.line) && cur < p) {
            // adjust_cursor(): an operator's w that stopped at the line's end takes the last
            // character, inclusive.
            p.column = previousBoundary(lineText(p.line), p.column);
            m.kind = MotionKind::Inclusive;
        }
        if (!ok && !forOperator && p == cur)
            return m;
        m.to = p;
        m.valid = p != cur || forOperator;
        return m;
    }
    if (keys == u"}" || keys == u"{") {
        const bool forward = keys == u"}";
        int curr = cur.line;
        for (int c = n; c > 0; --c) {
            bool didSkip = false;
            for (bool first = true;; first = false) {
                if (!lineText(curr).isEmpty())
                    didSkip = true;
                if (!first && didSkip && lineText(curr).isEmpty())
                    break;
                const int next = curr + (forward ? 1 : -1);
                if (next < 0 || next > lastLine()) {
                    if (c > 1 && curr == cur.line)
                        return m;
                    break;
                }
                curr = next;
            }
        }
        m.kind = MotionKind::Exclusive;
        if (curr == lastLine() && forward && !lineText(curr).isEmpty()) {
            const QString s = lineText(curr);
            m.to = Position{curr, previousBoundary(s, int(s.size()))};
            m.kind = MotionKind::Inclusive;
        } else {
            m.to = Position{curr, 0};
        }
        m.jump = true;
        m.valid = m.to != cur;
        return m;
    }
    if (keys == u"%") {
        if (hasCount) { // N%: that percentage of the lines
            if (n > 100)
                return m;
            lineMotion((n * m_buffer->lineCount() + 99) / 100 - 1);
            m.to = firstNonBlank(m.to.line);
            m.jump = true;
            return m;
        }
        const QString s = lineText(cur.line);
        int at = cur.column;
        bool forward = true;
        QChar open, close;
        while (at < s.size()) {
            close = matchingBracket(s[at], forward);
            if (!close.isNull()) {
                open = s[at];
                break;
            }
            ++at;
        }
        if (open.isNull())
            return m;
        int depth = 0;
        Position p{cur.line, at};
        for (;;) {
            const QString t = lineText(p.line);
            if (p.column >= 0 && p.column < t.size()) {
                if (t[p.column] == open)
                    ++depth;
                else if (t[p.column] == close && --depth == 0)
                    break;
            }
            if (forward) {
                if (++p.column >= t.size()) {
                    if (p.line >= lastLine())
                        return m;
                    ++p.line;
                    p.column = -1;
                    if (lineText(p.line).isEmpty())
                        continue;
                    p.column = 0;
                    // (re-check this column on the next pass)
                    const QString u = lineText(p.line);
                    if (u[0] == open)
                        ++depth;
                    else if (u[0] == close && --depth == 0)
                        break;
                }
            } else {
                if (--p.column < 0) {
                    do {
                        if (p.line <= 0)
                            return m;
                        --p.line;
                    } while (lineText(p.line).isEmpty());
                    p.column = int(lineText(p.line).size()) - 1;
                    const QString u = lineText(p.line);
                    if (u[p.column] == open)
                        ++depth;
                    else if (u[p.column] == close && --depth == 0)
                        break;
                }
            }
        }
        m.to = p;
        m.kind = MotionKind::Inclusive;
        m.jump = true;
        m.valid = true;
        return m;
    }
    if (keys == u"f" || keys == u"F" || keys == u"t" || keys == u"T" || keys == u";" || keys == u",") {
        QChar kind = keys[0];
        QString target = arg;
        if (kind == u';' || kind == u',') {
            if (m_shared->lastFind.isNull())
                return m;
            kind = m_shared->lastFind;
            target = m_shared->lastFindChar;
            if (keys == u",") {
                switch (kind.unicode()) {
                case 'f': kind = u'F'; break;
                case 'F': kind = u'f'; break;
                case 't': kind = u'T'; break;
                default: kind = u't'; break;
                }
            }
        } else {
            m_shared->lastFind = kind;
            m_shared->lastFindChar = arg;
        }
        if (target.isEmpty())
            return m;
        const QString s = lineText(cur.line);
        const bool forward = kind == u'f' || kind == u't';
        const bool till = kind == u't' || kind == u'T';
        int at = cur.column;
        for (int i = 0; i < n; ++i) {
            int from = at;
            // ; after t / T: Vim (cpo without ';') skips the character right next to the caret.
            if (till && (keys == u";" || keys == u",") && i == 0)
                from = forward ? at + 1 : at - 1;
            const int found = forward ? int(s.indexOf(target, nextBoundary(s, from)))
                                      : (from <= 0 ? -1 : int(s.lastIndexOf(target, previousBoundary(s, from))));
            if (found < 0 || (!forward && found >= from))
                return m;
            at = found;
        }
        if (till)
            at = forward ? previousBoundary(s, at) : nextBoundary(s, at);
        m.to = Position{cur.line, at};
        m.kind = forward ? MotionKind::Inclusive : MotionKind::Exclusive;
        m.valid = true;
        return m;
    }
    if (keys == u"`" || keys == u"'") {
        Position p;
        if (arg.isEmpty() || !markPosition(arg[0], p)) {
            setMessage(Message::MarkNotSet, arg);
            return m;
        }
        if (keys == u"'") {
            m.to = firstNonBlank(p.line);
            m.kind = MotionKind::Linewise;
        } else {
            m.to = Position{p.line, std::min(p.column, lineLength(p.line))};
            m.kind = MotionKind::Exclusive;
        }
        m.jump = true;
        m.valid = true;
        return m;
    }
    if (keys == u"n" || keys == u"N" || keys == u"*" || keys == u"#" || keys == u"g*" || keys == u"g#") {
        QString pattern = m_shared->lastSearch;
        bool forward = m_shared->lastSearchForward;
        bool whole = m_shared->lastSearchWholeWord;
        if (keys == u"*" || keys == u"#" || keys == u"g*" || keys == u"g#") {
            // The word under (or after) the caret.
            const QString s = lineText(cur.line);
            int start = cur.column;
            while (start < s.size() && charClass(s[start]) != 2)
                ++start;
            if (start >= s.size())
                return m;
            int end = start;
            while (end < s.size() && charClass(s[end]) == 2)
                ++end;
            while (start > 0 && charClass(s[start - 1]) == 2)
                --start;
            pattern = s.mid(start, end - start);
            forward = keys == u"*" || keys == u"g*";
            whole = keys.size() == 1;
            m_shared->lastSearch = pattern;
            m_shared->lastSearchForward = forward;
            m_shared->lastSearchWholeWord = whole;
            m_shared->searchHistory.removeAll(pattern);
            m_shared->searchHistory.append(pattern);
            m_buffer->setCursor(Position{cur.line, start}); // a match in the word itself is no move
        } else if (keys == u"N") {
            forward = !forward;
        }
        if (pattern.isEmpty())
            return m;
        Position p = m_buffer->cursor();
        bool found = false;
        for (int i = 0; i < n; ++i) {
            p = searchFrom(p, pattern, forward, whole, true, found);
            if (!found) {
                m_buffer->setCursor(cur);
                return m;
            }
        }
        m.to = p;
        m.kind = MotionKind::Exclusive;
        m.jump = true;
        m.valid = true;
        return m;
    }
    if (keys == tok(ctrl('D')) || keys == tok(ctrl('U')) || keys == tok(PageDown) || keys == tok(PageUp)
        || keys == tok(ctrl('F')) || keys == tok(ctrl('B'))) {
        const bool down = keys == tok(ctrl('D')) || keys == tok(PageDown) || keys == tok(ctrl('F'));
        const bool half = keys == tok(ctrl('D')) || keys == tok(ctrl('U'));
        const int step = half ? std::max(1, m_buffer->pageLines() / 2) : std::max(1, m_buffer->pageLines());
        const int target = std::clamp(cur.line + (down ? step : -step), 0, lastLine());
        if (target == cur.line)
            return m;
        m.to = firstNonBlank(target);
        m.kind = MotionKind::Linewise;
        m.valid = true;
        return m;
    }
    return m;
}

// --- Text objects ------------------------------------------------------------------------------

Engine::Range Engine::textObject(const QString &keys, int count, Position anchor, Position cursor, bool visual) const
{
    Q_UNUSED(anchor);
    Range r;
    const int n = std::max(1, count);
    if (keys.size() != 2)
        return r;
    const bool inner = keys[0] == u'i';
    const QChar what = keys[1];
    const QString s = lineText(cursor.line);

    if (what == u'w' || what == u'W') {
        const bool big = what == u'W';
        if (s.isEmpty()) {
            r.from = r.to = Position{cursor.line, 0};
            r.valid = !inner || true;
            return r;
        }
        const auto cls = [&](int i) {
            const int c = charClass(s[i]);
            return big && c != 0 ? 1 : c;
        };
        int col = std::min(cursor.column, int(s.size()) - 1);
        // The run of one class around the caret.
        const auto runStart = [&](int i) {
            const int c = cls(i);
            while (i > 0 && cls(i - 1) == c)
                --i;
            return i;
        };
        const auto runEnd = [&](int i) { // exclusive
            const int c = cls(i);
            while (i < s.size() && cls(i) == c)
                ++i;
            return i;
        };
        int start = runStart(col);
        int end = runEnd(col);
        if (inner) {
            for (int k = 1; k < n && end < s.size(); ++k)
                end = runEnd(end);
        } else {
            const bool onBlank = cls(col) == 0;
            if (onBlank) {
                // White space and the word after it.
                if (end < s.size())
                    end = runEnd(end);
                for (int k = 1; k < n && end < s.size(); ++k) {
                    end = runEnd(end);
                    if (end < s.size())
                        end = runEnd(end);
                }
            } else {
                for (int k = 1; k < n && end < s.size(); ++k) {
                    if (cls(end) == 0)
                        end = runEnd(end);
                    if (end < s.size())
                        end = runEnd(end);
                }
                if (end < s.size() && cls(end) == 0) {
                    end = runEnd(end); // the white space after it
                } else if (start > 0 && cls(start - 1) == 0) {
                    // None after it: the white space before it (not the line's indentation).
                    int ws = runStart(start - 1);
                    if (ws > 0)
                        start = ws;
                }
            }
        }
        r.from = Position{cursor.line, start};
        r.to = Position{cursor.line, end};
        r.valid = true;
        return r;
    }
    if (what == u'p') {
        int first = cursor.line;
        const bool blank = isBlankLine(lineText(first));
        while (first > 0 && isBlankLine(lineText(first - 1)) == blank)
            --first;
        int last = cursor.line;
        for (int k = 0; k < n; ++k) {
            if (k > 0) {
                if (last >= lastLine())
                    break;
                ++last;
            }
            const bool kind = isBlankLine(lineText(last));
            while (last < lastLine() && isBlankLine(lineText(last + 1)) == kind)
                ++last;
            if (!inner && k == n - 1 && !kind) {
                // ap: the blank lines after it, or else those before it.
                if (last < lastLine() && isBlankLine(lineText(last + 1))) {
                    ++last;
                    while (last < lastLine() && isBlankLine(lineText(last + 1)))
                        ++last;
                } else {
                    while (first > 0 && isBlankLine(lineText(first - 1)))
                        --first;
                }
            } else if (!inner && k == n - 1 && kind && last < lastLine()) {
                ++last;
                while (last < lastLine() && !isBlankLine(lineText(last + 1)))
                    ++last;
            }
        }
        r.from = Position{first, 0};
        r.to = Position{last, 0};
        r.linewise = true;
        r.valid = true;
        return r;
    }
    if (what == u'"' || what == u'\'' || what == u'`') {
        // Quotes pair up from the line's start (Vim's way); the caret in one, or the first pair
        // after it.
        QList<int> quotes;
        for (int i = 0; i < s.size(); ++i) {
            if (s[i] == u'\\') {
                ++i;
                continue;
            }
            if (s[i] == what)
                quotes.append(i);
        }
        int open = -1, close = -1;
        for (int i = 0; i + 1 < quotes.size(); i += 2) {
            if (cursor.column >= quotes[i] && cursor.column <= quotes[i + 1]) {
                open = quotes[i];
                close = quotes[i + 1];
                break;
            }
            if (quotes[i] > cursor.column) {
                open = quotes[i];
                close = quotes[i + 1];
                break;
            }
        }
        if (open < 0)
            return r;
        if (inner) {
            r.from = Position{cursor.line, open + 1};
            r.to = Position{cursor.line, close};
        } else {
            int from = open, to = close + 1;
            if (to < s.size() && (s[to] == u' ' || s[to] == u'\t')) {
                while (to < s.size() && (s[to] == u' ' || s[to] == u'\t'))
                    ++to;
            } else {
                while (from > 0 && (s[from - 1] == u' ' || s[from - 1] == u'\t'))
                    --from;
            }
            r.from = Position{cursor.line, from};
            r.to = Position{cursor.line, to};
        }
        r.valid = true;
        return r;
    }
    QChar open, close;
    switch (what.unicode()) {
    case '(':
    case ')':
    case 'b':
        open = u'(';
        close = u')';
        break;
    case '[':
    case ']':
        open = u'[';
        close = u']';
        break;
    case '{':
    case '}':
    case 'B':
        open = u'{';
        close = u'}';
        break;
    case '<':
    case '>':
        open = u'<';
        close = u'>';
        break;
    default: return r;
    }
    // The n-th unmatched opening bracket before the caret (the caret on one counts, the caret on
    // a closing one belongs to its pair), then its match.
    Position openAt{-1, 0};
    {
        Position p = cursor;
        QString t = lineText(p.line);
        int found = 0;
        int depth = 0;
        const auto back = [&]() {
            while (--p.column < 0) {
                if (p.line == 0)
                    return false;
                --p.line;
                t = lineText(p.line);
                p.column = int(t.size());
            }
            return true;
        };
        bool more = true;
        if (p.column < t.size() && t[p.column] == open) {
            if (++found == n)
                openAt = p;
            more = openAt.line < 0 && back();
        } else if (p.column < t.size() && t[p.column] == close) {
            more = back();
        } else if (p.column >= t.size()) {
            more = back();
        }
        while (more && openAt.line < 0) {
            if (p.column < t.size()) {
                if (t[p.column] == close) {
                    ++depth;
                } else if (t[p.column] == open) {
                    if (depth == 0) {
                        if (++found == n) {
                            openAt = p;
                            break;
                        }
                    } else {
                        --depth;
                    }
                }
            }
            more = back();
        }
    }
    if (openAt.line < 0)
        return r;
    // Its match.
    Position q = openAt;
    int depth = 0;
    Position closeAt{-1, 0};
    {
        QString t = lineText(q.line);
        for (;;) {
            if (q.column < t.size()) {
                if (t[q.column] == open)
                    ++depth;
                else if (t[q.column] == close && --depth == 0) {
                    closeAt = q;
                    break;
                }
            }
            if (++q.column >= t.size()) {
                if (q.line >= lastLine())
                    break;
                ++q.line;
                t = lineText(q.line);
                q.column = -1;
                ++q.column;
                if (t.isEmpty()) {
                    q.column = 0;
                    continue;
                }
                if (t[0] == open)
                    ++depth;
                else if (t[0] == close && --depth == 0) {
                    closeAt = q;
                    break;
                }
            }
        }
    }
    if (closeAt.line < 0)
        return r;
    if (inner) {
        r.from = Position{openAt.line, openAt.column + 1};
        r.to = closeAt;
        // "{\n…\n}": the inner lines whole (Vim: the text between the line breaks).
        if (r.from.column >= lineLength(openAt.line) && openAt.line < closeAt.line) {
            const QString last = lineText(closeAt.line);
            if (isBlankLine(last.left(closeAt.column))) {
                if (openAt.line + 1 <= closeAt.line - 1) {
                    r.from = Position{openAt.line + 1, 0};
                    r.to = Position{closeAt.line - 1, 0};
                    r.linewise = !visual || true;
                    r.valid = true;
                    return r;
                }
            }
        }
    } else {
        r.from = openAt;
        r.to = Position{closeAt.line, closeAt.column + 1};
    }
    r.valid = true;
    return r;
}

// --- Searching ---------------------------------------------------------------------------------

Position Engine::searchFrom(Position from, const QString &pattern, bool forward, bool wholeWord, bool wrapMessages,
                            bool &found)
{
    found = false;
    const Qt::CaseSensitivity cs = caseFor(pattern);
    const int lines = m_buffer->lineCount();
    if (forward) {
        const QString first = lineText(from.line);
        int at = findIn(first, pattern, nextBoundary(first, from.column), cs, wholeWord);
        if (at >= 0 && from.column < first.size()) {
            found = true;
            return Position{from.line, at};
        }
        for (int k = 1; k <= lines; ++k) {
            const int line = (from.line + k) % lines;
            const QString s = lineText(line);
            const int start = (k == lines) ? 0 : 0;
            at = findIn(s, pattern, start, cs, wholeWord);
            if (k == lines && at >= from.column + 1)
                at = -1;
            if (at >= 0) {
                found = true;
                if (line <= from.line && wrapMessages)
                    setMessage(Message::SearchHitBottom);
                return Position{line, at};
            }
        }
    } else {
        const QString first = lineText(from.line);
        int at = findLastIn(first, pattern, from.column, cs, wholeWord);
        if (at >= 0) {
            found = true;
            return Position{from.line, at};
        }
        for (int k = 1; k <= lines; ++k) {
            const int line = ((from.line - k) % lines + lines) % lines;
            const QString s = lineText(line);
            at = findLastIn(s, pattern, int(s.size()) + 1, cs, wholeWord);
            if (k == lines && at <= from.column)
                at = -1;
            if (at >= 0) {
                found = true;
                if (line >= from.line && wrapMessages)
                    setMessage(Message::SearchHitTop);
                return Position{line, at};
            }
        }
    }
    setMessage(Message::PatternNotFound, pattern);
    return from;
}

} // namespace brulion::vim

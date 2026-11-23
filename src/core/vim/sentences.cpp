// Sentences of the Vim engine (vim.h): the motions ( and ), the text objects is and as. A sentence
// ends at '.', '!' or '?' followed by the end of the line or a blank — closing brackets and quotes
// may stand between — and an empty line is a boundary too (Vim's definition, :h sentence). Kept
// simple for text of notes, Polish and English: a period does not end a sentence after a short list
// of abbreviations ("np.", "tzn.", "itd.", "dr", "e.g."…), after the number of a list item ("1. ")
// or when the next word on the line starts with a small letter ("około 5 kg. i tak").
#include "vim.h"
#include "vim_p.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::vim {

namespace {

bool isBlank(QChar c)
{
    return c == u' ' || c == u'\t';
}

bool isTerminator(QChar c)
{
    return c == u'.' || c == u'!' || c == u'?';
}

bool isCloser(QChar c)
{
    return c == u')' || c == u']' || c == u'"' || c == u'\'' || c == QChar(0x201D) || c == QChar(0x00BB)
           || c == QChar(0x2019) || c == QChar(0x203A);
}

int firstNonBlankIndex(const QString &s)
{
    int i = 0;
    while (i < int(s.size()) && isBlank(s[i]))
        ++i;
    return i;
}

/// The end of `s` without its trailing blanks, not before `from`.
int textEnd(const QString &s, int from)
{
    int e = int(s.size());
    while (e > from && isBlank(s[e - 1]))
        --e;
    return std::max(e, from);
}

/// Abbreviations a period after which does not end a sentence (lower case).
bool isAbbreviation(const QString &word)
{
    static const QStringList known{u"np"_s,   u"tzn"_s, u"tj"_s,  u"itd"_s, u"itp"_s, u"tzw"_s, u"ok"_s,  u"dr"_s,
                                   u"prof"_s, u"mgr"_s, u"inż"_s, u"ul"_s,  u"al"_s,  u"pl"_s,  u"godz"_s, u"tel"_s,
                                   u"nr"_s,   u"wg"_s,  u"zob"_s, u"por"_s, u"ang"_s, u"ks"_s,  u"św"_s,  u"płk"_s,
                                   u"gen"_s,  u"mjr"_s, u"kpt"_s, u"im"_s,  u"etc"_s, u"vs"_s,  u"mr"_s,  u"mrs"_s,
                                   u"ms"_s,   u"jr"_s,  u"sr"_s,  u"st"_s,  u"cf"_s,  u"eg"_s,  u"ie"_s,  u"fig"_s};
    return known.contains(word.toLower());
}

/// Whether the terminator at `s[i]` ends a sentence; `after` is then the index after it and the
/// closing brackets and quotes that follow it.
bool endsSentenceAt(const QString &s, int i, int &after)
{
    const QChar c = s[i];
    if (!isTerminator(c))
        return false;
    int j = i + 1;
    while (j < s.size() && isCloser(s[j]))
        ++j;
    if (j < s.size() && !isBlank(s[j]))
        return false;
    if (c == u'.') {
        int w = i;
        while (w > 0 && s[w - 1].isLetter())
            --w;
        if (w < i && isAbbreviation(s.mid(w, i - w)))
            return false;
        // "1. item": a number from the start of the line.
        int d = i;
        while (d > 0 && s[d - 1] >= u'0' && s[d - 1] <= u'9')
            --d;
        if (d < i && d == firstNonBlankIndex(s))
            return false;
        int k = j;
        while (k < s.size() && isBlank(s[k]))
            ++k;
        if (k < s.size() && s[k].isLower())
            return false;
    }
    after = j;
    return true;
}

/// A line whose last text ends a sentence.
bool lineEndsSentence(const QString &s)
{
    int e = textEnd(s, 0);
    int t = e - 1;
    while (t >= 0 && isCloser(s[t]))
        --t;
    int after = 0;
    return t >= 0 && endsSentenceAt(s, t, after) && after == e;
}

/// The sentences of a buffer, scanned from a known start.
class Sentences {
public:
    explicit Sentences(const Buffer *b) : m_b(b) {}

    int lastLine() const { return m_b->lineCount() - 1; }

    /// A line from which scanning forward finds true sentence starts: after an empty line, or after
    /// a line that ends a sentence.
    int windowStart(int line) const
    {
        int l = std::clamp(line, 0, lastLine());
        if (m_b->line(l).isEmpty()) {
            while (l > 0 && m_b->line(l - 1).isEmpty())
                --l;
            return l;
        }
        while (l > 0) {
            const QString prev = m_b->line(l - 1);
            if (prev.isEmpty() || lineEndsSentence(prev))
                break;
            --l;
        }
        return l;
    }

    /// The first sentence start of the window beginning at `line`.
    Position firstStart(int line) const
    {
        const QString s = m_b->line(line);
        return Position{line, s.isEmpty() ? 0 : std::min(firstNonBlankIndex(s), int(s.size()))};
    }

    /// The sentence starting at `start`: where its text ends, where the next sentence starts (or the
    /// end of the buffer: `hasNext` false).
    void at(Position start, Position &end, Position &next, bool &hasNext) const
    {
        const int lines = m_b->lineCount();
        QString s = m_b->line(start.line);
        if (s.isEmpty()) {
            // A run of empty lines is one boundary.
            int l = start.line + 1;
            while (l < lines && m_b->line(l).isEmpty())
                ++l;
            end = start;
            findNextStart(l, 0, next, hasNext);
            return;
        }
        int l = start.line;
        int i = start.column;
        for (;;) {
            int after = 0;
            for (; i < s.size(); ++i) {
                if (isTerminator(s[i]) && endsSentenceAt(s, i, after)) {
                    end = Position{l, after};
                    int j = after;
                    while (j < s.size() && isBlank(s[j]))
                        ++j;
                    if (j < s.size()) {
                        next = Position{l, j};
                        hasNext = true;
                    } else {
                        findNextStart(l + 1, 0, next, hasNext);
                    }
                    return;
                }
            }
            if (l + 1 >= lines) {
                end = Position{l, textEnd(s, l == start.line ? start.column : 0)};
                next = Position{l, int(s.size())};
                hasNext = false;
                return;
            }
            const QString following = m_b->line(l + 1);
            if (following.isEmpty()) {
                end = Position{l, textEnd(s, l == start.line ? start.column : 0)};
                next = Position{l + 1, 0};
                hasNext = true;
                return;
            }
            ++l;
            s = following;
            i = 0;
        }
    }

    /// The smallest start after `p` (false: none).
    bool nextStartAfter(Position p, Position &out) const
    {
        Position start = firstStart(windowStart(p.line));
        for (;;) {
            if (start > p) {
                out = start;
                return true;
            }
            Position end, next;
            bool hasNext = false;
            at(start, end, next, hasNext);
            if (!hasNext || !(next > start))
                return false;
            start = next;
        }
    }

    /// The largest start before `p` (false: none).
    bool previousStartBefore(Position p, Position &out) const
    {
        int line = p.line;
        for (;;) {
            const int ws = windowStart(line);
            Position start = firstStart(ws);
            bool found = false;
            Position best;
            while (start < p) {
                best = start;
                found = true;
                Position end, next;
                bool hasNext = false;
                at(start, end, next, hasNext);
                if (!hasNext || !(next > start))
                    break;
                start = next;
            }
            if (found) {
                out = best;
                return true;
            }
            if (ws == 0)
                return false;
            line = ws - 1;
        }
    }

private:
    /// The start of the first sentence on line `l` or after it.
    void findNextStart(int l, int, Position &next, bool &hasNext) const
    {
        const int lines = m_b->lineCount();
        for (; l < lines; ++l) {
            const QString t = m_b->line(l);
            if (t.isEmpty()) {
                next = Position{l, 0};
                hasNext = true;
                return;
            }
            const int k = firstNonBlankIndex(t);
            if (k < t.size()) {
                next = Position{l, k};
                hasNext = true;
                return;
            }
        }
        hasNext = false;
        next = Position{lines - 1, int(m_b->line(lines - 1).size())};
    }

    const Buffer *m_b;
};

} // namespace

Engine::Motion Engine::sentenceMotion(bool forward, int count, bool forOperator)
{
    Motion m;
    const Position cur = m_buffer->cursor();
    m.to = cur;
    const Sentences sentences(m_buffer);
    Position p = cur;
    for (int k = 0; k < std::max(1, count); ++k) {
        Position q;
        if (forward ? sentences.nextStartAfter(p, q) : sentences.previousStartBefore(p, q)) {
            p = q;
            continue;
        }
        // No more sentences: the end (or the start) of the text.
        if (forward) {
            const int last = lastLine();
            p = Position{last, lineLength(last)};
            if (!forOperator && lineLength(last) > 0)
                p.column = previousBoundary(lineText(last), lineLength(last));
        } else {
            p = Position{0, 0};
        }
        break;
    }
    if (p == cur)
        return m;
    m.to = p;
    m.kind = MotionKind::Exclusive;
    m.jump = true;
    m.valid = true;
    return m;
}

Engine::Range Engine::sentenceObject(bool inner, int count, Position cursor) const
{
    Range r;
    const Sentences sentences(m_buffer);
    const int n = std::max(1, count);
    if (lineText(cursor.line).isEmpty()) {
        r.from = r.to = Position{cursor.line, 0};
        r.valid = true;
        return r;
    }
    // The sentences and the blanks between them from the window's start on (Item::gap), up to the
    // item with the caret and as many after it as the count needs.
    struct Item {
        Position from;
        Position to;
        bool gap;
    };
    QList<Item> items;
    int at = -1; ///< the item with the caret
    Position start = sentences.firstStart(sentences.windowStart(cursor.line));
    if (cursor < start) {
        items.append(Item{Position{start.line, 0}, start, true});
        at = 0;
    }
    for (;;) {
        Position end, next;
        bool hasNext = false;
        sentences.at(start, end, next, hasNext);
        if (end != start) {
            items.append(Item{start, end, false});
            if (at < 0 && (cursor < end || !hasNext))
                at = int(items.size()) - 1;
        }
        // The blanks after a sentence: its line's, and the line break and indentation up to the
        // next sentence — not the break before an empty line.
        Position gapEnd = next;
        if (hasNext && next.column == 0 && lineText(next.line).isEmpty())
            gapEnd = Position{end.line, lineLength(end.line)};
        if (end != start && gapEnd > end) {
            items.append(Item{end, gapEnd, true});
            if (at < 0 && cursor < gapEnd)
                at = int(items.size()) - 1;
        }
        if (at >= 0 && int(items.size()) - at >= 2 * n + 2)
            break;
        if (!hasNext || !(next > start))
            break;
        start = next;
    }
    if (items.isEmpty())
        return r;
    if (at < 0)
        at = int(items.size()) - 1;
    int last = at;
    if (inner) {
        last = std::min(at + n - 1, int(items.size()) - 1);
    } else if (!items[at].gap) {
        // n sentences, each with the blanks after it.
        int taken = 0;
        for (int pos = at; pos < items.size() && taken < n;) {
            last = pos;
            if (!items[pos].gap) {
                ++taken;
                if (pos + 1 < items.size() && items[pos + 1].gap)
                    last = ++pos;
            }
            ++pos;
        }
    } else {
        // Blanks and the sentence after them, n times.
        int taken = 0;
        for (int pos = at; pos < items.size() && taken < n; ++pos) {
            last = pos;
            if (!items[pos].gap)
                ++taken;
        }
    }
    r.from = items[at].from;
    r.to = items[last].to;
    if (!inner && !items[at].gap && !items[last].gap) {
        // No blanks after it: those before it (not a line's indentation).
        const QString s = lineText(r.from.line);
        int c = r.from.column;
        while (c > 0 && isBlank(s[c - 1]))
            --c;
        if (c > 0)
            r.from.column = c;
    }
    r.valid = true;
    return r;
}

} // namespace brulion::vim

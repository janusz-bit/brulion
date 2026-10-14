#include "markdown/lines_p.h"

#include <QByteArray>
#include <QHash>
#include <QStringDecoder>

namespace brulion::markdown::detail {

Indent indentOf(QStringView s)
{
    Indent out;
    for (; out.chars < s.size(); ++out.chars) {
        const QChar c = s[out.chars];
        if (c == u' ')
            ++out.col;
        else if (c == u'\t')
            out.col += TabWidth - (out.col % TabWidth);
        else
            break;
    }
    return out;
}

qsizetype charsForColumns(QStringView s, int cols)
{
    int col = 0;
    qsizetype i = 0;
    for (; i < s.size() && col < cols; ++i) {
        const QChar c = s[i];
        if (c == u' ')
            ++col;
        else if (c == u'\t')
            col += TabWidth - (col % TabWidth);
        else
            break;
    }
    return i;
}

int columnsOf(QStringView whitespace)
{
    int col = 0;
    for (const QChar c : whitespace) {
        if (c == u'\t')
            col += TabWidth - (col % TabWidth);
        else
            ++col;
    }
    return col;
}

bool isBlank(QStringView s)
{
    for (const QChar c : s) {
        if (!isSpaceOrTab(c))
            return false;
    }
    return true;
}

QStringView trimEndBlanks(QStringView s)
{
    qsizetype end = s.size();
    while (end > 0 && isSpaceOrTab(s[end - 1]))
        --end;
    return s.first(end);
}

bool isDrawerStart(QStringView line)
{
    const QStringView t = trimEndBlanks(line.sliced(indentOf(line).chars));
    if (t.size() < 3 || t[0] != u':' || !t.endsWith(u':') || t[1] < u'A' || t[1] > u'Z')
        return false;
    for (qsizetype i = 2; i + 1 < t.size(); ++i) {
        const QChar c = t[i];
        if (!((c >= u'A' && c <= u'Z') || c == u'_' || c == u'-'))
            return false;
    }
    return true;
}

bool isDrawerEnd(QStringView line)
{
    return trimEndBlanks(line.sliced(indentOf(line).chars)) == u":END:";
}

QString blankOut(QStringView s)
{
    QString out(s.size(), u' ');
    for (qsizetype i = 0; i < s.size(); ++i) {
        if (isLineBreak(s[i]))
            out[i] = s[i];
    }
    return out;
}

namespace {

qsizetype runLength(QStringView s, qsizetype i, QChar ch)
{
    qsizetype j = i;
    while (j < s.size() && s[j] == ch)
        ++j;
    return j - i;
}

/// Index of the maximal backtick runs of a string grouped by length: "first run of exactly
/// `len` backticks at or after `from`" in amortised O(1) when `from` only grows. Built lazily
/// in one pass, so pairing code spans stays linear (CommonMark code span rule).
class BacktickRuns {
public:
    explicit BacktickRuns(QStringView s) : m_s(s) { }

    qsizetype closing(qsizetype from, qsizetype len)
    {
        if (!m_built)
            build();
        const auto it = m_byLength.constFind(len);
        if (it == m_byLength.constEnd())
            return -1;
        const QList<qsizetype> &starts = *it;
        qsizetype &k = m_cursor[len];
        while (k < starts.size() && starts[k] < from)
            ++k;
        return k < starts.size() ? starts[k] : -1;
    }

private:
    void build()
    {
        m_built = true;
        qsizetype i = m_s.indexOf(u'`');
        while (i >= 0) {
            const qsizetype len = runLength(m_s, i, u'`');
            m_byLength[len].append(i);
            i = m_s.indexOf(u'`', i + len);
        }
    }

    QStringView m_s;
    bool m_built = false;
    QHash<qsizetype, QList<qsizetype>> m_byLength;
    QHash<qsizetype, qsizetype> m_cursor;
};

/// Collects masked ranges and builds the masked string once.
class Masker {
public:
    explicit Masker(QStringView src) : m_src(src) { }

    /// Mask [from, to). Ranges must come in increasing, non-overlapping order.
    void mask(qsizetype from, qsizetype to)
    {
        if (to <= from || from < m_last)
            return;
        m_out += m_src.sliced(m_last, from - m_last);
        m_out += blankOut(m_src.sliced(from, to - from));
        m_last = to;
        m_any = true;
    }

    QString result()
    {
        if (!m_any)
            return m_src.toString();
        m_out += m_src.sliced(m_last);
        return m_out;
    }

private:
    QStringView m_src;
    QString m_out;
    qsizetype m_last = 0;
    bool m_any = false;
};

} // namespace

QList<InlineSpan> inlineSpans(QStringView s, MaskOptions options)
{
    QList<InlineSpan> spans;
    const bool comments = options.comments;
    const bool htmlComments = options.htmlComments;
    if (!s.contains(u'`') && (!comments || !s.contains(u"%%")) && (!htmlComments || !s.contains(u"<!--")))
        return spans;
    BacktickRuns runs(s);
    bool noCommentClose = false;
    bool noHtmlClose = false;
    const qsizetype n = s.size();
    qsizetype i = 0;
    while (i < n) {
        const QChar c = s[i];
        if (c == u'\\') {
            i += 2;
            continue;
        }
        if (c == u'`') {
            const qsizetype len = runLength(s, i, u'`');
            const qsizetype close = runs.closing(i + len, len);
            if (close >= 0) {
                spans.append({InlineSpan::Kind::Code, i, close + len, len});
                i = close + len;
            } else {
                i += len;
            }
            continue;
        }
        if (comments && c == u'%' && charAt(s, i + 1) == u'%' && !noCommentClose) {
            const qsizetype close = s.indexOf(u"%%", i + 2);
            if (close >= 0) {
                spans.append({InlineSpan::Kind::Comment, i, close + 2, 0});
                i = close + 2;
                continue;
            }
            noCommentClose = true;
        }
        if (htmlComments && c == u'<' && !noHtmlClose && s.sliced(i).startsWith(u"<!--")) {
            const qsizetype close = s.indexOf(u"-->", i + 4);
            if (close >= 0) {
                spans.append({InlineSpan::Kind::HtmlComment, i, close + 3, 0});
                i = close + 3;
                continue;
            }
            noHtmlClose = true;
        }
        ++i;
    }
    return spans;
}

QString maskInline(QStringView s, MaskOptions options)
{
    const QList<InlineSpan> spans = inlineSpans(s, options);
    if (spans.isEmpty())
        return s.toString();
    Masker m(s);
    for (const InlineSpan &span : spans)
        m.mask(span.from, span.to);
    return m.result();
}

std::optional<FenceOpen> fenceOpen(QStringView s)
{
    const qsizetype start = indentOf(s).chars;
    const QChar ch = charAt(s, start);
    if (ch != u'`' && ch != u'~')
        return std::nullopt;
    const qsizetype len = runLength(s, start, ch);
    if (len < 3)
        return std::nullopt;
    const QStringView info = s.sliced(start + len);
    if (ch == u'`' && info.contains(u'`'))
        return std::nullopt;
    return FenceOpen{ch, len, info.trimmed().toString()};
}

bool isFenceClose(QStringView s, const FenceOpen &open)
{
    const qsizetype start = indentOf(s).chars;
    const QChar ch = charAt(s, start);
    if (ch != open.ch)
        return false;
    const qsizetype len = runLength(s, start, ch);
    return len >= 3 && len >= open.length && isBlank(s.sliced(start + len));
}

QuotePrefix quotePrefixOf(QStringView s)
{
    // (?:[ \t]{0,3}>[ \t]?)+
    QuotePrefix out;
    qsizetype i = 0;
    for (;;) {
        qsizetype j = i;
        int spaces = 0;
        while (spaces < 3 && isSpaceOrTab(charAt(s, j))) {
            ++j;
            ++spaces;
        }
        if (charAt(s, j) != u'>')
            break;
        ++j;
        if (isSpaceOrTab(charAt(s, j)))
            ++j;
        ++out.depth;
        i = j;
    }
    out.length = i;
    return out;
}

namespace {

/// A list item marker followed by whitespace at the start of `s` (after indentation): length of
/// indentation + marker + whitespace, or -1.
qsizetype itemPrefixLength(QStringView s)
{
    qsizetype i = indentOf(s).chars;
    const QChar c = charAt(s, i);
    if (c == u'-' || c == u'*' || c == u'+') {
        ++i;
    } else {
        const qsizetype digits0 = i;
        while (isAsciiDigit(charAt(s, i)) && i - digits0 < 10)
            ++i;
        const qsizetype digits = i - digits0;
        if (digits < 1 || digits > 9 || (charAt(s, i) != u'.' && charAt(s, i) != u')'))
            return -1;
        ++i;
    }
    if (!isSpaceOrTab(charAt(s, i)))
        return -1;
    while (isSpaceOrTab(charAt(s, i)))
        ++i;
    return i;
}

/// A fenced code block opened somewhere in the document (possibly inside containers).
struct OpenFence {
    FenceOpen open;
    int quotes = 0;   ///< blockquote depth of the opening line: the fence ends with its quote
    int itemCol = -1; ///< marker column of a list item whose marker line opened the fence
};

std::optional<OpenFence> fenceOpenIn(QStringView text)
{
    const QuotePrefix q = quotePrefixOf(text);
    QStringView rest = text.sliced(q.length);
    int itemCol = -1;
    const qsizetype item = itemPrefixLength(rest);
    if (item >= 0) {
        itemCol = indentOf(rest).col;
        rest = rest.sliced(item);
    }
    auto open = fenceOpen(rest);
    if (!open)
        return std::nullopt;
    return OpenFence{std::move(*open), q.depth, itemCol};
}

enum class FenceLine { Inside, Close, Outside };

FenceLine fenceLine(QStringView text, const OpenFence &f)
{
    const QuotePrefix q = quotePrefixOf(text);
    if (q.depth < f.quotes)
        return FenceLine::Outside;
    const QStringView rest = text.sliced(q.length);
    if (f.itemCol >= 0 && !isBlank(rest) && indentOf(rest).col <= f.itemCol)
        return FenceLine::Outside;
    return isFenceClose(rest, f.open) ? FenceLine::Close : FenceLine::Inside;
}

} // namespace

QList<Range> docCommentRanges(QStringView content, const QList<text::Line> &lines)
{
    QList<Range> ranges;
    if (!content.contains(u"%%"))
        return ranges;
    qsizetype commentStart = -1;
    std::optional<OpenFence> fence;
    for (const text::Line &line : lines) {
        const QStringView t = line.text;
        const qsizetype base = line.start;
        if (commentStart < 0 && fence) {
            const FenceLine where = fenceLine(t, *fence);
            if (where != FenceLine::Outside) {
                if (where == FenceLine::Close)
                    fence.reset();
                continue;
            }
            fence.reset();
        }
        if (commentStart < 0) {
            if (auto open = fenceOpenIn(t)) {
                fence = std::move(open);
                continue;
            }
        }
        if (commentStart < 0 && !t.contains(u"%%"))
            continue;
        qsizetype i = 0;
        BacktickRuns runs(t);
        while (i < t.size()) {
            if (commentStart >= 0) {
                const qsizetype close = t.indexOf(u"%%", i);
                if (close < 0)
                    break;
                ranges.append({commentStart, base + close + 2});
                commentStart = -1;
                i = close + 2;
                continue;
            }
            const QChar c = t[i];
            if (c == u'\\') {
                i += 2;
                continue;
            }
            if (c == u'`') {
                const qsizetype len = runLength(t, i, u'`');
                const qsizetype close = runs.closing(i + len, len);
                i = close >= 0 ? close + len : i + len;
                continue;
            }
            if (c == u'%' && charAt(t, i + 1) == u'%') {
                commentStart = base + i;
                i += 2;
                continue;
            }
            ++i;
        }
    }
    return ranges;
}

QString maskRanges(QStringView content, const QList<Range> &ranges)
{
    if (ranges.isEmpty())
        return content.toString();
    Masker m(content);
    for (const Range &r : ranges)
        m.mask(r.first, r.second);
    return m.result();
}

namespace {

int hexValue(QChar c)
{
    if (c >= u'0' && c <= u'9')
        return c.unicode() - u'0';
    if (c >= u'a' && c <= u'f')
        return c.unicode() - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c.unicode() - u'A' + 10;
    return -1;
}

} // namespace

QString safeDecode(QStringView s)
{
    if (!s.contains(u'%'))
        return s.toString();
    QString out;
    out.reserve(s.size());
    qsizetype i = 0;
    while (i < s.size()) {
        if (s[i] != u'%' || hexValue(charAt(s, i + 1)) < 0 || hexValue(charAt(s, i + 2)) < 0) {
            out += s[i];
            ++i;
            continue;
        }
        // A maximal run of %XX escapes: decoded together when it is valid UTF-8.
        const qsizetype runStart = i;
        QByteArray bytes;
        while (charAt(s, i) == u'%' && hexValue(charAt(s, i + 1)) >= 0 && hexValue(charAt(s, i + 2)) >= 0) {
            bytes += char(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2]));
            i += 3;
        }
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        const QString decoded = decoder.decode(bytes);
        if (decoder.hasError())
            out += s.sliced(runStart, i - runStart);
        else
            out += decoded;
    }
    return out;
}

} // namespace brulion::markdown::detail

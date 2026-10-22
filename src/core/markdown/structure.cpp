#include "markdown/structure_p.h"

#include "markdown/properties.h"
#include "markdown/tokens.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::markdown::detail {

using namespace Qt::StringLiterals;

namespace {

/// Maximum callout / quote nesting parsed as containers (deeper quotes are plain quote blocks).
constexpr int MaxNest = 16;

qsizetype skipUpTo3Blanks(QStringView t)
{
    qsizetype i = 0;
    while (i < 3 && isSpaceOrTab(charAt(t, i)))
        ++i;
    return i;
}

} // namespace

std::optional<ListMarker> matchListMarker(QStringView t)
{
    ListMarker lm;
    lm.indent = indentOf(t);
    qsizetype i = lm.indent.chars;
    lm.markerFrom = i;
    const QChar c = charAt(t, i);
    if (c == u'-' || c == u'*' || c == u'+') {
        lm.bullet = c;
        ++i;
    } else {
        const qsizetype d0 = i;
        while (isAsciiDigit(charAt(t, i)) && i - d0 <= 9)
            ++i;
        const qsizetype digits = i - d0;
        const QChar delim = charAt(t, i);
        if (digits < 1 || digits > 9 || (delim != u'.' && delim != u')'))
            return std::nullopt;
        lm.ordered = true;
        lm.bullet = delim;
        lm.number = t.sliced(d0, digits).toInt();
        ++i;
    }
    lm.markerEnd = i;
    if (i < t.size() && !isSpaceOrTab(t[i]))
        return std::nullopt;
    if (isThematicBreak(t))
        return std::nullopt;
    int col = lm.indent.col + int(lm.markerEnd - lm.markerFrom);
    qsizetype j = i;
    if (j == t.size()) {
        col += 1;
    } else {
        for (; j < t.size() && isSpaceOrTab(t[j]); ++j)
            col = t[j] == u'\t' ? col + TabWidth - (col % TabWidth) : col + 1;
    }
    lm.contentStart = j;
    lm.contentCol = col;
    return lm;
}

bool isThematicBreak(QStringView t)
{
    qsizetype i = indentOf(t).chars;
    const QChar c = charAt(t, i);
    if (c != u'*' && c != u'-' && c != u'_')
        return false;
    int count = 0;
    for (; i < t.size(); ++i) {
        if (t[i] == c)
            ++count;
        else if (!isSpaceOrTab(t[i]))
            return false;
    }
    return count >= 3;
}

std::optional<AtxHeading> matchAtxHeading(QStringView t)
{
    qsizetype i = skipUpTo3Blanks(t);
    const qsizetype h0 = i;
    while (charAt(t, i) == u'#')
        ++i;
    const qsizetype level = i - h0;
    if (level < 1 || level > 6)
        return std::nullopt;
    if (i < t.size() && !isSpaceOrTab(t[i]))
        return std::nullopt;
    while (isSpaceOrTab(charAt(t, i)))
        ++i;
    return AtxHeading{int(level), i};
}

int setextLevel(QStringView t)
{
    qsizetype i = skipUpTo3Blanks(t);
    const QChar c = charAt(t, i);
    if (c != u'=' && c != u'-')
        return 0;
    while (charAt(t, i) == c)
        ++i;
    if (!isBlank(t.sliced(i)))
        return 0;
    return c == u'=' ? 1 : 2;
}

qsizetype quoteMarkerLength(QStringView t)
{
    qsizetype i = skipUpTo3Blanks(t);
    if (charAt(t, i) != u'>')
        return -1;
    ++i;
    if (isSpaceOrTab(charAt(t, i)))
        ++i;
    return i;
}

std::optional<CalloutHeader> matchCalloutHeader(QStringView t)
{
    // ^\[!([^\]\s]+)\]([+-])?[ \t]*(.*)$
    if (!t.startsWith(u"[!"))
        return std::nullopt;
    qsizetype i = 2;
    while (i < t.size() && t[i] != u']' && !t[i].isSpace())
        ++i;
    if (i == 2 || charAt(t, i) != u']')
        return std::nullopt;
    CalloutHeader out;
    out.type = t.sliced(2, i - 2).toString().toLower();
    ++i;
    if (charAt(t, i) == u'+' || charAt(t, i) == u'-') {
        out.fold = QString(t[i]);
        ++i;
    }
    while (isSpaceOrTab(charAt(t, i)))
        ++i;
    out.titleStart = i;
    return out;
}

namespace {

/// Number of table cells in a row (pipes escaped with '\' don't count).
int cellCount(QStringView row)
{
    QStringView t = row.trimmed();
    if (t.startsWith(u'|'))
        t = t.sliced(1);
    if (t.endsWith(u'|') && !t.endsWith(u"\\|"))
        t.chop(1);
    int n = 1;
    for (qsizetype i = 0; i < t.size(); ++i) {
        if (t[i] == u'\\')
            ++i;
        else if (t[i] == u'|')
            ++n;
    }
    return n;
}

/// GFM delimiter row: `|? :?-+:? (| :?-+:?)* |?` with optional blanks.
bool isDelimiterRow(QStringView s)
{
    const qsizetype n = s.size();
    qsizetype i = 0;
    while (isSpaceOrTab(charAt(s, i)))
        ++i;
    if (charAt(s, i) == u'|')
        ++i;
    int cells = 0;
    for (;;) {
        while (isSpaceOrTab(charAt(s, i)))
            ++i;
        if (i == n)
            return cells > 0;
        if (s[i] == u':')
            ++i;
        const qsizetype d0 = i;
        while (charAt(s, i) == u'-')
            ++i;
        if (i == d0)
            return false;
        if (charAt(s, i) == u':')
            ++i;
        while (isSpaceOrTab(charAt(s, i)))
            ++i;
        ++cells;
        if (i == n)
            return true;
        if (s[i] != u'|')
            return false;
        ++i;
    }
}

/// HTML block start type (CommonMark types 1, 2, 6, 7), or 0. Type 7 cannot interrupt a paragraph.
int htmlStart(QStringView t, bool inParagraph)
{
    if (!t.contains(u'<'))
        return 0;
    static const QRegularExpression html1(u"^[ \\t]{0,3}<(script|pre|style|textarea)(?=[\\s>]|$)"_s,
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression html2(u"^[ \\t]{0,3}<!--"_s);
    static const QRegularExpression html6(
            u"^[ \\t]{0,3}</?(?:address|article|aside|base|basefont|blockquote|body|caption|center|col|colgroup|dd|"
            "details|dialog|dir|div|dl|dt|fieldset|figcaption|figure|footer|form|frame|frameset|h[1-6]|head|header|hr|"
            "html|iframe|legend|li|link|main|menu|menuitem|nav|noframes|ol|optgroup|option|p|param|search|section|"
            "summary|table|tbody|td|tfoot|th|thead|title|tr|track|ul)(?=[\\s/>]|$)"_s,
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression html7(
            uR"(^[ \t]{0,3}(?:<[A-Za-z][A-Za-z0-9-]*(?:\s+[A-Za-z_:][\w.:-]*(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s"'=<>`]+))?)*\s*/?>|</[A-Za-z][A-Za-z0-9-]*\s*>)[ \t]*$)"_s);
    if (html1.matchView(t).hasMatch())
        return 1;
    if (html2.matchView(t).hasMatch())
        return 2;
    if (html6.matchView(t).hasMatch())
        return 6;
    if (!inParagraph && html7.matchView(t).hasMatch())
        return 7;
    return 0;
}

bool isOrderedOne(const ListMarker &lm)
{
    return lm.number == 1;
}

/// True when line `t` starts a block that ends a paragraph (or a lazy continuation).
bool interruptsParagraph(QStringView t, std::optional<QStringView> next)
{
    if (matchAtxHeading(t) || isThematicBreak(t) || quoteMarkerLength(t) >= 0 || fenceOpen(t))
        return true;
    if (const auto lm = matchListMarker(t)) {
        const bool empty = isBlank(t.sliced(lm->contentStart));
        if (!empty && (!lm->ordered || isOrderedOne(*lm)))
            return true;
    }
    if (htmlStart(t, true))
        return true;
    return isTableStart(t, next);
}

LineView subView(const LineView &v, qsizetype n)
{
    return LineView{v.line, v.offset + n, v.length - n};
}

/// Blocks inside a quote that make it a container (tasks must be found, code must not be scanned).
bool isStructural(const Node &n)
{
    return n.kind == BlockKind::ListItem || n.kind == BlockKind::Code || n.kind == BlockKind::Html
           || n.kind == BlockKind::Callout || n.container;
}

/// Blocks that take their id from a standalone `^id` line after them (Obsidian).
bool takesTrailingId(BlockKind kind)
{
    return kind == BlockKind::Code || kind == BlockKind::Table || kind == BlockKind::Quote || kind == BlockKind::Callout
           || kind == BlockKind::Html || kind == BlockKind::Rule;
}

/// Parses the lines of one container (the document body or a quote's body).
class ContainerParser {
public:
    ContainerParser(Structure &st, QList<LineView> views, int depth, int nest)
        : m_st(st), m_views(std::move(views)), m_depth(depth), m_nest(nest)
    { }

    QList<int> run()
    {
        while (m_i < m_views.size()) {
            const LineView v = m_views[m_i];
            const QStringView t = m_st.maskedText(v);
            if (isBlank(t)) {
                m_pendingBlanks.append(v);
                m_lazyOk = false;
                ++m_i;
                continue;
            }
            if (const auto lm = matchListMarker(t)) {
                openListItem(v, *lm);
                continue;
            }
            if (!m_stack.isEmpty()) {
                if (continueItem(v))
                    continue;
                m_stack.clear();
            }
            m_pendingBlanks.clear();
            m_lazyOk = false;
            parseLeaf();
        }
        return m_roots;
    }

private:
    Node &node(int idx) { return m_st.nodes[idx]; }

    QStringView masked(qsizetype k) const { return m_st.maskedText(m_views[k]); }

    std::optional<QStringView> nextMasked() const
    {
        if (m_i + 1 < m_views.size())
            return masked(m_i + 1);
        return std::nullopt;
    }

    int newNode(BlockKind kind, int line)
    {
        Node n;
        n.kind = kind;
        n.line = n.endLine = n.ownEndLine = line;
        n.depth = m_depth;
        m_st.nodes.append(std::move(n));
        return int(m_st.nodes.size() - 1);
    }

    /// A line that is only `$$` (`opener`: the line's content) opens a display formula up to the
    /// next line that is only `$$` (SYNTAX §11): the lines between are formula, not block syntax
    /// (`+ b`, `- c`, `> d`, `1. e`, `# f`). The closing line's index, or -1 when no such line comes
    /// before a blank line or the end of the container (the `$$` is then text). A search that
    /// fails remembers where it stopped: an opener before that line (an outline of `- $$` items)
    /// fails at once, so the lines of a container are searched at most twice.
    qsizetype displayMathClose(qsizetype k, QStringView opener)
    {
        if (opener.trimmed() != u"$$" || k < m_noMathCloseBefore)
            return -1;
        for (qsizetype j = k + 1; j < m_views.size(); ++j) {
            const QStringView t = masked(j);
            if (isBlank(t)) {
                m_noMathCloseBefore = j;
                return -1;
            }
            if (t.trimmed() == u"$$")
                return j;
        }
        m_noMathCloseBefore = m_views.size();
        if (m_nest == 0)
            m_st.displayMathOpenAtEnd = true; // a quote's lines end with the quote
        return -1;
    }

    /// The lines after a `$$` line up to its closing line `close` as text of node `idx`, from
    /// `contentCol` columns of indentation on (list items), else after all of it; m_i moves past.
    void takeDisplayMath(int idx, qsizetype close, int contentCol = -1)
    {
        for (; m_i <= close; ++m_i) {
            const LineView w = m_views[m_i];
            const QStringView u = m_st.maskedText(w);
            const qsizetype start = contentCol >= 0 ? charsForColumns(u, contentCol) : indentOf(u).chars;
            node(idx).own.append(OwnLine{w, start, LineRole::Text});
        }
    }

    void openListItem(const LineView &v, const ListMarker &lm)
    {
        while (!m_stack.isEmpty() && lm.indent.col <= node(m_stack.last()).markerCol)
            m_stack.removeLast();
        const int parent = m_stack.isEmpty() ? -1 : m_stack.last();
        const int idx = newNode(BlockKind::ListItem, v.line);
        Node &n = node(idx);
        n.depth = m_depth + int(m_stack.size());
        n.markerCol = lm.indent.col;
        n.contentCol = lm.contentCol;
        n.own.append(OwnLine{v, lm.contentStart, LineRole::Text});
        if (parent >= 0)
            node(parent).children.append(idx);
        else
            m_roots.append(idx);
        m_pendingBlanks.clear();
        m_stack.append(idx);
        ++m_i;
        const QStringView content = m_st.maskedText(v).sliced(lm.contentStart);
        if (const auto fo = fenceOpen(content)) {
            node(idx).own.first().role = LineRole::Fence;
            consumeItemFence(idx, *fo);
            m_lazyOk = false;
        } else {
            if (const qsizetype close = displayMathClose(m_i - 1, content); close >= 0)
                takeDisplayMath(idx, close, lm.contentCol);
            m_lazyOk = true;
        }
    }

    /// Attach a non-item line to an open list item (by indentation or lazily).
    bool continueItem(const LineView &v)
    {
        const QStringView t = m_st.maskedText(v);
        const Indent ind = indentOf(t);
        qsizetype k = m_stack.size() - 1;
        // Marker columns grow along the stack: a line at or before the outermost marker matches
        // no open item (checked first so lazy lines under a deep outline cost O(1)).
        if (ind.col <= node(m_stack.first()).markerCol)
            k = -1;
        else
            while (k >= 0 && ind.col <= node(m_stack[k]).markerCol)
                --k;
        if (k >= 0) {
            m_stack.resize(k + 1);
            const int item = m_stack[k];
            for (const LineView &b : std::as_const(m_pendingBlanks))
                node(item).own.append(OwnLine{b, 0, LineRole::Blank});
            m_pendingBlanks.clear();
            const qsizetype start = charsForColumns(t, node(item).contentCol);
            ++m_i;
            if (const auto fo = fenceOpen(t)) {
                node(item).own.append(OwnLine{v, start, LineRole::Fence});
                consumeItemFence(item, *fo);
                m_lazyOk = false;
                return true;
            }
            node(item).own.append(OwnLine{v, start, LineRole::Text});
            if (const qsizetype close = displayMathClose(m_i - 1, t); close >= 0) {
                takeDisplayMath(item, close, node(item).contentCol);
                m_lazyOk = true;
                return true;
            }
            m_lazyOk = !isThematicBreak(t) && !matchAtxHeading(t.sliced(start));
            return true;
        }
        if (m_lazyOk && m_pendingBlanks.isEmpty() && !interruptsParagraph(t, nextMasked())) {
            node(m_stack.last()).own.append(OwnLine{v, ind.chars, LineRole::Text});
            ++m_i;
            if (const qsizetype close = displayMathClose(m_i - 1, t); close >= 0)
                takeDisplayMath(m_stack.last(), close);
            return true;
        }
        return false;
    }

    /// Lines of a fenced code block inside a list item (closed by the fence or by dedent).
    void consumeItemFence(int item, const FenceOpen &fo)
    {
        QList<LineView> blanks;
        const int contentCol = node(item).contentCol;
        const int markerCol = node(item).markerCol;
        const auto flush = [&] {
            for (const LineView &b : std::as_const(blanks))
                node(item).own.append(OwnLine{b, charsForColumns(m_st.maskedText(b), contentCol), LineRole::Code});
            blanks.clear();
        };
        while (m_i < m_views.size()) {
            const LineView v = m_views[m_i];
            const QStringView t = m_st.maskedText(v);
            if (isBlank(t)) {
                blanks.append(v);
                ++m_i;
                continue;
            }
            if (isFenceClose(t, fo)) {
                flush();
                node(item).own.append(OwnLine{v, charsForColumns(t, contentCol), LineRole::Fence});
                ++m_i;
                return;
            }
            if (indentOf(t).col <= markerCol) {
                m_pendingBlanks = blanks;
                return;
            }
            flush();
            node(item).own.append(OwnLine{v, charsForColumns(t, contentCol), LineRole::Code});
            ++m_i;
        }
    }

    void parseLeaf()
    {
        const LineView v = m_views[m_i];
        const QStringView t = m_st.maskedText(v);
        if (const auto fo = fenceOpen(t)) {
            parseFenced(*fo);
            return;
        }
        if (const auto h = matchAtxHeading(t)) {
            const int idx = newNode(BlockKind::Heading, v.line);
            node(idx).headingLevel = h->level;
            node(idx).own.append(OwnLine{v, h->prefixLength, LineRole::Text});
            m_roots.append(idx);
            ++m_i;
            return;
        }
        if (isThematicBreak(t)) {
            const int idx = newNode(BlockKind::Rule, v.line);
            node(idx).own.append(OwnLine{v, 0, LineRole::Meta});
            m_roots.append(idx);
            ++m_i;
            return;
        }
        if (quoteMarkerLength(t) >= 0) {
            parseQuote();
            return;
        }
        if (const int type = htmlStart(t, false)) {
            parseHtml(type);
            return;
        }
        if (isTableStart(t, nextMasked())) {
            parseTable();
            return;
        }
        parseParagraph();
    }

    void parseFenced(const FenceOpen &fo)
    {
        const LineView v = m_views[m_i];
        const int idx = newNode(BlockKind::Code, v.line);
        const QString info = fo.info;
        // The language is the first word of the info string (`query {.x}` -> "query").
        qsizetype a = 0;
        while (a < info.size() && (info[a].isSpace() || info[a] == u'{' || info[a] == u'}'))
            ++a;
        qsizetype b = a;
        while (b < info.size() && !info[b].isSpace() && info[b] != u'{' && info[b] != u'}')
            ++b;
        if (b > a)
            node(idx).lang = info.sliced(a, b - a).toLower();
        node(idx).own.append(OwnLine{v, 0, LineRole::Fence});
        ++m_i;
        QList<LineView> blanks;
        while (m_i < m_views.size()) {
            const LineView w = m_views[m_i];
            if (isFenceClose(m_st.maskedText(w), fo)) {
                for (const LineView &bl : std::as_const(blanks))
                    node(idx).own.append(OwnLine{bl, 0, LineRole::Code});
                node(idx).own.append(OwnLine{w, 0, LineRole::Fence});
                ++m_i;
                m_roots.append(idx);
                return;
            }
            if (isBlank(m_st.maskedText(w))) {
                blanks.append(w);
            } else {
                for (const LineView &bl : std::as_const(blanks))
                    node(idx).own.append(OwnLine{bl, 0, LineRole::Code});
                blanks.clear();
                node(idx).own.append(OwnLine{w, 0, LineRole::Code});
            }
            ++m_i;
        }
        // Unclosed fence: runs to the end of the container (trailing blank lines excluded).
        m_roots.append(idx);
    }

    void parseQuote()
    {
        struct QuoteLine {
            LineView view;
            qsizetype prefix;
        };
        QList<QuoteLine> lines;
        bool lastHadContent = false;
        while (m_i < m_views.size()) {
            const LineView w = m_views[m_i];
            const QStringView u = m_st.maskedText(w);
            const qsizetype q = quoteMarkerLength(u);
            if (q >= 0) {
                lines.append({w, q});
                lastHadContent = !isBlank(u.sliced(q));
                ++m_i;
                continue;
            }
            if (isBlank(u) || !lastHadContent || interruptsParagraph(u, nextMasked()))
                break;
            lines.append({w, indentOf(u).chars}); // lazy continuation
            ++m_i;
        }
        const QuoteLine first = lines.first();
        const int lastLine = lines.last().view.line;
        if (m_nest < MaxNest) {
            if (const auto ch = matchCalloutHeader(m_st.maskedText(first.view).sliced(first.prefix))) {
                const int idx = newNode(BlockKind::Callout, first.view.line);
                node(idx).calloutType = ch->type;
                node(idx).calloutFold = ch->fold;
                node(idx).own.append(OwnLine{first.view, first.prefix + ch->titleStart, LineRole::Text});
                node(idx).endLine = lastLine;
                QList<LineView> sub;
                for (qsizetype k = 1; k < lines.size(); ++k)
                    sub.append(subView(lines[k].view, lines[k].prefix));
                const QList<int> children = ContainerParser(m_st, sub, m_depth + 1, m_nest + 1).run();
                node(idx).children = children;
                m_roots.append(idx);
                return;
            }
            QList<LineView> sub;
            for (const QuoteLine &l : std::as_const(lines))
                sub.append(subView(l.view, l.prefix));
            const qsizetype arenaBefore = m_st.nodes.size();
            const QList<int> children = ContainerParser(m_st, sub, m_depth + 1, m_nest + 1).run();
            const bool structural =
                    std::any_of(children.begin(), children.end(), [&](int c) { return isStructural(m_st.nodes[c]); });
            if (structural) {
                const int idx = newNode(BlockKind::Quote, first.view.line);
                node(idx).container = true;
                node(idx).endLine = lastLine;
                node(idx).children = children;
                m_roots.append(idx);
                return;
            }
            m_st.nodes.resize(arenaBefore); // discard the trial parse
        }
        const int idx = newNode(BlockKind::Quote, first.view.line);
        for (const QuoteLine &l : std::as_const(lines))
            node(idx).own.append(OwnLine{l.view, l.prefix, LineRole::Text});
        m_roots.append(idx);
    }

    void parseHtml(int type)
    {
        const LineView v = m_views[m_i];
        const int idx = newNode(BlockKind::Html, v.line);
        static const QRegularExpression end1(u"</(?:script|pre|style|textarea)>"_s,
                                             QRegularExpression::CaseInsensitiveOption);
        bool first = true;
        while (m_i < m_views.size()) {
            const LineView w = m_views[m_i];
            if (type != 1 && type != 2 && isBlank(m_st.maskedText(w)))
                break;
            node(idx).own.append(OwnLine{w, 0, LineRole::Code});
            ++m_i;
            QStringView probe = m_st.text(w);
            if (first && type == 2)
                probe = probe.sliced(probe.indexOf(u"<!--") + 4);
            first = false;
            if ((type == 1 && end1.matchView(probe).hasMatch()) || (type == 2 && probe.contains(u"-->")))
                break;
        }
        m_roots.append(idx);
    }

    void parseTable()
    {
        const LineView v = m_views[m_i];
        const int idx = newNode(BlockKind::Table, v.line);
        node(idx).own.append(OwnLine{v, 0, LineRole::Text});
        node(idx).own.append(OwnLine{m_views[m_i + 1], 0, LineRole::Meta});
        m_i += 2;
        while (m_i < m_views.size()) {
            const LineView w = m_views[m_i];
            const QStringView u = m_st.maskedText(w);
            if (isBlank(u) || !u.contains(u'|'))
                break;
            node(idx).own.append(OwnLine{w, 0, LineRole::Text});
            ++m_i;
        }
        m_roots.append(idx);
    }

    void parseParagraph()
    {
        const LineView v = m_views[m_i];
        // A standalone `^id` line right after a table / code block / quote names that block.
        const QString id = standaloneBlockId(m_st.maskedText(v));
        if (!id.isEmpty() && !m_roots.isEmpty()) {
            Node &prev = node(m_roots.last());
            if (prev.trailingId.isEmpty() && takesTrailingId(prev.kind)) {
                prev.trailingId = id;
                prev.trailingIdLine = v.line;
                if (prev.kind != BlockKind::Callout && !prev.container)
                    prev.own.append(OwnLine{v, 0, LineRole::Meta});
                ++m_i;
                return;
            }
        }
        const int idx = newNode(BlockKind::Paragraph, v.line);
        node(idx).own.append(OwnLine{v, indentOf(m_st.maskedText(v)).chars, LineRole::Text});
        ++m_i;
        if (const qsizetype close = displayMathClose(m_i - 1, m_st.maskedText(v)); close >= 0)
            takeDisplayMath(idx, close);
        int headingLevel = 0;
        while (m_i < m_views.size()) {
            const LineView w = m_views[m_i];
            const QStringView u = m_st.maskedText(w);
            if (isBlank(u))
                break;
            if (const qsizetype close = displayMathClose(m_i, u); close >= 0) {
                takeDisplayMath(idx, close);
                continue;
            }
            if (const int level = setextLevel(u)) {
                headingLevel = level;
                node(idx).own.append(OwnLine{w, 0, LineRole::Meta});
                ++m_i;
                break;
            }
            if (interruptsParagraph(u, nextMasked()))
                break;
            node(idx).own.append(OwnLine{w, indentOf(u).chars, LineRole::Text});
            ++m_i;
        }
        if (headingLevel) {
            node(idx).kind = BlockKind::Heading;
            node(idx).headingLevel = headingLevel;
        }
        m_roots.append(idx);
    }

    Structure &m_st;
    QList<LineView> m_views;
    int m_depth = 0;
    int m_nest = 0;
    qsizetype m_i = 0;
    QList<int> m_roots;
    QList<int> m_stack; ///< open list items, outermost first
    QList<LineView> m_pendingBlanks;
    bool m_lazyOk = false; ///< the previous line was paragraph text of the innermost item
    /// Where the last failed displayMathClose() search stopped (a blank line, or the end): no line
    /// before it closes a display formula, so a `$$` line before it opens none.
    qsizetype m_noMathCloseBefore = -1;
};

/// Compute ownEndLine / endLine for the forest (iterative post-order; deep outlines are safe).
void finalize(Structure &st)
{
    QList<int> order;
    QList<int> todo = st.roots;
    while (!todo.isEmpty()) {
        const int n = todo.takeLast();
        order.append(n);
        for (int c : std::as_const(st.nodes[n].children))
            todo.append(c);
    }
    for (qsizetype k = order.size() - 1; k >= 0; --k) {
        Node &n = st.nodes[order[k]];
        int own = n.line;
        for (const OwnLine &l : std::as_const(n.own)) {
            if (l.role != LineRole::Blank && l.view.line > own)
                own = l.view.line;
        }
        if (n.kind == BlockKind::Callout)
            own = n.line;
        n.ownEndLine = own;
        int end = std::max({n.endLine, own, n.trailingIdLine});
        for (int c : std::as_const(n.children))
            end = std::max(end, st.nodes[c].endLine);
        n.endLine = end;
    }
}

} // namespace

bool isTableStart(QStringView t, std::optional<QStringView> next)
{
    if (!next || !t.contains(u'|') || !next->contains(u'|') || !next->contains(u'-'))
        return false;
    if (!isDelimiterRow(*next))
        return false;
    return cellCount(t) == cellCount(*next);
}

bool isPropertyLine(QStringView t)
{
    return matchPropertyLine(t).has_value();
}

Structure parseStructure(const QString &content, StructureOptions options)
{
    Structure st;
    st.content = content;
    st.lines = text::splitLines(content);
    st.comments = docCommentRanges(content, st.lines);
    st.masked = maskRanges(content, st.comments);
    if (!st.masked.isEmpty() && st.masked[0].unicode() == 0xfeff)
        st.masked[0] = u' ';
    const int lineCount = int(st.lines.size());
    QList<LineView> views;
    views.reserve(lineCount);
    for (int k = 0; k < lineCount; ++k)
        views.append(st.lineView(k));

    if (options.frontmatter)
        st.frontmatter = findFrontmatter(content);
    int first = 0;
    if (st.frontmatter) {
        Node n;
        n.kind = BlockKind::Frontmatter;
        for (int k = 0; k <= st.frontmatter->endLine && k < lineCount; ++k)
            n.own.append(OwnLine{views[k], 0, LineRole::Code});
        st.nodes.append(std::move(n));
        st.roots.append(int(st.nodes.size() - 1));
        first = st.frontmatter->bodyStartLine;
    }
    // Logseq-style page properties: `key:: value` lines before any other block.
    int j = first;
    while (j < lineCount && isBlank(st.maskedText(views[j])))
        ++j;
    const int propStart = j;
    // (A byte order mark is blanked in `masked`; it is not indentation.)
    const qsizetype bom = !content.isEmpty() && content[0].unicode() == 0xfeff ? 1 : 0;
    while (options.pageProperties && j < lineCount
           && isPropertyLine(st.maskedText(views[j]).sliced(j == 0 ? std::min(bom, views[j].length) : 0)))
        ++j;
    if (j > propStart) {
        Node n;
        n.kind = BlockKind::Frontmatter;
        n.line = n.endLine = n.ownEndLine = propStart;
        for (int k = propStart; k < j; ++k)
            n.own.append(OwnLine{views[k], 0, LineRole::Text});
        st.nodes.append(std::move(n));
        st.roots.append(int(st.nodes.size() - 1));
        first = j;
    }
    const QList<int> body = ContainerParser(st, first == 0 ? views : views.mid(first), 0, 0).run();
    st.roots.append(body);
    finalize(st);
    return st;
}

int nodeAtLine(const Structure &st, int line)
{
    int best = -1;
    const QList<int> *level = &st.roots;
    for (;;) {
        int hit = -1;
        for (int n : *level) {
            const Node &node = st.nodes[n];
            if (node.line <= line && line <= node.endLine) {
                hit = n;
                break;
            }
        }
        if (hit < 0)
            return best;
        best = hit;
        const Node &h = st.nodes[hit];
        const bool ownLine =
                std::any_of(h.own.begin(), h.own.end(), [&](const OwnLine &l) { return l.view.line == line; });
        const bool inChild = std::any_of(h.children.begin(), h.children.end(), [&](int c) {
            return st.nodes[c].line <= line && line <= st.nodes[c].endLine;
        });
        if (ownLine && !inChild)
            return hit;
        level = &h.children;
    }
}

QList<std::pair<int, int>> preOrder(const Structure &st)
{
    QList<std::pair<int, int>> out;
    out.reserve(st.nodes.size());
    QList<std::pair<int, int>> todo;
    for (qsizetype k = st.roots.size() - 1; k >= 0; --k)
        todo.append({st.roots[k], -1});
    while (!todo.isEmpty()) {
        const auto [n, parent] = todo.takeLast();
        out.append({n, parent});
        const QList<int> &children = st.nodes[n].children;
        for (qsizetype k = children.size() - 1; k >= 0; --k)
            todo.append({children[k], n});
    }
    return out;
}

} // namespace brulion::markdown::detail

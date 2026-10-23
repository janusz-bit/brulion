#include "markdown/blockclipboard.h"

#include "markdown/lines_p.h"
#include "markdown/properties.h"
#include "markdown/structure_p.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

/// "\r\n" and "\r" become "\n".
QString lf(const QString &text)
{
    if (!text.contains(u'\r'))
        return text;
    QString out = text;
    out.replace(u"\r\n"_s, u"\n"_s);
    out.replace(u'\r', u'\n');
    return out;
}

/// `line` without `cols` visual columns of indentation. A tab that crosses the edge leaves the
/// rest of its width as spaces; a line with less indentation loses all of it.
QString dedentLine(QStringView line, int cols)
{
    if (cols <= 0)
        return line.toString();
    int col = 0;
    qsizetype i = 0;
    while (i < line.size() && col < cols) {
        const QChar c = line[i];
        if (c == u' ') {
            ++col;
            ++i;
        } else if (c == u'\t') {
            const int next = (col / TabWidth + 1) * TabWidth;
            if (next > cols)
                return QString(next - cols, u' ') + line.sliced(i + 1);
            col = next;
            ++i;
        } else {
            break;
        }
    }
    return line.sliced(i).toString();
}

/// Lines of `text` (LF) dedented by `cols`, joined by "\n".
QString dedented(const QString &text, int cols)
{
    if (cols <= 0)
        return text;
    QStringList lines = text.split(u'\n');
    for (QString &l : lines)
        l = dedentLine(l, cols);
    return lines.join(u'\n');
}

/// Does a block of kind `b` right after one of kind `a` need a blank line to stay a block of its
/// own (BlockDocument keeps the same rule)?
bool needsBlankLine(BlockKind a, BlockKind b)
{
    switch (a) {
    case BlockKind::Paragraph:
    case BlockKind::Html:
    case BlockKind::Quote:
    case BlockKind::Callout:
    case BlockKind::Table: return true;
    case BlockKind::ListItem: return b != BlockKind::ListItem;
    default: return false;
    }
}

/// The top rows in order with their subtree ends, nested or invalid tops skipped.
struct TopRange {
    int first = 0;
    int end = 0; ///< exclusive
};
QList<TopRange> topRanges(const BlockDocument &doc, const QList<int> &tops)
{
    QList<TopRange> out;
    int prevEnd = -1;
    for (const int t : tops) {
        if (t < 0 || t >= doc.count() || t < prevEnd || doc.at(t).kind == BlockKind::Frontmatter)
            continue;
        const RowRange range = doc.subtreeRange(t);
        out.append({range.first, range.first + range.count});
        prevEnd = range.first + range.count;
    }
    return out;
}

/// The separator (line breaks) between block `prev` and block `next`: the blank lines written
/// after `prev`, at least one when the two are not neighbours in the document and would merge.
int blankLinesBetween(const BlockDocument &doc, int prev, int next)
{
    const QString trailing = lf(doc.at(prev).trailing);
    int blank = int(trailing.count(u'\n'));
    if (!trailing.isEmpty() && !trailing.endsWith(u'\n'))
        ++blank;
    if (blank == 0 && next != prev + 1 && needsBlankLine(doc.at(prev).kind, doc.at(next).kind))
        blank = 1;
    return blank;
}

/// The visual column of a list item's marker (its indentation); 0 for other blocks.
int markerColumn(const BlockDocument &doc, int i)
{
    if (doc.at(i).kind != BlockKind::ListItem)
        return 0;
    return indentOf(doc.blockSource(i)).col;
}

// --- Plain text ---------------------------------------------------------------------------------

/// A line of text with references resolved, its block id dropped and inline markup stripped.
QString plainLine(const QString &line, const PlainTextOptions &options)
{
    QString t = line;
    if (const auto id = blockIdAtEnd(t))
        t = t.first(id->from) + t.sliced(id->to);
    static const QRegularExpression ref(u"!?\\(\\(([A-Za-z0-9-]+)\\)\\)"_s);
    if (t.contains(u"((")) {
        QString out;
        qsizetype at = 0;
        auto it = ref.globalMatch(t);
        while (it.hasNext()) {
            const auto m = it.next();
            out += t.sliced(at, m.capturedStart() - at);
            const QString text = options.blockText ? options.blockText(m.captured(1)) : QString();
            out += text.isEmpty() ? m.captured(0) : stripInlineMarkup(lf(text)).split(u'\n').join(u' ');
            at = m.capturedEnd();
        }
        t = out + t.sliced(at);
    }
    return stripInlineMarkup(t);
}

/// A property line that only holds the block's id or Logseq bookkeeping.
bool isIdPropertyLine(QStringView line)
{
    const auto p = matchPropertyLine(line.trimmed());
    return p
           && (p->key.compare(u"id"_s, Qt::CaseInsensitive) == 0
               || p->key.compare(u"collapsed"_s, Qt::CaseInsensitive) == 0);
}

/// The text lines of one block as plain text (no indentation, no marker yet).
QStringList plainLines(const BlockEntry &e, const PlainTextOptions &options)
{
    QStringList src = lf(e.body).split(u'\n');
    QStringList out;
    switch (e.kind) {
    case BlockKind::Table:
    case BlockKind::Html:
        for (const QString &l : std::as_const(src)) {
            if (standaloneBlockId(l).isEmpty() && !isIdPropertyLine(l))
                out.append(l);
        }
        return out;
    case BlockKind::Rule: return {u"———"_s};
    case BlockKind::Frontmatter: return {};
    case BlockKind::Quote:
    case BlockKind::Callout: {
        bool first = true;
        for (const QString &l : std::as_const(src)) {
            QStringView rest = l;
            // Every level of quote markers.
            for (qsizetype n = quoteMarkerLength(rest); n > 0; n = quoteMarkerLength(rest))
                rest = rest.sliced(n);
            if (first && e.kind == BlockKind::Callout) {
                if (const auto h = matchCalloutHeader(rest)) {
                    const QString title = rest.sliced(h->titleStart).trimmed().toString();
                    first = false;
                    if (!title.isEmpty())
                        out.append(plainLine(title, options));
                    continue;
                }
            }
            first = false;
            if (!standaloneBlockId(rest).isEmpty() || isIdPropertyLine(rest))
                continue;
            out.append(plainLine(rest.toString(), options));
        }
        return out;
    }
    default: break;
    }
    // Text, headings, list items, code: fences dropped, code verbatim.
    std::optional<FenceOpen> fence;
    for (qsizetype k = 0; k < src.size(); ++k) {
        QString l = src[k];
        if (fence) {
            if (isFenceClose(l, *fence))
                fence.reset();
            else
                out.append(l);
            continue;
        }
        if (const auto open = fenceOpen(l)) {
            fence = open;
            continue;
        }
        if (!standaloneBlockId(l).isEmpty() || isIdPropertyLine(l))
            continue;
        if (k == 0 && (e.kind == BlockKind::Heading || e.kind == BlockKind::ListItem)) {
            static const QRegularExpression heading(u"^[ ]{0,3}#{1,6}(?:[ \\t]+|$)"_s);
            static const QRegularExpression closing(u"[ \\t]+#+[ \\t]*$"_s);
            if (const auto m = heading.match(l); m.hasMatch()) {
                l = l.sliced(m.capturedEnd());
                l.remove(closing);
            }
        }
        // A setext heading's underline.
        if (k == 1 && e.kind == BlockKind::Heading && setextLevel(l) > 0)
            continue;
        out.append(plainLine(l, options));
    }
    return out;
}

} // namespace

QList<int> topRows(const BlockDocument &doc, const QList<int> &rows)
{
    QList<int> sorted;
    sorted.reserve(rows.size());
    for (const int r : rows) {
        if (r >= 0 && r < doc.count() && doc.at(r).kind != BlockKind::Frontmatter)
            sorted.append(r);
    }
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    const QSet<int> in(sorted.cbegin(), sorted.cend());
    QList<int> out;
    for (const int r : std::as_const(sorted)) {
        bool nested = false;
        for (int p = doc.parent(r); p >= 0 && !nested; p = doc.parent(p))
            nested = in.contains(p);
        if (!nested)
            out.append(r);
    }
    return out;
}

QString blocksMarkdown(const BlockDocument &doc, const QList<int> &tops)
{
    QString out;
    int prev = -1;
    for (const TopRange &range : topRanges(doc, tops)) {
        const int cols = markerColumn(doc, range.first);
        for (int i = range.first; i < range.end; ++i) {
            if (prev >= 0) {
                const QString between = i == prev + 1 ? lf(doc.at(prev).trailing) : QString();
                if (i == prev + 1 && !between.isEmpty()) {
                    // The blank lines as written (whitespace on them dedented like the text).
                    out += u'\n' + dedented(between.endsWith(u'\n') ? between.chopped(1) : between, cols);
                } else {
                    out += QString(blankLinesBetween(doc, prev, i), u'\n');
                }
                out += u'\n';
            }
            out += dedented(lf(doc.blockSource(i)), cols);
            prev = i;
        }
    }
    return out;
}

QString blocksPlainText(const BlockDocument &doc, const QList<int> &tops, const PlainTextOptions &options)
{
    QString out;
    int prev = -1;
    for (const TopRange &range : topRanges(doc, tops)) {
        const int baseDepth = doc.at(range.first).depth;
        for (int i = range.first; i < range.end; ++i) {
            const BlockEntry &e = doc.at(i);
            if (prev >= 0)
                out += u'\n' + QString(std::min(1, blankLinesBetween(doc, prev, i)), u'\n');
            prev = i;
            const QString indent(2 * std::max(0, e.depth - baseDepth), u' ');
            QString marker;
            BlockEntry text = e;
            if (e.kind == BlockKind::ListItem) {
                const QString written = e.prefix.trimmed();
                const bool ordered = !written.isEmpty() && written.front().isDigit();
                QString box;
                if (const auto task = matchTaskMarker(e.body, 0)) {
                    box = task->status == TaskStatus::Done        ? u"☑ "_s
                          : task->status == TaskStatus::Cancelled ? u"☒ "_s
                                                                  : u"☐ "_s;
                    text.body = e.body.sliced(task->contentStart);
                }
                marker = ordered ? written + u' ' + box : box.isEmpty() ? u"• "_s : box;
            }
            const QStringList lines = plainLines(text, options);
            const QString continuation = indent + QString(marker.size(), u' ');
            if (lines.join(QString()).isEmpty()) {
                out += indent + marker.trimmed(); // an empty item: its bullet only
                continue;
            }
            for (qsizetype k = 0; k < lines.size(); ++k) {
                if (k > 0)
                    out += u'\n';
                out += (k == 0 ? indent + marker : lines[k].isEmpty() ? QString() : continuation) + lines[k];
            }
        }
    }
    return out;
}

QString stripInlineMarkup(const QString &text)
{
    static const QRegularExpression wikiAlias(u"!?\\[\\[(?:[^\\]|\\n]*\\|)?([^\\]\\n]*)\\]\\]"_s);
    static const QRegularExpression mdLink(u"!?\\[([^\\]\\n]*)\\]\\([^)\\n]*\\)"_s);
    // Marks in pairs around text that starts and ends with a non-space (`**b**`, `==h==`); a lone
    // or spaced-out one is text ("2**3", "a == b").
    static const QRegularExpression pairs(u"(\\*\\*|__|~~|==)(?=\\S)([^\\n]*?\\S)\\1"_s);
    // A single * or _ around a word or words (not inside words: snake_case, 2*3*4 stay).
    static const QRegularExpression single(u"(?<![\\w*\\\\])([*_])(?=\\S)([^*_\\n]*?\\S)\\1(?![\\w*])"_s);
    // Code spans (a run of backticks up to the next run as long on the same line) are what they
    // show, verbatim: each is one private-use character while the rest is stripped. (A text that
    // has such characters itself keeps its code marks instead.)
    constexpr char16_t placeholder = 0xE000;
    constexpr qsizetype maxSpans = 0x1800;
    const bool canMask = std::none_of(text.cbegin(), text.cend(), [](QChar c) {
        return c.unicode() >= placeholder && c.unicode() < placeholder + maxSpans;
    });
    QStringList code;
    QString t;
    t.reserve(text.size());
    for (qsizetype i = 0; i < text.size();) {
        if (text[i] != u'`' || !canMask || code.size() >= maxSpans) {
            t += text[i++];
            continue;
        }
        const auto runAt = [&](qsizetype at) {
            qsizetype n = 0;
            while (at + n < text.size() && text[at + n] == u'`')
                ++n;
            return n;
        };
        const qsizetype n = runAt(i);
        qsizetype close = -1;
        for (qsizetype j = i + n; j < text.size() && text[j] != u'\n';) {
            const qsizetype m = text[j] == u'`' ? runAt(j) : 0;
            if (m == n) {
                close = j;
                break;
            }
            j += std::max<qsizetype>(1, m);
        }
        if (close < 0) {
            t += text.sliced(i, n); // no closing run: backticks as written
            i += n;
            continue;
        }
        QString inner = text.sliced(i + n, close - i - n);
        // One space on both sides is padding (`` ` `x` ` ``), unless it is all spaces.
        if (inner.size() >= 2 && inner.front() == u' ' && inner.back() == u' ' && !inner.trimmed().isEmpty())
            inner = inner.sliced(1, inner.size() - 2);
        t += QChar(char16_t(placeholder + code.size()));
        code.append(inner);
        i = close + n;
    }
    if (!canMask) {
        static const QRegularExpression codeSpan(u"(?<!`)(`+)(?!`)([^\\n]*?[^`\\n])\\1(?!`)"_s);
        t.replace(codeSpan, u"\\2"_s);
    }
    t.replace(wikiAlias, u"\\1"_s);
    t.replace(mdLink, u"\\1"_s);
    // Nested pairs ("**__b__**") come off one level per pass.
    for (int pass = 0; pass < 4; ++pass) {
        const QString before = t;
        t.replace(pairs, u"\\2"_s);
        if (t == before)
            break;
    }
    t.replace(single, u"\\2"_s);
    if (code.isEmpty())
        return t;
    QString out;
    out.reserve(t.size());
    for (const QChar c : std::as_const(t)) {
        const qsizetype k = qsizetype(c.unicode()) - placeholder;
        if (k >= 0 && k < code.size())
            out += code.at(k);
        else
            out += c;
    }
    return out;
}

QStringList blockIdsOf(const QString &markdown)
{
    QStringList out;
    const BlockDocument doc(markdown);
    for (int i = 0; i < doc.count(); ++i) {
        const QString &id = doc.at(i).blockId;
        if (!id.isEmpty() && !out.contains(id))
            out.append(id);
    }
    return out;
}

QString withoutBlockIds(const QString &markdown, const std::function<bool(const QString &id)> &drop)
{
    if (!drop || (!markdown.contains(u'^') && !markdown.contains(u"::")))
        return markdown;
    BlockDocument doc(markdown);
    bool changed = false;
    for (int i = 0; i < doc.count(); ++i) {
        const QString id = doc.at(i).blockId;
        if (id.isEmpty() || !drop(id))
            continue;
        // The id is near the end of the block: the last line naming it (a code block's lines
        // before its id line are code).
        QStringList lines = doc.at(i).body.split(u'\n');
        for (qsizetype k = lines.size() - 1; k >= 0; --k) {
            const QString &l = lines[k];
            if (standaloneBlockId(l) == id) {
                lines.removeAt(k);
                break;
            }
            if (const auto at = blockIdAtEnd(l); at && at->id == id) {
                lines[k] = l.first(at->from) + l.sliced(at->to);
                break;
            }
            const QStringView trimmed = QStringView(l).trimmed();
            if (const auto p = matchPropertyLine(trimmed);
                p && p->key.compare(u"id"_s, Qt::CaseInsensitive) == 0
                && trimmed.sliced(p->valueFrom, p->valueTo - p->valueFrom) == id) {
                lines.removeAt(k);
                break;
            }
        }
        doc.setBody(i, lines.join(u'\n'));
        changed = true;
    }
    return changed ? doc.toString() : markdown;
}

} // namespace brulion::markdown

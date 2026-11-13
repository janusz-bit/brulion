#include "qr/qrtext.h"

#include "markdown/blockclipboard.h"
#include "markdown/blockdocument.h"
#include "types.h"

#include <QList>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <cstdlib>
#include <numeric>

using namespace Qt::StringLiterals;

namespace brulion::qr {

namespace {

using markdown::BlockDocument;

/// `text` with every line break as "\n" and without a byte order mark.
QString withLineFeeds(const QString &text)
{
    QString out = text;
    if (out.startsWith(QChar(0xFEFF)))
        out.remove(0, 1);
    out.replace(u"\r\n"_s, u"\n"_s);
    out.replace(u'\r', u'\n');
    return out;
}

bool isBlank(QChar c)
{
    return c == u' ' || c == u'\t' || c == u'\n';
}

/// `text` without the blank lines before its first line and without the blanks after its last
/// character (the indentation of the first line stays: an indented line means something).
QString trimmedLines(const QString &text)
{
    qsizetype begin = 0;
    for (qsizetype i = 0; i < text.size() && isBlank(text[i]); ++i) {
        if (text[i] == u'\n')
            begin = i + 1;
    }
    qsizetype end = text.size();
    while (end > begin && isBlank(text[end - 1]))
        --end;
    return text.sliced(begin, end - begin);
}

/// The first row after the frontmatter and the page properties; doc.count() when there is none.
int firstContentRow(const BlockDocument &doc)
{
    int row = 0;
    while (row < doc.count() && doc.at(row).kind == BlockKind::Frontmatter)
        ++row;
    return row;
}

bool startsWithOwnTitle(const BlockDocument &doc)
{
    const int row = firstContentRow(doc);
    return row < doc.count() && doc.at(row).kind == BlockKind::Heading && doc.at(row).headingLevel == 1;
}

/// Where line `line` (from 0) of `text` starts; its size past the last line.
qsizetype offsetOfLine(const QString &text, int line)
{
    qsizetype at = 0;
    for (int l = 0; l < line; ++l) {
        const qsizetype next = text.indexOf(u'\n', at);
        if (next < 0)
            return text.size();
        at = next + 1;
    }
    return at;
}

/// A title on one line.
QString oneLine(const QString &title)
{
    QString line = title;
    line.replace(u'\r', u' ');
    line.replace(u'\n', u' ');
    return line.trimmed();
}

/// A place where a part may end: after the character before `at` (UTF-16), `bytes` UTF-8 bytes
/// into the text.
struct Cut {
    qsizetype at = 0;
    qsizetype bytes = 0;
    /// How good a place it is: 0 after a line break, 1 after a space or tab, 2 between two other
    /// characters, 3 inside a character (between the code points of one grapheme cluster).
    int rank = 2;
};

/// Every place `text` may be cut, ascending; its end is one, its start is not.
QList<Cut> cutsOf(const QString &text)
{
    QList<Cut> cuts;
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text);
    qsizetype bytes = 0;
    qsizetype from = 0;
    for (qsizetype at = graphemes.toNextBoundary(); at > 0; at = graphemes.toNextBoundary()) {
        if (at <= from)
            continue;
        // The code points inside the cluster: places of the last resort.
        for (qsizetype i = from; i < at;) {
            const bool pair = text[i].isHighSurrogate() && i + 1 < at && text[i + 1].isLowSurrogate();
            const qsizetype next = i + (pair ? 2 : 1);
            bytes += utf8Size(QStringView(text).sliced(i, next - i));
            if (next < at)
                cuts.append({next, bytes, 3});
            i = next;
        }
        const QChar before = text[at - 1];
        cuts.append({at, bytes, before == u'\n' ? 0 : before == u' ' || before == u'\t' ? 1 : 2});
        from = at;
    }
    return cuts;
}

} // namespace

QString noteText(const QString &markdown, const NoteTextOptions &options)
{
    const QString text = withLineFeeds(markdown);
    const BlockDocument doc(text);
    const int first = firstContentRow(doc);
    if (first >= doc.count())
        return {};
    const QString title = startsWithOwnTitle(doc) ? QString() : oneLine(options.title);
    if (options.format == TextFormat::PlainText) {
        QList<int> rows(doc.count());
        std::iota(rows.begin(), rows.end(), 0);
        markdown::PlainTextOptions plain;
        plain.blockText = options.blockText;
        const QString body = trimmedLines(markdown::blocksPlainText(doc, markdown::topRows(doc, rows), plain));
        if (body.isEmpty())
            return {};
        return title.isEmpty() ? body : title + u"\n\n"_s + body;
    }
    if (title.isEmpty())
        return trimmedLines(text);
    // The heading goes right before the first block: after the frontmatter and the page
    // properties, which have to stay first (a frontmatter that does not open the note is none).
    const qsizetype at = offsetOfLine(text, doc.at(first).line);
    QString head = text.left(at);
    if (!head.isEmpty() && !head.endsWith(u'\n'))
        head += u'\n';
    return trimmedLines(head + u"# "_s + title + u"\n\n"_s + text.sliced(at));
}

bool hasOwnTitle(const QString &markdown)
{
    return startsWithOwnTitle(BlockDocument(withLineFeeds(markdown)));
}

qsizetype utf8Size(QStringView text)
{
    qsizetype bytes = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c.unicode() < 0x80) {
            bytes += 1;
        } else if (c.unicode() < 0x800) {
            bytes += 2;
        } else if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            bytes += 4;
            ++i;
        } else {
            bytes += 3; // the rest of the BMP, and a lone surrogate (encode(): U+FFFD)
        }
    }
    return bytes;
}

QStringList splitText(const QString &text, qsizetype maxBytes)
{
    maxBytes = std::max<qsizetype>(maxBytes, 4);
    if (text.isEmpty())
        return {};
    const qsizetype total = utf8Size(text);
    if (total <= maxBytes)
        return {text};

    const QList<Cut> cuts = cutsOf(text);
    QStringList parts;
    qsizetype start = 0;      // where the next part starts (UTF-16)
    qsizetype startBytes = 0; // and how many bytes come before it
    qsizetype first = 0;      // the first cut after `start`
    while (total - startBytes > maxBytes) {
        const qsizetype remaining = total - startBytes;
        const qsizetype count = (remaining + maxBytes - 1) / maxBytes; // parts still needed, 2 or more
        const qsizetype share = (remaining + count - 1) / count;       // their even share
        // The shortest this part may be: the rest still fits `count - 1` parts.
        const qsizetype least = remaining - (count - 1) * maxBytes;
        // The cut of `cuts[first…]` whose part is [lo, hi] bytes long and at most `maxRank`: the
        // lowest rank, then the nearest to the even share; -1 for none.
        const auto best = [&](qsizetype lo, qsizetype hi, int maxRank) {
            qsizetype found = -1;
            int foundRank = 4;
            qsizetype foundDistance = 0;
            for (qsizetype i = first; i < cuts.size(); ++i) {
                const qsizetype size = cuts[i].bytes - startBytes;
                if (size > hi)
                    break;
                if (size < lo || cuts[i].rank > maxRank)
                    continue;
                const qsizetype distance = std::abs(size - share);
                if (cuts[i].rank < foundRank || (cuts[i].rank == foundRank && distance < foundDistance)) {
                    found = i;
                    foundRank = cuts[i].rank;
                    foundDistance = distance;
                }
            }
            return found;
        };
        // Near the even share (within a quarter of it) first, then anywhere the rest still fits.
        qsizetype pick = best(std::max(least, share - share / 4), std::min(maxBytes, share + share / 4), 2);
        if (pick < 0)
            pick = best(least, maxBytes, 2);
        if (pick < 0)
            pick = best(least, maxBytes, 3);
        if (pick < 0) {
            // No cut leaves the rest that few parts (a character across the limit): the longest
            // part that fits, the rest takes one part more. Every code point is at most 4 bytes,
            // so one always fits.
            for (qsizetype i = first; i < cuts.size() && cuts[i].bytes - startBytes <= maxBytes; ++i)
                pick = i;
        }
        const Cut cut = cuts[pick];
        parts.append(text.sliced(start, cut.at - start));
        start = cut.at;
        startBytes = cut.bytes;
        first = pick + 1;
    }
    parts.append(text.sliced(start));
    return parts;
}

} // namespace brulion::qr

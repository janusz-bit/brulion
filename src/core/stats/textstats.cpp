#include "stats/textstats.h"

#include "markdown/blockclipboard.h"
#include "markdown/frontmatter.h"
#include "markdown/properties.h"
#include "markdown/tokens.h"

#include <QRegularExpression>
#include <QTextBoundaryFinder>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::stats {

namespace {

bool isBlank(QChar c)
{
    return c == u' ' || c == u'\t';
}

QStringView trimmedLeft(QStringView s)
{
    qsizetype i = 0;
    while (i < s.size() && isBlank(s[i]))
        ++i;
    return s.mid(i);
}

/// The line without its indentation and quote marks (`> > text` -> "text").
QStringView withoutContainers(QStringView s)
{
    for (;;) {
        s = trimmedLeft(s);
        if (!s.startsWith(u'>'))
            return s;
        s = s.mid(1);
        if (s.startsWith(u' '))
            s = s.mid(1);
    }
}

/// A list marker at the start (`- `, `* `, `+ `, `1. `, `12) `; also alone on the line): its length
/// with the blanks after it, else 0.
qsizetype listMarkerLength(QStringView s)
{
    qsizetype i = 0;
    if (!s.isEmpty() && (s[0] == u'-' || s[0] == u'*' || s[0] == u'+')) {
        i = 1;
    } else {
        while (i < s.size() && i < 9 && s[i].isDigit() && s[i].unicode() < 128)
            ++i;
        if (i == 0 || i >= s.size() || (s[i] != u'.' && s[i] != u')'))
            return 0;
        ++i;
    }
    if (i < s.size() && !isBlank(s[i]))
        return 0;
    while (i < s.size() && isBlank(s[i]))
        ++i;
    return i;
}

struct Fence {
    QChar ch;
    qsizetype length = 0;
};

/// A code fence opening at the start of `s` (three or more backticks or tildes; a backtick
/// fence's info string has no backtick).
std::optional<Fence> fenceOpening(QStringView s)
{
    if (s.isEmpty() || (s[0] != u'`' && s[0] != u'~'))
        return std::nullopt;
    const QChar ch = s[0];
    qsizetype n = 0;
    while (n < s.size() && s[n] == ch)
        ++n;
    if (n < 3 || (ch == u'`' && s.mid(n).contains(u'`')))
        return std::nullopt;
    return Fence{ch, n};
}

/// `s` closes `fence`: as many of its characters or more, then only blanks.
bool closesFence(QStringView s, const Fence &fence)
{
    qsizetype n = 0;
    while (n < s.size() && s[n] == fence.ch)
        ++n;
    if (n < fence.length)
        return false;
    for (qsizetype i = n; i < s.size(); ++i) {
        if (!isBlank(s[i]))
            return false;
    }
    return true;
}

/// `line` without its comments (`%%…%%`, `<!-- … -->`); `inPercent` / `inHtml` carry an open one
/// over to the next line.
QString withoutComments(QStringView line, bool &inPercent, bool &inHtml)
{
    QString out;
    qsizetype i = 0;
    while (i <= line.size()) {
        if (inPercent || inHtml) {
            const qsizetype end = line.indexOf(inPercent ? u"%%"_s : u"-->"_s, i);
            if (end < 0)
                return out;
            i = end + (inPercent ? 2 : 3);
            inPercent = inHtml = false;
            continue;
        }
        const qsizetype percent = line.indexOf(u"%%"_s, i);
        const qsizetype html = line.indexOf(u"<!--"_s, i);
        if (percent < 0 && html < 0) {
            out += line.mid(i);
            break;
        }
        const bool isPercent = html < 0 || (percent >= 0 && percent < html);
        const qsizetype at = isPercent ? percent : html;
        out += line.mid(i, at - i);
        if (isPercent) {
            inPercent = true;
            i = at + 2;
        } else {
            inHtml = true;
            i = at + 4;
        }
    }
    return out;
}

/// A table row's cells joined by spaces (`| a | b |` -> "a b"); `\|` stays a character.
QString tableCells(QStringView row)
{
    QString out;
    QString cell;
    const auto flush = [&] {
        const QString t = cell.trimmed();
        if (!t.isEmpty()) {
            if (!out.isEmpty())
                out += u' ';
            out += t;
        }
        cell.clear();
    };
    for (qsizetype i = 0; i < row.size(); ++i) {
        if (row[i] == u'\\' && i + 1 < row.size() && row[i + 1] == u'|') {
            cell += u'|';
            ++i;
        } else if (row[i] == u'|') {
            flush();
        } else {
            cell += row[i];
        }
    }
    flush();
    return out;
}

/// The visible part of one line outside code and formulas (block syntax removed), "" for a line
/// with nothing to read. Inline markup is left for the whole text.
QString lineText(QStringView line)
{
    static const QRegularExpression rule(u"^([-*_])(?:[ \\t]*\\1){2,}[ \\t]*$"_s);
    static const QRegularExpression tableSeparator(u"^\\|?(?:[ \\t]*:?-+:?[ \\t]*\\|)+(?:[ \\t]*:?-+:?[ \\t]*)?$"_s);
    static const QRegularExpression callout(u"^\\[![A-Za-z0-9_-]+\\][+-]?[ \\t]*"_s);
    static const QRegularExpression checkbox(u"^\\[[^\\]]\\](?:[ \\t]+|$)"_s);
    static const QRegularExpression keyword(
            u"^(?:TODO|LATER|NOW|DOING|DONE|WAITING|CANCELED|CANCELLED)(?:[ \\t]+|$)"_s);
    static const QRegularExpression heading(u"^#{1,6}(?:[ \\t]+|$)"_s);

    QStringView s = withoutContainers(line);
    if (s.isEmpty() || rule.matchView(s).hasMatch() || tableSeparator.matchView(s).hasMatch())
        return {};
    if (std::all_of(s.cbegin(), s.cend(), [](QChar c) { return c == u'=' || isBlank(c); }))
        return {}; // a setext heading's underline
    if (s.startsWith(u'|'))
        return tableCells(s);
    if (const auto m = callout.matchView(s); m.hasMatch())
        s = s.mid(m.capturedLength());
    bool listItem = false;
    if (const qsizetype marker = listMarkerLength(s); marker > 0) {
        s = s.mid(marker);
        listItem = true;
    }
    if (markdown::matchPropertyLine(s) || !markdown::standaloneBlockId(s).isEmpty())
        return {};
    if (listItem) {
        if (const auto m = checkbox.matchView(s); m.hasMatch())
            s = s.mid(m.capturedLength());
        if (const auto m = keyword.matchView(s); m.hasMatch())
            s = s.mid(m.capturedLength());
    }
    if (const auto m = heading.matchView(s); m.hasMatch())
        s = s.mid(m.capturedLength());
    return s.toString();
}

/// Inline markup and tokens of the joined lines reduced to their text (line by line).
QString inlineText(const QString &lines)
{
    static const QRegularExpression embeds(u"!\\[\\[[^\\]\\n]*\\]\\]|!\\[[^\\]\\n]*\\]\\([^)\\n]*\\)"_s);
    static const QRegularExpression refs(u"\\(\\([A-Za-z0-9-]+\\)\\)|\\[\\^[^\\]\\s]+\\]:?"_s);
    static const QRegularExpression autolink(u"<((?:[A-Za-z][A-Za-z0-9+.-]*:|www\\.)[^>\\s]+)>"_s);
    static const QRegularExpression html(u"</?[A-Za-z][A-Za-z0-9-]*(?:\\s[^>\\n]*)?/?>"_s);
    static const QRegularExpression escape(u"\\\\([!-/:-@\\[-`{-~])"_s);

    QString t = markdown::stripTokens(lines);
    t.replace(embeds, QString());
    t.replace(refs, QString());
    t.replace(autolink, u"\\1"_s);
    t.replace(html, QString());
    t = markdown::stripInlineMarkup(t);
    t.replace(escape, u"\\1"_s);
    return t;
}

bool isWordSpace(char32_t c)
{
    return QChar::isSpace(c) || c == 0x200B; // (a zero-width space separates too)
}

/// No-break and thin spaces: between digits they group thousands.
bool isGroupingSpace(char32_t c)
{
    return c == 0x00A0 || c == 0x202F || c == 0x2007 || c == 0x2009;
}

} // namespace

int readingMinutes(int words)
{
    if (words <= 0)
        return 0;
    return std::max(1, (words + WordsPerMinute / 2) / WordsPerMinute);
}

int countWords(QStringView text)
{
    // By code point (surrogate pairs: letters beyond the BMP); a word counts at its first letter
    // or digit.
    int words = 0;
    bool counted = false;
    char32_t previous = 0;
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n;) {
        char32_t c = text[i].unicode();
        qsizetype width = 1;
        if (QChar::isHighSurrogate(c) && i + 1 < n && text[i + 1].isLowSurrogate()) {
            c = QChar::surrogateToUcs4(text[i], text[i + 1]);
            width = 2;
        }
        bool separator = isWordSpace(c);
        if (separator && isGroupingSpace(c) && QChar::isDigit(previous) && i + width < n && text[i + width].isDigit())
            separator = false;
        if (separator) {
            counted = false;
        } else if (!counted && QChar::isLetterOrNumber(c)) {
            ++words;
            counted = true;
        }
        previous = c;
        i += width;
    }
    return words;
}

TextStats countPlainText(QStringView text)
{
    TextStats stats;
    stats.words = countWords(text);
    QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, text.data(), text.size());
    qsizetype from = 0;
    for (qsizetype to = graphemes.toNextBoundary(); to > 0; to = graphemes.toNextBoundary()) {
        const QStringView g = text.mid(from, to - from);
        from = to;
        if (g == u"\n" || g == u"\r\n" || g == u"\r")
            continue;
        ++stats.characters;
        if (!std::all_of(g.cbegin(), g.cend(), [](QChar c) { return c.isSpace(); }))
            ++stats.charactersNoSpaces;
    }
    stats.readingMinutes = readingMinutes(stats.words);
    return stats;
}

QString readableText(QStringView markdown, const CountOptions &options)
{
    qsizetype start = !markdown.isEmpty() && markdown[0].unicode() == 0xfeff ? 1 : 0;
    if (options.page) {
        if (const auto fm = markdown::findFrontmatter(markdown))
            start = fm->bodyStart;
    }
    QString lines;
    std::optional<Fence> fence;
    bool inMath = false;
    bool inPercent = false;
    bool inHtml = false;
    qsizetype at = start;
    while (at <= markdown.size()) {
        qsizetype end = markdown.indexOf(u'\n', at);
        if (end < 0)
            end = markdown.size();
        QStringView raw = markdown.mid(at, end - at);
        at = end + 1;
        if (raw.endsWith(u'\r'))
            raw.chop(1);

        const QStringView inner = withoutContainers(raw);
        if (fence) {
            if (closesFence(inner, *fence))
                fence.reset();
            continue;
        }
        if (inMath) {
            if (inner.trimmed() == u"$$")
                inMath = false;
            continue;
        }
        const QString visible = withoutComments(raw, inPercent, inHtml);
        QStringView body = withoutContainers(visible);
        // A fence or a formula may also be a list item's text.
        if (const qsizetype marker = listMarkerLength(body); marker > 0)
            body = body.mid(marker);
        if (const auto opening = fenceOpening(body)) {
            fence = opening;
            continue;
        }
        const QStringView trimmed = body.trimmed();
        if (trimmed == u"$$") {
            inMath = true;
            continue;
        }
        if (trimmed.size() >= 4 && trimmed.startsWith(u"$$") && trimmed.endsWith(u"$$"))
            continue; // a display formula on one line
        const QString text = lineText(visible);
        if (text.trimmed().isEmpty())
            continue;
        if (!lines.isEmpty())
            lines += u'\n';
        lines += text;
    }
    // Inline markup over every line at once, then the lines with nothing left in them go.
    const QString stripped = inlineText(lines);
    QString out;
    out.reserve(stripped.size());
    for (QStringView line : QStringView(stripped).split(u'\n')) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (!out.isEmpty())
            out += u'\n';
        // Spaces and tabs in a row are one space, as the note shows them (also where something
        // left out stood between two words).
        bool blank = false;
        for (const QChar c : line) {
            if (isBlank(c)) {
                if (!blank)
                    out += u' ';
                blank = true;
            } else {
                out += c;
                blank = false;
            }
        }
    }
    return out;
}

TextStats countMarkdown(QStringView markdown, const CountOptions &options)
{
    return countPlainText(readableText(markdown, options));
}

} // namespace brulion::stats

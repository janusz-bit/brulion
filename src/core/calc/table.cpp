// GFM tables: parsing (following the markdown-it table rule, so parser and renderer agree on
// what is a table), pretty serialisation and locating tables in a page.
#include "calc/table.h"

#include "calc/blocks_p.h"
#include "calc/parser_p.h"
#include "calc/util_p.h"

#include "text/text.h"

#include <QTextBoundaryFinder>

#include <algorithm>
#include <climits>
#include <cstdlib>

namespace brulion::calc {

namespace {

/// markdown-it's escapedSplit: split on '|' not preceded by '\', dropping that backslash.
QStringList escapedSplit(QStringView str)
{
    QStringList result;
    QString current;
    qsizetype lastPos = 0;
    bool escaped = false;
    for (qsizetype pos = 0; pos < str.size(); ++pos) {
        const QChar ch = str.at(pos);
        if (ch == u'|') {
            if (!escaped) {
                result.append(QString(current + str.mid(lastPos, pos - lastPos)));
                current.clear();
                lastPos = pos + 1;
            } else {
                current += str.mid(lastPos, pos - 1 - lastPos);
                lastPos = pos;
            }
        }
        escaped = ch == u'\\';
    }
    result.append(QString(current + str.mid(lastPos)));
    return result;
}

std::optional<QList<TableAlign>> tableHead(QStringView header, QStringView delimiter)
{
    if (!header.contains(u'|'))
        return std::nullopt;
    std::optional<QList<TableAlign>> aligns = parseDelimiterRow(delimiter);
    if (!aligns || splitTableRow(header).size() != aligns->size())
        return std::nullopt;
    return aligns;
}

QStringList fitRow(QStringList cells, qsizetype n)
{
    if (cells.size() > n)
        cells.resize(n);
    while (cells.size() < n)
        cells.append(QString());
    return cells;
}

// --- display width -------------------------------------------------------------------------

bool inRanges(char32_t cp, std::initializer_list<std::pair<char32_t, char32_t>> ranges)
{
    for (const auto &[from, to] : ranges) {
        if (cp >= from && cp <= to)
            return true;
    }
    return false;
}

bool isWide(char32_t cp)
{
    return inRanges(cp, {{0x1100, 0x115f}, {0x2e80, 0x303e}, {0x3041, 0x33ff}, {0x3400, 0x4dbf}, {0x4e00, 0x9fff},
                         {0xa000, 0xa4cf}, {0xac00, 0xd7a3}, {0xf900, 0xfaff}, {0xfe30, 0xfe4f}, {0xff00, 0xff60},
                         {0xffe0, 0xffe6}, {0x1f300, 0x1f64f}, {0x1f900, 0x1f9ff}, {0x20000, 0x3fffd}});
}

/// Code points with Emoji_Presentation (shown as wide emoji without a variation selector).
bool isEmojiPresentation(char32_t cp)
{
    return inRanges(cp, {{0x231a, 0x231b}, {0x23e9, 0x23ec}, {0x23f0, 0x23f0}, {0x23f3, 0x23f3}, {0x25fd, 0x25fe},
                         {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267f, 0x267f}, {0x2693, 0x2693}, {0x26a1, 0x26a1},
                         {0x26aa, 0x26ab}, {0x26bd, 0x26be}, {0x26c4, 0x26c5}, {0x26ce, 0x26ce}, {0x26d4, 0x26d4},
                         {0x26ea, 0x26ea}, {0x26f2, 0x26f3}, {0x26f5, 0x26f5}, {0x26fa, 0x26fa}, {0x26fd, 0x26fd},
                         {0x2705, 0x2705}, {0x270a, 0x270b}, {0x2728, 0x2728}, {0x274c, 0x274c}, {0x274e, 0x274e},
                         {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797}, {0x27b0, 0x27b0}, {0x27bf, 0x27bf},
                         {0x2b1b, 0x2b1c}, {0x2b50, 0x2b50}, {0x2b55, 0x2b55}, {0x1f004, 0x1f004}, {0x1f0cf, 0x1f0cf},
                         {0x1f18e, 0x1f18e}, {0x1f191, 0x1f19a}, {0x1f1e6, 0x1f1ff}, {0x1f201, 0x1f201},
                         {0x1f21a, 0x1f21a}, {0x1f22f, 0x1f22f}, {0x1f232, 0x1f236}, {0x1f238, 0x1f23a},
                         {0x1f250, 0x1f251}, {0x1f300, 0x1f64f}, {0x1f680, 0x1f6ff}, {0x1f7e0, 0x1f7f0},
                         {0x1f90c, 0x1f9ff}, {0x1fa70, 0x1faff}});
}

bool isZeroWidth(char32_t cp)
{
    return QChar::isMark(cp) || (cp >= 0x200b && cp <= 0x200f) || cp == 0x2060 || cp == 0xfeff;
}

int graphemeWidth(QStringView g)
{
    bool allZero = true;
    bool emoji = false;
    char32_t first = 0;
    qsizetype i = 0;
    while (i < g.size()) {
        qsizetype len = 1;
        const char32_t cp = detail::codePointAt(g, i, &len);
        if (i == 0)
            first = cp;
        if (!isZeroWidth(cp))
            allZero = false;
        if (cp == 0xfe0f || isEmojiPresentation(cp))
            emoji = true;
        i += len;
    }
    if (allZero)
        return 0;
    if (emoji)
        return 2;
    if (first < 0x20 || (first >= 0x7f && first < 0xa0))
        return 0;
    return isWide(first) ? 2 : 1;
}

bool isPrintableAscii(QStringView s)
{
    for (const QChar c : s) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e)
            return false;
    }
    return true;
}

// --- serialisation -------------------------------------------------------------------------

QString escapeCell(const QString &text)
{
    QString s = text;
    s.replace(QStringLiteral("\r\n"), QStringLiteral("<br>"));
    s.replace(u'\r', QStringLiteral("<br>"));
    s.replace(u'\n', QStringLiteral("<br>"));
    s.replace(u'|', QStringLiteral("\\|"));
    return s.trimmed();
}

void appendPadded(QString &out, const QString &text, int textWidth, int width, TableAlign align)
{
    const int diff = std::max(0, width - textWidth);
    if (align == TableAlign::Right) {
        out += QString(diff, u' ');
        out += text;
    } else if (align == TableAlign::Center) {
        const int left = diff / 2;
        out += QString(left, u' ');
        out += text;
        out += QString(diff - left, u' ');
    } else {
        out += text;
        out += QString(diff, u' ');
    }
}

QString delimiterCell(int width, TableAlign align)
{
    switch (align) {
    case TableAlign::Left:
        return u':' + QString(width - 1, u'-');
    case TableAlign::Right:
        return QString(width - 1, u'-') + u':';
    case TableAlign::Center:
        return u':' + QString(width - 2, u'-') + u':';
    case TableAlign::None:
        break;
    }
    return QString(width, u'-');
}

// --- locating -------------------------------------------------------------------------------

int indentWidth(QStringView line)
{
    return columnWidth(leadingWhitespace(line));
}

/// `^([-*_])(\s*\1){2,}\s*$`
bool isThematicBreak(QStringView t)
{
    if (t.isEmpty() || (t.front() != u'-' && t.front() != u'*' && t.front() != u'_'))
        return false;
    int count = 0;
    for (const QChar c : t) {
        if (c == t.front())
            ++count;
        else if (!c.isSpace())
            return false;
    }
    return count >= 3;
}

/// Lines that start another block and therefore end a table (markdown-it terminators).
bool isTerminator(QStringView line)
{
    qsizetype i = 0;
    while (i < line.size() && line.at(i).isSpace())
        ++i;
    const QStringView t = line.mid(i);
    if (t.startsWith(u"```") || t.startsWith(u"~~~") || t.startsWith(u'>'))
        return true;
    qsizetype hashes = 0;
    while (hashes < t.size() && t.at(hashes) == u'#')
        ++hashes;
    if (hashes >= 1 && hashes <= 6 && (hashes == t.size() || t.at(hashes).isSpace()))
        return true;
    // list item: ^([-*+]|\d{1,9}[.)])(\s|$)
    qsizetype marker = 0;
    if (!t.isEmpty() && (t.front() == u'-' || t.front() == u'*' || t.front() == u'+')) {
        marker = 1;
    } else {
        qsizetype digits = 0;
        while (digits < t.size() && detail::isAsciiDigit(t.at(digits)))
            ++digits;
        if (digits >= 1 && digits <= 9 && digits < t.size() && (t.at(digits) == u'.' || t.at(digits) == u')'))
            marker = digits + 1;
    }
    if (marker > 0 && (marker == t.size() || t.at(marker).isSpace()))
        return true;
    if (isThematicBreak(t))
        return true;
    if (t.size() >= 2 && t.front() == u'<') {
        const QChar c = t.at(1);
        if ((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'/' || c == u'!' || c == u'?')
            return true;
    }
    return false;
}

struct TableStart {
    qsizetype offset = 0; ///< characters before the header row's text
    int base = 0;         ///< indentation column of the table
};

/// Header of a table starting at `text` (a plain or list-item line), or nullopt.
std::optional<TableStart> tableStart(QStringView text, QStringView next)
{
    const qsizetype item = listItemPrefixLength(text);
    if (item > 0) {
        // Logseq / Markdown list item whose content starts with the table: `- | a | b |`.
        const int base = columnWidth(text.left(item));
        const int delimiterIndent = indentWidth(next);
        if (!tableHead(text.mid(item), next) || delimiterIndent < base || delimiterIndent - base >= 4)
            return std::nullopt;
        return TableStart{item, base};
    }
    if (isTerminator(text) || !tableHead(text, next))
        return std::nullopt;
    const int base = indentWidth(text);
    if (std::abs(indentWidth(next) - base) >= 4)
        return std::nullopt;
    return TableStart{leadingWhitespace(text).size(), base};
}

} // namespace

QStringList splitTableRow(QStringView line)
{
    QStringList cells = escapedSplit(line.trimmed());
    if (!cells.isEmpty() && cells.front().isEmpty())
        cells.removeFirst();
    if (!cells.isEmpty() && cells.back().isEmpty())
        cells.removeLast();
    for (QString &c : cells)
        c = c.trimmed();
    return cells;
}

std::optional<QList<TableAlign>> parseDelimiterRow(QStringView line)
{
    const QStringView t = line.trimmed();
    if (t.size() < 2)
        return std::nullopt;
    const auto isDelimiterChar = [](QChar c) { return c == u'|' || c == u'-' || c == u':'; };
    const QChar first = t.at(0);
    const QChar second = t.at(1);
    if (!isDelimiterChar(first))
        return std::nullopt;
    if (!isDelimiterChar(second) && !second.isSpace())
        return std::nullopt;
    if (first == u'-' && second.isSpace())
        return std::nullopt;
    for (const QChar c : t) {
        if (!isDelimiterChar(c) && !c.isSpace())
            return std::nullopt;
    }
    const QList<QStringView> columns = t.split(u'|');
    QList<TableAlign> aligns;
    for (qsizetype i = 0; i < columns.size(); ++i) {
        const QStringView c = columns.at(i).trimmed();
        if (c.isEmpty()) {
            if (i == 0 || i == columns.size() - 1)
                continue;
            return std::nullopt;
        }
        const bool left = c.startsWith(u':');
        const bool right = c.endsWith(u':');
        const QStringView dashes = c.mid(left ? 1 : 0, c.size() - (left ? 1 : 0) - (right ? 1 : 0));
        if (dashes.isEmpty() || dashes.count(u'-') != dashes.size())
            return std::nullopt;
        aligns.append(left && right ? TableAlign::Center : right ? TableAlign::Right : left ? TableAlign::Left : TableAlign::None);
    }
    if (aligns.isEmpty())
        return std::nullopt;
    return aligns;
}

std::optional<TableModel> parseTable(QStringView markdown)
{
    const QList<text::Line> lines = text::splitLines(markdown);
    qsizetype h = 0;
    while (h < lines.size() && QStringView(lines.at(h).text).trimmed().isEmpty())
        ++h;
    if (h + 1 >= lines.size())
        return std::nullopt;
    const std::optional<QList<TableAlign>> aligns = tableHead(lines.at(h).text, lines.at(h + 1).text);
    if (!aligns)
        return std::nullopt;
    TableModel model;
    model.header = splitTableRow(lines.at(h).text);
    model.aligns = *aligns;
    for (qsizetype i = h + 2; i < lines.size(); ++i) {
        const QString &line = lines.at(i).text;
        if (QStringView(line).trimmed().isEmpty())
            break;
        model.rows.append(fitRow(splitTableRow(line), aligns->size()));
    }
    return model;
}

QString serializeTable(const TableModel &model)
{
    qsizetype cols = model.header.size();
    for (const QStringList &r : model.rows)
        cols = std::max(cols, r.size());
    if (cols == 0)
        return {};
    QStringList header;
    for (const QString &h : model.header)
        header.append(escapeCell(h));
    header = fitRow(header, cols);
    QList<QStringList> rows;
    rows.reserve(model.rows.size());
    for (const QStringList &r : model.rows) {
        QStringList cells;
        cells.reserve(cols);
        for (const QString &c : r)
            cells.append(escapeCell(c));
        rows.append(fitRow(cells, cols));
    }
    QList<TableAlign> aligns;
    for (qsizetype c = 0; c < cols; ++c)
        aligns.append(c < model.aligns.size() ? model.aligns.at(c) : TableAlign::None);

    QList<int> headerWidths;
    QList<int> widths;
    for (qsizetype c = 0; c < cols; ++c) {
        headerWidths.append(displayWidth(header.at(c)));
        widths.append(std::max(3, headerWidths.last()));
    }
    QList<QList<int>> cellWidths;
    cellWidths.reserve(rows.size());
    for (const QStringList &r : rows) {
        QList<int> w;
        w.reserve(cols);
        for (qsizetype c = 0; c < cols; ++c) {
            w.append(displayWidth(r.at(c)));
            widths[c] = std::max(widths.at(c), w.last());
        }
        cellWidths.append(std::move(w));
    }

    QString out;
    const auto appendRow = [&](const QStringList &cells, const QList<int> &textWidths) {
        out += QStringLiteral("| ");
        for (qsizetype c = 0; c < cols; ++c) {
            if (c > 0)
                out += QStringLiteral(" | ");
            appendPadded(out, cells.at(c), textWidths.at(c), widths.at(c), aligns.at(c));
        }
        out += QStringLiteral(" |");
    };
    appendRow(header, headerWidths);
    out += QStringLiteral("\n| ");
    for (qsizetype c = 0; c < cols; ++c) {
        if (c > 0)
            out += QStringLiteral(" | ");
        out += delimiterCell(widths.at(c), aligns.at(c));
    }
    out += QStringLiteral(" |");
    for (qsizetype r = 0; r < rows.size(); ++r) {
        out += u'\n';
        appendRow(rows.at(r), cellWidths.at(r));
    }
    return out;
}

QList<TableSpan> findTables(QStringView content)
{
    const QList<text::Line> lines = text::splitLines(content);
    QList<TableSpan> found;
    qsizetype i = 0;
    if (!lines.isEmpty() && QStringView(lines.front().text).trimmed() == u"---") {
        for (qsizetype k = 1; k < lines.size(); ++k) {
            const QStringView l = lines.at(k).text;
            if ((l.startsWith(u"---") || l.startsWith(u"...")) && l.mid(3).trimmed().isEmpty()) {
                i = k + 1;
                break;
            }
        }
    }
    std::optional<FenceOpen> fence;
    for (; i < lines.size(); ++i) {
        const QString &text = lines.at(i).text;
        if (fence) {
            if (isFenceClose(text, *fence))
                fence.reset();
            continue;
        }
        if (const std::optional<FenceOpen> f = parseFenceOpen(text)) {
            fence = f;
            continue;
        }
        if (i + 1 >= lines.size())
            continue;
        const QString &next = lines.at(i + 1).text;
        const std::optional<TableStart> start = tableStart(text, next);
        if (!start)
            continue;
        qsizetype end = i + 1;
        for (qsizetype j = i + 2; j < lines.size(); ++j) {
            const QString &row = lines.at(j).text;
            const int w = indentWidth(row);
            if (QStringView(row).trimmed().isEmpty() || isTerminator(row) || w < start->base || w - start->base >= 4)
                break;
            end = j;
        }
        TableSpan span;
        span.from = lines.at(i).start + start->offset;
        span.to = lines.at(end).start + lines.at(end).text.size();
        span.startLine = static_cast<int>(i);
        span.endLine = static_cast<int>(end);
        span.indent = leadingWhitespace(next).toString();
        found.append(span);
        i = end;
    }
    return found;
}

namespace {

/// The line ending after the table's header row (a table always has a next row), else the
/// document's.
QString tableEol(QStringView content, qsizetype from)
{
    for (qsizetype i = from; i < content.size(); ++i) {
        if (content.at(i) == u'\n')
            return QStringLiteral("\n");
        if (content.at(i) == u'\r')
            return i + 1 < content.size() && content.at(i + 1) == u'\n' ? QStringLiteral("\r\n") : QStringLiteral("\r");
    }
    return text::detectEol(content);
}

} // namespace

QString replaceTable(QStringView content, const TableSpan &span, const TableModel &model)
{
    const qsizetype from = std::clamp<qsizetype>(span.from, 0, content.size());
    const qsizetype to = std::clamp<qsizetype>(span.to, from, content.size());
    QString table = serializeTable(model);
    table.replace(u'\n', tableEol(content, from) + span.indent);
    return content.left(from).toString() + table + content.mid(to);
}

int displayWidth(QStringView text)
{
    if (isPrintableAscii(text))
        return static_cast<int>(text.size());
    const QString s = detail::toNfc(text);
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, s);
    int width = 0;
    qsizetype start = 0;
    while (start < s.size()) {
        qsizetype end = finder.toNextBoundary();
        if (end <= start)
            end = s.size();
        width += graphemeWidth(QStringView(s).mid(start, end - start));
        start = end;
    }
    return width;
}

QString columnName(int index)
{
    QString s;
    qint64 n = qint64(index) + 1;
    while (n > 0) {
        const auto rem = static_cast<char16_t>((n - 1) % 26);
        s.prepend(QChar(char16_t(u'A' + rem)));
        n = (n - 1) / 26;
    }
    return s;
}

int columnIndex(QStringView letters)
{
    if (letters.isEmpty())
        return -1;
    qint64 n = 0;
    for (const QChar c : letters) {
        char16_t u = c.unicode();
        if (u >= u'a' && u <= u'z')
            u = char16_t(u - u'a' + u'A');
        if (u < u'A' || u > u'Z')
            return -1;
        n = n * 26 + (u - u'A' + 1);
        if (n - 1 > INT_MAX)
            return -1;
    }
    return static_cast<int>(n - 1);
}

QString cellName(int row, int column)
{
    return columnName(column) + QString::number(qint64(row) + 1);
}

} // namespace brulion::calc

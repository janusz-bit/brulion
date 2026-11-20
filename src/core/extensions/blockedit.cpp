#include "extensions/blockedit.h"

#include "markdown/blockprops.h"
#include "markdown/edit.h"
#include "markdown/inline.h"
#include "markdown/tasks.h"
#include "text/text.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

// indentation, a list marker, then a checkbox or a Logseq keyword (each with the spaces after it)
const QRegularExpression &linePattern()
{
    static const QRegularExpression re(
        uR"(^([ \t]*(?:[-*+]|\d{1,9}[.)])[ \t]+)(?:(\[(.)\][ \t]+)|((?:TODO|LATER|DOING|NOW|IN-PROGRESS|DONE|CANCELED|CANCELLED|WAITING|WAIT)[ \t]+))?)"_s);
    return re;
}

// An Obsidian Tasks date field — a due, scheduled or start date with an optional time — and the
// spaces before it
const QRegularExpression &datePattern()
{
    static const QRegularExpression re(
        uR"([ \t]*(?:(?:✅|❌|➕)\x{FE0F}?[ \t]*\d{4}-\d{2}-\d{2}|(?:📅|⏳|🛫)\x{FE0F}?[ \t]*\d{4}-\d{2}-\d{2}(?:[ \t]+\d{1,2}:\d{2})?)(?![\d:]))"_s);
    return re;
}

// A block id ending a block's text, the spaces before it
const QRegularExpression &idPattern()
{
    static const QRegularExpression re(uR"(([ \t]+\^[A-Za-z0-9-]+)[ \t]*$)"_s);
    return re;
}

// `text` with what its code spans hold blanked out (the same length): a field there is code.
QString withoutCode(const QString &text)
{
    QString out = text;
    qsizetype i = 0;
    while (i < out.size()) {
        if (out[i] != u'`') {
            ++i;
            continue;
        }
        qsizetype ticks = 0;
        while (i + ticks < out.size() && out[i + ticks] == u'`')
            ++ticks;
        const QString fence(ticks, u'`');
        const qsizetype close = out.indexOf(fence, i + ticks);
        if (close < 0)
            break; // no closing run: plain text from here on
        for (qsizetype k = i + ticks; k < close; ++k)
            out[k] = u' ';
        i = close + ticks;
    }
    return out;
}

// The date fields of `text` wherever they stand (outside code) and its block id, in their order,
// each with the spaces before it: what the new text of the block keeps.
QString keptFields(const QString &text)
{
    const QString plain = withoutCode(text);
    QString kept;
    for (auto it = datePattern().globalMatch(plain); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        QString field = text.mid(m.capturedStart(0), m.capturedLength(0));
        if (!field.front().isSpace())
            field.prepend(u' ');
        kept += field;
    }
    const QRegularExpressionMatch id = idPattern().match(plain);
    if (id.hasMatch())
        kept += text.mid(id.capturedStart(1), id.capturedLength(1));
    return kept;
}

const QRegularExpression &fieldPattern()
{
    static const QRegularExpression re(uR"((?:✅|❌|➕|📅|⏳|🛫)|(?:^|\s)\^[A-Za-z0-9-]+\s*$)"_s);
    return re;
}

// Where the text starts: after a BOM, the indentation, the list marker and the checkbox or keyword.
qsizetype textStart(QStringView line)
{
    const qsizetype bom = line.startsWith(QChar(0xFEFF)) ? 1 : 0;
    const QRegularExpressionMatch m = linePattern().matchView(line.mid(bom));
    if (m.hasMatch())
        return bom + m.capturedEnd(0);
    qsizetype i = bom;
    while (i < line.size() && line[i].isSpace())
        ++i;
    return i;
}

} // namespace

QString blockLineText(QStringView line)
{
    return line.mid(textStart(line)).trimmed().toString();
}

std::optional<QString> taskCharacter(QStringView line)
{
    const QRegularExpressionMatch m = linePattern().matchView(line.startsWith(QChar(0xFEFF)) ? line.mid(1) : line);
    if (!m.hasMatch())
        return std::nullopt;
    if (m.capturedLength(3) > 0)
        return m.captured(3);
    if (m.capturedLength(4) > 0) {
        const auto status = markdown::statusFromKeyword(m.capturedView(4).trimmed());
        if (status)
            return QString(markdown::statusChar(*status));
    }
    return std::nullopt;
}

std::optional<QString> replaceBlockText(const QString &content, int line, const QString &text,
                                        const std::optional<QString> &task, QDate today)
{
    if (text.contains(u'\n') || text.contains(u'\r') || line < 0)
        return std::nullopt;
    QList<text::Line> lines = text::splitLines(content);
    if (line >= lines.size())
        return std::nullopt;
    const QString old = lines[line].text;
    const qsizetype start = textStart(old);
    const QString oldText = old.mid(start);

    QString newText = text.trimmed();
    if (!fieldPattern().match(withoutCode(newText)).hasMatch()) {
        const QString kept = keptFields(oldText);
        newText += newText.isEmpty() ? kept.trimmed() : kept;
    }
    lines[line].text = old.left(start) + newText;
    QString result = text::joinLines(lines);

    if (task && task->size() == 1) {
        const std::optional<QString> current = taskCharacter(lines[line].text);
        const TaskStatus wanted = markdown::statusFromCheckbox(task->at(0));
        const bool same = current && markdown::statusFromCheckbox(current->at(0)) == wanted;
        if (!same)
            result = markdown::setTaskStatus(result, line, wanted, today);
    }
    return result;
}

namespace {

QString leadingSpace(QStringView line)
{
    qsizetype n = line.startsWith(QChar(0xFEFF)) ? 1 : 0;
    const qsizetype bom = n;
    while (n < line.size() && (line[n] == u' ' || line[n] == u'\t'))
        ++n;
    return line.mid(bom, n - bom).toString();
}

// The lines of a script's markdown: any line break, the trailing empty lines left out.
QStringList markdownLines(const QString &markdown)
{
    QString text = markdown;
    text.replace(u"\r\n"_s, u"\n"_s);
    text.replace(u'\r', u'\n');
    QStringList lines = text.split(u'\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    return lines;
}

} // namespace

std::optional<QString> insertBlockLines(const QString &content, int line, int endLine, const QString &markdown,
                                        Placement where, const QString &childIndent, int *firstLine)
{
    QList<text::Line> lines = text::splitLines(content);
    // splitLines() gives a note ending with a line break a last empty line: no block's line.
    const int count = int(lines.size()) - (lines.size() > 1 && lines.last().text.isEmpty() && lines.last().eol.isEmpty() ? 1 : 0);
    if (line < 0 || endLine < line || endLine >= count)
        return std::nullopt;
    const QStringList added = markdownLines(markdown);
    if (added.isEmpty())
        return std::nullopt;
    QString indent = leadingSpace(lines[line].text);
    if (where == Placement::Child) {
        QString more = childIndent;
        if (more.isEmpty())
            more = indent.contains(u'\t') ? u"\t"_s : u"  "_s;
        indent += more;
    }
    const int at = where == Placement::Before ? line : endLine + 1;
    const QString eol = text::preferredEol(lines, at > 0 ? at - 1 : 0);
    QList<text::Line> inserted;
    for (const QString &text : added) {
        text::Line l;
        l.text = text.trimmed().isEmpty() ? QString() : indent + text;
        l.eol = eol;
        inserted << l;
    }
    if (at >= int(lines.size()) || (at == count && lines[at - 1].eol.isEmpty())) {
        // After the note's last line, which has no line break: it gets one, the new last line none.
        lines[at - 1].eol = eol;
        inserted.last().eol.clear();
    }
    for (qsizetype i = 0; i < inserted.size(); ++i)
        lines.insert(at + i, inserted[i]);
    if (firstLine)
        *firstLine = at;
    return text::joinLines(lines);
}

std::optional<QString> removeBlockLines(const QString &content, int line, int endLine)
{
    QList<text::Line> lines = text::splitLines(content);
    const int count = int(lines.size()) - (lines.size() > 1 && lines.last().text.isEmpty() && lines.last().eol.isEmpty() ? 1 : 0);
    if (line < 0 || endLine < line || endLine >= count)
        return std::nullopt;
    const bool wasLast = endLine == int(lines.size()) - 1;
    lines.remove(line, endLine - line + 1);
    if (wasLast && !lines.isEmpty())
        lines.last().eol.clear(); // the note ended without a line break: it still does
    return text::joinLines(lines);
}

std::optional<QString> setBlockProperty(const QString &content, int line, int ownEndLine, const QString &key,
                                        const QString &value)
{
    return markdown::setBlockProperty(content, line, ownEndLine, key, value);
}

// (blockedit.h) The words of a line as a reader takes them: [[links]] as their labels (the alias, else the
// target's last part without its heading), tags left out, white space single.
QString plainWords(QStringView text)
{
    const qsizetype eol = text.indexOf(u'\n');
    const QStringView line = eol < 0 ? text : text.first(eol);
    struct Cut
    {
        qsizetype from = 0;
        qsizetype to = 0;
        QString with;
    };
    QList<Cut> cuts;
    for (const auto tags = markdown::findTags(line); const markdown::TagSpan &tag : tags)
        cuts.append({tag.from, tag.to, QString()});
    const markdown::InlineScan scan = markdown::extractInline(line);
    for (const Link &link : scan.links) {
        if (link.kind != LinkKind::Wiki && link.kind != LinkKind::Embed)
            continue;
        QString label = link.display.trimmed();
        if (label.isEmpty()) {
            label = link.target;
            const qsizetype hash = label.indexOf(u'#');
            if (hash >= 0)
                label.truncate(hash);
            const qsizetype slash = label.lastIndexOf(u'/');
            if (slash >= 0)
                label = label.sliced(slash + 1);
            if (label.endsWith(u".md"_s, Qt::CaseInsensitive))
                label.chop(3);
        }
        cuts.append({link.from, link.to, label});
    }
    std::ranges::sort(cuts, {}, &Cut::from);
    QString out;
    qsizetype at = 0;
    for (const Cut &cut : std::as_const(cuts)) {
        if (cut.from < at || cut.to > line.size())
            continue; // inside an earlier cut (the link of a #[[tag]])
        out += line.sliced(at, cut.from - at);
        out += u' ' + cut.with + u' ';
        at = cut.to;
    }
    out += line.sliced(at);
    return out.simplified();
}

} // namespace brulion::extensions

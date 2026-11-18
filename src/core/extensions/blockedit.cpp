#include "extensions/blockedit.h"

#include "markdown/edit.h"
#include "markdown/tasks.h"
#include "text/text.h"

#include <QRegularExpression>

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

} // namespace brulion::extensions

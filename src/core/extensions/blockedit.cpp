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

// Obsidian Tasks' date fields and a block id at the end of a block's text
const QRegularExpression &tailPattern()
{
    static const QRegularExpression re(
        uR"(((?:[ \t]+(?:✅|❌|➕|📅|⏳|🛫)\x{FE0F}?[ \t]*\d{4}-\d{2}-\d{2})*)([ \t]+\^[A-Za-z0-9-]+)?[ \t]*$)"_s);
    return re;
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
    const QRegularExpressionMatch tail = tailPattern().match(oldText);
    if (tail.hasMatch() && tail.capturedLength(0) > 0 && !fieldPattern().match(newText).hasMatch())
        newText += tail.captured(1) + tail.captured(2);
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

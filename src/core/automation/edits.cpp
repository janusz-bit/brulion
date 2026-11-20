#include "automation/edits.h"

#include "dates/local.h"
#include "dates/natural.h"
#include "dates/recurrence.h"
#include "markdown/blockdocument.h"
#include "markdown/linkformat.h"
#include "markdown/tokens.h"
#include "notebook/capture.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "text/text.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::automation {

namespace {

/// A block's text as the body of a new list item: no marker, continuation lines unindented
/// (BlockDocument::insertAfter() writes them with the item's own indentation).
QString itemBody(const QString &block)
{
    static const QRegularExpression marker(u"^([-*+]|\\d+[.)])[ \\t]+"_s);
    QStringList lines = QString(block).remove(u'\r').split(u'\n');
    if (!lines.isEmpty())
        lines.first().remove(marker);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (lines.at(i).startsWith(u"  "))
            lines[i].remove(0, 2);
    }
    return lines.join(u'\n');
}

/// "^abc", "((abc))", "abc" -> "abc".
QString bareBlockId(const QString &raw)
{
    QString id = raw.trimmed();
    if (id.startsWith(u"((") && id.endsWith(u"))"))
        id = id.mid(2, id.size() - 4).trimmed();
    if (id.startsWith(u'^'))
        id.remove(0, 1);
    return id.trimmed();
}

/// The text as one line of a list item: line breaks become spaces, no marker or checkbox of its
/// own (the task gets its own "- [ ] ").
QString oneLine(const QString &text)
{
    static const QRegularExpression marker(u"^\\s*(?:[-*+]|\\d+[.)])\\s+"_s);
    static const QRegularExpression checkbox(u"^\\[[ xX/>\\-]\\]\\s*"_s);
    static const QRegularExpression breaks(u"\\s*[\\r\\n]+\\s*"_s);
    QString t = text;
    t.replace(breaks, u" "_s);
    t = t.trimmed();
    t.remove(marker);
    t.remove(checkbox);
    return t.trimmed();
}

/// A date phrase: ISO ("2026-11-21", "2026-11-21 18:00") or the first natural-language date of it.
std::optional<DateSpec> phraseDate(const QString &phrase, const QDateTime &now, Qt::DayOfWeek weekStart,
                                   dates::UiLocale locale)
{
    const QString t = phrase.trimmed();
    if (t.isEmpty())
        return std::nullopt;
    if (const auto spec = dates::parseDateSpec(t))
        return spec;
    if (t.compare(u"today", Qt::CaseInsensitive) == 0)
        return DateSpec{now.date(), std::nullopt};
    if (const auto found = dates::parseNaturalDate(t, now, weekStart, locale))
        return found->spec;
    return std::nullopt;
}

} // namespace

namespace {

/// Line breaks in a block's trailing blank lines (BlockEntry::trailing).
int blankLines(const QString &trailing)
{
    const qsizetype lf = trailing.count(u'\n');
    return int(lf > 0 ? lf : trailing.count(u'\r'));
}

/// The lines of `content` as splitLines() gives them, without the empty line after a final line
/// break.
QList<text::Line> linesOf(const QString &content)
{
    QList<text::Line> lines = text::splitLines(content);
    if (!lines.isEmpty() && lines.last().text.isEmpty() && lines.last().eol.isEmpty())
        lines.removeLast();
    return lines;
}

/// The last line of block `i` (before its trailing blank lines).
int lastLineOf(const markdown::BlockDocument &doc, int i, int lineCount)
{
    const int next = i + 1 < doc.count() ? doc.at(i + 1).line : lineCount;
    return std::max(doc.at(i).line, next - 1 - blankLines(doc.at(i).trailing));
}

/// `content` with `block` (lines joined with "\n") inserted as whole lines before line `line`
/// (the line count: at the end), in the note's own line ending; a blank line before or after it
/// where a list item would otherwise run into other text.
Inserted insertAtLine(const QString &content, int line, const QString &block, bool blankBefore, bool blankAfter)
{
    const QList<text::Line> lines = linesOf(content);
    const QString eol = text::detectEol(content);
    const qsizetype offset = line < lines.size() ? lines.at(line).start : content.size();
    QString piece = QString(block).remove(u'\r').replace(u'\n', eol) + eol;
    if (blankBefore)
        piece.prepend(eol);
    if (blankAfter)
        piece.append(eol);
    if (offset == content.size() && !content.isEmpty() && !content.endsWith(u'\n') && !content.endsWith(u'\r'))
        piece.prepend(eol);
    Inserted out;
    out.content = content.left(offset) + piece + content.mid(offset);
    out.line = line + (blankBefore ? 1 : 0);
    out.ok = true;
    return out;
}

bool isListItem(const markdown::BlockDocument &doc, int i)
{
    return i >= 0 && i < doc.count() && doc.at(i).kind == BlockKind::ListItem;
}

} // namespace

Inserted insertBlock(const QString &content, const InsertSpec &spec)
{
    Inserted out;
    if (spec.block.trimmed().isEmpty()) {
        out.error = u"empty"_s;
        return out;
    }
    const QString heading = spec.heading.trimmed();
    const QString blockId = bareBlockId(spec.blockId);
    markdown::BlockDocument doc(content);
    int first = 0; // the first block after the frontmatter and the page's properties
    while (first < doc.count() && doc.at(first).kind == BlockKind::Frontmatter)
        ++first;

    if (blockId.isEmpty() && heading.isEmpty() && (spec.place == Place::End || first >= doc.count())) {
        // The page's end (also its start when it has nothing to put the block before).
        out.content = notebook::appendBlock(content, spec.block);
        const QString eol = text::detectEol(out.content);
        const QChar breakChar = eol == u"\r" ? u'\r' : u'\n';
        out.line = int(out.content.count(breakChar) - (QString(spec.block).remove(u'\r').count(u'\n') + 1));
        out.ok = true;
        return out;
    }
    const int lineCount = int(linesOf(content).size());

    if (!blockId.isEmpty()) {
        // A block's child: the outline's own insertion (indentation and marker of its children).
        int target = -1;
        for (int i = 0; i < doc.count() && target < 0; ++i) {
            if (doc.at(i).blockId == blockId)
                target = i;
        }
        if (target < 0) {
            out.error = u"block_not_found"_s;
            return out;
        }
        const markdown::BlockEntry &entry = doc.at(target);
        if (entry.kind != BlockKind::ListItem) {
            // A paragraph or heading with an id has no children: the block goes right after it.
            const int last = lastLineOf(doc, target, lineCount);
            return insertAtLine(content, last + 1, spec.block, true, target + 1 < doc.count() && !isListItem(doc, target + 1)
                                                                       && blankLines(entry.trailing) == 0);
        }
        const int after = spec.place == Place::Start ? target : doc.subtreeRange(target).last();
        const markdown::EditResult edit = doc.insertAfter(after, itemBody(spec.block), entry.depth + 1, true);
        if (!edit.changed()) {
            out.error = u"empty"_s;
            return out;
        }
        out.content = doc.toString();
        out.line = edit.focus >= 0 ? doc.at(edit.focus).line : -1;
        out.ok = true;
        return out;
    }

    int anchor = -1; // the heading (or the page's title) the block goes under
    int end = doc.count(); // the first block after its section
    if (!heading.isEmpty()) {
        QList<Heading> headings;
        QList<int> rows;
        for (int i = 0; i < doc.count(); ++i) {
            if (const markdown::BlockEntry &e = doc.at(i); e.headingLevel > 0) {
                headings.append(Heading{e.headingLevel, e.body.section(u'\n', 0, 0), e.line});
                rows.append(i);
            }
        }
        QStringView fragment = heading;
        while (fragment.startsWith(u'#'))
            fragment = fragment.sliced(1);
        const int h = markdown::findHeading(headings, fragment.trimmed());
        if (h < 0) {
            out.error = u"heading_not_found"_s;
            return out;
        }
        anchor = rows.at(h);
        const markdown::BlockEntry &entry = doc.at(anchor);
        for (int i = anchor + 1; i < doc.count(); ++i) {
            const markdown::BlockEntry &e = doc.at(i);
            if (e.headingLevel > 0 && e.prefix.isEmpty() && e.headingLevel <= entry.headingLevel) {
                end = i;
                break;
            }
        }
    } else {
        // The page's start: after a title heading that opens the page.
        anchor = first < doc.count() && doc.at(first).headingLevel == 1 && doc.at(first).prefix.isEmpty() ? first
                                                                                                          : first - 1;
    }

    if (spec.place == Place::Start) {
        const int next = anchor + 1;
        if (next < end && next < doc.count()) {
            // Before the section's first block; a paragraph after a list item needs a blank line.
            return insertAtLine(content, doc.at(next).line, spec.block, false, !isListItem(doc, next));
        }
        // An empty section: right under the heading.
        const int line = anchor >= 0 ? lastLineOf(doc, anchor, lineCount) + 1 : 0;
        return insertAtLine(content, line, spec.block, false, false);
    }
    // The section's end: after its last block (its blank lines stay after the new block).
    const int last = end - 1;
    if (last <= anchor) {
        const int line = lastLineOf(doc, anchor, lineCount) + 1;
        return insertAtLine(content, line, spec.block, false, false);
    }
    return insertAtLine(content, lastLineOf(doc, last, lineCount) + 1, spec.block, !isListItem(doc, last), false);
}

BuiltTask buildTask(const TaskSpec &spec, const QDateTime &now, Qt::DayOfWeek weekStart, dates::UiLocale locale,
                    QTime defaultTime)
{
    BuiltTask out;
    const auto fail = [&out](const QString &error, const QString &detail) {
        out = BuiltTask();
        out.error = error;
        out.detail = detail;
        return out;
    };
    const QString text = oneLine(spec.text);
    if (text.isEmpty())
        return fail(u"missing_text"_s, u"text"_s);

    QString extra; // priority and recurrence, after the dates
    if (!spec.priority.trimmed().isEmpty()) {
        const auto p = notebook::priorityFromName(spec.priority.trimmed());
        if (!p)
            return fail(u"bad_priority"_s, spec.priority.trimmed().left(64));
        if (*p != Priority::None)
            extra += u' ' + markdown::tokens::priorityEmoji(*p).toString();
    }
    if (!spec.every.trimmed().isEmpty()) {
        const QString rule = dates::canonicalRecurrence(spec.every.trimmed());
        if (rule.isEmpty())
            return fail(u"recurrence_not_understood"_s, spec.every.trimmed().left(200));
        extra += u' ' + markdown::tokens::Recurrence.toString() + u' ' + rule;
    }

    const bool dated = !spec.due.trimmed().isEmpty() || !spec.scheduled.trimmed().isEmpty()
                       || !spec.start.trimmed().isEmpty() || !spec.remind.trimmed().isEmpty();
    if (!dated && spec.parseText) {
        // As quick capture reads it: "jutro o 18" in the text is the reminder, "w piątek" the due date.
        const notebook::CaptureLine line = notebook::buildCaptureLine(text, now, weekStart, locale);
        QString block;
        if (line.kind == notebook::CaptureLine::Kind::Reminder || line.kind == notebook::CaptureLine::Kind::Due) {
            block = line.block.section(u'\n', 0, 0);
            if (line.kind == notebook::CaptureLine::Kind::Reminder) {
                out.reminder = line.when;
                if (spec.alarm) {
                    // The reminder token becomes an alarm's.
                    const QString plain = markdown::formatReminderToken(line.when, false);
                    const QString loud = markdown::formatReminderToken(line.when, true);
                    if (const qsizetype at = block.lastIndexOf(plain); at >= 0)
                        block.replace(at, plain.size(), loud);
                    out.alarm = true;
                }
            } else {
                out.due = line.when.date;
            }
        } else {
            block = u"- [ ] "_s + text;
        }
        out.block = block + extra;
        return out;
    }

    QString line = u"- [ ] "_s + text;
    const struct {
        const QString *phrase;
        QStringView token;
        const char *name;
    } days[] = {{&spec.start, markdown::tokens::Start, "start"},
                {&spec.scheduled, markdown::tokens::Scheduled, "scheduled"},
                {&spec.due, markdown::tokens::Due, "due"}};
    for (const auto &d : days) {
        if (d.phrase->trimmed().isEmpty())
            continue;
        const auto when = phraseDate(*d.phrase, now, weekStart, locale);
        if (!when)
            return fail(u"date_not_understood"_s, QString::fromLatin1(d.name));
        line += u' ' + d.token.toString() + u' ' + DateSpec{when->date, std::nullopt}.toString();
        if (qstrcmp(d.name, "due") == 0)
            out.due = when->date;
    }
    if (!spec.remind.trimmed().isEmpty()) {
        auto when = phraseDate(spec.remind, now, weekStart, locale);
        if (!when)
            return fail(u"date_not_understood"_s, spec.alarm ? u"alarm"_s : u"remind"_s);
        if (!when->time)
            when->time = defaultTime.isValid() ? defaultTime : QTime(9, 0);
        line += u' ' + markdown::formatReminderToken(*when, spec.alarm);
        out.reminder = *when;
        out.alarm = spec.alarm;
    }
    out.block = line + extra;
    return out;
}

std::optional<QDate> dayOf(QStringView rawPhrase, const QDateTime &now, Qt::DayOfWeek weekStart,
                           dates::UiLocale locale)
{
    const QString phrase = rawPhrase.trimmed().toString();
    if (phrase.isEmpty())
        return std::nullopt;
    if (const auto iso = dates::parseIsoDate(phrase))
        return iso;
    if (phrase.compare(u"today", Qt::CaseInsensitive) == 0)
        return now.date();
    const auto found = dates::parseNaturalDate(phrase, now, weekStart, locale);
    if (!found || !phrase.left(found->start).trimmed().isEmpty() || !phrase.mid(found->end()).trimmed().isEmpty())
        return std::nullopt;
    return found->spec.date;
}

} // namespace brulion::automation

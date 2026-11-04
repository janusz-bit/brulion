// A habit's check in a daily note (see habitchecks.h).
#include "notebook/habitchecks.h"

#include "markdown/edit.h"
#include "markdown/inline.h"
#include "markdown/parse.h"
#include "notebook/journals.h"
#include "text/text.h"

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

/// The checks of `content` read as the daily note of `date` (whatever its path says).
QList<habits::Check> checksIn(const QString &content, const VaultPath &path, const VaultConfig &config, QDate date)
{
    ParsedNote note = markdown::parseNote(path, content, config);
    note.journalDate = date;
    return habits::checksOf(note);
}

TaskStatus statusOf(habits::Mark mark)
{
    switch (mark) {
    case habits::Mark::Done: return TaskStatus::Done;
    case habits::Mark::Skipped: return TaskStatus::Cancelled;
    case habits::Mark::Open: return TaskStatus::Todo;
    }
    return TaskStatus::Todo;
}

/// `line` inserted after line `after` (0-based) of `content`, with that line's line ending (the
/// last line without one gets the note's first).
QString insertLineAfter(const QString &content, int after, const QString &line)
{
    QList<text::Line> lines = text::splitLines(content);
    if (after < 0 || after >= lines.size())
        return appendBlock(content, line);
    const QString eol = text::preferredEol(lines, after);
    text::Line inserted;
    inserted.text = line;
    if (lines[after].eol.isEmpty()) {
        lines[after].eol = eol; // the note's last line: the new one ends the note as it did
        inserted.eol = QString();
    } else {
        inserted.eol = lines[after].eol;
    }
    lines.insert(after + 1, inserted);
    return text::joinLines(lines);
}

/// The leading white space of line `line` of `content`.
QString indentOf(const QString &content, int line)
{
    const QList<text::Line> lines = text::splitLines(content);
    if (line < 0 || line >= lines.size())
        return {};
    const QString &t = lines[line].text;
    qsizetype n = 0;
    while (n < t.size() && (t[n] == u' ' || t[n] == u'\t'))
        ++n;
    return t.first(n);
}

} // namespace

QString habitCheckLine(const QString &name, const QString &tag, bool pageLink)
{
    const QString clean = name.simplified();
    const QString shown = pageLink ? u"[["_s + clean + u"]]"_s : clean;
    return u"- [ ] "_s + shown + u' ' + markdown::tagSyntax(tag);
}

HabitCheckEdit setHabitCheck(const QString &content, const VaultPath &path, const VaultConfig &config, QDate date,
                             const QString &name, habits::Mark mark, QDate today, const QString &tag, bool pageLink)
{
    HabitCheckEdit out;
    out.content = content;
    const QString key = habits::habitKey(habits::habitName(name));
    if (key.isEmpty() || !date.isValid()) {
        out.refused = true;
        return out;
    }
    const QList<habits::Check> checks = checksIn(content, path, config, date);
    QList<habits::Check> mine;
    for (const habits::Check &c : checks) {
        if (c.key == key)
            mine.append(c);
    }
    out.hadCheck = !mine.isEmpty();
    for (const habits::Check &c : std::as_const(mine))
        out.before = habits::strongerMark(out.before, c.mark);

    if (mark == habits::Mark::Open) {
        QString text = content;
        for (const habits::Check &c : std::as_const(mine)) {
            if (c.mark != habits::Mark::Open)
                text = markdown::setTaskStatus(text, c.line, TaskStatus::Todo, today);
        }
        out.content = text;
        out.changed = text != content;
        out.line = mine.isEmpty() ? -1 : mine.first().line;
        return out;
    }

    for (const habits::Check &c : std::as_const(mine)) {
        if (c.mark == mark) {
            out.line = c.line;
            return out; // already so
        }
    }
    QString text = content;
    int line = -1;
    if (!mine.isEmpty()) {
        line = mine.first().line;
    } else {
        const QString newLine = habitCheckLine(name, tag, pageLink);
        if (!checks.isEmpty()) {
            // Next to the other habits' checks: after the last one (and its children).
            const habits::Check &last = checks.last();
            const ParsedNote note = markdown::parseNote(path, content, config);
            const int end = last.blockIndex >= 0 && last.blockIndex < note.blocks.size()
                                    ? note.blocks[last.blockIndex].endLine
                                    : last.line;
            text = insertLineAfter(content, end, indentOf(content, last.line) + newLine);
        } else {
            text = appendBlock(content, newLine);
        }
        // Find the new line: the check of the habit (the note had none).
        for (const habits::Check &c : checksIn(text, path, config, date)) {
            if (c.key == key) {
                line = c.line;
                break;
            }
        }
        if (line < 0) {
            out.refused = true; // the name reads back as another habit's (or none)
            return out;
        }
        out.created = true;
    }
    text = markdown::setTaskStatus(text, line, statusOf(mark), today);
    out.content = text;
    out.changed = text != content;
    out.line = line;
    return out;
}

QString habitDefinitionBlock(const QString &name, const QString &schedule, dates::UiLocale locale)
{
    QString s = schedule.simplified();
    if (s.isEmpty())
        s = locale == dates::UiLocale::English ? u"every day"_s : u"codziennie"_s;
    return u"- "_s + name.simplified() + u"\n  habit:: "_s + s;
}

QString habitsPageTitle(dates::UiLocale locale)
{
    return locale == dates::UiLocale::English ? u"Habits"_s : u"Nawyki"_s;
}

} // namespace brulion::notebook

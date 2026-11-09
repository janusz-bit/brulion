#include "markdown/logbook.h"

#include "markdown/edit.h"
#include "markdown/lines_p.h"
#include "markdown/parse.h"
#include "markdown/tasks.h"
#include "text/text.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

constexpr QStringView DayNames[] = {u"Mon", u"Tue", u"Wed", u"Thu", u"Fri", u"Sat", u"Sun"};

/// A written `=> HH:MM:SS` of more hours than this (100 years) is no real clock: the line does not
/// read (and the hours of a hostile note cannot overflow the count).
constexpr qint64 MaxWrittenHours = 100 * 366 * 24;

bool isLogbookStart(QStringView line)
{
    return line.trimmed() == u":LOGBOOK:";
}

bool isDrawerEndLine(QStringView line)
{
    return line.trimmed() == u":END:";
}

/// The inside of `[2026-11-03 Tue 10:00:00]`: a date, an optional day name (any word, also a
/// Polish "wt."), a time with optional seconds. Invalid dates or times: an invalid QDateTime.
QDateTime parseStamp(QStringView inner)
{
    static const QRegularExpression re(
            u"^[ \\t]*(\\d{4})-(\\d{2})-(\\d{2})(?:[ \\t]+[^\\d\\s\\]]+)?[ \\t]+(\\d{1,2}):(\\d{2})(?::(\\d{2}))?[ \\t]*$"_s);
    const QRegularExpressionMatch m = re.matchView(inner);
    if (!m.hasMatch())
        return {};
    const QDate date(m.capturedView(1).toInt(), m.capturedView(2).toInt(), m.capturedView(3).toInt());
    const int seconds = m.capturedView(6).isEmpty() ? 0 : m.capturedView(6).toInt();
    const QTime time(m.capturedView(4).toInt(), m.capturedView(5).toInt(), seconds);
    if (!date.isValid() || !time.isValid())
        return {};
    return QDateTime(date, time);
}

/// A match of the `CLOCK:` pattern as the entry of line `index`; nullopt when it does not read.
std::optional<ClockEntry> clockEntry(const QRegularExpressionMatch &m, int index)
{
    if (!m.hasMatch())
        return std::nullopt;
    ClockEntry e;
    e.line = index;
    e.start = parseStamp(m.capturedView(1));
    if (!e.start.isValid())
        return std::nullopt;
    if (m.capturedStart(2) < 0)
        return e; // a running clock
    e.end = parseStamp(m.capturedView(2));
    if (!e.end.isValid())
        return std::nullopt;
    e.seconds = e.start.secsTo(e.end);
    if (e.seconds >= 0)
        return e;
    // The timestamps give no duration (an hour repeated when the clocks went back, a hand edit):
    // the written one, else the line does not count.
    bool ok = false;
    const qint64 hours = m.capturedView(3).toLongLong(&ok);
    if (!ok || hours > MaxWrittenHours)
        return std::nullopt;
    e.seconds = hours * 3600 + m.capturedView(4).toInt() * 60 + m.capturedView(5).toInt();
    return e;
}

/// One logbook line read into `out`.
void readLine(QStringView line, int index, Logbook &out)
{
    static const QRegularExpression clock(
            u"^[ \\t]*CLOCK:[ \\t]*\\[([^\\]\\r\\n]*)\\](?:[ \\t]*--[ \\t]*\\[([^\\]\\r\\n]*)\\]"
            "(?:[ \\t]*=>[ \\t]*(\\d+):(\\d{2})(?::(\\d{2}))?)?)?[ \\t]*$"_s);
    static const QRegularExpression state(
            u"^[ \\t]*(?:[-+*][ \\t]+)?State[ \\t]+\"([^\"\\r\\n]*)\"(?:[ \\t]+from(?:[ \\t]+\"([^\"\\r\\n]*)\")?)?"
            "[ \\t]+\\[([^\\]\\r\\n]*)\\][ \\t]*(?:\\\\\\\\)?[ \\t]*$"_s);
    const QStringView t = line.trimmed();
    if (t.startsWith(u"CLOCK:")) {
        if (const auto entry = clockEntry(clock.matchView(line), index))
            out.clocks.append(*entry);
        else
            ++out.malformed;
        return;
    }
    if (t.startsWith(u"State ") || t.startsWith(u"- State") || t.startsWith(u"+ State") || t.startsWith(u"* State")) {
        const QRegularExpressionMatch m = state.matchView(line);
        const QDateTime at = m.hasMatch() ? parseStamp(m.capturedView(3)) : QDateTime();
        if (at.isValid())
            out.changes.append(StatusChange{m.captured(1), m.captured(2), at, index});
        else
            ++out.malformed;
    }
}

/// Closed intervals [start, end) of the entries, as instants (seconds since the epoch: only for
/// measuring — what is written stays local wall-clock time), merged where they overlap.
QList<std::pair<qint64, qint64>> mergedIntervals(const QList<ClockEntry> &clocks, const QDateTime &now)
{
    QList<std::pair<qint64, qint64>> spans;
    for (const ClockEntry &c : clocks) {
        const qint64 s = c.start.toSecsSinceEpoch();
        if (c.running()) {
            if (now.isValid() && now > c.start)
                spans.append({s, now.toSecsSinceEpoch()});
        } else if (c.seconds > 0) {
            spans.append({s, s + c.seconds});
        }
    }
    std::ranges::sort(spans);
    QList<std::pair<qint64, qint64>> merged;
    for (const auto &span : std::as_const(spans)) {
        if (!merged.isEmpty() && span.first <= merged.last().second)
            merged.last().second = std::max(merged.last().second, span.second);
        else
            merged.append(span);
    }
    return merged;
}

/// Where a task block's logbook is (or goes) in the file's lines.
struct DrawerPlace {
    int block = -1;    ///< the task's block (index into ParsedNote::blocks)
    int start = -1;    ///< the `:LOGBOOK:` line, -1 when there is none
    int end = -1;      ///< its `:END:` line
    int insertAt = -1; ///< where a new drawer goes (before this line index)
    QString indent;    ///< of the drawer's lines
    QList<int> own;    ///< the block's own lines (children excluded)
};

bool isPlanningOrProperty(QStringView line)
{
    static const QRegularExpression re(u"^[ \\t]*(?:(?:SCHEDULED|DEADLINE):[ \\t]*<|[A-Za-z0-9_-]+::(?:[ \\t]|$))"_s);
    return re.matchView(line).hasMatch();
}

/// The indentation of a list item's continuation lines: the first line's indentation and the
/// marker ("- ", "12. ") as spaces.
QString continuationIndent(QStringView first)
{
    const QStringView indent = leadingWhitespace(first);
    qsizetype j = indent.size();
    if (const QChar c = charAt(first, j); c == u'-' || c == u'*' || c == u'+') {
        ++j;
    } else {
        while (charAt(first, j).isDigit())
            ++j;
        if (charAt(first, j) == u'.' || charAt(first, j) == u')')
            ++j;
    }
    return indent + QString(j - indent.size() + 1, u' ');
}

std::optional<DrawerPlace> drawerPlace(const QList<text::Line> &lines, const ParsedNote &note, int line)
{
    const int b = blockAtLine(note, line);
    if (b < 0)
        return std::nullopt;
    const Block &block = note.blocks[b];
    if (block.kind != BlockKind::ListItem || !block.task || block.line >= lines.size())
        return std::nullopt;
    const QStringView first = lines[block.line].text;
    if (first.trimmed().startsWith(u'>'))
        return std::nullopt; // inside a quote: its prefixes are not written here
    DrawerPlace p;
    p.block = b;
    for (int l = block.line; l <= block.ownEndLine && l < lines.size(); ++l) {
        const bool inChild = std::ranges::any_of(
                block.children, [&](int c) { return note.blocks[c].line <= l && l <= note.blocks[c].endLine; });
        if (!inChild)
            p.own.append(l);
    }
    for (qsizetype k = 1; k < p.own.size(); ++k) {
        const QStringView t = lines[p.own.at(k)].text;
        if (p.start < 0 && isLogbookStart(t)) {
            p.start = p.own.at(k);
        } else if (p.start >= 0 && isDrawerEndLine(t)) {
            p.end = p.own.at(k);
            break;
        }
    }
    if (p.start >= 0 && p.end < 0)
        return std::nullopt; // an unclosed drawer: left alone
    // The indentation: an existing continuation line's, else past the list marker.
    for (qsizetype k = 1; k < p.own.size() && p.indent.isEmpty(); ++k) {
        const QStringView t = lines[p.own.at(k)].text;
        if (!t.trimmed().isEmpty())
            p.indent = leadingWhitespace(t).toString();
    }
    if (p.indent.isEmpty())
        p.indent = continuationIndent(first);
    // A new drawer: after the first line and the planning / property lines right after it.
    qsizetype k = 1;
    while (k < p.own.size() && p.own.at(k) == p.own.at(k - 1) + 1 && isPlanningOrProperty(lines[p.own.at(k)].text))
        ++k;
    p.insertAt = p.own.at(k - 1) + 1;
    return p;
}

/// Insert `newLines` before line `at` (each with the document's terminator; at the end of a
/// document without a final line break the previous line gets one).
QString insertLines(QList<text::Line> lines, int at, const QStringList &newLines)
{
    const QString eol = text::preferredEol(lines, std::max(0, at - 1));
    at = std::min(at, int(lines.size()));
    // At the end of a document without a final line break, the new last line goes without one.
    const bool atEnd = at > 0 && lines[at - 1].eol.isEmpty();
    if (atEnd)
        lines[at - 1].eol = eol;
    for (qsizetype k = 0; k < newLines.size(); ++k)
        lines.insert(at + k, text::Line{newLines[k], atEnd && k + 1 == newLines.size() ? QString() : eol, 0});
    return text::joinLines(lines);
}

/// Add `newLine` at the end of the task's logbook at `place` (a new drawer when it has none).
QString appendToLogbook(const QList<text::Line> &lines, const DrawerPlace &place, const QString &newLine)
{
    if (place.start >= 0)
        return insertLines(lines, place.end, {leadingWhitespace(lines[place.start].text) + newLine});
    return insertLines(lines, place.insertAt,
                       {place.indent + u":LOGBOOK:"_s, place.indent + newLine, place.indent + u":END:"_s});
}

/// The task's logbook as the file has it: where its drawer is, what it holds and the file line of
/// each scanned line.
struct FileLogbook {
    DrawerPlace place;
    Logbook book;
    QList<int> fileLines; ///< file line of each scanned line (index = ClockEntry::line)
};

std::optional<FileLogbook> fileLogbook(const QList<text::Line> &lines, const ParsedNote &note, int line)
{
    const auto place = drawerPlace(lines, note, line);
    if (!place)
        return std::nullopt;
    FileLogbook out;
    out.place = *place;
    if (place->start < 0)
        return out;
    QStringList scanned;
    for (const int l : place->own) {
        if (l < place->start || l > place->end)
            continue;
        scanned.append(lines[l].text);
        out.fileLines.append(l);
    }
    out.book = readLogbook(scanned.join(u'\n'));
    return out;
}

} // namespace

const ClockEntry *Logbook::running() const
{
    for (qsizetype k = clocks.size() - 1; k >= 0; --k) {
        if (clocks[k].running())
            return &clocks[k];
    }
    return nullptr;
}

Logbook readLogbook(QStringView text)
{
    Logbook out;
    bool inside = false;
    int index = 0;
    qsizetype from = 0;
    while (from <= text.size()) {
        qsizetype to = text.indexOf(u'\n', from);
        if (to < 0)
            to = text.size();
        QStringView line = text.sliced(from, to - from);
        if (line.endsWith(u'\r'))
            line.chop(1);
        if (!inside) {
            if (isLogbookStart(line)) {
                inside = true;
                out.present = true;
            }
        } else if (isDrawerEndLine(line)) {
            inside = false;
        } else {
            readLine(line, index, out);
        }
        ++index;
        from = to + 1;
    }
    // Only the last clock without an end runs; earlier ones were never closed (one pass: a drawer
    // of a hostile note may hold thousands of them).
    if (const ClockEntry *last = out.running()) {
        const int keep = last->line;
        const auto unclosed = [keep](const ClockEntry &c) { return c.running() && c.line != keep; };
        out.malformed += int(out.clocks.removeIf(unclosed));
    }
    return out;
}

qint64 clockedSeconds(const QList<ClockEntry> &clocks)
{
    qint64 total = 0;
    const auto spans = mergedIntervals(clocks, QDateTime());
    for (const auto &[s, e] : spans)
        total += e - s;
    return total;
}

QMap<QDate, qint64> clockedSecondsByDay(const QList<ClockEntry> &clocks, const QDateTime &now)
{
    QMap<QDate, qint64> out;
    const auto spans = mergedIntervals(clocks, now);
    for (const auto &[s, e] : spans) {
        const QDateTime start = QDateTime::fromSecsSinceEpoch(s);
        const QDateTime end = QDateTime::fromSecsSinceEpoch(e);
        for (QDate day = start.date(); day.isValid() && day <= end.date(); day = day.addDays(1)) {
            // The day's own midnights in local time (23 or 25 hours on a daylight-saving change).
            const qint64 dayStart = QDateTime(day, QTime(0, 0)).toSecsSinceEpoch();
            const qint64 dayEnd = QDateTime(day.addDays(1), QTime(0, 0)).toSecsSinceEpoch();
            const qint64 seconds = std::min(e, dayEnd) - std::max(s, dayStart);
            if (seconds > 0)
                out[day] += seconds;
        }
    }
    return out;
}

QString formatClockTimestamp(const QDateTime &local, bool seconds)
{
    const QDate d = local.date();
    const QTime t = local.time();
    return u'[' + d.toString(u"yyyy-MM-dd"_s) + u' ' + DayNames[d.dayOfWeek() - 1] + u' '
           + t.toString(seconds ? u"HH:mm:ss"_s : u"HH:mm"_s) + u']';
}

QString formatClockDuration(qint64 seconds)
{
    seconds = std::max<qint64>(0, seconds);
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    return u"%1:%2:%3"_s.arg(h, 2, 10, QLatin1Char('0'))
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
}

QString formatClockLine(const QDateTime &start, const QDateTime &end)
{
    QString out = u"CLOCK: "_s + formatClockTimestamp(start);
    if (end.isValid())
        out += u"--"_s + formatClockTimestamp(end) + u" =>  "_s + formatClockDuration(start.secsTo(end));
    return out;
}

QString formatStatusChange(QStringView to, QStringView from, const QDateTime &at)
{
    const auto quoted = [](QStringView s) {
        QString q = u'"' + s.toString() + u'"';
        return q.leftJustified(12, u' ');
    };
    return u"State "_s + quoted(to) + u" from "_s + quoted(from) + u' ' + formatClockTimestamp(at, false);
}

QString formatSpentTime(qint64 seconds)
{
    seconds = std::max<qint64>(0, seconds);
    if (seconds < 60)
        return QString::number(seconds) + u" s"_s;
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    if (h == 0)
        return QString::number(m) + u" min"_s;
    return QString::number(h) + u" h "_s + QString::number(m).rightJustified(2, u'0') + u" min"_s;
}

Logbook taskLogbook(const QString &content, int line)
{
    const auto book = fileLogbook(text::splitLines(content), parseNote(VaultPath(), content), line);
    return book ? book->book : Logbook();
}

int taskBlockLine(const QString &content, int line)
{
    const ParsedNote note = parseNote(VaultPath(), content);
    const int b = blockAtLine(note, line);
    if (b < 0 || note.blocks[b].kind != BlockKind::ListItem || !note.blocks[b].task)
        return -1;
    return note.blocks[b].line;
}

QString startTaskClock(const QString &content, int line, const QDateTime &now)
{
    const QList<text::Line> lines = text::splitLines(content);
    const auto book = fileLogbook(lines, parseNote(VaultPath(), content), line);
    if (!book || book->book.running() || !now.isValid())
        return content;
    return appendToLogbook(lines, book->place, formatClockLine(now));
}

QString stopTaskClock(const QString &content, int line, const QDateTime &now)
{
    QList<text::Line> lines = text::splitLines(content);
    const auto book = fileLogbook(lines, parseNote(VaultPath(), content), line);
    const ClockEntry *running = book ? book->book.running() : nullptr;
    const int fileLine = running ? book->fileLines.value(running->line, -1) : -1;
    if (fileLine < 0 || !now.isValid())
        return content;
    QString &text = lines[fileLine].text;
    text = text.first(text.indexOf(u"CLOCK:")) + formatClockLine(running->start, std::max(now, running->start));
    return text::joinLines(lines);
}

QString logStatusChange(const QString &content, int line, TaskStatus from, TaskStatus to, const QDateTime &now)
{
    if (!now.isValid())
        return content;
    const ParsedNote note = parseNote(VaultPath(), content);
    const QList<text::Line> lines = text::splitLines(content);
    const auto place = drawerPlace(lines, note, line);
    if (!place)
        return content;
    // The new keyword follows the workflow of the task's own one (LATER / NOW vs TODO / DOING).
    const QString &marker = note.blocks[place->block].task->marker;
    const QStringView previous = statusFromKeyword(marker) ? QStringView(marker) : QStringView();
    return appendToLogbook(lines, *place, formatStatusChange(keywordFor(to, previous), keywordFor(from), now));
}

QString setTaskStatusLogged(const QString &content, int line, TaskStatus status, const StatusLogOptions &options)
{
    const QDate today = options.now.isValid() ? options.now.date() : QDate();
    if (!options.history || !options.now.isValid())
        return setTaskStatus(content, line, status, today);
    const ParsedNote note = parseNote(VaultPath(), content);
    const int b = blockAtLine(note, line);
    const bool wasTask = b >= 0 && note.blocks[b].kind == BlockKind::ListItem && note.blocks[b].task;
    QString out = setTaskStatus(content, line, status, today);
    if (!wasTask || out == content)
        return out;
    const TaskStatus before = note.blocks[b].task->status;
    if (before == status)
        return out;
    // A recurring task's next occurrence is inserted above it: the task moved down.
    const int moved = int(text::splitLines(out).size() - text::splitLines(content).size());
    const int at = note.blocks[b].line + std::max(0, moved);
    if (status == TaskStatus::Doing)
        out = startTaskClock(out, at, options.now);
    else if (before == TaskStatus::Doing)
        out = stopTaskClock(out, at, options.now);
    return logStatusChange(out, at, before, status, options.now);
}

QString toggleTaskLogged(const QString &content, int line, const StatusLogOptions &options)
{
    const ParsedNote note = parseNote(VaultPath(), content);
    const int b = blockAtLine(note, line);
    if (!options.history || b < 0 || !note.blocks[b].task)
        return toggleTask(content, line, options.now.isValid() ? options.now.date() : QDate());
    const TaskStatus s = note.blocks[b].task->status;
    const bool closed = s == TaskStatus::Done || s == TaskStatus::Cancelled;
    return setTaskStatusLogged(content, line, closed ? TaskStatus::Todo : TaskStatus::Done, options);
}

} // namespace brulion::markdown

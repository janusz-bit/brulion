#include "markdown/logbook.h"

#include "markdown/edit.h"
#include "markdown/parse.h"
#include "markdown/tasks.h"
#include "text/text.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::markdown {

using namespace Qt::StringLiterals;

namespace {

constexpr QStringView DayNames[] = {u"Mon", u"Tue", u"Wed", u"Thu", u"Fri", u"Sat", u"Sun"};

QStringView trimmedView(QStringView s)
{
    return s.trimmed();
}

bool isLogbookStart(QStringView line)
{
    return trimmedView(line) == u":LOGBOOK:";
}

bool isDrawerEndLine(QStringView line)
{
    return trimmedView(line) == u":END:";
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

/// One logbook line read into `out`.
void readLine(QStringView line, int index, Logbook &out, QList<int> &runningLines)
{
    static const QRegularExpression clock(
            u"^[ \\t]*CLOCK:[ \\t]*\\[([^\\]\\r\\n]*)\\](?:[ \\t]*--[ \\t]*\\[([^\\]\\r\\n]*)\\]"
            "(?:[ \\t]*=>[ \\t]*(\\d+):(\\d{2})(?::(\\d{2}))?)?)?[ \\t]*$"_s);
    static const QRegularExpression state(
            u"^[ \\t]*(?:[-+*][ \\t]+)?State[ \\t]+\"([^\"\\r\\n]*)\"(?:[ \\t]+from(?:[ \\t]+\"([^\"\\r\\n]*)\")?)?"
            "[ \\t]+\\[([^\\]\\r\\n]*)\\][ \\t]*(?:\\\\\\\\)?[ \\t]*$"_s);
    const QStringView t = trimmedView(line);
    if (t.startsWith(u"CLOCK:")) {
        const QRegularExpressionMatch m = clock.matchView(line);
        if (!m.hasMatch()) {
            ++out.malformed;
            return;
        }
        ClockEntry e;
        e.line = index;
        e.start = parseStamp(m.capturedView(1));
        if (!e.start.isValid()) {
            ++out.malformed;
            return;
        }
        if (m.capturedLength(2) > 0 || m.capturedStart(2) >= 0) {
            e.end = parseStamp(m.capturedView(2));
            if (!e.end.isValid()) {
                ++out.malformed;
                return;
            }
            qint64 seconds = e.start.secsTo(e.end);
            if (seconds < 0) {
                // The timestamps give no duration (an hour repeated when the clocks went back, a
                // hand edit): the written one, else the line does not count.
                if (m.capturedStart(3) < 0) {
                    ++out.malformed;
                    return;
                }
                const qint64 written = m.capturedView(3).toLongLong() * 3600 + m.capturedView(4).toInt() * 60
                                       + (m.capturedStart(5) >= 0 ? m.capturedView(5).toInt() : 0);
                seconds = written;
            }
            e.seconds = seconds;
        } else {
            runningLines.append(int(out.clocks.size()));
        }
        out.clocks.append(e);
        return;
    }
    if (t.startsWith(u"State ") || t.startsWith(u"- State") || t.startsWith(u"+ State") || t.startsWith(u"* State")) {
        const QRegularExpressionMatch m = state.matchView(line);
        const QDateTime at = m.hasMatch() ? parseStamp(m.capturedView(3)) : QDateTime();
        if (!at.isValid()) {
            ++out.malformed;
            return;
        }
        out.changes.append(StatusChange{m.captured(1), m.captured(2), at, index});
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
    std::sort(spans.begin(), spans.end());
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
    int first = -1;    ///< the block's first line
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
    qsizetype i = 0;
    while (i < first.size() && (first[i] == u' ' || first[i] == u'\t'))
        ++i;
    QString indent = first.first(i).toString();
    qsizetype j = i;
    if (j < first.size() && (first[j] == u'-' || first[j] == u'*' || first[j] == u'+')) {
        ++j;
    } else {
        while (j < first.size() && first[j].isDigit())
            ++j;
        if (j < first.size() && (first[j] == u'.' || first[j] == u')'))
            ++j;
    }
    return indent + QString(j - i + 1, u' ');
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
    p.first = block.line;
    for (int l = block.line; l <= block.ownEndLine && l < lines.size(); ++l) {
        const bool inChild = std::any_of(block.children.cbegin(), block.children.cend(), [&](int c) {
            return note.blocks[c].line <= l && l <= note.blocks[c].endLine;
        });
        if (!inChild)
            p.own.append(l);
    }
    for (qsizetype k = 1; k < p.own.size(); ++k) {
        const QStringView t = lines[p.own[k]].text;
        if (p.start < 0 && isLogbookStart(t)) {
            p.start = p.own[k];
        } else if (p.start >= 0 && isDrawerEndLine(t)) {
            p.end = p.own[k];
            break;
        }
    }
    if (p.start >= 0 && p.end < 0)
        return std::nullopt; // an unclosed drawer: left alone
    // The indentation: an existing continuation line's, else past the list marker.
    for (qsizetype k = 1; k < p.own.size() && p.indent.isEmpty(); ++k) {
        const QStringView t = lines[p.own[k]].text;
        if (t.trimmed().isEmpty())
            continue;
        qsizetype i = 0;
        while (i < t.size() && (t[i] == u' ' || t[i] == u'\t'))
            ++i;
        if (i > 0)
            p.indent = t.first(i).toString();
    }
    if (p.indent.isEmpty())
        p.indent = continuationIndent(first);
    // A new drawer: after the first line and the planning / property lines right after it.
    qsizetype k = 1;
    while (k < p.own.size() && p.own[k] == p.own[k - 1] + 1 && isPlanningOrProperty(lines[p.own[k]].text))
        ++k;
    p.insertAt = p.own[k - 1] + 1;
    return p;
}

/// Insert `newLines` before line `at` (each with the document's terminator; at the end of a
/// document without a final line break the previous line gets one).
QString insertLines(QList<text::Line> lines, int at, const QStringList &newLines)
{
    const QString eol = text::preferredEol(lines, std::max(0, at - 1));
    if (at >= lines.size()) {
        at = int(lines.size());
    }
    if (at > 0 && lines[at - 1].eol.isEmpty()) {
        // The last line: it ends the document without a line break.
        lines[at - 1].eol = eol;
        QList<text::Line> added;
        for (qsizetype k = 0; k < newLines.size(); ++k)
            added.append(text::Line{newLines[k], k + 1 < newLines.size() ? eol : QString(), 0});
        lines.append(added);
        return text::joinLines(lines);
    }
    QList<text::Line> added;
    for (const QString &l : newLines)
        added.append(text::Line{l, eol, 0});
    lines.insert(at, added.size(), text::Line());
    for (qsizetype k = 0; k < added.size(); ++k)
        lines[at + k] = added[k];
    return text::joinLines(lines);
}

/// Add `newLine` at the end of the task's logbook (a new drawer when it has none).
QString appendToLogbook(const QString &content, int line, const QString &newLine)
{
    const ParsedNote note = parseNote(VaultPath(), content);
    const QList<text::Line> lines = text::splitLines(content);
    const auto place = drawerPlace(lines, note, line);
    if (!place)
        return content;
    if (place->start >= 0) {
        const QStringView startLine = lines[place->start].text;
        qsizetype i = 0;
        while (i < startLine.size() && (startLine[i] == u' ' || startLine[i] == u'\t'))
            ++i;
        const QString indent = startLine.first(i).toString();
        return insertLines(lines, place->end, {indent + newLine});
    }
    return insertLines(lines, place->insertAt,
                       {place->indent + u":LOGBOOK:"_s, place->indent + newLine, place->indent + u":END:"_s});
}

/// The own drawer lines of the task's logbook, with their line numbers in the file.
struct FileLogbook {
    Logbook book;
    QList<int> fileLines; ///< file line of each scanned line (index = ClockEntry::line)
};

std::optional<FileLogbook> fileLogbook(const QList<text::Line> &lines, const ParsedNote &note, int line)
{
    const auto place = drawerPlace(lines, note, line);
    if (!place)
        return std::nullopt;
    FileLogbook out;
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

QString markerOf(const Block &block)
{
    return block.task ? block.task->marker : QString();
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
    QList<int> running;
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
            readLine(line, index, out, running);
        }
        ++index;
        from = to + 1;
    }
    // Only the last clock without an end runs; earlier ones were never closed.
    if (running.size() > 1) {
        for (qsizetype k = running.size() - 2; k >= 0; --k) {
            out.clocks.removeAt(running[k]);
            ++out.malformed;
        }
    }
    return out;
}

qint64 clockedSeconds(const QList<ClockEntry> &clocks)
{
    qint64 total = 0;
    for (const auto &span : mergedIntervals(clocks, QDateTime()))
        total += span.second - span.first;
    return total;
}

QMap<QDate, qint64> clockedSecondsByDay(const QList<ClockEntry> &clocks, const QDateTime &now)
{
    QMap<QDate, qint64> out;
    for (const auto &[s, e] : mergedIntervals(clocks, now)) {
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
    return u'[' + d.toString(u"yyyy-MM-dd"_s) + u' ' + DayNames[d.dayOfWeek() - 1].toString() + u' '
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
    const ParsedNote note = parseNote(VaultPath(), content);
    const auto book = fileLogbook(text::splitLines(content), note, line);
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
    const ParsedNote note = parseNote(VaultPath(), content);
    const auto book = fileLogbook(text::splitLines(content), note, line);
    if (!book || book->book.running() || !now.isValid())
        return content;
    return appendToLogbook(content, line, formatClockLine(now));
}

QString stopTaskClock(const QString &content, int line, const QDateTime &now)
{
    const ParsedNote note = parseNote(VaultPath(), content);
    QList<text::Line> lines = text::splitLines(content);
    const auto book = fileLogbook(lines, note, line);
    if (!book || !now.isValid())
        return content;
    const ClockEntry *running = book->book.running();
    if (!running)
        return content;
    const int fileLine = book->fileLines.value(running->line, -1);
    if (fileLine < 0)
        return content;
    QString &text = lines[fileLine].text;
    const qsizetype clock = text.indexOf(u"CLOCK:");
    const QDateTime stop = now < running->start ? running->start : now;
    text = text.first(clock) + formatClockLine(running->start, stop);
    return text::joinLines(lines);
}

QString logStatusChange(const QString &content, int line, TaskStatus from, TaskStatus to, const QDateTime &now)
{
    if (!now.isValid())
        return content;
    const ParsedNote note = parseNote(VaultPath(), content);
    const int b = blockAtLine(note, line);
    if (b < 0 || !note.blocks[b].task)
        return content;
    const QString marker = markerOf(note.blocks[b]);
    const bool keyword = statusFromKeyword(marker).has_value();
    return appendToLogbook(
            content, line,
            formatStatusChange(keywordFor(to, keyword ? QStringView(marker) : QStringView()), keywordFor(from), now));
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

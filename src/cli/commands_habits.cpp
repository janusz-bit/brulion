// Habits (docs/SYNTAX.md §5 "Habits"): habit list, habit set (+ tick, skip).
#include "commands_p.h"

#include "habits/habitcache.h"
#include "index/vaultindex.h"
#include "notebook/habitchecks.h"
#include "notebook/journals.h"
#include "vault/vault.h"

#include <QFileInfo>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// The index's habits (a cache per call: the command line runs one command).
struct Habits {
    VaultIndex *index = nullptr;
    habits::HabitCache cache;
};

bool loadHabits(Call &call, Habits &h)
{
    h.index = call.index();
    if (!h.index)
        return false;
    h.cache.sync(*h.index);
    return true;
}

QJsonObject habitJson(Call &call, const Habits &h, const habits::Habit &habit, QDate today, int days)
{
    const Qt::DayOfWeek weekStart = call.ctx.weekStart();
    const habits::DayMarks marks = h.cache.days(habit.key);
    QJsonObject o{{u"name"_s, habit.name},
                  {u"key"_s, habit.key},
                  {u"schedule"_s, habit.scheduleText},
                  {u"valid"_s, habit.isValid()},
                  {u"path"_s, habit.path},
                  {u"line"_s, habit.line},
                  {u"ref"_s, habit.blockIndex < 0 ? habit.path
                                                  : Call::lineRef(habit.path, noteText(call, habit.path), habit.line)}};
    if (habit.duplicate) {
        o.insert(u"problem"_s, u"duplicate"_s);
        return o;
    }
    if (!habit.schedule) {
        o.insert(u"problem"_s, u"schedule_not_understood"_s);
        return o;
    }
    o.insert(u"scheduleText"_s, habits::formatSchedule(*habit.schedule, call.ctx.locale()));
    const habits::Stats st = habits::statsOf(habit, marks, today, weekStart);
    o.insert(u"start"_s, st.start.toString(Qt::ISODate));
    o.insert(u"today"_s, QJsonObject{{u"date"_s, today.toString(Qt::ISODate)},
                                     {u"mark"_s, st.todayRecorded ? habits::markName(st.todayMark) : QString()},
                                     {u"due"_s, st.dueToday}});
    o.insert(u"streak"_s, QJsonObject{{u"current"_s, st.currentStreak},
                                      {u"longest"_s, st.longestStreak},
                                      {u"unit"_s, habits::streakUnitName(st.unit)}});
    o.insert(u"done"_s, st.done);
    o.insert(u"met"_s, st.met);
    o.insert(u"expected"_s, st.expected);
    o.insert(u"rate"_s, st.expected > 0 ? QJsonValue(std::round(st.rate() * 1000) / 1000) : QJsonValue());
    if (habit.schedule->kind == habits::Schedule::Kind::Times)
        o.insert(u"period"_s, QJsonObject{{u"done"_s, st.periodDone}, {u"target"_s, st.periodTarget}});
    if (st.lastDone.isValid())
        o.insert(u"lastDone"_s, st.lastDone.toString(Qt::ISODate));
    if (days > 0) {
        const QDate from = today.addDays(-(days - 1));
        const QList<habits::DayState> states = habits::daysOf(habit, marks, from, today, today, weekStart);
        QJsonArray arr;
        for (qsizetype i = 0; i < states.size(); ++i)
            arr.append(QJsonObject{{u"date"_s, from.addDays(i).toString(Qt::ISODate)},
                                   {u"state"_s, habits::dayStateName(states[i])}});
        o.insert(u"days"_s, arr);
    }
    return o;
}

/// One line of `--format text`: "✔ Medytacja — codziennie — seria 5 (najdłuższa 12), 80 %".
QString habitLine(const QJsonObject &o)
{
    if (o.contains(u"problem"_s))
        return u"? "_s + o.value(u"name"_s).toString() + u" — "_s
               + (o.value(u"problem"_s).toString() == u"duplicate"_s
                          ? tr("defined twice: %1").arg(o.value(u"ref"_s).toString())
                          : tr("schedule not understood: %1").arg(o.value(u"schedule"_s).toString()));
    const QJsonObject today = o.value(u"today"_s).toObject();
    const QString mark = today.value(u"mark"_s).toString();
    const QString box = mark == u"done"_s ? u"[x]"_s : mark == u"skipped"_s ? u"[-]"_s : u"[ ]"_s;
    const QJsonObject streak = o.value(u"streak"_s).toObject();
    QString line = box + u' ' + o.value(u"name"_s).toString() + u" — "_s + o.value(u"scheduleText"_s).toString()
                   + u" — "_s
                   + tr("streak %1 (longest %2)")
                             .arg(streak.value(u"current"_s).toInt())
                             .arg(streak.value(u"longest"_s).toInt());
    if (o.value(u"rate"_s).isDouble())
        line += u", "_s + QString::number(qRound(o.value(u"rate"_s).toDouble() * 100)) + u" %"_s;
    if (o.contains(u"period"_s)) {
        const QJsonObject p = o.value(u"period"_s).toObject();
        line += u", "_s + tr("%1/%2 this period").arg(p.value(u"done"_s).toInt()).arg(p.value(u"target"_s).toInt());
    }
    if (today.value(u"due"_s).toBool() && mark != u"done"_s)
        line += u" — "_s + tr("due today");
    return line;
}

Result habitList(Call &call)
{
    Habits h;
    if (!loadHabits(call, h))
        return call.failed();
    const auto date = call.date(call.str(u"date"_s));
    if (!date)
        return call.failed();
    const int days = std::clamp(call.integer(u"days"_s, 7), 0, 400);
    QJsonArray list;
    QString text;
    for (const habits::Habit &habit : h.cache.habits()) {
        const QJsonObject o = habitJson(call, h, habit, *date, days);
        list.append(o);
        text += habitLine(o) + u'\n';
    }
    QJsonArray undefined;
    for (const QString &key : h.cache.undefinedKeys())
        undefined.append(key);
    if (list.isEmpty())
        text = tr("No habits: give a block the property habit:: (e.g. \"- Medytacja\" and \"  habit:: codziennie\")")
               + u'\n';
    return call.ok(
            QJsonObject{{u"date"_s, date->toString(Qt::ISODate)}, {u"habits"_s, list}, {u"undefined"_s, undefined}},
            text);
}

Result habitSet(Call &call)
{
    const auto mark = habits::markFromName(call.str(u"state"_s));
    if (!mark)
        return call.fail(ExitUsage, u"bad_argument"_s,
                         tr("The state is done, skipped or open (also: zrobione, pominięte, brak)"),
                         {{u"state"_s, call.str(u"state"_s)}});
    Habits h;
    if (!loadHabits(call, h))
        return call.failed();
    const QString name = call.text(u"name"_s).trimmed();
    const habits::Habit *habit = h.cache.find(habits::habitName(name));
    if (!habit) {
        QJsonArray names;
        for (const habits::Habit &other : h.cache.habits()) {
            if (!other.duplicate)
                names.append(other.name);
        }
        return call.fail(ExitNotFound, u"habit_not_found"_s, tr("No habit named %1").arg(name),
                         {{u"name"_s, name}, {u"habits"_s, names}});
    }
    const auto date = call.date(call.str(u"date"_s));
    if (!date)
        return call.failed();
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    const VaultPath path = notebook::journalFileFor(*v, *date);
    const QString tag = habits::checkTag(call.ctx.locale());
    notebook::HabitCheckEdit result;
    // "Open" on a day without a note: nothing to change, and no note created for it.
    if (*mark == habits::Mark::Open && !QFileInfo(v->absolutePath(path)).isFile()) {
        return call.ok(QJsonObject{{u"path"_s, path},
                                   {u"changed"_s, false},
                                   {u"created"_s, false},
                                   {u"habit"_s, habit->name},
                                   {u"date"_s, date->toString(Qt::ISODate)},
                                   {u"mark"_s, habits::markName(*mark)},
                                   {u"before"_s, QString()},
                                   {u"line"_s, -1}},
                       QString());
    }
    const auto written = call.edit(
            path,
            [&](const QString &content, bool, QString *next) {
                result = notebook::setHabitCheck(content, path, v->config(), *date, habit->name, *mark,
                                                 call.ctx.today(), tag, habit->blockIndex < 0);
                if (result.refused) {
                    call.setError(ExitUsage, u"bad_name"_s, tr("The habit's name cannot be written as a check"));
                    return false;
                }
                *next = result.content;
                return true;
            },
            creatingJournal(call, path));
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"habit"_s, habit->name);
    o.insert(u"date"_s, date->toString(Qt::ISODate));
    o.insert(u"mark"_s, habits::markName(*mark));
    o.insert(u"before"_s, result.hadCheck ? habits::markName(result.before) : QString());
    o.insert(u"line"_s, result.line);
    if (result.line >= 0)
        o.insert(u"ref"_s, Call::lineRef(path, written->after, result.line));
    return call.ok(o, result.line >= 0 ? lineText(written->after, result.line) : QString());
}

Result habitAlias(Call &call, const QString &state)
{
    call.args.insert(u"state"_s, state);
    return habitSet(call);
}

} // namespace

void addHabitCommands(QList<Command> &out)
{
    const Arg name =
            stdinable(positional(u"name"_s, ArgType::Text,
                                 QT_TRANSLATE_NOOP("brulion::cli", "The habit's name (any letter case)"), true, true));
    const Arg date = option(u"date"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The day (default: today)"));
    Command c;
    c.id = u"habit.list"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "The habits: schedule, today's check, streaks, completion rate, recent days"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "A habit is a block with the property habit:: (its schedule: codziennie, pn, śr, pt, 3 razy w "
            "tygodniu, …); a day's check is a task tagged #nawyk or #habit in that day's daily note."));
    c.args = {date, option(u"days"_s, ArgType::Int,
                           QT_TRANSLATE_NOOP("brulion::cli", "The state of this many days up to it (default 7)"))};
    c.run = habitList;
    out.append(c);
    c = Command();
    c.id = u"habit.set"_s;
    c.summary = cliText(
            QT_TRANSLATE_NOOP("brulion::cli", "Record a habit's day in its daily note: done, skipped or open again"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Writes \"- [x] Medytacja #nawyk ✅ <today>\" (next to the day's other checks) or ticks the day's "
            "existing check; open sets the day's checks back to [ ]. The daily note is created from the journal "
            "template when missing."));
    c.args = {name,
              positional(u"state"_s, ArgType::String,
                         QT_TRANSLATE_NOOP("brulion::cli", "done, skipped, open — or zrobione, pominięte, brak")),
              date};
    // The name takes every word but the last: the state comes last.
    c.args[0].rest = false;
    c.run = habitSet;
    c.writes = true;
    out.append(c);
    const struct {
        const char *word;
        const char *state;
        const char *summary;
    } aliases[] = {
            {"tick", "done", QT_TRANSLATE_NOOP("brulion::cli", "Tick a habit today (habit set <name> done)")},
            {"skip", "skipped", QT_TRANSLATE_NOOP("brulion::cli", "Skip a habit's day (excused: no streak lost)")}};
    for (const auto &a : aliases) {
        c = Command();
        c.id = u"habit."_s + QString::fromLatin1(a.word);
        c.summary = cliText(a.summary);
        c.args = {name, date};
        const QString state = QString::fromLatin1(a.state);
        c.run = [state](Call &call) { return habitAlias(call, state); };
        c.writes = true;
        c.mcp = false; // brulion_habit_set covers them
        c.aliasOf = u"habit.set"_s;
        out.append(c);
    }
}

} // namespace brulion::cli

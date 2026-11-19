// Habits (docs/SYNTAX.md §5 "Habits"): habit list, habit set (+ tick, skip) — run by the built-in
// habits extension (src/core/extensions/builtin/nawyki.luau, its commands `list` and `set`) over a
// dry-run NotebookHost: what it would write goes through Call::edit like every other write of the
// command line (the lock, --dry-run, --diff, the hashes, a daily note made from its template).
#include "commands_p.h"

#include "extensions/blockedit.h"
#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "markdown/parse.h"
#include "notebook/journals.h"
#include "util/unicode.h"
#include "vault/vault.h"

#include <QFileInfo>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

namespace ext = brulion::extensions;

const QString kHabits = u"nawyki"_s;

/// The habits extension on an engine of its own over the notebook, its writes only computed.
struct Habits {
    std::unique_ptr<ext::NotebookHost> host;
    ext::Engine engine;
};

bool startHabits(Call &call, Habits &h)
{
    Vault *v = call.vault();
    VaultIndex *index = v ? call.index() : nullptr;
    if (!index)
        return false;
    ext::NotebookHost::Options options;
    const QDateTime now = call.ctx.now();
    options.now = [now] { return now; };
    options.language = call.ctx.language();
    options.uiLanguage = call.ctx.language();
    options.dryRun = true;
    h.host = std::make_unique<ext::NotebookHost>(*v, *index, options);
    QString error;
    if (!h.engine.start(h.host.get(), ext::Limits{}, &error)) {
        call.setError(ExitInternal, u"engine_unavailable"_s, tr("The Luau engine did not start: %1").arg(error));
        return false;
    }
    const ext::Result loaded = h.engine.load(ext::builtinExtension(kHabits));
    if (!loaded.ok()) {
        call.setError(ExitInternal, u"extension_error"_s, tr("The habits extension does not load: %1").arg(loaded.error));
        return false;
    }
    return true;
}

/// The extension's command `name` with `args` (its ctx.args): its result, or nullopt + call.error.
std::optional<QVariantMap> runHabits(Call &call, Habits &h, const QString &name, const QVariantMap &args)
{
    const ext::Result r =
            h.engine.runCommand(kHabits, name, {{u"page"_s, QString()}, {u"selection"_s, QString()}, {u"args"_s, args}});
    if (!r.ok()) {
        call.setError(ExitInternal, u"extension_error"_s,
                      tr("The habits extension failed: %1").arg(r.error.section(u'\n', 0, 0)));
        return std::nullopt;
    }
    return r.value.toMap();
}

/// A list from the extension (an empty one crosses as an empty record).
QJsonArray arrayOf(const QVariant &value)
{
    return QJsonValue::fromVariant(value.metaType().id() == QMetaType::QVariantList ? value : QVariantList()).toArray();
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
    if (!startHabits(call, h))
        return call.failed();
    const auto date = call.date(call.str(u"date"_s));
    if (!date)
        return call.failed();
    const int days = std::clamp(call.integer(u"days"_s, 7), 0, 400);
    const auto r = runHabits(call, h, u"list"_s, {{u"date"_s, date->toString(Qt::ISODate)}, {u"days"_s, days}});
    if (!r)
        return call.failed();
    QJsonArray list;
    QString text;
    for (const QJsonValue &value : arrayOf(r->value(u"habits"_s))) {
        QJsonObject o = value.toObject();
        if (o.value(u"valid"_s).toBool() && !o.contains(u"rate"_s))
            o.insert(u"rate"_s, QJsonValue()); // nothing expected yet
        list.append(o);
        text += habitLine(o) + u'\n';
    }
    if (list.isEmpty())
        text = tr("No habits: give a block the property habit:: (e.g. \"- Medytacja\" and \"  habit:: codziennie\")")
               + u'\n';
    return call.ok(QJsonObject{{u"date"_s, date->toString(Qt::ISODate)},
                               {u"habits"_s, list},
                               {u"undefined"_s, arrayOf(r->value(u"undefined"_s))}},
                   text);
}

/// The 0-based line of the check of habit `key` in the daily note `content` (-1: none) — of the
/// first with `mark` (done, skipped), else the first one ("open"); a check is a task tagged
/// #nawyk / #habit that defines no habit (docs/SYNTAX.md §5 "Habits").
int checkLine(const QString &content, const VaultPath &path, const VaultConfig &config, const QString &key,
              const QString &mark)
{
    const ParsedNote note = markdown::parseNote(path, content, config);
    int first = -1;
    for (const Block &b : note.blocks) {
        if (!b.task || !(b.tags.contains(u"nawyk"_s) || b.tags.contains(u"habit"_s))
            || b.properties.contains(u"habit"_s) || b.properties.contains(u"nawyk"_s))
            continue;
        if (unicode::nfc(ext::plainWords(b.task->text)).toCaseFolded().simplified() != key)
            continue;
        const QString m = b.task->status == TaskStatus::Done        ? u"done"_s
                          : b.task->status == TaskStatus::Cancelled ? u"skipped"_s
                                                                    : u"open"_s;
        if (first < 0)
            first = b.line;
        if (mark != u"open"_s && m == mark)
            return b.line;
    }
    return first;
}

Result habitSet(Call &call)
{
    Habits h;
    if (!startHabits(call, h))
        return call.failed();
    const QString name = call.text(u"name"_s).trimmed();
    // The day is told about after the name (a name no habit has comes first, as it always did).
    const auto date = call.date(call.str(u"date"_s));
    const std::optional<Error> dateError = call.error;
    call.error.reset();
    const auto r = runHabits(call, h,
                             u"set"_s,
                             {{u"name"_s, name},
                              {u"state"_s, call.str(u"state"_s)},
                              {u"date"_s, date ? date->toString(Qt::ISODate) : QString()}});
    if (!r)
        return call.failed();
    const QString error = r->value(u"error"_s).toString();
    if (error == u"bad_argument"_s)
        return call.fail(ExitUsage, u"bad_argument"_s,
                         tr("The state is done, skipped or open (also: zrobione, pominięte, brak)"),
                         {{u"state"_s, call.str(u"state"_s)}});
    if (error == u"habit_not_found"_s)
        return call.fail(ExitNotFound, u"habit_not_found"_s, tr("No habit named %1").arg(name),
                         {{u"name"_s, name}, {u"habits"_s, arrayOf(r->value(u"habits"_s))}});
    if (!date) {
        call.error = dateError;
        return call.failed();
    }
    if (error == u"bad_name"_s)
        return call.fail(ExitUsage, u"bad_name"_s, tr("The habit's name cannot be written as a check"));
    if (!error.isEmpty())
        return call.fail(ExitInternal, u"extension_error"_s, r->value(u"message"_s).toString());
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    const QList<ext::NotebookHost::DryWrite> writes = h.host->dryWrites();
    if (writes.size() > 1)
        return call.fail(ExitInternal, u"extension_error"_s, tr("The habits extension wrote more than one note"));
    const VaultPath path = writes.isEmpty() ? notebook::journalFileFor(*v, *date) : writes.first().path;
    const QString mark = r->value(u"mark"_s).toString();
    const QString key = r->value(u"key"_s).toString();
    // `o` with the habit, the day, its mark before and after and the check's line.
    const auto answer = [&](QJsonObject o, int line) {
        o.insert(u"habit"_s, r->value(u"habit"_s).toString());
        o.insert(u"date"_s, date->toString(Qt::ISODate));
        o.insert(u"mark"_s, mark);
        o.insert(u"before"_s, r->value(u"before"_s).toString());
        o.insert(u"line"_s, line);
        return o;
    };
    // Nothing to write on a day without a note ("open"): no note created for it.
    if (writes.isEmpty() && !QFileInfo(v->absolutePath(path)).isFile())
        return call.ok(answer({{u"path"_s, path}, {u"changed"_s, false}, {u"created"_s, false}}, -1));
    const auto written = call.edit(
            path,
            [&](const QString &content, bool, QString *next) {
                if (writes.isEmpty()) {
                    *next = content;
                    return true;
                }
                const ext::NotebookHost::DryWrite &w = writes.first();
                if (w.existed && content != w.before) {
                    call.setError(ExitConflict, u"concurrent_change"_s,
                                  tr("%1 kept changing while it was written (another program writes it): try again")
                                          .arg(path),
                                  {{u"path"_s, path}});
                    return false;
                }
                *next = w.after;
                return true;
            },
            creatingJournal(call, path));
    if (!written)
        return call.failed();
    const int line = checkLine(written->after, path, v->config(), key, mark);
    QJsonObject o = answer(call.writeJson(*written), line);
    if (line >= 0)
        o.insert(u"ref"_s, Call::lineRef(path, written->after, line));
    return call.ok(o, line >= 0 ? lineText(written->after, line) : QString());
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

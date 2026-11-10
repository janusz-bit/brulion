// Daily notes and quick capture: journal read/path/append, capture; weekly and monthly notes:
// week read/path/append, month read/path/append.
#include "commands_p.h"

#include "dates/dates.h"
#include "markdown/parse.h"
#include "notebook/capture.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "notebook/periodicnotes.h"
#include "vault/vault.h"

#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// The daily note of a date phrase: {date, path, exists}.
struct Day {
    QDate date;
    VaultPath path;
    bool exists = false;
};

std::optional<Day> day(Call &call, const QString &phrase)
{
    const auto date = call.date(phrase);
    if (!date)
        return std::nullopt;
    Vault *v = call.vault();
    if (!v)
        return std::nullopt;
    Day d;
    d.date = *date;
    d.path = notebook::journalFileFor(*v, *date);
    d.exists = QFileInfo(v->absolutePath(d.path)).isFile();
    return d;
}

QJsonObject dayJson(const Day &d)
{
    return {{u"date"_s, d.date.toString(Qt::ISODate)}, {u"path"_s, d.path}, {u"exists"_s, d.exists}};
}

/// `o` plus the {hash, content} of the note at `path` ("" for one not created yet): its text.
QString insertContent(Call &call, QJsonObject &o, const VaultPath &path, bool exists)
{
    const notebook::Notebook::Current current = exists ? call.nb()->read(path) : notebook::Notebook::Current();
    o.insert(u"hash"_s, current.hash);
    o.insert(u"content"_s, current.text);
    return current.text;
}

/// Append `block` to the note at `path` (a missing daily, weekly or monthly note is created from
/// its template): the write JSON plus {line, ref}; a dry run shows the template too.
std::optional<QJsonObject> appendTo(Call &call, const VaultPath &path, const QString &block)
{
    int line = -1;
    const auto written = call.edit(
            path,
            [&](const QString &current, bool, QString *next) {
                *next = notebook::appendBlock(current, block);
                line = appendedLine(*next, block);
                return true;
            },
            creatingJournal(call, path));
    if (!written)
        return std::nullopt;
    QJsonObject o = call.writeJson(*written);
    o.insert(u"line"_s, line);
    o.insert(u"ref"_s, Call::lineRef(path, written->after, line));
    return o;
}

/// appendTo() the daily note of `date`, plus {date}.
std::optional<QJsonObject> appendToJournal(Call &call, QDate date, const QString &block)
{
    Vault *v = call.vault();
    if (!v)
        return std::nullopt;
    auto o = appendTo(call, notebook::journalFileFor(*v, date), block);
    if (o)
        o->insert(u"date"_s, date.toString(Qt::ISODate));
    return o;
}

Result journalRead(Call &call)
{
    const auto d = day(call, call.str(u"date"_s));
    if (!d)
        return call.failed();
    QJsonObject o = dayJson(*d);
    const QString text = insertContent(call, o, d->path, d->exists);
    if (d->exists && call.flag(u"blocks"_s)) {
        QJsonArray blocks;
        const ParsedNote note = markdown::parseNote(d->path, text, call.vault()->config());
        for (const Block &b : note.blocks) {
            QJsonObject bo = notebook::blockJson(b);
            bo.insert(u"ref"_s, Call::lineRef(d->path, text, b.line));
            blocks.append(bo);
        }
        o.insert(u"blocks"_s, blocks);
    }
    return call.ok(o, text);
}

Result journalPath(Call &call)
{
    const auto d = day(call, call.str(u"date"_s));
    if (!d)
        return call.failed();
    return call.ok(dayJson(*d), d->path);
}

Result journalAppend(Call &call)
{
    const QString block = notebook::blockFromText(call.text(u"text"_s));
    if (block.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("Nothing to append: give the text (or --stdin)"));
    const auto date = call.date(call.str(u"date"_s));
    if (!date)
        return call.failed();
    const auto o = appendToJournal(call, *date, block);
    return o ? call.ok(*o) : call.failed();
}

Result capture(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return call.failed();
    const QString input = call.text(u"text"_s);
    const QDateTime now = call.ctx.now();
    const notebook::CaptureLine line =
            notebook::buildCaptureLine(input, now, v->config().weekStartsOn, call.ctx.locale());
    if (line.kind == notebook::CaptureLine::Kind::Empty)
        return call.fail(ExitUsage, u"text_required"_s, tr("Nothing to capture: give the text (or --stdin)"));
    auto o = appendToJournal(call, now.date(), line.block);
    if (!o)
        return call.failed();
    const QString written = line.block.section(u'\n', 0, 0);
    o->insert(u"kind"_s, notebook::captureKindName(line.kind));
    o->insert(u"text"_s, line.text);
    o->insert(u"written"_s, written);
    if (line.when.isValid()) {
        o->insert(u"when"_s, QJsonObject{{u"date"_s, line.when.date.toString(Qt::ISODate)},
                                         {u"time"_s, line.when.time ? line.when.time->toString(u"HH:mm"_s) : QString()},
                                         {u"human"_s, dates::formatHuman(line.when, call.ctx.locale(), now.date())}});
    }
    return call.ok(*o, written + u"  →  "_s + o->value(u"path"_s).toString());
}

// ---- Weekly and monthly notes ------------------------------------------------------------------

/// The note of a period phrase ("ten tydzień", "2026-W45", "listopad", "next month"; "" = the
/// current one): {period, start, end, path, exists}.
struct PeriodNote {
    periodic::Period period = periodic::Period::Week;
    QDate start;
    VaultPath path;
    bool exists = false;
};

std::optional<PeriodNote> periodNote(Call &call, periodic::Period period, const QString &phrase)
{
    Vault *v = call.vault();
    if (!v)
        return std::nullopt;
    const VaultConfig config = v->config();
    const auto start = periodic::parsePeriodPhrase(period, phrase, call.ctx.now(), call.ctx.weekStart(),
                                                   call.ctx.locale(), &config);
    if (!start) {
        const bool week = period == periodic::Period::Week;
        const QString given = phrase.trimmed();
        call.setError(ExitParse, week ? u"week_not_understood"_s : u"month_not_understood"_s,
                      (week ? tr("Not a week: %1") : tr("Not a month: %1")).arg(given),
                      {{u"phrase"_s, given},
                       {u"examples"_s, week ? QJsonArray{u"ten tydzień"_s, u"next week"_s, u"2026-W45"_s,
                                                         u"tydzień 45"_s, u"2026-11-03"_s}
                                            : QJsonArray{u"ten miesiąc"_s, u"last month"_s, u"2026-11"_s,
                                                         u"listopad 2026"_s, u"2026-11-03"_s}}});
        return std::nullopt;
    }
    PeriodNote n;
    n.period = period;
    n.start = *start;
    n.path = notebook::periodicFileFor(*v, period, *start);
    n.exists = QFileInfo(v->absolutePath(n.path)).isFile();
    return n;
}

QJsonObject periodJson(const PeriodNote &n)
{
    QJsonObject o{{u"period"_s, periodic::periodName(n.period)},
                  {u"start"_s, n.start.toString(Qt::ISODate)},
                  {u"end"_s, periodic::periodEnd(n.period, n.start).toString(Qt::ISODate)},
                  {u"path"_s, n.path},
                  {u"exists"_s, n.exists}};
    if (n.period == periodic::Period::Week) {
        const periodic::IsoWeek w = periodic::isoWeek(n.start);
        o.insert(u"week"_s, w.week);
        o.insert(u"weekYear"_s, w.year);
    }
    return o;
}

Result periodRead(Call &call, periodic::Period period)
{
    const auto n = periodNote(call, period, call.str(u"when"_s));
    if (!n)
        return call.failed();
    QJsonObject o = periodJson(*n);
    const QString text = insertContent(call, o, n->path, n->exists);
    return call.ok(o, text);
}

Result periodPath(Call &call, periodic::Period period)
{
    const auto n = periodNote(call, period, call.str(u"when"_s));
    if (!n)
        return call.failed();
    return call.ok(periodJson(*n), n->path);
}

Result periodAppend(Call &call, periodic::Period period)
{
    const QString block = notebook::blockFromText(call.text(u"text"_s));
    if (block.isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("Nothing to append: give the text (or --stdin)"));
    const auto n = periodNote(call, period, call.str(u"when"_s));
    if (!n)
        return call.failed();
    auto o = appendTo(call, n->path, block);
    if (!o)
        return call.failed();
    const QJsonObject info = periodJson(*n);
    for (const QString &key : {u"period"_s, u"start"_s, u"end"_s, u"week"_s, u"weekYear"_s}) {
        if (info.contains(key))
            o->insert(key, info.value(key));
    }
    return call.ok(*o);
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.run = std::move(run);
    c.args = std::move(args);
    return c;
}

} // namespace

void addJournalCommands(QList<Command> &out)
{
    const Arg date =
            positional(u"date"_s, ArgType::Date,
                       QT_TRANSLATE_NOOP("brulion::cli",
                                         "The day: today (default), wczoraj, jutro, w piątek, 2026-10-15, last monday"),
                       false, true);
    const Arg text = stdinable(
            positional(u"text"_s, ArgType::Text, QT_TRANSLATE_NOOP("brulion::cli", "The block's text"), true, true));
    Command c = make(u"journal.read"_s,
                     QT_TRANSLATE_NOOP("brulion::cli", "Read a daily note (`journal` alone: today's)"), journalRead,
                     {date, flag(u"blocks"_s, QT_TRANSLATE_NOOP("brulion::cli", "Also every block with its ref"))});
    out.append(c);
    c = make(u"journal.path"_s,
             QT_TRANSLATE_NOOP(
                     "brulion::cli",
                     "The path of a daily note (its existing file under any accepted name, else the configured one)"),
             journalPath, {date});
    out.append(c);
    c = make(u"journal.append"_s,
             QT_TRANSLATE_NOOP("brulion::cli", "Append a block to a daily note (created from the journal template)"),
             journalAppend,
             {text, option(u"date"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The day (default: today)"))});
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "A template's empty last bullet is filled; the note's line endings and BOM stay."));
    c.writes = true;
    out.append(c);
    c = make(u"capture"_s,
             QT_TRANSLATE_NOOP(
                     "brulion::cli",
                     "Quick capture into today's journal, as the app's: a date in it makes a task with ⏰ or 📅"),
             capture,
             {stdinable(positional(u"text"_s, ArgType::Text,
                                   QT_TRANSLATE_NOOP("brulion::cli", "What to capture (Polish or English)"), true,
                                   true))});
    c.help = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                       "\"Kupić mleko jutro o 18\" becomes \"- [ ] Kupić mleko ⏰ 2026-10-15 "
                                       "18:00\"; without a time a due date 📅; dates in the past stay text."));
    c.writes = true;
    out.append(c);

    // Weekly and monthly notes (docs/SYNTAX.md §1 "Weekly and monthly notes").
    struct PeriodTexts {
        periodic::Period period;
        const char *word;
        const char *when;
        const char *read;
        const char *path;
        const char *append;
        const char *appendHelp;
        const char *whenOption;
    };
    const PeriodTexts periods[] = {
            {periodic::Period::Week, "week",
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The week: this week (default), next week, zeszły tydzień, 2026-W45, tydzień 45, a day"),
             QT_TRANSLATE_NOOP("brulion::cli", "Read a weekly note, of an ISO week (`week` alone: this week's)"),
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The path of a weekly note (its existing file, else where it will be created)"),
             QT_TRANSLATE_NOOP("brulion::cli", "Append a block to a weekly note (created from its template)"),
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The template is weeklyTemplate: {{date}} is the Monday, {{end}} the Sunday. A "
                               "template's empty last bullet is filled; the note's line endings and BOM stay."),
             QT_TRANSLATE_NOOP("brulion::cli", "The week (default: this week)")},
            {periodic::Period::Month, "month",
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The month: this month (default), last month, listopad, 2026-11, November 2026, a day"),
             QT_TRANSLATE_NOOP("brulion::cli", "Read a monthly note (`month` alone: this month's)"),
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The path of a monthly note (its existing file, else where it will be created)"),
             QT_TRANSLATE_NOOP("brulion::cli", "Append a block to a monthly note (created from its template)"),
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The template is monthlyTemplate: {{date}} is the 1st, {{end}} the last day. A "
                               "template's empty last bullet is filled; the note's line endings and BOM stay."),
             QT_TRANSLATE_NOOP("brulion::cli", "The month (default: this month)")},
    };
    for (const PeriodTexts &p : periods) {
        const QString word = QString::fromLatin1(p.word);
        const periodic::Period period = p.period;
        const Arg when = positional(u"when"_s, ArgType::String, p.when, false, true);
        c = make(word + u".read"_s, p.read, [period](Call &call) { return periodRead(call, period); }, {when});
        out.append(c);
        c = make(word + u".path"_s, p.path, [period](Call &call) { return periodPath(call, period); }, {when});
        out.append(c);
        c = make(word + u".append"_s, p.append, [period](Call &call) { return periodAppend(call, period); },
                 {text, option(u"when"_s, ArgType::String, p.whenOption)});
        c.help = cliText(p.appendHelp);
        c.writes = true;
        out.append(c);
    }
}

} // namespace brulion::cli

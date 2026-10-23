// Daily notes and quick capture: journal read/path/append, capture.
#include "commands_p.h"

#include "dates/dates.h"
#include "markdown/parse.h"
#include "notebook/capture.h"
#include "notebook/journals.h"
#include "notebook/json.h"
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

/// Append `block` to the daily note of `date` (created from the journal template), with the
/// write JSON plus {date, line, ref}; a dry run shows the template too.
std::optional<QJsonObject> appendToJournal(Call &call, QDate date, const QString &block)
{
    Vault *v = call.vault();
    if (!v)
        return std::nullopt;
    const VaultPath path = notebook::journalFileFor(*v, date);
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
    o.insert(u"date"_s, date.toString(Qt::ISODate));
    o.insert(u"line"_s, line);
    o.insert(u"ref"_s, Call::lineRef(path, written->after, line));
    return o;
}

Result journalRead(Call &call)
{
    const auto d = day(call, call.str(u"date"_s));
    if (!d)
        return call.failed();
    QJsonObject o{{u"date"_s, d->date.toString(Qt::ISODate)}, {u"path"_s, d->path}, {u"exists"_s, d->exists}};
    if (!d->exists) {
        o.insert(u"content"_s, QString());
        o.insert(u"hash"_s, QString());
        return call.ok(o, QString());
    }
    const notebook::Notebook::Current current = call.nb()->read(d->path);
    o.insert(u"hash"_s, current.hash);
    o.insert(u"content"_s, current.text);
    if (call.flag(u"blocks"_s)) {
        QJsonArray blocks;
        for (const Block &b : markdown::parseNote(d->path, current.text, call.vault()->config()).blocks) {
            QJsonObject bo = notebook::blockJson(b);
            bo.insert(u"ref"_s, Call::lineRef(d->path, current.text, b.line));
            blocks.append(bo);
        }
        o.insert(u"blocks"_s, blocks);
    }
    return call.ok(o, current.text);
}

Result journalPath(Call &call)
{
    const auto d = day(call, call.str(u"date"_s));
    if (!d)
        return call.failed();
    return call.ok(
            QJsonObject{{u"date"_s, d->date.toString(Qt::ISODate)}, {u"path"_s, d->path}, {u"exists"_s, d->exists}},
            d->path);
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
    o->insert(u"kind"_s, notebook::captureKindName(line.kind));
    o->insert(u"text"_s, line.text);
    o->insert(u"written"_s, line.block.section(u'\n', 0, 0));
    if (line.when.isValid()) {
        o->insert(u"when"_s, QJsonObject{{u"date"_s, line.when.date.toString(Qt::ISODate)},
                                         {u"time"_s, line.when.time ? line.when.time->toString(u"HH:mm"_s) : QString()},
                                         {u"human"_s, dates::formatHuman(line.when, call.ctx.locale(), now.date())}});
    }
    return call.ok(*o, line.block.section(u'\n', 0, 0) + u"  →  "_s + o->value(u"path"_s).toString());
}

} // namespace

void addJournalCommands(QList<Command> &out)
{
    const Arg date =
            positional(u"date"_s, ArgType::Date,
                       QT_TRANSLATE_NOOP("brulion::cli",
                                         "The day: today (default), wczoraj, jutro, w piątek, 2026-10-15, last monday"),
                       false, true);
    Command c;
    c.id = u"journal.read"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP("brulion::cli", "Read a daily note (`journal` alone: today's)"));
    c.args = {date, flag(u"blocks"_s, QT_TRANSLATE_NOOP("brulion::cli", "Also every block with its ref"))};
    c.run = journalRead;
    out.append(c);
    c = Command();
    c.id = u"journal.path"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "The path of a daily note (its existing file under any accepted name, else the configured one)"));
    c.args = {date};
    c.run = journalPath;
    out.append(c);
    c = Command();
    c.id = u"journal.append"_s;
    c.summary = cliText(
            QT_TRANSLATE_NOOP("brulion::cli", "Append a block to a daily note (created from the journal template)"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "A template's empty last bullet is filled; the note's line endings and BOM stay."));
    c.args = {stdinable(positional(u"text"_s, ArgType::Text, QT_TRANSLATE_NOOP("brulion::cli", "The block's text"),
                                   true, true)),
              option(u"date"_s, ArgType::Date, QT_TRANSLATE_NOOP("brulion::cli", "The day (default: today)"))};
    c.run = journalAppend;
    c.writes = true;
    out.append(c);
    c = Command();
    c.id = u"capture"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Quick capture into today's journal, as the app's: a date in it makes a task with ⏰ or 📅"));
    c.help = cliText(QT_TRANSLATE_NOOP("brulion::cli",
                                       "\"Kupić mleko jutro o 18\" becomes \"- [ ] Kupić mleko ⏰ 2026-10-15 "
                                       "18:00\"; without a time a due date 📅; dates in the past stay text."));
    c.args = {stdinable(positional(u"text"_s, ArgType::Text,
                                   QT_TRANSLATE_NOOP("brulion::cli", "What to capture (Polish or English)"), true,
                                   true))};
    c.run = capture;
    c.writes = true;
    out.append(c);
}

} // namespace brulion::cli

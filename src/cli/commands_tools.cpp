// Tools: date parse, calc, calc page, block link, history list/show/restore.
#include "commands_p.h"

#include "calc/calc.h"
#include "dates/dates.h"
#include "index/vaultindex.h"
#include "notebook/links.h"
#include "vault/history.h"
#include "vault/vault.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

calc::CalcLocale calcLocale(const Context &ctx)
{
    return ctx.language() == u"en" ? calc::CalcLocale::English : calc::CalcLocale::Polish;
}

QJsonObject calcLineJson(const calc::CalcLineResult &r, int lineOffset)
{
    QJsonObject o{{u"line"_s, r.line + lineOffset}, {u"input"_s, r.input}, {u"value"_s, r.value}};
    if (r.hasError())
        o.insert(u"error"_s, r.error);
    if (!r.assigned.isEmpty())
        o.insert(u"assigned"_s, r.assigned);
    if (!r.label.isEmpty())
        o.insert(u"label"_s, r.label);
    if (r.numeric)
        o.insert(u"numeric"_s, *r.numeric);
    return o;
}

Result dateParse(Call &call)
{
    const QString phrase = call.str(u"phrase"_s);
    const QDateTime now = call.ctx.now();
    const auto found = dates::parseNaturalDate(phrase, now, call.ctx.weekStart(), call.ctx.locale());
    if (!found)
        return call.fail(ExitParse, u"date_not_understood"_s, tr("No date in: %1").arg(phrase),
                         {{u"found"_s, false}, {u"phrase"_s, phrase}});
    const DateSpec &spec = found->spec;
    QJsonObject o{{u"found"_s, true},
                  {u"date"_s, spec.date.toString(Qt::ISODate)},
                  {u"time"_s, spec.time ? spec.time->toString(u"HH:mm"_s) : QString()},
                  {u"spec"_s, spec.toString()},
                  {u"iso"_s, spec.time ? spec.date.toString(Qt::ISODate) + u'T' + spec.time->toString(u"HH:mm"_s)
                                       : spec.date.toString(Qt::ISODate)},
                  {u"human"_s, dates::formatHuman(spec, call.ctx.locale(), now.date())},
                  {u"past"_s, spec.time ? spec.toLocalDateTime() < now : spec.date < now.date()},
                  {u"matched"_s, QJsonObject{{u"start"_s, int(found->start)},
                                             {u"length"_s, int(found->length)},
                                             {u"text"_s, phrase.mid(found->start, found->length)}}},
                  {u"rest"_s, dates::stripNaturalDate(phrase, *found).trimmed()}};
    return call.ok(o, spec.toString());
}

Result calcEval(Call &call)
{
    const QString source = call.text(u"expression"_s);
    if (source.trimmed().isEmpty())
        return call.fail(ExitUsage, u"text_required"_s, tr("What to calculate?"));
    calc::Scope scope;
    QJsonArray lines;
    QString text;
    bool errors = false;
    for (const calc::CalcLineResult &r : calc::evaluateCalc(source, scope, calcLocale(call.ctx))) {
        if (r.input.trimmed().isEmpty())
            continue;
        lines.append(calcLineJson(r, 0));
        errors = errors || r.hasError();
        text += r.input + u"  =  "_s + (r.hasError() ? u"error: "_s + r.error : r.value) + u'\n';
    }
    const QJsonObject o{{u"lines"_s, lines},
                        {u"value"_s, lines.isEmpty() ? QJsonValue() : lines.last().toObject().value(u"value"_s)}};
    if (errors)
        return call.fail(ExitParse, u"calc_error"_s, tr("The calculation has errors"), o);
    return call.ok(o, text);
}

Result calcPage(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    const QString content = call.nb()->read(ref->path).text;
    const calc::CalcLocale locale = calcLocale(call.ctx);
    QJsonArray blocks;
    QString text;
    for (const calc::CalcBlock &b : calc::findCalcBlocks(content)) {
        calc::Scope scope = calc::pageScope(content, b.startLine, locale); // what is defined above it
        QJsonArray lines;
        for (const calc::CalcLineResult &r : calc::evaluateCalc(b.source, scope, locale)) {
            if (r.input.trimmed().isEmpty())
                continue;
            lines.append(calcLineJson(r, b.contentStartLine));
            text += r.input + u"  =  "_s + (r.hasError() ? u"error: "_s + r.error : r.value) + u'\n';
        }
        blocks.append(QJsonObject{{u"startLine"_s, b.startLine}, {u"endLine"_s, b.endLine}, {u"lines"_s, lines}});
    }
    // Inline calculations: `= expression` code spans.
    static const QRegularExpression inlineCalc(u"`=([^`\\n]+)`"_s);
    QJsonArray inlines;
    const QStringList pageLines = QString(content).replace(u"\r\n"_s, u"\n"_s).split(u'\n');
    for (int i = 0; i < pageLines.size(); ++i) {
        for (const auto &m : inlineCalc.globalMatch(pageLines.at(i))) {
            const calc::CalcLineResult r =
                    calc::evaluateInline(m.captured(1), calc::pageScope(content, i, locale), locale);
            QJsonObject o{{u"line"_s, i}, {u"expression"_s, m.captured(1).trimmed()}, {u"value"_s, r.value}};
            if (r.hasError())
                o.insert(u"error"_s, r.error);
            inlines.append(o);
        }
    }
    return call.ok(QJsonObject{{u"path"_s, ref->path}, {u"blocks"_s, blocks}, {u"inline"_s, inlines}}, text);
}

Result blockLink(Call &call)
{
    const auto ref = call.page(call.str(u"ref"_s), true);
    if (!ref)
        return call.failed();
    if (!ref->item)
        return call.fail(ExitUsage, u"item_ref_required"_s,
                         tr("Point at a block: Dom.md#L12@hash (from page read --blocks) or ^id"));
    VaultIndex *index = call.index();
    if (!index)
        return call.failed();
    const VaultConfig config = call.vault()->config();
    const QString pageTarget = notebook::pageLinkTarget(
            ref->path, config, [&](const QString &title) { return index->resolveLink(title).value_or(QString()); });
    notebook::ItemRef item = *ref->item;
    item.page = ref->path;
    std::optional<notebook::BlockLinks> links;
    const auto written = call.edit(ref->path, [&](const QString &content, bool, QString *next) {
        const notebook::ResolvedLine line = notebook::resolve(content, item, config);
        if (line.status == notebook::ResolvedLine::Stale) {
            call.setError(ExitConflict, u"stale_ref"_s,
                          tr("The line changed since the ref was taken: read the page again"),
                          {{u"ref"_s, notebook::formatRef(item)}});
            return false;
        }
        if (line.status == notebook::ResolvedLine::NotFound) {
            call.setError(ExitNotFound, u"line_not_found"_s, tr("No such line or block"),
                          {{u"ref"_s, notebook::formatRef(item)}});
            return false;
        }
        // Any block of the notebook with that id counts: a new id avoids them all.
        links = notebook::blockLinks(content, line.line, pageTarget,
                                     [&](const QString &id) { return index->blockById(id).has_value(); });
        if (!links) {
            call.setError(ExitConflict, u"no_block"_s, tr("That line is no block that can carry an id"),
                          {{u"ref"_s, notebook::formatRef(item)}});
            return false;
        }
        *next = links->content;
        return true;
    });
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"id"_s, links->id);
    o.insert(u"added"_s, links->added);
    o.insert(u"line"_s, links->line);
    o.insert(u"ref"_s, links->ref);
    o.insert(u"wikiLink"_s, links->wikiLink);
    o.insert(u"embed"_s, links->embed);
    if (!links->headingLink.isEmpty())
        o.insert(u"headingLink"_s, links->headingLink);
    return call.ok(o, links->ref + u'\n' + links->wikiLink + u'\n');
}

History *history(Call &call)
{
    Vault *v = call.vault();
    if (!v)
        return nullptr;
    History *h = v->history();
    if (!h)
        call.setError(ExitUnavailable, u"no_history"_s,
                      tr("No version history here (--no-history, or no device state folder)"));
    return h;
}

/// The page whose versions are meant: a path ending in ".md" as it is (also a deleted page), else
/// a page ref.
std::optional<VaultPath> historyTarget(Call &call, const QString &raw)
{
    const QString t = raw.trimmed();
    if (t.endsWith(u".md"_s, Qt::CaseInsensitive) && !notebook::looksLikeItemRef(t)) {
        const auto p = call.vaultPath(t);
        if (!p || !call.vault())
            return std::nullopt;
        return call.vault()->existingCase(*p);
    }
    const auto ref = call.page(t, false);
    return ref ? std::optional<VaultPath>(ref->path) : std::nullopt;
}

Result historyList(Call &call)
{
    History *h = history(call);
    if (!h)
        return call.failed();
    const auto target = historyTarget(call, call.str(u"ref"_s));
    if (!target)
        return call.failed();
    QJsonArray out;
    QString text;
    for (const HistoryEntry &e : h->list(*target)) {
        const QString time = e.time.toLocalTime().toString(u"yyyy-MM-dd HH:mm:ss"_s);
        out.append(QJsonObject{{u"id"_s, e.id}, {u"time"_s, time}, {u"size"_s, e.size}, {u"reason"_s, e.reason}});
        text += e.id + u"  "_s + time + u"  "_s + e.reason + u'\n';
    }
    return call.ok(QJsonObject{{u"path"_s, *target}, {u"versions"_s, out}}, text);
}

Result historyShow(Call &call)
{
    History *h = history(call);
    if (!h)
        return call.failed();
    bool ok = false;
    const QString id = call.str(u"id"_s).trimmed();
    const QByteArray bytes = h->readBytes(id, &ok);
    if (!ok)
        return call.fail(ExitNotFound, u"version_not_found"_s, tr("No version %1").arg(id), {{u"id"_s, id}});
    const QString content = h->read(id);
    return call.ok(QJsonObject{{u"id"_s, id},
                               {u"size"_s, bytes.size()},
                               {u"hash"_s, notebook::contentHash(bytes)},
                               {u"content"_s, content}},
                   content);
}

Result historyRestore(Call &call)
{
    History *h = history(call);
    if (!h)
        return call.failed();
    const auto target = historyTarget(call, call.str(u"ref"_s));
    if (!target)
        return call.failed();
    bool ok = false;
    const QString id = call.str(u"id"_s).trimmed();
    const QString text = h->read(id, &ok);
    if (!ok)
        return call.fail(ExitNotFound, u"version_not_found"_s, tr("No version %1").arg(id), {{u"id"_s, id}});
    // The version must belong to the page (ids are unique, so this is cheap to check).
    bool belongs = false;
    for (const HistoryEntry &e : h->list(*target))
        belongs = belongs || e.id == id;
    if (!belongs)
        return call.fail(ExitConflict, u"version_of_another_page"_s,
                         tr("Version %1 is not one of %2 (history list shows them)").arg(id, *target));
    notebook::Notebook::WriteOptions options;
    options.mayCreate = true; // a deleted page comes back
    const auto written = call.edit(
            *target,
            [&](const QString &, bool, QString *next) {
                *next = text;
                return true;
            },
            options);
    if (!written)
        return call.failed();
    QJsonObject o = call.writeJson(*written);
    o.insert(u"restored"_s, id);
    return call.ok(o);
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

void addToolCommands(QList<Command> &out)
{
    Command c =
            make(u"date.parse"_s,
                 QT_TRANSLATE_NOOP("brulion::cli", "Understand a date phrase (Polish or English) as Brulion does"),
                 dateParse,
                 {positional(u"phrase"_s, ArgType::String,
                             QT_TRANSLATE_NOOP("brulion::cli",
                                               "jutro o 9, w piątek 18:00, za 2 godziny, 15 października, next friday"),
                             true, true)});
    c.needsVault = false;
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Check \"jutro 9:00\" before writing it. Uses --now, --lang; without a notebook weeks start on Monday."));
    out.append(c);
    c = make(u"calc"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Calculate with Brulion's calculator (units, percent, Polish decimal comma)"),
             calcEval,
             {stdinable(positional(u"expression"_s, ArgType::Text,
                                   QT_TRANSLATE_NOOP("brulion::cli", "One expression, or lines of a calc block"), true,
                                   true))});
    c.needsVault = false;
    out.append(c);
    out.append(
            make(u"calc.page"_s,
                 QT_TRANSLATE_NOOP("brulion::cli", "The results of a page's calc blocks and inline `= …` calculations"),
                 calcPage, {positional(u"ref"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "The page"))}));
    c = make(u"block.link"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "The links to a block, adding its id when it has none (as the app's Kopiuj odnośnik)"),
             blockLink,
             {positional(u"ref"_s, ArgType::Ref,
                         QT_TRANSLATE_NOOP("brulion::cli",
                                           "The block: Dom.md#L12@hash (page read --blocks) or Dom.md#^id"))});
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Returns {id, ref ((id)), wikiLink [[Page#^id]], embed, headingLink [[Page#Heading]] for headings}."));
    c.writes = true;
    out.append(c);
    out.append(make(
            u"history.list"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "The versions of a page kept on this device (also of a deleted page)"),
            historyList, {positional(u"ref"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "The page"))}));
    out.append(make(u"history.show"_s, QT_TRANSLATE_NOOP("brulion::cli", "The text of a version"), historyShow,
                    {positional(u"id"_s, ArgType::String,
                                QT_TRANSLATE_NOOP("brulion::cli", "The version id (history list)"))}));
    c = make(
            u"history.restore"_s,
            QT_TRANSLATE_NOOP("brulion::cli", "Write a version back (the current text is kept as a version first)"),
            historyRestore,
            {positional(u"ref"_s, ArgType::Ref, QT_TRANSLATE_NOOP("brulion::cli", "The page")),
             positional(u"id"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "The version id (history list)"))});
    c.writes = c.destructive = true;
    out.append(c);
}

} // namespace brulion::cli

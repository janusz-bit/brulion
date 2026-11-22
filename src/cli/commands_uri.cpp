// brulion:// addresses on the command line (docs/CLI.md "brulion:// addresses"; core/automation):
// `uri build` writes one (with the notebook's automation key on request), `uri parse` reads one,
// `uri run` carries one out in the notebook headless (as Android's background run does: no key
// needed — the command line writes anyway), `uri open` hands one to the running app, which checks
// it as any link's.
#include "commands_p.h"

#include "appmessage.h"
#include "automation/automation.h"
#include "singleinstance.h"
#include "vault/vault.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

/// The exit code of an automation error code (automation/plan.h, uri.h).
int exitFor(const QString &error)
{
    static const QStringList notFound{u"page_not_found"_s, u"heading_not_found"_s, u"block_not_found"_s,
                                      u"template_not_found"_s, u"unknown_notebook"_s};
    static const QStringList conflict{u"exists"_s, u"conflict"_s, u"not_utf8"_s, u"ambiguous_notebook"_s};
    static const QStringList parse{u"date_not_understood"_s, u"recurrence_not_understood"_s};
    if (notFound.contains(error))
        return ExitNotFound;
    if (conflict.contains(error))
        return ExitConflict;
    if (parse.contains(error))
        return ExitParse;
    if (error == u"needs_app")
        return ExitUnavailable;
    if (error == u"failed")
        return ExitInternal;
    return ExitUsage;
}

/// The address read, or the call's error (2 bad_uri).
std::optional<automation::Request> readUri(Call &call)
{
    const QString uri = call.text(u"uri"_s).trimmed();
    const automation::ParsedUri parsed = automation::parseUri(uri);
    if (!parsed.request) {
        call.setError(ExitUsage, u"bad_uri"_s,
                      tr("Not a usable brulion:// address: %1").arg(automation::uriErrorCode(parsed.error)),
                      {{u"reason"_s, automation::uriErrorCode(parsed.error)}, {u"detail"_s, parsed.detail}});
        return std::nullopt;
    }
    return parsed.request;
}

/// Writes through the command's edit: the device's lock, --dry-run, History "cli".
class CallWriter final : public automation::Writer {
public:
    explicit CallWriter(Call &call) : m_call(call) {}

    notebook::Notebook::WriteResult edit(const VaultPath &path, const notebook::Notebook::Edit &edit,
                                         const notebook::Notebook::WriteOptions &options) override
    {
        using W = notebook::Notebook::WriteResult;
        const auto written = m_call.edit(path, edit, options);
        if (written) {
            m_written = *written;
            return *written;
        }
        W failed;
        failed.path = path;
        failed.status = W::Failed;
        if (m_call.error && m_call.error->code == u"refused") {
            failed.status = W::Refused; // the plan says why (a heading gone meanwhile …)
            m_call.error.reset();
        }
        return failed;
    }
    std::optional<notebook::Notebook::WriteResult> written() const { return m_written; }

private:
    Call &m_call;
    std::optional<notebook::Notebook::WriteResult> m_written;
};

Result uriParse(Call &call)
{
    const auto request = readUri(call);
    if (!request)
        return call.failed();
    QJsonObject params;
    for (const auto &[key, value] : request->params)
        params.insert(key, value);
    QJsonObject out{{u"action"_s, automation::actionName(request->action)},
                    {u"access"_s, automation::accessName(automation::accessOf(*request))},
                    {u"scheme"_s, request->scheme},
                    {u"params"_s, params},
                    {u"hasKey"_s, !request->key.isEmpty()}};
    const auto put = [&out](const QString &key, const QString &value) {
        if (!value.isEmpty())
            out.insert(key, value);
    };
    put(u"vault"_s, request->vault);
    put(u"source"_s, request->source);
    put(u"id"_s, request->id);
    put(u"xSuccess"_s, request->xSuccess);
    put(u"xError"_s, request->xError);
    put(u"xCancel"_s, request->xCancel);
    if (const QStringList unknown = request->unknownKeys(automation::knownParameters(request->action));
        !unknown.isEmpty())
        call.warn(u"unknown_parameter"_s, tr("Brulion does not know these parameters: %1").arg(unknown.join(u", "_s)),
                  {{u"parameters"_s, QJsonArray::fromStringList(unknown)}});
    return call.ok(out, automation::actionName(request->action));
}

Result uriBuild(Call &call)
{
    const auto action = automation::actionFromName(call.str(u"action"_s));
    if (!action)
        return call.fail(ExitUsage, u"unknown_action"_s, tr("No such action: %1").arg(call.str(u"action"_s)),
                         {{u"actions"_s, [] {
                               QJsonArray all;
                               for (const automation::Action a : automation::allActions())
                                   all.append(automation::actionName(a));
                               return all;
                           }()}});
    automation::Request r;
    r.action = *action;
    if (call.flag(u"web"_s))
        r.scheme = u"web+brulion"_s;
    if (call.has(u"params"_s)) {
        const QJsonDocument doc = QJsonDocument::fromJson(call.str(u"params"_s).toUtf8());
        if (!doc.isObject())
            return call.fail(ExitUsage, u"bad_argument"_s, tr("--params is a JSON object of strings"));
        const QJsonObject object = doc.object();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!it.value().isString())
                return call.fail(ExitUsage, u"bad_argument"_s, tr("--params is a JSON object of strings"));
            r.params.append({it.key().toLower(), it.value().toString()});
        }
    }
    for (const QString &name : {u"page"_s, u"title"_s, u"heading"_s, u"date"_s}) {
        if (call.has(name))
            r.params.append({name, call.str(name)});
    }
    // A prompt link (chat): the text is its prompt, without what nobody sees, at most 8 000
    // characters — as the app reads it.
    QJsonObject cleaned;
    if (const QString text = call.text(u"text"_s); !text.isEmpty()) {
        if (r.action == automation::Action::Chat) {
            const automation::CleanedPrompt prompt = automation::cleanPrompt(text);
            r.params.append({u"prompt"_s, prompt.text});
            if (prompt.hidden > 0)
                cleaned.insert(u"hidden"_s, prompt.hidden);
            if (prompt.capped)
                cleaned.insert(u"capped"_s, true);
        } else {
            r.params.append({u"text"_s, text});
        }
    }
    for (const QString &name : {u"due"_s, u"remind"_s, u"query"_s, u"partner"_s}) {
        if (call.has(name))
            r.params.append({name, call.str(name)});
    }
    const QStringList unknown = r.unknownKeys(automation::knownParameters(r.action));
    if (!unknown.isEmpty())
        return call.fail(ExitUsage, u"unknown_parameter"_s,
                         tr("%1 does not take: %2").arg(automation::actionName(r.action), unknown.join(u", "_s)),
                         {{u"parameters"_s, QJsonArray::fromStringList(unknown)},
                          {u"known"_s, QJsonArray::fromStringList(automation::knownParameters(r.action))}});
    // The notebook by its folder's name (the address works whichever notebook the app shows), and
    // its key on request.
    if (call.flag(u"withKey"_s) || call.flag(u"named"_s)) {
        if (auto e = call.ctx.resolveVault())
            return failWith(*e);
        r.vault = QFileInfo(call.ctx.vaultRoot()).fileName();
    }
    if (call.flag(u"withKey"_s)) {
        const QString key = automation::KeyStore(automation::KeyStore::fileIn(call.ctx.appDataDir()))
                                    .key(Vault::idForPath(call.ctx.vaultRoot()));
        if (key.isEmpty())
            return call.fail(ExitNotFound, u"no_key"_s,
                             tr("The notebook has no automation key on this device: make one in the app (Settings → "
                                "Automation; in Polish: Ustawienia → Automatyzacja)"));
        r.key = key;
    }
    const QString uri = automation::buildUri(r);
    QJsonObject out{{u"uri"_s, uri}, {u"action"_s, automation::actionName(r.action)}};
    for (auto it = cleaned.begin(); it != cleaned.end(); ++it)
        out.insert(it.key(), it.value());
    return call.ok(out, uri);
}

Result uriRun(Call &call)
{
    const auto request = readUri(call);
    if (!request)
        return call.failed();
    if (auto e = call.ctx.resolveVault())
        return failWith(*e);
    // An address that names another notebook is not run in this one.
    if (!request->vault.isEmpty()) {
        const automation::NotebookMatch match = automation::matchNotebook(
                request->vault, {{call.ctx.vaultRoot(), QFileInfo(call.ctx.vaultRoot()).fileName()}});
        if (match.index < 0)
            return call.fail(ExitNotFound, u"unknown_notebook"_s,
                             tr("The address is for the notebook “%1”, not %2: give it with --vault")
                                     .arg(request->vault, call.ctx.vaultRoot()),
                             {{u"vault"_s, request->vault}});
    }
    Vault *vault = call.vault();
    VaultIndex *index = call.index();
    if (!vault || !index)
        return call.failed();
    const automation::Context context{call.ctx.now(), call.ctx.locale()};
    const automation::Plan plan = automation::plan(*request, *vault, *index, context);
    if (!plan.ok())
        return call.fail(exitFor(plan.error), plan.error, tr("The address cannot be carried out: %1 (%2)").arg(plan.error, plan.detail),
                         {{u"detail"_s, plan.detail}, {u"outcome"_s, automation::failure(plan.error, plan.detail, plan.action).toJson()}});
    if (plan.access == automation::Access::Write && call.ctx.globals.readOnly)
        return call.fail(ExitRefused, u"read_only"_s, tr("--read-only: the address writes"));
    CallWriter writer(call);
    const automation::Outcome outcome = automation::execute(plan, writer, request->id);
    if (call.error)
        return call.failed();
    if (!outcome.ok)
        return call.fail(exitFor(outcome.error), outcome.error,
                         tr("The address cannot be carried out: %1 (%2)").arg(outcome.error, outcome.message),
                         {{u"outcome"_s, outcome.toJson()}});
    QJsonObject out = outcome.toJson();
    if (const auto written = writer.written()) {
        const QJsonObject write = call.writeJson(*written);
        for (auto it = write.begin(); it != write.end(); ++it) {
            if (!out.contains(it.key()))
                out.insert(it.key(), it.value());
        }
    }
    return call.ok(out);
}

Result uriOpen(Call &call)
{
    const auto request = readUri(call);
    if (!request)
        return call.failed();
    const QString uri = call.text(u"uri"_s).trimmed();
    if (call.dryRun())
        return call.ok(QJsonObject{{u"dryRun"_s, true}, {u"sent"_s, u"uri"_s}});
    ipc::SingleInstance app(ipc::SingleInstance::keyFor(call.ctx.appDataDir()));
    if (!app.isPrimaryRunning()) {
        if (!app.errorString().isEmpty())
            return call.fail(ExitUnavailable, u"app_not_reached"_s,
                             tr("The running app did not answer: %1").arg(app.errorString()),
                             {{u"reason"_s, u"foreign_socket"_s}});
        return call.fail(ExitUnavailable, u"app_not_running"_s,
                         tr("Brulion is not running on this desktop (the app must run in this user session)"),
                         {{u"gui"_s, QJsonObject{{u"running"_s, false}}}});
    }
    if (!app.sendToPrimary(ipc::uriMessage(uri)))
        return call.fail(ExitUnavailable, u"app_not_reached"_s, tr("The running app did not take the message"),
                         {{u"gui"_s, QJsonObject{{u"running"_s, true}}}});
    return call.ok(QJsonObject{{u"sent"_s, u"uri"_s},
                               {u"action"_s, automation::actionName(request->action)},
                               {u"gui"_s, QJsonObject{{u"running"_s, true}}},
                               {u"via"_s, u"message"_s}});
}

Command make(const QString &id, const char *summary, Handler run, QList<Arg> args)
{
    Command c;
    c.id = id;
    c.summary = cliText(summary);
    c.run = std::move(run);
    c.args = std::move(args);
    c.needsVault = false;
    return c;
}

} // namespace

void addUriCommands(QList<Command> &out)
{
    const Arg uri = stdinable(positional(
            u"uri"_s, ArgType::Text,
            QT_TRANSLATE_NOOP("brulion::cli", "A brulion:// address (brulion://append?page=Zakupy&text=Mleko)")));
    Command c = make(u"uri.build"_s, QT_TRANSLATE_NOOP("brulion::cli", "Write a brulion:// address"), uriBuild,
                     {choice(u"action"_s,
                             {u"open"_s, u"daily"_s, u"new"_s, u"append"_s, u"prepend"_s, u"task"_s, u"capture"_s,
                              u"search"_s, u"view"_s, u"run"_s, u"chat"_s},
                             QT_TRANSLATE_NOOP("brulion::cli", "What the address does"), true, true),
                      option(u"page"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A page (name, path, date)")),
                      option(u"heading"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A heading of the page")),
                      option(u"date"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A day (daily notes)")),
                      option(u"title"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A new page's title")),
                      option(u"due"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A task's due date")),
                      option(u"remind"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "A task's reminder")),
                      option(u"query"_s, ArgType::String, QT_TRANSLATE_NOOP("brulion::cli", "What to search for")),
                      option(u"partner"_s, ArgType::String,
                             QT_TRANSLATE_NOOP("brulion::cli", "Who answers a prompt link (chat: acp:claude …)")),
                      stdinable(option(u"text"_s, ArgType::Text, QT_TRANSLATE_NOOP("brulion::cli", "The text to write"))),
                      option(u"params"_s, ArgType::String,
                             QT_TRANSLATE_NOOP("brulion::cli", "Any parameters as a JSON object of strings")),
                      flag(u"named"_s, QT_TRANSLATE_NOOP("brulion::cli", "Name the notebook (vault=) in the address")),
                      flag(u"withKey"_s,
                           QT_TRANSLATE_NOOP("brulion::cli",
                                             "Add the notebook's automation key from this device (writes without "
                                             "asking); names the notebook too")),
                      flag(u"web"_s, QT_TRANSLATE_NOOP("brulion::cli", "web+brulion: (the browser version)"))});
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Values are percent-encoded as UTF-8. The parameters of each action: docs/UI.md \"Automation\". The key "
            "is a secret: an address with it writes into the notebook without asking."));
    out.append(c);
    out.append(make(u"uri.parse"_s, QT_TRANSLATE_NOOP("brulion::cli", "Read a brulion:// address (never runs it)"),
                    uriParse, {uri}));
    c = make(u"uri.run"_s,
             QT_TRANSLATE_NOOP("brulion::cli", "Carry out a brulion:// address in the notebook (no app needed)"), uriRun,
             {uri});
    c.writes = true;
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "As Android's background run does: writing needs no key (the command line writes anyway) and is recorded "
            "in the device's History; opening returns the route the app would show; extension commands need the app "
            "(exit 5 needs_app). An address naming another notebook than --vault's is refused."));
    out.append(c);
    c = make(u"uri.open"_s,
             QT_TRANSLATE_NOOP("brulion::cli",
                               "Hand a brulion:// address to the running app, which checks it as any link's"),
             uriOpen, {uri});
    c.writes = true;
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Desktop only. The app opens what the address shows; a write without the notebook's key asks the user "
            "first (Settings → Automation decides)."));
    out.append(c);
}

} // namespace brulion::cli

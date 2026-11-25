// `brulion-cli acp usage`: what Claude Code and Codex used on this computer, read from their own
// session logs (core agentusage; docs/CLI.md "acp usage", docs/UI.md "AI agents (ACP)", "Its usage"):
// per day, model and project folder, tokens by kind, requests and an estimate of what they would cost
// on the API. The logs are where CLAUDE_CONFIG_DIR and CODEX_HOME put them (else ~/.claude/projects,
// ~/.config/claude/projects, ~/.codex/sessions and archived_sessions); the incremental cache is the
// app's (its cache folder's agent-usage/), so the app and the command line read each file once.
// Local files only, nothing sent; not an MCP tool (the folders of every project are the user's).

#include "commands_p.h"

#include "agentusage/history.h"
#include "agentusage/usage.h"

#include <QDir>
#include <QLocale>
#include <QStandardPaths>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::cli {

namespace {

using namespace agentusage;

QJsonObject totalsJson(const Totals &t)
{
    return QJsonObject{{u"input"_s, double(t.tokens.input)},
                       {u"output"_s, double(t.tokens.output)},
                       {u"cacheWrite5m"_s, double(t.tokens.cacheWrite5m)},
                       {u"cacheWrite1h"_s, double(t.tokens.cacheWrite1h)},
                       {u"cacheRead"_s, double(t.tokens.cacheRead)},
                       {u"reasoning"_s, double(t.tokens.reasoning)},
                       {u"tokens"_s, double(t.tokens.total())},
                       {u"requests"_s, double(t.tokens.requests)},
                       {u"costUsd"_s, std::round(t.cost * 1e6) / 1e6},
                       {u"costKnown"_s, t.costKnown()},
                       {u"unpricedRequests"_s, double(t.unpricedRequests)},
                       {u"unpricedTokens"_s, double(t.unpricedTokens)}};
}

QString number(qint64 n)
{
    return QLocale().toString(n);
}

/// "≈ 4.10 USD", "≥ 4.10 USD" (a part without a price), "cost not known".
QString cost(const Totals &t)
{
    if (t.cost <= 0 && t.unpricedRequests > 0)
        return QCoreApplication::translate("brulion::cli", "cost not known");
    const QString usd = QLocale().toString(t.cost, 'f', 2);
    return t.unpricedRequests > 0 ? QCoreApplication::translate("brulion::cli", "≥ %1 USD").arg(usd)
                                  : QCoreApplication::translate("brulion::cli", "≈ %1 USD").arg(usd);
}

/// One line of the text: "<what>  12 345 tokens  3 requests  ≈ 4.10 USD".
QString line(const QString &what, const Totals &t)
{
    return QCoreApplication::translate("brulion::cli", "%1  %2 tokens  %3 requests  %4")
            .arg(what, number(t.tokens.total()), number(t.tokens.requests), cost(t));
}

Result acpUsage(Call &call)
{
    const int days = call.integer(u"days"_s, 30);
    if (days < 1 || days > 366)
        return call.fail(ExitUsage, u"bad_days"_s, QCoreApplication::translate("brulion::cli", "--days takes 1 … 366"));
    if (call.flag(u"notebook"_s) && call.flag(u"all"_s))
        return call.fail(ExitUsage, u"bad_scope"_s,
                         QCoreApplication::translate("brulion::cli", "--notebook and --all exclude each other"));
    QString folder;
    if (call.flag(u"notebook"_s)) {
        if (const std::optional<Error> e = call.ctx.resolveVault())
            return failWith(*e);
        folder = QDir(call.ctx.vaultRoot()).absolutePath();
    }
    QList<Provider> providers{Provider::Claude, Provider::Codex};
    if (const QString agent = call.str(u"agent"_s); !agent.isEmpty())
        providers = {agent == u"codex" ? Provider::Codex : Provider::Claude};
    QHash<QString, QString> environment;
    for (const char *name : {"CLAUDE_CONFIG_DIR", "CODEX_HOME"}) {
        if (qEnvironmentVariableIsSet(name))
            environment.insert(QString::fromLatin1(name), qEnvironmentVariable(name));
    }
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QDateTime now = call.ctx.now();
    const QDate today = call.ctx.today();
    const Query query = lastDays(today, days, folder);

    QJsonArray agents;
    QStringList lines;
    lines.append(folder.isEmpty()
                         ? QCoreApplication::translate("brulion::cli", "The last %1 days (%2 … %3), all projects")
                                   .arg(days)
                                   .arg(query.from.toString(Qt::ISODate), query.to.toString(Qt::ISODate))
                         : QCoreApplication::translate("brulion::cli", "The last %1 days (%2 … %3), in %4")
                                   .arg(days)
                                   .arg(query.from.toString(Qt::ISODate), query.to.toString(Qt::ISODate),
                                        QDir::toNativeSeparators(folder)));
    for (const Provider provider : std::as_const(providers)) {
        const Source source = sourceOf(provider, {}, environment, QDir::homePath());
        ReadOptions options;
        options.cacheFile = cacheDir.isEmpty() ? QString() : cacheFileFor(cacheDir, source);
        options.now = now.toUTC();
        const History history = readHistory(source, options);
        const Report r = report(history, query);
        const QString name = provider == Provider::Claude ? u"Claude Code"_s : u"Codex"_s;
        QJsonArray dayRows;
        for (const DayTotals &d : r.days) {
            QJsonObject o = totalsJson(d.totals);
            o.insert(u"date"_s, d.date.toString(Qt::ISODate));
            dayRows.append(o);
        }
        QJsonArray modelRows;
        for (const NamedTotals &m : r.models) {
            QJsonObject o = totalsJson(m.totals);
            o.insert(u"model"_s, m.name);
            o.insert(u"priced"_s, priceOf(m.name).has_value());
            modelRows.append(o);
        }
        QJsonArray projectRows;
        for (const NamedTotals &p : r.projects) {
            QJsonObject o = totalsJson(p.totals);
            o.insert(u"folder"_s, p.name);
            projectRows.append(o);
        }
        agents.append(QJsonObject{{u"agent"_s, agentlimits::providerName(provider)},
                                  {u"name"_s, name},
                                  {u"folders"_s, QJsonArray::fromStringList(source.folders)},
                                  {u"found"_s, history.found},
                                  {u"files"_s, history.files},
                                  {u"skippedLines"_s, double(history.skippedLines)},
                                  {u"total"_s, totalsJson(r.total)},
                                  {u"days"_s, dayRows},
                                  {u"models"_s, modelRows},
                                  {u"projects"_s, projectRows}});
        lines.append(QString());
        if (!history.found) {
            lines.append(QCoreApplication::translate("brulion::cli", "%1: no session logs in %2")
                                 .arg(name, QDir::toNativeSeparators(source.folders.join(u", "_s))));
            continue;
        }
        lines.append(line(name, r.total));
        for (const NamedTotals &m : r.models)
            lines.append(u"  "_s
                         + line(m.name.isEmpty() ? QCoreApplication::translate("brulion::cli", "(a model not named)")
                                                 : m.name,
                                m.totals));
        if (history.skippedLines > 0)
            lines.append(u"  "_s
                         + QCoreApplication::translate("brulion::cli", "%1 lines of the logs could not be read")
                                   .arg(history.skippedLines));
    }
    const QString note =
            QCoreApplication::translate("brulion::cli",
                                        "Costs are what the same tokens would cost on the API (its prices of %1): a "
                                        "subscription pays nothing more for them.")
                    .arg(QLatin1String(PricesAsOf));
    lines.append(QString());
    lines.append(note);
    QJsonObject out{{u"days"_s, days},
                    {u"from"_s, query.from.toString(Qt::ISODate)},
                    {u"to"_s, query.to.toString(Qt::ISODate)},
                    {u"folder"_s, folder.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(folder)},
                    {u"pricesAsOf"_s, QLatin1String(PricesAsOf)},
                    {u"note"_s, note},
                    {u"agents"_s, agents}};
    return call.ok(out, lines.join(u'\n') + u'\n');
}

} // namespace

void addAcpUsageCommands(QList<Command> &out)
{
    Command c;
    c.id = u"acp.usage"_s;
    c.summary = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli", "What Claude Code and Codex used on this computer, from their own session logs"));
    c.help = cliText(QT_TRANSLATE_NOOP(
            "brulion::cli",
            "Tokens by kind, requests and an estimate of what they would cost on the API, per day, model and "
            "project folder, read from the agents' own logs (CLAUDE_CONFIG_DIR's or ~/.claude's projects, "
            "CODEX_HOME's or ~/.codex's sessions) with the app's cache, so only what was appended is read again. "
            "A subscription pays nothing more for these tokens. Local files only: nothing is sent anywhere."));
    c.args = {choice(u"agent"_s, {u"claude"_s, u"codex"_s},
                     QT_TRANSLATE_NOOP("brulion::cli", "Only this agent (default: both)")),
              option(u"days"_s, ArgType::Int,
                     QT_TRANSLATE_NOOP("brulion::cli", "The last N days, today included (default 30)")),
              flag(u"notebook"_s, QT_TRANSLATE_NOOP("brulion::cli", "Only the notebook's folder (and below)")),
              flag(u"all"_s, QT_TRANSLATE_NOOP("brulion::cli", "Every project folder (the default)"))};
    c.run = acpUsage;
    c.needsVault = false; // (only --notebook needs one)
    c.mcp = false;
    out.append(c);
}

} // namespace brulion::cli

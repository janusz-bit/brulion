#include "agentusage.h"

#include "workspace.h"
#include "workspaceagents.h"

#include <QDir>
#include <QLocale>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using namespace agentusage;
using agentusage::History; // not brulion::History (vault/history.h) that workspace.h brings

namespace {

QString providerLabel(Provider provider)
{
    return provider == Provider::Claude ? u"Claude Code"_s : provider == Provider::Codex ? u"Codex"_s : QString();
}

/// `value` with `decimals`, without a trailing zero ("1.0" → "1").
QString number(double value, int decimals)
{
    const QLocale locale;
    QString text = locale.toString(value, 'f', decimals);
    if (decimals > 0) {
        const QString zero = locale.decimalPoint() + QString(decimals, locale.zeroDigit().at(0));
        if (text.endsWith(zero))
            text.chop(zero.size());
    }
    return text;
}

/// The money of a cost (no sign): "4.10", "61".
QString money(double usd)
{
    return QLocale().toString(usd, 'f', usd < 10 ? 2 : 0);
}

/// What a period used, in a few words: "1.2 M tokens · ≈ 4.10 USD".
QString usedText(const Totals &t)
{
    return AgentUsage::tr("%1 · %2", "tokens · their cost")
            .arg(AgentUsage::tokensWords(t.tokens.total()), AgentUsage::costText(t));
}

/// The cost of a period, or its tokens when no part of it has a price.
QString costOrTokens(const Totals &t)
{
    return t.cost <= 0 && t.unpricedRequests > 0 ? AgentUsage::tokensWords(t.tokens.total()) : AgentUsage::costText(t);
}

/// No history yet.
History none(Provider provider)
{
    History out;
    out.provider = provider;
    return out;
}

} // namespace

AgentUsage::AgentUsage(Workspace *workspace, WorkspaceAgents *agents, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_agents(agents), m_home(QDir::homePath())
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    for (const char *name : {"CLAUDE_CONFIG_DIR", "CODEX_HOME"}) {
        if (qEnvironmentVariableIsSet(name))
            m_environment.insert(QString::fromLatin1(name), qEnvironmentVariable(name));
    }
    // The agents changed, or a program said what it is.
    const auto bump = [this] {
        ++m_serial;
        Q_EMIT changed();
    };
    connect(m_agents, &WorkspaceAgents::checked, this, bump);
    connect(m_agents, &WorkspaceAgents::sessionChanged, this, [bump](const QString &path) {
        if (path.isEmpty())
            bump();
    });
}

AgentUsage::~AgentUsage() = default;

Source AgentUsage::sourceOf(const AgentConfig &agent, const QString &programName,
                            const QHash<QString, QString> &environment, const QString &home)
{
    if (agent.isRemote() || agent.command.trimmed().isEmpty())
        return {};
    const QStringList words = WorkspaceAgents::commandLine(agent.command, false, [](const QString &p) { return p; });
    const Provider provider = agentlimits::providerOf(words, programName);
    if (provider == Provider::None)
        return {};
    return agentusage::sourceOf(provider, words, environment, home);
}

Source AgentUsage::source(const QString &partner) const
{
    if (!WorkspaceAgents::isAgent(partner) || !WorkspaceAgents::available())
        return {};
    const QString id = partner.trimmed().mid(WorkspaceAgents::Prefix.size());
    for (const AgentConfig &a : m_agents->agents()) {
        if (a.id == id)
            return sourceOf(a, m_agents->programName(id), m_environment, m_home);
    }
    return {};
}

UsageReader *AgentUsage::reader()
{
    if (!m_reader) {
        m_reader = std::make_unique<UsageReader>(m_cacheDir);
        if (m_clock)
            m_reader->setClock([this] { return now().toUTC(); });
        m_reader->setTimeZone(m_zone);
        connect(m_reader.get(), &UsageReader::changed, this, [this] {
            ++m_serial;
            Q_EMIT changed();
        });
    }
    return m_reader.get();
}

QDateTime AgentUsage::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

QDate AgentUsage::today() const
{
    return now().toTimeZone(m_zone).date();
}

void AgentUsage::setCacheDir(const QString &dir)
{
    m_cacheDir = dir;
    m_reader.reset();
}

void AgentUsage::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    m_reader.reset();
}

void AgentUsage::setEnvironment(const QHash<QString, QString> &environment, const QString &home)
{
    m_environment = environment;
    m_home = home;
}

void AgentUsage::setTimeZone(const QTimeZone &zone)
{
    m_zone = zone;
    m_reader.reset();
}

bool AgentUsage::applies(const QString &partner, int) const
{
    return source(partner).valid();
}

void AgentUsage::agentsChanged()
{
    ++m_serial;
    Q_EMIT changed();
}

void AgentUsage::show(const QString &partner)
{
    if (const Source s = source(partner); s.valid())
        reader()->refresh(s);
}

void AgentUsage::refresh(const QString &partner)
{
    if (const Source s = source(partner); s.valid())
        reader()->refresh(s, true);
}

QString AgentUsage::tokensText(qint64 tokens)
{
    if (tokens < 1000)
        return QLocale().toString(std::max<qint64>(tokens, 0));
    // Thousands, millions, billions: one decimal below ten (a hundred for millions and billions).
    double value = double(tokens) / 1000.0;
    for (int unit = 0; unit < 3; ++unit) {
        const int decimals = value < (unit == 0 ? 10 : 100) ? 1 : 0;
        const double scale = decimals > 0 ? 10.0 : 1.0;
        const double shown = std::round(value * scale) / scale;
        if (shown < 1000 || unit == 2) {
            const QString n = number(shown, decimals);
            switch (unit) {
            case 0: return tr("%1 K", "thousands of tokens").arg(n);
            case 1: return tr("%1 M", "millions of tokens").arg(n);
            default: return tr("%1 B", "billions of tokens").arg(n);
            }
        }
        value /= 1000.0;
    }
    return {};
}

QString AgentUsage::tokensWords(qint64 tokens)
{
    if (tokens < 1000)
        return tr("%n tokens", nullptr, int(std::max<qint64>(tokens, 0)));
    return tr("%1 tokens", "an abbreviated number: 1.2 M tokens").arg(tokensText(tokens));
}

QString AgentUsage::costText(const Totals &totals)
{
    if (totals.cost <= 0 && totals.unpricedRequests > 0)
        return tr("cost not known");
    if (totals.cost <= 0)
        return tr("%1 USD", "no cost").arg(QLocale().toString(0));
    if (totals.cost < 0.005)
        return tr("< %1 USD", "a cost under a cent").arg(QLocale().toString(0.01, 'f', 2));
    return totals.unpricedRequests > 0 ? tr("≥ %1 USD", "a cost of which a part is not known").arg(money(totals.cost))
                                       : tr("≈ %1 USD", "an estimated cost").arg(money(totals.cost));
}

QVariantMap AgentUsage::describe(const History &history, const QDate &today)
{
    QVariantMap out;
    out.insert(u"provider"_s, providerLabel(history.provider));
    out.insert(u"read"_s, history.readAt.isValid());
    if (!history.readAt.isValid())
        return out;
    out.insert(u"checked"_s, tr("Read at %1").arg(QLocale().toString(history.readAt.toLocalTime().time(), u"HH:mm"_s)));
    if (!history.found) {
        out.insert(u"failure"_s, tr("No session logs of %1 on this computer yet: they appear once it has worked here.")
                                         .arg(providerLabel(history.provider)));
        out.insert(u"summary"_s, tr("No usage yet"));
        return out;
    }
    const Totals day = report(history, lastDays(today, 1)).total;
    const Totals month = report(history, lastDays(today, 30)).total;
    out.insert(u"summary"_s,
               day.tokens.requests == 0
                       ? tr("Today nothing · 30 days %1").arg(costOrTokens(month))
                       : tr("Today %1 · %2 · 30 days %3", "usage: today's tokens · their cost · 30 days' cost")
                                 .arg(tokensWords(day.tokens.total()), costText(day), costOrTokens(month)));
    out.insert(u"today"_s, day.tokens.requests == 0 ? tr("Used today: nothing")
                                                    : tr("Used today: %1 at API prices").arg(usedText(day)));
    return out;
}

QVariantMap AgentUsage::describeDetails(const History &history, const QDate &today, int days, const QString &folder)
{
    QVariantMap out = describe(history, today);
    days = std::clamp(days, 1, 366);
    const Report r = report(history, lastDays(today, days, folder));
    out.insert(u"folder"_s, QDir::toNativeSeparators(folder));
    out.insert(u"total"_s, tr("%1 · %2 · %n requests", nullptr,
                              int(std::min<qint64>(r.total.tokens.requests, std::numeric_limits<int>::max())))
                                   .arg(tokensWords(r.total.tokens.total()), costText(r.total)));
    // The bars: each day, a date under every 7th (14th in 90 days) counted back from today.
    const QLocale locale;
    const int step = days <= 7 ? 1 : days <= 31 ? 7 : 14;
    QVariantList bars;
    double maxTokens = 0;
    double maxCost = 0;
    for (qsizetype i = 0; i < r.days.size(); ++i) {
        const DayTotals &d = r.days.at(i);
        const double tokens = double(d.totals.tokens.total());
        maxTokens = std::max(maxTokens, tokens);
        maxCost = std::max(maxCost, d.totals.cost);
        const QString date = locale.toString(d.date, u"ddd d MMM"_s);
        const bool marked = (r.days.size() - 1 - i) % step == 0;
        bars.append(QVariantMap{
                {u"day"_s, d.date.toString(Qt::ISODate)},
                {u"label"_s, date},
                {u"mark"_s, marked ? locale.toString(d.date, days <= 7 ? u"ddd"_s : u"d MMM"_s) : QString()},
                {u"tip"_s, d.totals.tokens.requests == 0
                                   ? tr("%1: nothing", "a day: no use").arg(date)
                                   : tr("%1: %2", "a day: its use").arg(date, usedText(d.totals))},
                {u"tokens"_s, tokens},
                {u"cost"_s, d.totals.cost},
                {u"today"_s, d.date == today}});
    }
    out.insert(u"bars"_s, bars);
    out.insert(u"maxTokens"_s, maxTokens);
    out.insert(u"maxCost"_s, maxCost);
    QVariantList models;
    for (const NamedTotals &m : r.models) {
        const Tokens &t = m.totals.tokens;
        QStringList kinds{tr("input %1").arg(tokensText(t.input)), tr("output %1").arg(tokensText(t.output))};
        if (t.cacheWrite() > 0)
            kinds.append(tr("cache write %1").arg(tokensText(t.cacheWrite())));
        if (t.cacheRead > 0)
            kinds.append(tr("cache read %1").arg(tokensText(t.cacheRead)));
        if (t.reasoning > 0)
            kinds.append(tr("reasoning %1", "of the output").arg(tokensText(t.reasoning)));
        models.append(
                QVariantMap{{u"name"_s, m.name.isEmpty() ? tr("A model not named") : m.name},
                            {u"cost"_s, costText(m.totals)},
                            {u"kinds"_s, kinds.join(u" · "_s)},
                            {u"requests"_s, tr("%n requests", nullptr,
                                               int(std::min<qint64>(t.requests, std::numeric_limits<int>::max())))}});
    }
    out.insert(u"models"_s, models);
    const bool empty = r.total.tokens.requests == 0;
    out.insert(u"empty"_s, empty);
    out.insert(u"emptyText"_s, folder.isEmpty() ? tr("Nothing used in these days.")
                                                : tr("Nothing used in this notebook in these days."));
    out.insert(u"note"_s, tr("The cost is what the same tokens would cost on the API (its prices of %1): a "
                             "subscription pays nothing more for them.")
                                  .arg(locale.toString(QDate::fromString(QLatin1String(PricesAsOf), Qt::ISODate),
                                                       u"d MMMM yyyy"_s)));
    out.insert(u"unpriced"_s, r.total.unpricedRequests > 0
                                      ? tr("Models without a known price: their %1 are not in the cost.")
                                                .arg(tokensWords(r.total.unpricedTokens))
                                      : QString());
    return out;
}

QVariantMap AgentUsage::state(const QString &partner, int) const
{
    const Source s = source(partner);
    if (!s.valid())
        return {{u"applies"_s, false}};
    QVariantMap out = describe(m_reader ? m_reader->history(s) : none(s.provider), today());
    out.insert(u"applies"_s, true);
    out.insert(u"busy"_s, m_reader && m_reader->busy(s));
    return out;
}

QVariantMap AgentUsage::details(const QString &partner, int days, bool notebookOnly, int) const
{
    const Source s = source(partner);
    if (!s.valid())
        return {{u"applies"_s, false}, {u"bars"_s, QVariantList()}, {u"models"_s, QVariantList()}};
    const QString folder = notebookOnly && m_workspace ? QDir(m_workspace->rootPath()).absolutePath() : QString();
    QVariantMap out = describeDetails(m_reader ? m_reader->history(s) : none(s.provider), today(), days, folder);
    out.insert(u"applies"_s, true);
    out.insert(u"busy"_s, m_reader && m_reader->busy(s));
    QStringList where;
    for (const QString &f : s.folders)
        where.append(QDir::toNativeSeparators(f));
    out.insert(u"where"_s, tr("From the session logs in %1, on this computer only.").arg(where.join(u", "_s)));
    return out;
}

} // namespace brulion::shell

#include "agentlimits.h"

#include "workspaceagents.h"

#include "sync/http.h"

#include <QDir>
#include <QLocale>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using namespace agentlimits;

namespace {

QString percentText(double percent)
{
    return AgentLimits::tr("%1 %").arg(QLocale().toString(std::floor(percent + 0.5), 'f', 0));
}

QString levelName(Level level)
{
    switch (level) {
    case Level::Warning: return u"warning"_s;
    case Level::Danger: return u"danger"_s;
    case Level::Normal: break;
    }
    return {};
}

/// A window's short name in the chip ("5 h", "week", "Opus").
QString shortLabel(const Window &w)
{
    switch (w.kind) {
    case Kind::Session:
        return AgentLimits::tr("%1 h", "hours of a limit window").arg(w.minutes > 0 ? (w.minutes + 30) / 60 : 5);
    case Kind::Weekly: return AgentLimits::tr("week", "a limit window");
    case Kind::Model: return w.model;
    case Kind::Monthly: return AgentLimits::tr("month", "a limit window");
    }
    return {};
}

/// A window's name in the menu ("5-hour window", "Week, all models", "Week, Opus").
QString longLabel(const Window &w, Provider provider)
{
    switch (w.kind) {
    case Kind::Session:
        return AgentLimits::tr("%1-hour window").arg(w.minutes > 0 ? (w.minutes + 30) / 60 : 5);
    case Kind::Weekly: return AgentLimits::tr("Week, all models");
    case Kind::Model:
        return w.minutes > 0 && w.minutes < 24 * 60
                       ? AgentLimits::tr("%1, %2-hour window").arg(w.model).arg((w.minutes + 30) / 60)
                       : AgentLimits::tr("Week, %1").arg(w.model);
    case Kind::Monthly:
        return provider == Provider::Claude && w.id == u"extra_usage" ? AgentLimits::tr("Extra usage this month")
                                                                      : AgentLimits::tr("Month");
    }
    return {};
}

QString providerLabel(Provider provider)
{
    return provider == Provider::Claude ? u"Claude Code"_s : provider == Provider::Codex ? u"Codex"_s : QString();
}

/// What to do when the limits cannot be shown.
QString failureText(const Limits &limits, const QString &file)
{
    const bool claude = limits.provider == Provider::Claude;
    switch (limits.failure) {
    case Failure::None: return {};
    case Failure::NoCredentials:
        return claude ? AgentLimits::tr("Not signed in to Claude Code on this computer: run claude in a terminal and "
                                        "sign in (/login), then refresh.")
                      : AgentLimits::tr("Not signed in to Codex with ChatGPT on this computer: run codex login in a "
                                        "terminal, then refresh.");
    case Failure::Unreadable:
        return AgentLimits::tr("The sign-in file cannot be read: %1").arg(QDir::toNativeSeparators(file));
    case Failure::ApiKey: return AgentLimits::tr("Signed in with an API key: a key has no subscription limits.");
    case Failure::Expired:
        return AgentLimits::tr("The sign-in has expired: use %1 once (it renews it), then refresh.")
                .arg(providerLabel(limits.provider));
    case Failure::SignedOut:
        return claude ? AgentLimits::tr("Claude did not take the sign-in: run claude /login in a terminal, then "
                                        "refresh.")
                      : AgentLimits::tr("ChatGPT did not take the sign-in: run codex login in a terminal, then "
                                        "refresh.");
    case Failure::RateLimited: return AgentLimits::tr("Asked too often: try again in a few minutes.");
    case Failure::Offline: return AgentLimits::tr("No connection: try again when you are online.");
    case Failure::ServerError:
        return AgentLimits::tr("The limits could not be read (HTTP %1): try again later.").arg(limits.status);
    case Failure::Malformed: return AgentLimits::tr("The answer had no limits in it: try again later.");
    }
    return {};
}

/// The failure in a few words (the chip, the menu).
QString failureShort(Failure failure)
{
    switch (failure) {
    case Failure::None: return {};
    case Failure::NoCredentials:
    case Failure::ApiKey: return AgentLimits::tr("Limits: not signed in");
    case Failure::Expired:
    case Failure::SignedOut: return AgentLimits::tr("Limits: sign in again");
    case Failure::Offline: return AgentLimits::tr("Limits: offline");
    case Failure::RateLimited:
    case Failure::Unreadable:
    case Failure::ServerError:
    case Failure::Malformed: break;
    }
    return AgentLimits::tr("Limits: not available");
}

} // namespace

AgentLimits::AgentLimits(WorkspaceAgents *agents, QObject *parent)
    : QObject(parent), m_agents(agents), m_home(QDir::homePath())
{
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

AgentLimits::~AgentLimits() = default;

Account AgentLimits::accountOf(const AgentConfig &agent, const QString &programName,
                               const QHash<QString, QString> &environment, const QString &home)
{
    if (agent.isRemote() || agent.command.trimmed().isEmpty())
        return {};
    const QStringList words = WorkspaceAgents::commandLine(agent.command, false, [](const QString &p) { return p; });
    const Provider provider = providerOf(words, programName);
    if (provider == Provider::None)
        return {};
    return Account{provider, credentialsFile(provider, words, environment, home)};
}

Account AgentLimits::account(const QString &partner) const
{
    if (!WorkspaceAgents::isAgent(partner) || !WorkspaceAgents::available())
        return {};
    const QString id = partner.trimmed().mid(WorkspaceAgents::Prefix.size());
    for (const AgentConfig &a : m_agents->agents()) {
        if (a.id == id)
            return accountOf(a, m_agents->programName(id), m_environment, m_home);
    }
    return {};
}

LimitsReader *AgentLimits::reader()
{
    if (!m_reader) {
        if (!m_transport) {
            m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
            m_transport = m_ownTransport.get();
        }
        m_reader = std::make_unique<LimitsReader>(m_transport);
        if (m_clock)
            m_reader->setClock([this] { return now().toUTC(); });
        if (m_read)
            m_reader->setCredentialsReader(m_read);
        connect(m_reader.get(), &LimitsReader::changed, this, [this] {
            ++m_serial;
            Q_EMIT changed();
        });
    }
    return m_reader.get();
}

QDateTime AgentLimits::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void AgentLimits::setTransport(sync::HttpTransport *transport)
{
    m_transport = transport;
    m_reader.reset();
}

void AgentLimits::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
    m_reader.reset();
}

void AgentLimits::setEnvironment(const QHash<QString, QString> &environment, const QString &home)
{
    m_environment = environment;
    m_home = home;
}

void AgentLimits::setCredentialsReader(LimitsReader::CredentialsReader read)
{
    m_read = std::move(read);
    m_reader.reset();
}

bool AgentLimits::applies(const QString &partner, int) const
{
    return account(partner).valid();
}

void AgentLimits::agentsChanged()
{
    ++m_serial;
    Q_EMIT changed();
}

void AgentLimits::show(const QString &partner)
{
    if (const Account a = account(partner); a.valid())
        reader()->refresh(a);
}

void AgentLimits::refresh(const QString &partner)
{
    if (const Account a = account(partner); a.valid())
        reader()->refresh(a, true);
}

QString AgentLimits::resetText(const QDateTime &resetsAt, const QDateTime &now)
{
    if (!resetsAt.isValid())
        return {};
    const QDateTime local = resetsAt.toLocalTime();
    const QDate today = now.toLocalTime().date();
    const QString time = QLocale().toString(local.time(), u"HH:mm"_s);
    if (local.date() == today)
        return tr("resets at %1").arg(time);
    if (local.date() > today && local.date() < today.addDays(7))
        return tr("resets %1 %2", "weekday, time").arg(QLocale().toString(local.date(), u"ddd"_s), time);
    return tr("resets %1 %2", "date, time").arg(QLocale().toString(local.date(), u"d MMM"_s), time);
}

QVariantMap AgentLimits::describe(const Limits &limits, const QDateTime &now, const QString &file)
{
    QVariantMap out;
    out.insert(u"provider"_s, providerLabel(limits.provider));
    out.insert(u"read"_s, limits.checkedAt.isValid());
    if (!limits.plan.isEmpty()) {
        QString plan = limits.plan;
        plan[0] = plan.at(0).toUpper();
        out.insert(u"plan"_s, tr("Plan: %1").arg(plan));
    }
    if (limits.checkedAt.isValid())
        out.insert(u"checked"_s, tr("Checked at %1").arg(QLocale().toString(limits.checkedAt.toLocalTime().time(), u"HH:mm"_s)));
    out.insert(u"failure"_s, failureText(limits, file));
    out.insert(u"failureShort"_s, failureShort(limits.failure));
    // The chip: the five hours and the week (a month where neither is: Codex Free).
    QStringList parts;
    for (const Window &w : limits.windows) {
        if (w.kind == Kind::Session || w.kind == Kind::Weekly)
            parts.append(tr("%1: %2", "a limit window: its percentage").arg(shortLabel(w), percentText(w.usedPercent)));
    }
    if (parts.isEmpty()) {
        for (const Window &w : limits.windows) {
            if (w.kind == Kind::Monthly)
                parts.append(tr("%1: %2", "a limit window: its percentage").arg(shortLabel(w), percentText(w.usedPercent)));
        }
    }
    out.insert(u"summary"_s, parts.join(u" · "_s));
    out.insert(u"level"_s, levelName(levelOf(limits.highest())));
    QVariantList rows;
    for (const Window &w : limits.windows) {
        const QString reset = resetText(w.resetsAt, now);
        rows.append(QVariantMap{
                {u"label"_s, longLabel(w, limits.provider)},
                {u"text"_s, reset.isEmpty() ? percentText(w.usedPercent)
                                            : tr("%1 · %2", "a percentage · when it resets").arg(percentText(w.usedPercent), reset)},
                {u"level"_s, levelName(levelOf(w.usedPercent))}});
    }
    out.insert(u"windows"_s, rows);
    // The tooltip: who, each window, what to do, when it was read.
    QStringList tip{limits.plan.isEmpty() ? providerLabel(limits.provider)
                                          : tr("%1 · %2", "an agent · its plan").arg(providerLabel(limits.provider),
                                                                                     out.value(u"plan"_s).toString())};
    for (const QVariant &row : std::as_const(rows)) {
        const QVariantMap r = row.toMap();
        tip.append(tr("%1: %2", "a limit window: its percentage and reset")
                           .arg(r.value(u"label"_s).toString(), r.value(u"text"_s).toString()));
    }
    if (const QString failure = out.value(u"failure"_s).toString(); !failure.isEmpty())
        tip.append(failure);
    if (const QString checked = out.value(u"checked"_s).toString(); !checked.isEmpty())
        tip.append(checked);
    out.insert(u"tooltip"_s, tip.join(u'\n'));
    return out;
}

QVariantMap AgentLimits::state(const QString &partner, int) const
{
    const Account a = account(partner);
    if (!a.valid())
        return {{u"applies"_s, false}, {u"windows"_s, QVariantList()}};
    Limits none;
    none.provider = a.provider;
    QVariantMap out = describe(m_reader ? m_reader->limits(a) : none, now(), a.file);
    out.insert(u"applies"_s, true);
    out.insert(u"busy"_s, m_reader && m_reader->busy(a));
    return out;
}

} // namespace brulion::shell

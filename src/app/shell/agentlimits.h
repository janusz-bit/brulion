#pragma once
// AgentLimits — the subscription limits of the AI agents of this computer that are Claude Code or
// Codex (docs/UI.md "AI agents (ACP)", "Its limits"; QML: Workspace.chats.limits): the chat's chip
// beside the session's context usage ("5 h: 12 % · week: 68 %", ChatAgentLimits.qml) and the agent's
// row in Settings → Extensions → AI agents. The agent is recognized by its command (core
// agentlimits::providerOf(): the presets, claude-agent-acp, codex-acp …) or what its program said it
// is; its account is its sign-in file (CLAUDE_CONFIG_DIR / CODEX_HOME as its command or the app's
// environment sets them, else ~/.claude, ~/.codex), read when the limits are read — the token only
// in memory for that request, sent only to the provider's own host over https (core
// agentlimits::LimitsReader: at most every five minutes per account, refresh() at once).
//
// Not for an agent on another computer (its sign-in is that computer's) nor where no programs run
// (Android, the browser): applies() is false there.
//
//   Workspace.chats.limits.show("acp:claude")                // read when older than five minutes
//   Workspace.chats.limits.state("acp:claude", serial)       // {summary, level, windows, failure …}
//
// Single-threaded (the GUI thread).

#include "agentlimits/reader.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion::sync {
class HttpTransport;
}

namespace brulion::shell {

class WorkspaceAgents;
struct AgentConfig;

class AgentLimits : public QObject
{
    Q_OBJECT
    /// Changes whenever limits are read (state() asks again).
    Q_PROPERTY(int serial READ serial NOTIFY changed FINAL)

public:
    explicit AgentLimits(WorkspaceAgents *agents, QObject *parent = nullptr);
    ~AgentLimits() override;

    int serial() const { return m_serial; }

    /// The account of `agent` whose limits are shown ({} none: not Claude Code or Codex, on another
    /// computer). `programName`: what its program said it is ("" not started); `environment` and
    /// `home` as the app has them.
    static agentlimits::Account accountOf(const AgentConfig &agent, const QString &programName,
                                          const QHash<QString, QString> &environment, const QString &home);

    /// The partner ("acp:<id>") is an agent of this computer whose limits can be shown (`serial`:
    /// serial, so a binding asks again).
    Q_INVOKABLE bool applies(const QString &partner, int serial = 0) const;
    /// The agents set up changed (WorkspaceChats::setAgents()): applies() and state() ask again.
    void agentsChanged();
    /// It is shown: its limits are read when the last reading is older than five minutes.
    Q_INVOKABLE void show(const QString &partner);
    /// Reads them now (the menu's Refresh).
    Q_INVOKABLE void refresh(const QString &partner);
    /// What to show: {applies, busy, read (ever read), summary ("5 h: 12 % · week: 68 %"), level ("",
    /// "warning" above 80 %, "danger" at 100 %), provider ("Claude Code", "Codex"), plan, checked
    /// ("Checked at 14:30"), failure (what to do, "" none) and failureShort ("Limits: offline"),
    /// windows: [{label, text ("12 % · resets at 18:40"), level}], tooltip (all of it, a line
    /// each)}. `serial`: serial, so a binding asks again.
    Q_INVOKABLE QVariantMap state(const QString &partner, int serial = 0) const;

    /// The texts of `limits` as state() gives them, at `now` (local time) — tests.
    static QVariantMap describe(const agentlimits::Limits &limits, const QDateTime &now, const QString &file = {});
    /// When a window resets, in local time: "resets at 18:40" today, "resets Mon 09:00" within a
    /// week, "resets 2 Dec 09:00" later; "" not known.
    static QString resetText(const QDateTime &resetsAt, const QDateTime &now);

    /// Tests: the transport (default one over the network of its own), the clock, the environment
    /// and home folder, how sign-in files are read.
    void setTransport(sync::HttpTransport *transport);
    void setClock(std::function<QDateTime()> clock);
    void setEnvironment(const QHash<QString, QString> &environment, const QString &home);
    void setCredentialsReader(agentlimits::LimitsReader::CredentialsReader read);

Q_SIGNALS:
    void changed();

private:
    agentlimits::Account account(const QString &partner) const;
    agentlimits::LimitsReader *reader();
    QDateTime now() const;

    WorkspaceAgents *m_agents = nullptr;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::HttpTransport> m_ownTransport;
    std::unique_ptr<agentlimits::LimitsReader> m_reader;
    std::function<QDateTime()> m_clock;
    agentlimits::LimitsReader::CredentialsReader m_read;
    QHash<QString, QString> m_environment;
    QString m_home;
    int m_serial = 0;
};

} // namespace brulion::shell

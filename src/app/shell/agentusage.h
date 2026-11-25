#pragma once
// AgentUsage — what the AI agents of this computer that are Claude Code or Codex used, read from
// their own session logs (docs/UI.md "AI agents (ACP)", "Its usage"; QML: Workspace.chats.usage):
// tokens by kind, requests and what they would cost on the API, per day, model and project folder
// (core agentusage: the logs, the price table, the incremental cache in the app's cache folder,
// UsageReader on a thread of its own). Shown on the agent's row in Settings → Extensions → AI
// agents ("Today 1.2 M tokens · ≈ 4.10 USD · 30 days ≈ 61 USD"), in its Usage… dialog
// (AgentUsageDialog.qml: the days as bars, a table per model, this notebook or all projects, 7 / 30
// / 90 days) and in the chat's limits pill's tooltip (today). The agent is recognized as for its
// limits (agentlimits::providerOf()); its logs are where its command or the app's environment put
// CLAUDE_CONFIG_DIR / CODEX_HOME, else ~/.claude/projects (and ~/.config/claude/projects) and
// ~/.codex/sessions. Only local files are read; nothing is sent anywhere.
//
// Read when shown, at most every five minutes per log source (Refresh at once), never on the GUI
// thread. Not for an agent on another computer (its logs are that computer's) nor where no programs
// run (Android, the browser): applies() is false there.
//
//   Workspace.chats.usage.show("acp:claude")                    // read when older than five minutes
//   Workspace.chats.usage.state("acp:claude", serial)           // {summary, today, checked …}
//   Workspace.chats.usage.details("acp:claude", 30, false, serial) // the dialog's bars and models
//
// Single-threaded (the GUI thread).

#include "agentusage/reader.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimeZone>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion::shell {

class Workspace;
class WorkspaceAgents;
struct AgentConfig;

class AgentUsage : public QObject {
    Q_OBJECT
    /// Changes whenever a history is read (state() and details() ask again).
    Q_PROPERTY(int serial READ serial NOTIFY changed FINAL)

public:
    /// `workspace`: its notebook folder is "this notebook" (nullptr: none).
    AgentUsage(Workspace *workspace, WorkspaceAgents *agents, QObject *parent = nullptr);
    ~AgentUsage() override;

    int serial() const { return m_serial; }

    /// The logs of `agent` ({} none: not Claude Code or Codex, on another computer).
    static agentusage::Source sourceOf(const AgentConfig &agent, const QString &programName,
                                       const QHash<QString, QString> &environment, const QString &home);

    /// The partner ("acp:<id>") is an agent of this computer whose usage can be shown.
    Q_INVOKABLE bool applies(const QString &partner, int serial = 0) const;
    /// The agents set up changed (WorkspaceChats::setAgents()).
    void agentsChanged();
    /// It is shown: its logs are read when the last reading is older than five minutes.
    Q_INVOKABLE void show(const QString &partner);
    /// Reads them now (Refresh).
    Q_INVOKABLE void refresh(const QString &partner);
    /// The row's and the pill's texts: {applies, busy, read, provider, summary ("Today 1.2 M tokens ·
    /// ≈ 4.10 USD · 30 days ≈ 61 USD"), today ("Today: 1.2 M tokens · ≈ 4.10 USD at API prices"),
    /// checked ("Read at 14:30"), failure ("" none)}.
    Q_INVOKABLE QVariantMap state(const QString &partner, int serial = 0) const;
    /// The dialog: {applies, busy, read, provider, checked, failure, total, bars [{day, label, mark,
    /// tip, tokens, cost, today}], maxTokens, maxCost, models [{name, cost, kinds, requests}], empty,
    /// emptyText, note, unpriced, folder} of the last `days` days, of this notebook only or all.
    Q_INVOKABLE QVariantMap details(const QString &partner, int days, bool notebookOnly, int serial = 0) const;

    /// The texts of `history` as state() gives them, `today` the local date — tests.
    static QVariantMap describe(const agentusage::History &history, const QDate &today);
    /// The texts of `history` as details() gives them — tests.
    static QVariantMap describeDetails(const agentusage::History &history, const QDate &today, int days,
                                       const QString &folder);
    /// "950", "1.2 K", "12 K", "1.2 M", "3.4 B" (the interface's language).
    static QString tokensText(qint64 tokens);
    /// "1.2 M tokens", "1 token", "950 tokens".
    static QString tokensWords(qint64 tokens);
    /// "≈ 4.10 USD", "≈ 61 USD", "< 0.01 USD", "≥ 4.10 USD" (some models without a price), "cost not
    /// known" (none priced).
    static QString costText(const agentusage::Totals &totals);

    /// Tests: the cache folder (default: the app's cache), the clock, the environment and home folder,
    /// the days' time zone.
    void setCacheDir(const QString &dir);
    void setClock(std::function<QDateTime()> clock);
    void setEnvironment(const QHash<QString, QString> &environment, const QString &home);
    void setTimeZone(const QTimeZone &zone);

Q_SIGNALS:
    void changed();

private:
    agentusage::Source source(const QString &partner) const;
    agentusage::UsageReader *reader();
    QDateTime now() const;
    QDate today() const;

    Workspace *m_workspace = nullptr;
    WorkspaceAgents *m_agents = nullptr;
    std::unique_ptr<agentusage::UsageReader> m_reader;
    QString m_cacheDir;
    std::function<QDateTime()> m_clock;
    QHash<QString, QString> m_environment;
    QString m_home;
    QTimeZone m_zone = QTimeZone::LocalTime;
    int m_serial = 0;
};

} // namespace brulion::shell

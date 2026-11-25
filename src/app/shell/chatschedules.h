#pragma once
// ChatSchedules — scheduled conversations of the AI chat (docs/UI.md "Scheduled conversations";
// QML: Workspace.chats.schedules; Settings → Extensions → "Scheduled conversations"): a prompt sent
// to a partner when a rule written in words says — "codziennie o 7:30", "w poniedziałki o 9:00",
// "co 3 godziny", "jutro o 8" (core schedule/schedule.h) —, each run a new conversation or the
// next message of one conversation.
//
// Kept on this device, per notebook (setStorage(): AppController keeps them in AppSettings under
// the notebook's id) — never in the notebook, which syncs from anywhere: a synced file must not
// start agents. They run while the app runs (the desktop tray keeps it running; Android while
// the app is open, with partners that answer there): checked every half minute and when the
// notebook is ready, so a run missed while the app was closed runs once at the next start, not once
// per missed occurrence. A run whose partner does not answer on this device, or whose one
// conversation is still answering, is skipped and says why (lastError).
//
// Agents and scripts manage them too (ChatControl: `brulion-cli app chat schedule …`): one an AI
// agent sets up names the agent (`by`) and its conversation (`parent`), so its runs are
// sub-conversations of it, under the limits of delegation (ChatLineage).
//
// A webhook (docs/UI.md "Scheduled conversations" → "Webhooks"; core webhook/webhook.h): a schedule
// may also be started by a request from elsewhere to its own address `<HookServer's address>/hooks/
// <token>` (256 random bits, kept with the schedule on this device). Only the user sets one up, in
// Settings (save(), newHookToken()): the remote control's schedules never show a token nor take
// one (toJson() says only `webhook: true`), and every token is registered with the event log's
// redaction. A schedule with a webhook may have no rule of time ("" in the dialog: When::Kind::None,
// "only by its webhook"). runHook() starts a run as Run now does — the schedule's message, then the
// request's body as untrusted data in a fenced block (webhook::message()) — at most one a minute,
// never while its one conversation still answers, without moving its timed rule (lastHook).
//
// Single-threaded (the GUI thread).

#include "log/redact.h"
#include "schedule/schedule.h"
#include "webhook/webhook.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <vector>

namespace brulion::shell {

class Workspace;
class WorkspaceChats;

class ChatSchedules : public QObject {
    Q_OBJECT
    /// The schedules as Settings shows them: {id, title, prompt, partner, partnerName, when (as
    /// written), rule (in words), next ("jutro 07:30", "" none), continues, enabled, by,
    /// lastRun, lastConversation, lastError, webhook (it has one), hookToken (for the dialog — QML
    /// shows it only masked; the remote control's inspect never reads a "token" property), lastHook}.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed FINAL)
    /// Schedules are kept here: a notebook of the user's is open (not the documentation).
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)

public:
    ChatSchedules(Workspace *workspace, WorkspaceChats *chats, QObject *parent = nullptr);

    /// Where the open notebook's schedules are kept (load: their JSON, save: it written); unset:
    /// none kept, none run.
    using Load = std::function<QByteArray()>;
    using Save = std::function<void(const QByteArray &)>;
    void setStorage(Load loader, Save saver);
    /// Tests: the clock (local wall-clock time; default the Workspace's, Workspace::now()).
    void setClock(std::function<QDateTime()> clock);

    bool available() const { return bool(m_load); }
    QList<schedule::Schedule> schedules() const { return m_schedules; }
    QVariantList rows() const;
    const schedule::Schedule *find(const QString &id) const;

    /// A rule as written, checked: {ok, text (the rule in words), next (its first run in words),
    /// error (why not)} — and each of them alone (QML: "" when not understood / none).
    QVariantMap check(const QString &when) const;
    Q_INVOKABLE QString whenText(const QString &when) const;
    Q_INVOKABLE QString whenNext(const QString &when) const;
    Q_INVOKABLE QString whenError(const QString &when) const;

    /// What a schedule is made of (add(), update(): the fields set).
    struct Fields {
        std::optional<QString> title;
        std::optional<QString> prompt;
        std::optional<QString> partner;
        std::optional<QString> when;
        std::optional<bool> continues;
        std::optional<bool> enabled;
        /// Its webhook's token ("" none). Only the user's dialog sets it (save()), never the
        /// remote control.
        std::optional<QString> hook{};
    };
    /// A new schedule: its id, "" with `*error` (no prompt, a rule not understood, too many, no
    /// notebook). `by` and `parent`: the AI agent that sets it up and its conversation ("" the
    /// user).
    QString add(const Fields &fields, const QString &by, const QString &parent, QString *error);
    /// Changes the fields given; false with `*error`.
    bool update(const QString &id, const Fields &fields, QString *error);
    Q_INVOKABLE QString add(const QString &title, const QString &prompt, const QString &partner, const QString &when,
                            bool continues, bool enabled);
    Q_INVOKABLE bool update(const QString &id, const QString &title, const QString &prompt, const QString &partner,
                            const QString &when, bool continues, bool enabled);
    /// The dialog's Save: a new schedule ("" `id`) or the fields of one, with its webhook's token
    /// (`hook`, "" none; newHookToken()) — and with one, `when` may be "" (only by its webhook).
    /// Its id, "" when it failed (lastError()).
    Q_INVOKABLE QString save(const QString &id, const QString &title, const QString &prompt, const QString &partner,
                             const QString &when, bool continues, bool enabled, const QString &hook);
    /// A new webhook token (the dialog's switch, "New address").
    Q_INVOKABLE QString newHookToken() const { return webhook::newToken(); }
    /// A hook's address: the webhook server's address and `/hooks/<token>`, whole (copyHookUrl())
    /// or with the token masked (shown: "http://127.0.0.1:8791/hooks/Xk3d…").
    QString hookUrl(const QString &token) const;
    Q_INVOKABLE QString maskedHookUrl(const QString &token) const;
    /// Puts a hook's whole address on the clipboard as a secret (clipboard managers keep it out of
    /// their history); false without a token.
    Q_INVOKABLE bool copyHookUrl(const QString &token) const;
    /// The rule field's words for a schedule with a webhook and no time ("only by its webhook").
    Q_INVOKABLE QString hookOnlyText() const;
    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool remove(const QString &id);
    /// Runs it now (whatever its rule; the next run is counted from now): the conversation's path,
    /// "" when it did not start (its lastError says why).
    Q_INVOKABLE QString runNow(const QString &id);
    /// Why the last add() or update() from QML failed ("" it did not).
    Q_INVOKABLE QString lastError() const { return m_lastError; }

    /// Runs the schedules that are due (the timer's, the notebook ready).
    void runDue();

    /// What a webhook's request brought (the hook server read and checked it).
    struct HookRequest {
        QString token;
        webhook::Body body;
    };
    struct HookOutcome {
        /// "started" | "unknown" (no such hook, one that is off, no notebook of the user's open) |
        /// "busy" (one a minute; its conversation still answers: retryAfterMs) | "unavailable" (it
        /// did not start: the notebook not ready, its partner does not answer here).
        QString status;
        QString conversation{}; ///< the conversation's name (started)
        qint64 retryAfterMs = 0;
    };
    /// A webhook's request (RemoteControl's chatHook): the schedule whose token it is starts.
    HookOutcome runHook(const HookRequest &request);

    /// A schedule as the remote control answers it ({id, title, prompt, partner, when, rule,
    /// next, destination, conversation, enabled, by, parent, lastRun, lastConversation, lastError}).
    QJsonObject toJson(const schedule::Schedule &s) const;

Q_SIGNALS:
    void changed();
    /// A scheduled conversation started (`path`, its title): AppController says so with Open.
    void ran(const QString &id, const QString &title, const QString &path);

private:
    void load();
    void save();
    /// Runs `s` now: its conversation's path ("" it did not start: lastError).
    QString run(schedule::Schedule &s);
    /// Sends `message` as `s`'s run (a new conversation or the next message of its one): the path,
    /// "" with s.lastError.
    QString start(schedule::Schedule &s, const QString &message);
    /// The webhooks' tokens registered with the event log's redaction (again after a change).
    void maskHooks();
    QString describe(const schedule::Schedule &s) const;
    QString nextText(const schedule::Schedule &s) const;
    dates::UiLocale locale() const;
    QString errorText(schedule::WhenError error) const;

    Workspace *m_workspace = nullptr;
    WorkspaceChats *m_chats = nullptr;
    Load m_load;
    Save m_save;
    std::function<QDateTime()> m_clock;
    QList<schedule::Schedule> m_schedules;
    QTimer m_timer;
    QString m_lastError;
    bool m_running = false; // (a run in progress: no nested runDue())
    webhook::RateLimit m_hookLimit;
    std::vector<logging::ScopedSecret> m_hookSecrets;
};

} // namespace brulion::shell

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
// Single-threaded (the GUI thread).

#include "schedule/schedule.h"

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

namespace brulion::shell {

class Workspace;
class WorkspaceChats;

class ChatSchedules : public QObject {
    Q_OBJECT
    /// The schedules as Settings shows them: {id, title, prompt, partner, partnerName, when (as
    /// written), rule (in words), next ("jutro 07:30", "" none), continues, enabled, by,
    /// lastRun, lastConversation, lastError}.
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
    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool remove(const QString &id);
    /// Runs it now (whatever its rule; the next run is counted from now): the conversation's path,
    /// "" when it did not start (its lastError says why).
    Q_INVOKABLE QString runNow(const QString &id);
    /// Why the last add() or update() from QML failed ("" it did not).
    Q_INVOKABLE QString lastError() const { return m_lastError; }

    /// Runs the schedules that are due (the timer's, the notebook ready).
    void runDue();

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
};

} // namespace brulion::shell

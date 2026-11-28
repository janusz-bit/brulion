#pragma once
// RemoteProjects — Code mode's projects of the user's other computers (docs/CODE.md "Threads on the
// phone"; QML: Workspace.projects.remote): a computer that shares its AI agents with this device
// (`brulion-cli acp serve --projects`, the desktop app's "Share agents with the phone") offers the
// projects its user added in Code mode there (GET projects, core acp/codehost.h). CodeProjects lists
// them after this device's own (CodeProjects::setRemote(): `remote: true`, the computer's name), and a
// thread of one runs its turns on that computer, through its shared agent (WorkspaceAgents): in the
// project's folder there or a worktree that computer makes for it (createWorktree()). What the last
// turn changed comes from there too (requestChanges(), requestDiff()), and its git is asked there
// (commit(), push(), createPullRequest(), revertTurn(): POST projects/<id>/commit | push | pr |
// revert — the commit's message is never empty here); the terminal stays that computer's.
//
// The computers are those of the agents paired with this device (setComputers(): an address and a
// token each, from the app's secrets). Their lists are asked for when the agents change and on
// refresh() (Code mode shown), and kept in the notebook's device state (setStorage()) so the projects
// show while the computer is not reached. A project id this device has itself, or one an earlier
// computer offers, is not listed again (CodeProjects).
//
//   Workspace.projects.remote.refresh()
//   Workspace.projects.remote.requestChanges(project, worktree, since)  // → changesReady(serial, …)
//   Workspace.projects.remote.commit(project, worktree, message, [], true, false, "", "", false)
//                                                                        // → gitDone(serial, "commit", …)
//
// Single-threaded (the GUI thread).

#include "acp/codehost.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace brulion::sync {
class HttpTransport;
}

namespace brulion::shell {

class RemoteProjects : public QObject {
    Q_OBJECT
    /// A computer is asked for its projects now.
    Q_PROPERTY(bool loading READ loading NOTIFY changed FINAL)
    /// The computers that could not be reached the last time, by name ("" every one answered).
    Q_PROPERTY(QString unreachable READ unreachable NOTIFY changed FINAL)

public:
    explicit RemoteProjects(QObject *parent = nullptr);
    ~RemoteProjects() override;

    /// A computer this device is paired with (its agents' address, a token, its name).
    struct Computer {
        QString url;
        QString token;
        QString host;
        friend bool operator==(const Computer &, const Computer &) = default;
    };
    /// A project of one of them.
    struct Project {
        QString id;
        QString name;
        QString path; ///< its folder there
        QString host; ///< the computer's name
        QString url;  ///< the computer's address (acp::normalizeRemoteBase())
        friend bool operator==(const Project &, const Project &) = default;
    };

    /// The transport to the computers (WorkspaceAgents': its own network one, a test's).
    void setTransport(std::function<sync::HttpTransport *()> transport) { m_transport = std::move(transport); }
    /// Where the last lists are kept (the notebook's device state); unset: in memory only.
    void setStorage(std::function<QByteArray()> load, std::function<void(const QByteArray &)> save);
    /// The notebook changed: the lists kept in its device state read again (when it is open).
    void reload()
    {
        m_loaded = false;
        load();
    }
    /// The computers (one per address; the first token of each). Changed: their projects asked again.
    void setComputers(const QList<Computer> &computers);
    QList<Computer> computers() const { return m_computers; }

    bool loading() const { return !m_asking.isEmpty(); }
    QString unreachable() const;
    /// Every computer's projects, in the computers' order (an id once: the first computer's).
    QList<Project> projects() const;
    std::optional<Project> find(const QString &id) const;
    /// The computer of a project ({} none).
    std::optional<Computer> computerOf(const QString &id) const;

    /// Asks every computer for its projects again.
    Q_INVOKABLE void refresh();
    /// Makes a worktree of project `id` there for a new thread `title` (from its HEAD): false when
    /// it is no project of a computer; else worktreeCreated() says how it went (CodeProjects's
    /// signal is emitted with it, as for a worktree of this device).
    bool createWorktree(const QString &id, const QString &title);
    /// The files of project `id` (its folder there, or its worktree `worktree`) changed since the
    /// checkpoint `since` ("" against its last commit): changesReady() with the request's number —
    /// [{path, oldPath, change, letter, additions, deletions, binary}] — or its error in words.
    Q_INVOKABLE int requestChanges(const QString &id, const QString &worktree, const QString &since);
    /// A file's two texts there: diffReady() with them (a binary one or one too large without).
    Q_INVOKABLE int requestDiff(const QString &id, const QString &worktree, const QString &since, const QString &path,
                                const QString &oldPath);

    /// A commit there of `files` (none: every change) with `message` (never empty), then — `push` —
    /// pushed, or — `createPr` — pushed with a pull request (`prTitle`, "" the message's first line):
    /// gitDone(serial, "commit", …) once all of it is done or one step failed (said which).
    Q_INVOKABLE int commit(const QString &id, const QString &worktree, const QString &message, const QStringList &files,
                           bool push, bool createPr, const QString &prTitle, const QString &prBody, bool draft);
    /// The branch there pushed: gitDone(serial, "push", …).
    Q_INVOKABLE int push(const QString &id, const QString &worktree);
    /// A pull request there into `base` ("" the default branch), the branch pushed first:
    /// gitDone(serial, "pr", ok, message, its address).
    Q_INVOKABLE int createPullRequest(const QString &id, const QString &worktree, const QString &title,
                                      const QString &body, const QString &base, bool draft);
    /// The files there back as the turn's checkpoint has them: gitDone(serial, "revert", …).
    Q_INVOKABLE int revertTurn(const QString &id, const QString &worktree, const QString &checkpoint);
    /// What a commit's message gives a pull request's title: its first line, trimmed.
    static QString subjectOf(const QString &message);

Q_SIGNALS:
    void changed();
    /// A git request ended: `action` "commit" | "push" | "pr" | "revert", what happened (or why
    /// not) in words, a pull request's address ("" none).
    void gitDone(int serial, const QString &action, bool ok, const QString &message, const QString &url);
    void worktreeCreated(const QString &project, const QString &folder, const QString &branch, const QString &error);
    void changesReady(int serial, const QVariantList &changes, bool truncated, const QString &error);
    void diffReady(int serial, const QString &path, const QString &oldText, const QString &newText, bool binary,
                   bool tooLarge, const QString &error);

private:
    void load();
    void save();
    void ask(const Computer &computer);
    QString errorText(const Computer &computer, const acp::RemoteError &error) const;
    /// `request` sent to project `id`'s computer: `then` with what git did there — or gitDone()
    /// with why not (`failure` "%1" before it: an earlier step's words).
    void askGit(int serial, const QString &id, const QString &action, const acp::RemoteGitRequest &request,
                const QString &failure, std::function<void(const acp::RemoteGitResult &result)> then);
    void gitFinished(int serial, const QString &action, bool ok, const QString &message, const QString &url = {});
    /// One request (its endpoint the action): gitDone() with `words` of what git did.
    int askGitOnce(const QString &id, const acp::RemoteGitRequest &request,
                   std::function<QString(const acp::RemoteGitResult &result)> words);

    std::function<sync::HttpTransport *()> m_transport;
    std::function<QByteArray()> m_load;
    std::function<void(const QByteArray &)> m_save;
    QList<Computer> m_computers;
    /// The projects of each computer (by its address), as it last said.
    QHash<QString, QList<acp::RemoteProject>> m_lists;
    QSet<QString> m_asking;
    QSet<QString> m_failed;
    bool m_loaded = false;
    int m_serial = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace brulion::shell

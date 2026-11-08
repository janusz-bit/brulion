// WorkspaceGitHub — the open notebook kept in a GitHub repository (QML: `Workspace.github`; docs/UI.md
// "Notebooks from GitHub"): a folder of a branch is the notebook — its newest version pulled when
// the notebook opens, every 15 minutes while the app is in front and on request; the notebook's
// changes listed, committed to the branch or proposed as a pull request (core module github:
// RepoSync over a GitHubClient on a NetworkHttpTransport — the same on the desktop, Android and in
// the browser). New notebooks from a repository (the start screen's "From GitHub") live in the
// app's storage, <app data>/GitHub/<owner>/<name>[ – <folder>], unless the desktop's dialog names
// another folder.
//
// Settings (AppSettings, through the accessors the shell's integration sets — workspaceintegration):
// per notebook under the id of its folder (Vault::idForPath, as its device state),
// "github/notebooks/<id>/repository" ("owner/name"), ".../branch", ".../folder", ".../path"; the
// login GitHub named for the token ("github/login"). The personal access token is kept like the
// Nextcloud app password, in an owner-only file (<app data>/secrets/github-token), and is only ever
// sent to api.github.com (masked in every log line while it is in memory).
//
// QML values (plain data):
//   linked        the open notebook is linked to a repository
//   repository    "owner/name"; branch, folder ("" = the whole repository); webUrl (the folder on
//                 github.com); linkText ("owner/name · main · folder")
//   signedIn      a token is saved; login ("" until GitHub named its user); tokenUrl (the page
//                 that makes a classic token with the `repo` scope); checkingToken
//   state         "disabled" | "idle" | "pulling" | "committing" | "proposing" | "error" | "offline"
//   busy, statusText (one translated line), progressText, progress (0…1, -1 unknown)
//   lastPullText  "today 10:32" ("" never); headText (the commit's short id), headUrl
//   pushKnown, canPush   the token may push here (commit), else only propose
//   changes       [{ path, title, kind: "added" | "modified" | "deleted", kindText, proposed }]
//   changeCount
//   proposal      { number, url, title, state: "open" | "closed" | "merged", stateText, open } ({} none)
//   lastError, lastErrorCode   of the last operation ("" when it went well)
//   lastReport    { operation: "pull" | "commit" | "propose", text, downloaded, deletedLocal, renamed, merged,
//                   kept, conflicts, skipped: [{ path, code, text }], errors: [{ path, code, text }],
//                   committed, commit, commitUrl }
#pragma once

#include "types.h"

#include <QDateTime>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class Vault;
}

namespace brulion::sync {
class HttpTransport;
class NetworkHttpTransport;
} // namespace brulion::sync

namespace brulion::github {
class GitHubClient;
class RepoSync;
struct CommitReport;
struct GitHubError;
struct PullReport;
struct RepoLink;
} // namespace brulion::github

namespace brulion::shell {

class WorkspaceGitHub : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool linked READ linked NOTIFY linkChanged FINAL)
    Q_PROPERTY(QString repository READ repository NOTIFY linkChanged FINAL)
    Q_PROPERTY(QString branch READ branch NOTIFY linkChanged FINAL)
    Q_PROPERTY(QString folder READ folder NOTIFY linkChanged FINAL)
    Q_PROPERTY(QString webUrl READ webUrl NOTIFY linkChanged FINAL)
    Q_PROPERTY(QString linkText READ linkText NOTIFY linkChanged FINAL)

    Q_PROPERTY(bool signedIn READ signedIn NOTIFY accountChanged FINAL)
    Q_PROPERTY(QString login READ login NOTIFY accountChanged FINAL)
    Q_PROPERTY(QString tokenUrl READ tokenUrl CONSTANT FINAL)
    Q_PROPERTY(bool checkingToken READ checkingToken NOTIFY checkingChanged FINAL)
    /// linkNotebook() / openRepository() asks GitHub.
    Q_PROPERTY(bool checkingLink READ checkingLink NOTIFY checkingChanged FINAL)

    Q_PROPERTY(QString state READ state NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY statusChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString lastPullText READ lastPullText NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString headText READ headText NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString headUrl READ headUrl NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool pushKnown READ pushKnown NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool canPush READ canPush NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY statusChanged FINAL)
    Q_PROPERTY(QVariantMap lastReport READ lastReport NOTIFY statusChanged FINAL)

    Q_PROPERTY(QVariantList changes READ changes NOTIFY changesChanged FINAL)
    Q_PROPERTY(int changeCount READ changeCount NOTIFY changesChanged FINAL)
    Q_PROPERTY(QVariantMap proposal READ proposal NOTIFY changesChanged FINAL)

public:
    /// Automatic pulls while the app is in front.
    static constexpr int PullIntervalMs = 15 * 60 * 1000;
    /// The first pull after a notebook opens or the link changes.
    static constexpr int StartDelayMs = 1500;
    /// The app back in front pulls when the last pull is older than this.
    static constexpr qint64 ForegroundPullAfterMs = 5 * 60 * 1000;

    explicit WorkspaceGitHub(QObject *parent = nullptr);
    ~WorkspaceGitHub() override;

    // ---- wiring -------------------------------------------------------------------------
    /// HTTP (tests: an in-memory GitHub); default: a NetworkHttpTransport. Not owned; set first.
    void setTransport(sync::HttpTransport *transport);
    /// Where AppSettings keeps values (the shell's integration): read, write.
    void setSettingsAccess(std::function<QVariant(const QString &, const QVariant &)> get,
                           std::function<void(const QString &, const QVariant &)> set);
    /// The owner-only file of the token; read now.
    void setTokenFile(const QString &path);
    /// The folder of new notebooks from GitHub (<app data>/GitHub).
    void setNotebooksRoot(const QString &path);
    QString notebooksRoot() const { return m_notebooksRoot; }
    /// The browser: no User-Agent of our own (setBrowser() of the client).
    void setBrowser(bool browser);
    /// "pl" | "en" for the texts.
    void setLanguage(const QString &language);
    void setClock(std::function<QDateTime()> clock);
    /// Automatic pulls (default on; tests turn them off).
    void setAutomatic(bool on);
    void attach(Vault *vault);
    void detach();
    void setAppActive(bool active);
    github::RepoSync *repoSync() const { return m_repo.get(); }

    // ---- state --------------------------------------------------------------------------
    bool linked() const;
    QString repository() const;
    QString branch() const;
    QString folder() const;
    QString webUrl() const;
    QString linkText() const;
    bool signedIn() const { return !m_token.isEmpty(); }
    QString login() const;
    QString tokenUrl() const;
    bool checkingToken() const { return m_checkingToken; }
    bool checkingLink() const { return m_checkingLink; }
    QString state() const;
    bool busy() const;
    QString statusText() const;
    QString progressText() const;
    qreal progress() const;
    QString lastPullText() const;
    QString headText() const;
    QString headUrl() const;
    bool pushKnown() const;
    bool canPush() const;
    QString lastError() const { return m_lastError; }
    QString lastErrorCode() const { return m_lastErrorCode; }
    QVariantMap lastReport() const { return m_lastReport; }
    QVariantList changes() const { return m_changes; }
    int changeCount() const { return int(m_changes.size()); }
    QVariantMap proposal() const;
    /// { state: "idle" | "syncing" | "offline" | "error", text, icon } for the sync seam (the status
    /// footer) of a notebook kept on GitHub without Nextcloud; {} when not linked.
    QVariantMap appStatus() const;

    // ---- actions ------------------------------------------------------------------------
    /// The newest version of the branch's folder.
    Q_INVOKABLE void pull();
    /// Commit every change but `leaveOut` (the notes unticked in the dialog) with `message`.
    Q_INVOKABLE void commit(const QString &message, const QStringList &leaveOut = {});
    /// Propose every change but `leaveOut` as a pull request (or add it to the open one, which
    /// keeps every note it has).
    Q_INVOKABLE void propose(const QString &title, const QString &body, const QStringList &leaveOut = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void refreshChanges();
    Q_INVOKABLE void refreshProposal();
    /// Forget the proposal shown (a merged or closed one).
    Q_INVOKABLE void forgetProposal();
    /// What an address names, for the forms: { valid, owner, name, fullName, branch (the first
    /// segment after tree/ — GitHub tells which is the branch), folder, file, error } — no network.
    Q_INVOKABLE QVariantMap parseAddress(const QString &address) const;
    /// Link the open notebook (checked with GitHub first): linkFinished().
    Q_INVOKABLE void linkNotebook(const QString &address, const QString &branch, const QString &folder);
    /// The open notebook is no notebook from GitHub any more (its files stay).
    Q_INVOKABLE void unlink();
    /// Check `token` with GitHub, then keep it: tokenChecked().
    Q_INVOKABLE void saveToken(const QString &token);
    Q_INVOKABLE void forgetToken();
    /// A repository as a new notebook (checked with GitHub first): its folder is linked, then
    /// notebookPrepared(path) — the caller opens it (AppController.openVaultAt). `localFolder`: ""
    /// = suggestedFolder().
    Q_INVOKABLE void openRepository(const QString &address, const QString &branch, const QString &folder,
                                    const QString &localFolder = {});
    /// The folder a new notebook from `address` gets ("" for an invalid address).
    Q_INVOKABLE QString suggestedFolder(const QString &address, const QString &folder = {}) const;
    /// A commit message / pull request title for `count` changes in the UI language.
    Q_INVOKABLE QString defaultCommitMessage(int count) const;
    Q_INVOKABLE QString defaultProposalTitle(int count) const;
    /// Translated texts (also the tests'): an error or refusal code, a file problem's code.
    QString errorText(const QString &code, const QString &message = {}, qint64 resetAt = 0, int retryAfter = -1) const;
    QString problemText(const QString &code) const;

    /// The settings key of a notebook's value (`name`: "repository", "branch", "folder", "path").
    static QString notebookKey(const QString &notebookPath, const QString &name);

Q_SIGNALS:
    void linkChanged();
    void accountChanged();
    void checkingChanged();
    void statusChanged();
    void changesChanged();
    void linkFinished(bool ok, const QString &message);
    void tokenChecked(bool ok, const QString &message);
    void notebookPrepared(const QString &path);
    void prepareFailed(const QString &message);
    /// An operation ended: `operation` "pull" | "commit" | "propose", a translated summary
    /// (`message`, "" for a pull that changed nothing) and its tone ("success" | "warning" | "error").
    void operationFinished(const QString &operation, bool ok, const QString &message, const QString &tone);

private:
    using Resolved = std::function<void(bool ok, const github::RepoLink &link, const QString &message, bool empty)>;
    sync::HttpTransport *transport();
    github::GitHubClient *client();
    QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    void setSetting(const QString &key, const QVariant &value);
    std::optional<github::RepoLink> storedLink(const QString &notebookPath) const;
    void storeLink(const QString &notebookPath, const std::optional<github::RepoLink> &link);
    void rebuild();
    void resolveLink(const QString &address, const QString &branch, const QString &folder, Resolved done);
    QString linkErrorText(const github::GitHubError &error) const;
    void startPull(bool automatic);
    /// The changes to send ({} = every one); `none`: everything was left out.
    QStringList selectedPaths(const QStringList &leaveOut, bool *none) const;
    void onPulled(const github::PullReport &report, bool automatic);
    void onCommitted(const github::CommitReport &report, bool proposal);
    void setError(const QString &code, const QString &text);
    void scheduleChanges();
    QString human(const QDateTime &time) const;
    QString kindText(const QString &kind) const;
    QVariantList problems(const QList<QString> &paths, const QList<QString> &codes,
                          const QList<QString> &messages) const;
    QDateTime now() const;

    QPointer<Vault> m_vault;
    QMetaObject::Connection m_vaultConnection;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::NetworkHttpTransport> m_ownTransport;
    std::unique_ptr<github::GitHubClient> m_client;
    std::unique_ptr<github::RepoSync> m_repo;
    std::function<QVariant(const QString &, const QVariant &)> m_get;
    std::function<void(const QString &, const QVariant &)> m_set;
    std::function<QDateTime()> m_clock;
    QString m_tokenFile;
    QString m_token;
    QString m_notebooksRoot;
    QString m_language = QStringLiteral("pl");
    bool m_browser = false;
    bool m_automatic = true;
    bool m_appActive = true;
    bool m_checkingToken = false;
    bool m_checkingLink = false;
    bool m_offline = false;
    QString m_operation; ///< "pull" | "commit" | "propose" while one runs
    QString m_phase;
    int m_done = 0;
    int m_total = 0;
    QString m_lastError;
    QString m_lastErrorCode;
    QVariantMap m_lastReport;
    QVariantList m_changes;
    QTimer m_pullTimer;
    QTimer m_startTimer;
    QTimer m_changesTimer;
};

} // namespace brulion::shell

#pragma once
// CodeProjects — the projects of Code mode (docs/CODE.md; QML: Workspace.projects): folders on this
// device — a git repository usually — that AI agents work in, as in T3 Code. Kept in the device's
// settings (AppSettings "code/projects", JSON), never in the notebook: a path is this device's, and
// a synced file must never point an agent at a folder. A thread of a project is a conversation of
// the AI chat whose page names it (`project: <id>`, chat::Conversation::project) — the agent's
// session runs in the project's folder, or in the git worktree the thread has of its own.
//
//   Workspace.projects.list / add(folder) / remove(id) / rename(id, name) / move(id, index)
//   Workspace.projects.setScripts(id, [{name, command, icon, shortcut, runOnWorktreeCreate, wait}])
//   Workspace.projects.scripts(id)          // this device's scripts and the project's t3.json's, merged
//   Workspace.projects.setPinned / setIcon / setDefaults(id, agent, env)
//   Workspace.projects.createWorktree(id, title, base) / listBranches(folder) / switchBranch …
//   Workspace.projects.editors / openInEditor(path[, line]) / openWith(editor, path, line)
//   Workspace.projects.cloneRepository(source, folder) / clones / cancelClone / retryClone …
//
// The files: codeprojects.cpp (the list, settings, scripts), codeconfig.cpp (t3.json),
// codeworktrees.cpp (git and gh: worktrees, branches, pull requests), codeeditors.cpp ("Open in…"),
// codeclones.cpp (a project cloned from GitHub or any git address, as T3 Code's "Clone a repository").
//
// Cloning (docs/CODE.md "Cloning a repository"): what the user typed — "owner/name", an https or
// ssh address, a repository's page — is read by gitcli::parseCloneSource(); the clone runs in the
// background (`git clone --progress`, listed in `clones` with its stage and percent while it runs,
// with its error when it failed: Retry, Dismiss); a clone that cannot sign in is tried once more
// over the other protocol (ssh ↔ https); done, its folder is added as a project (cloned()). The
// user's repositories on GitHub come from `gh repo list` (listRepositories()) where gh is signed in.
//
// t3.json (T3 Code's project file, docs/CODE.md "t3.json"): read from the project's root when the
// project is added or loaded and again when it changes (a file system watcher), never written.
// Its scripts are data: a command runs only when the user clicks it, runs it from the palette or
// its shortcut, or makes a worktree with it set to run there.
//
// The projects of the user's other computers (RemoteProjects, `remote`: a computer that shares its
// AI agents with this device) are listed after this device's own, read-only — {remote: true, host,
// remotePath}, no folder here (folderOf() ""): their threads run on that computer, a new worktree is
// made there (createWorktree()). A project id of this device's own hides one of a computer.
//
// Single-threaded (the GUI thread).

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "gitcli/gitcli.h"

#include <functional>

namespace brulion::gitcli {
class Client;
struct Outcome;
}

namespace brulion::shell {

class RemoteProjects;

class CodeProjects : public QObject {
    Q_OBJECT
    Q_MOC_INCLUDE("remoteprojects.h")
    /// The projects, pinned ones first, each part in the user's order: [{id, name, path, exists,
    /// pinned, icon (an emoji, "" none), iconUrl (t3.json's iconPath as a file: URL, "" none), agent,
    /// env ("" | "local" | "worktree": the project's default for new threads), newThreadEnv (the one
    /// a new thread gets: newThreadEnv()), scripts (scripts()), config (a t3.json was read),
    /// configError (why it was not, "" fine)}], then those of other computers: {id, name, path "",
    /// exists false, scripts [], remote true, host, remotePath}.
    Q_PROPERTY(QVariantList list READ list NOTIFY changed FINAL)
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
    /// Where new worktrees go: <worktreesRoot>/<project id>/<thread> ("" the default:
    /// ~/.brulion/worktrees). A device setting.
    Q_PROPERTY(QString worktreesRoot READ worktreesRoot WRITE setWorktreesRoot NOTIFY changed FINAL)
    /// A custom editor command (the "Custom command" of "Open in…"), with the path as its last
    /// argument ("" none). A device setting: `code -n`, `emacsclient -c` …
    Q_PROPERTY(QString editorCommand READ editorCommand WRITE setEditorCommand NOTIFY changed FINAL)
    /// The projects of the user's other computers (null: none here).
    Q_PROPERTY(brulion::shell::RemoteProjects *remote READ remote CONSTANT FINAL)
    /// The editors "Open in…" offers (editors()): those of this computer, the custom command, the
    /// file manager.
    Q_PROPERTY(QVariantList editors READ editors NOTIFY editorsChanged FINAL)
    /// The one Ctrl+O and the header's button use (an id of editors(); "" none chosen: the first
    /// one found, else the custom command, else the file manager). A device setting.
    Q_PROPERTY(QString preferredEditor READ preferredEditor WRITE setPreferredEditor NOTIFY editorsChanged FINAL)
    /// The editor preferredEditor gives now (never "").
    Q_PROPERTY(QString effectiveEditor READ effectiveEditor NOTIFY editorsChanged FINAL)
    /// The start of a new worktree's branch ("brulion/" by default; "" none). A device setting.
    Q_PROPERTY(QString branchPrefix READ branchPrefix WRITE setBranchPrefix NOTIFY changed FINAL)
    /// What happens to a worktree of its own when its thread is deleted and no other thread uses it:
    /// "ask" (the default), "delete", "keep". A device setting.
    Q_PROPERTY(QString deletedWorktrees READ deletedWorktrees WRITE setDeletedWorktrees NOTIFY changed FINAL)
    /// Where new threads work unless their project says otherwise: "" (as the project's t3.json says,
    /// else Local), "local", "worktree". A device setting.
    Q_PROPERTY(QString newThreadEnvDefault READ newThreadEnvDefault WRITE setNewThreadEnvDefault NOTIFY changed FINAL)
    /// How the sidebar orders a project's threads: "activity" (what needs the user, then the last
    /// change: the default), "created" (the newest thread first). A device setting.
    Q_PROPERTY(QString threadSort READ threadSort WRITE setThreadSort NOTIFY changed FINAL)
    /// Where a cloned repository goes by default: <clonesRoot>/<its name> ("" the default:
    /// ~/.brulion/projects). A device setting.
    Q_PROPERTY(QString clonesRoot READ clonesRoot WRITE setClonesRoot NOTIFY changed FINAL)
    /// The clones running, and those that failed until dismissed: [{folder (the key), source (as
    /// typed), url (without a login), name ("owner/name", the folder's name for a local one),
    /// phase ("running" | "failed"), stage (gitcli::cloneStageName()), percent (-1 not said), detail,
    /// status (the stage, percent and detail in words), error ("" none)}], oldest first.
    Q_PROPERTY(QVariantList clones READ clones NOTIFY clonesChanged FINAL)
    /// git and gh are on this computer (branches, worktrees, pull requests).
    Q_PROPERTY(bool gitAvailable READ gitAvailable CONSTANT FINAL)
    Q_PROPERTY(bool ghAvailable READ ghAvailable CONSTANT FINAL)

public:
    explicit CodeProjects(QObject *parent = nullptr);
    ~CodeProjects() override;

    /// A script ("action" in T3 Code): a button over the thread that runs `command` in a terminal of
    /// the thread's folder.
    struct Script {
        QString name;
        QString command;
        /// T3 Code's icons: "play" (the default), "test", "lint", "configure", "build", "debug".
        QString icon;
        /// Its keyboard shortcut in Code mode (portable text, "" none).
        QString shortcut;
        /// Runs in the thread's terminal when a new worktree is made for a thread (T3 Code's
        /// runOnWorktreeCreate) …
        bool onWorktree = false;
        /// … and the thread's first message waits for it to end (T3 Code's `async: false`).
        bool wait = false;
        bool operator==(const Script &) const = default;
    };
    struct Project {
        QString id;
        QString name;
        QString path;
        /// This device's scripts (a t3.json's come on top: scripts()).
        QList<Script> scripts;
        bool pinned = false;
        /// An emoji of the user's ("" none).
        QString icon;
        /// The partner a new thread of it gets ("" the default) and where it works ("" the
        /// device's default, "local", "worktree").
        QString agent;
        QString env;
        bool operator==(const Project &) const = default;
    };
    /// A project's t3.json as read (parseConfig()).
    struct Config {
        /// The file was there.
        bool found = false;
        /// Why it was not used ("" fine): not JSON, not an object, too large.
        QString error;
        /// What was left out and why (an entry without a name, a third "Test", the 51st script …).
        QStringList warnings;
        QList<Script> scripts;
        /// "local", "worktree" or "" (none, or anything else).
        QString defaultThreadEnvMode;
        /// iconPath as an absolute path inside the project ("" none, or one that leaves it, is
        /// missing or no picture).
        QString iconPath;
        bool operator==(const Config &) const = default;
    };

    /// How many projects a device keeps at most, and scripts a project (T3 Code's limit).
    static constexpr int MaxProjects = 100;
    static constexpr int MaxScripts = 50;
    /// The largest t3.json read.
    static constexpr qsizetype MaxConfigBytes = 256 * 1024;

    /// Where the list is kept: `load` gives the JSON, `save` keeps it (AppController: the device's
    /// settings). Unset: nothing is kept.
    void setStorage(std::function<QByteArray()> load, std::function<void(const QByteArray &)> save);
    /// What brings pages back from the trash (Workspace::restoreAll): a thread's Undo.
    void setRestorer(std::function<void(const QStringList &)> restore) { m_restore = std::move(restore); }

    QVariantList list() const;
    int count() const;
    /// This device's projects, in the user's order (pinned ones not first).
    QList<Project> projects() const { return m_projects; }

    /// The projects of other computers listed after this device's (set once, by WorkspaceAgents).
    void setRemote(RemoteProjects *remote);
    RemoteProjects *remote() const { return m_remote; }
    /// The project `id` is another computer's (not this device's).
    Q_INVOKABLE bool isRemote(const QString &id) const;

    /// Adds the folder `folder` (a path or a file: URL) as a project: its id (that of the project
    /// it is already, "" for a folder that is not there).
    Q_INVOKABLE QString add(const QString &folder);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    /// Moves the project `id` to `index` of list() (within the pinned ones or the others: a drag in
    /// the sidebar).
    Q_INVOKABLE void move(const QString &id, int index);
    Q_INVOKABLE void setPinned(const QString &id, bool pinned);
    /// Its emoji ("" none): the first character (a grapheme) of `icon`.
    Q_INVOKABLE void setIcon(const QString &id, const QString &icon);
    /// Its defaults for new threads: the agent ("" the device's) and where they work ("",
    /// "local", "worktree").
    Q_INVOKABLE void setDefaults(const QString &id, const QString &agent, const QString &env);
    /// Where a new thread of `id` works by default: the project's own choice, else the device's
    /// (newThreadEnvDefault), else its t3.json's defaultThreadEnvMode, else "local".
    Q_INVOKABLE QString newThreadEnv(const QString &id) const;

    /// This device's scripts of `id`: [{name, command, icon, shortcut, runOnWorktreeCreate, wait}]
    /// (empty ones left out, a name given to a nameless one, at most MaxScripts, a second one of a
    /// name left out).
    Q_INVOKABLE void setScripts(const QString &id, const QVariantList &scripts);
    /// Its scripts as shown and run: this device's in their order, then those of its t3.json no
    /// script of the device's overrides (one of the same name, letter case aside):
    /// [{id (a slug of the name, unique), name, command, icon, iconName (Brulion's icon),
    /// shortcut, runOnWorktreeCreate, wait, source ("device" | "t3json"), overrides (a device's one
    /// that replaces one of the file)}].
    Q_INVOKABLE QVariantList scripts(const QString &id) const;
    /// The scripts that run when a worktree of `id` is made (scripts() with runOnWorktreeCreate).
    Q_INVOKABLE QVariantList setupScripts(const QString &id) const;
    /// One field ("name", "command") of the setup scripts of `id` that are / are not waited for
    /// (`waited`: async false) — what CodeView runs and waits for.
    Q_INVOKABLE QStringList setupList(const QString &id, const QString &field, bool waited) const;
    /// How many scripts of `id` run when a worktree is made.
    Q_INVOKABLE int setupCount(const QString &id) const { return int(setupScripts(id).size()); }
    /// A field ("name", "command" …) of the script `scriptId` of `id` ("" none).
    Q_INVOKABLE QString scriptField(const QString &id, const QString &scriptId, const QString &field) const;
    /// scripts() merged from these two lists (tests).
    static QList<std::pair<Script, QString>> mergeScripts(const QList<Script> &device, const QList<Script> &file);
    /// Brulion's icon of a T3 Code icon name ("play" for an unknown one).
    static QString iconOf(const QString &t3Icon);
    Q_INVOKABLE QString scriptIcon(const QString &t3Icon) const { return iconOf(t3Icon); }

    /// {id, name, path, exists, …} of `id` (list()'s entry; {} none).
    Q_INVOKABLE QVariantMap project(const QString &id) const;
    /// Its folder ("" none) / its name ("" none).
    Q_INVOKABLE QString folderOf(const QString &id) const;
    Q_INVOKABLE QString nameOf(const QString &id) const;
    Q_INVOKABLE bool contains(const QString &id) const;
    /// The project whose folder `folder` is, or one of whose worktrees it is under worktreesRoot ("" none).
    Q_INVOKABLE QString projectOfFolder(const QString &folder) const;

    // ---- t3.json (codeconfig.cpp) -------------------------------------------------------------------
    /// The t3.json of `data` (the file's bytes) of the project in `folder` (iconPath is checked
    /// against it): pure, no other file read but the icon's existence.
    static Config parseConfig(const QByteArray &data, const QString &folder);
    /// What was read of the project's t3.json ({} none read).
    Config config(const QString &id) const { return m_configs.value(id); }
    /// Reads the project's t3.json again now (the watcher does it by itself).
    Q_INVOKABLE void reloadConfig(const QString &id);

    QString worktreesRoot() const;
    void setWorktreesRoot(const QString &root);
    QString editorCommand() const { return m_editor; }
    void setEditorCommand(const QString &command);
    QString branchPrefix() const { return m_branchPrefix; }
    void setBranchPrefix(const QString &prefix);
    QString deletedWorktrees() const { return m_deletedWorktrees; }
    void setDeletedWorktrees(const QString &policy);
    QString newThreadEnvDefault() const { return m_newThreadEnv; }
    void setNewThreadEnvDefault(const QString &env);
    QString threadSort() const { return m_threadSort; }
    void setThreadSort(const QString &sort);

    // ---- worktrees, branches, pull requests (codeworktrees.cpp) ------------------------------------
    bool gitAvailable() const;
    bool ghAvailable() const;
    /// The git and gh programs (tests: a fake gh; "" none).
    void setPrograms(const QString &git, const QString &gh);
    /// The folder and branch of a new worktree for a thread `title` of project `id`:
    /// {folder: <worktreesRoot>/<id>/<slug>, branch: <branchPrefix><slug>} — a free folder name.
    Q_INVOKABLE QVariantMap worktreeFor(const QString &id, const QString &title) const;
    /// Makes a git worktree of project `id` for a new thread `title` (its first message): a new
    /// branch (worktreeFor(), one no branch has yet; named by the AI first when the branch namer is
    /// set — worktreeName()) from `base` ("" the project's HEAD; a remote-tracking branch such as
    /// "origin/main" is fetched first) — without waiting: worktreeCreated() says how it went. False
    /// when it could not start (no such project, no git). Another computer's project: made there
    /// (from its HEAD, named as that computer names it).
    Q_INVOKABLE bool createWorktree(const QString &id, const QString &title, const QString &base = QString());
    /// What asks the AI for a new worktree's branch name (T3 Code's AI-made name; Workspace: the AI
    /// of Settings → Extensions → AI, codeai::branchName): `namer(message, done)` calls `done` once
    /// with the AI's answer ("" none: no AI set up, a failure) — at once or later — and gives back
    /// what stops the question (or nothing). Unset: the message's slug at once.
    using BranchNamer = std::function<std::function<void()>(const QString &message,
                                                             std::function<void(const QString &answer)> done)>;
    void setBranchNamer(BranchNamer namer) { m_namer = std::move(namer); }
    /// How long createWorktree() waits for the AI's name before it stops the question and takes the
    /// message's slug (T3 Code's 20 seconds).
    static constexpr int BranchNameTimeoutMs = 20000;
    void setBranchNameTimeout(int ms) { m_namerTimeoutMs = ms; }
    /// A new worktree's name — its folder's, and its branch's after `prefix` — from the AI's
    /// `answer` (codeai::readBranchName, then slug()) when it gives one that `prefix` + it is a valid
    /// branch name with (gitcli::isValidBranchName); else the slug of the thread's `title`, the name
    /// without the AI.
    static QString worktreeName(const QString &answer, const QString &title, const QString &prefix);
    /// The project's worktrees but its own folder: worktreesListed(id, [{folder, branch, detached}]).
    Q_INVOKABLE bool listWorktrees(const QString &id);
    /// The branches of the repository of `folder`: branchesListed(folder, [{name, remote, current,
    /// isDefault, worktree (the folder that has it checked out, "" none), upstream, ahead, behind}],
    /// error) — local ones first, each part most recently committed first.
    Q_INVOKABLE bool listBranches(const QString &folder);
    /// `git switch <name>` in `folder` (a remote-tracking one: a local branch tracking it) /
    /// `git switch -c <name>` there: branchSwitched(folder, name, ok, message).
    /// A row of `branches` is named `name` exactly (a local one, or "origin/<name>" for a remote one).
    Q_INVOKABLE bool hasBranch(const QVariantList &branches, const QString &name) const;
    /// The branch picker's rows (BPalette items) of `branches` (branchesListed()): {title (its name),
    /// subtitle (its badges in words: current, default, remote, the worktree that has it — "in the
    /// project's folder" for `projectFolder`), icon,
    /// name, remote, current, worktree, kind: "branch"}.
    Q_INVOKABLE QVariantList branchItems(const QVariantList &branches, const QString &projectFolder) const;
    /// The picker's offers for what is typed: "Check out pull request #123" ({kind: "pr", number})
    /// for a pull request's number or address, "Create branch “x”" ({kind: "create", name}) for a
    /// valid name no branch has when `canCreate`; none for anything else.
    Q_INVOKABLE QVariantList branchOffers(const QString &query, const QVariantList &branches, bool canCreate) const;
    Q_INVOKABLE bool switchBranch(const QString &folder, const QString &name);
    Q_INVOKABLE bool createBranch(const QString &folder, const QString &name);
    /// A valid name for a new branch (gitcli::isValidBranchName).
    Q_INVOKABLE bool validBranchName(const QString &name) const;
    /// The pull request `text` names: "#123", "123", a URL ending in /pull/123 (… /files …) → 123;
    /// 0 none.
    static int parsePullRequest(const QString &text);
    Q_INVOKABLE int pullRequestNumber(const QString &text) const { return parsePullRequest(text); }
    /// Checks out pull request `number` of project `id`: in the project's folder (`inWorktree`
    /// false; refused with uncommitted changes) or in a new worktree — `gh pr checkout` where gh is,
    /// else `git fetch <remote> pull/<n>/head:<branchPrefix>pr-<n>`. pullRequestCheckedOut() says
    /// how it went (the worktree's folder, "" in the project's folder; its branch; the pull
    /// request's title where gh told it).
    Q_INVOKABLE bool checkoutPullRequest(const QString &id, int number, bool inWorktree);
    /// The uncommitted changes of `folder`: worktreeChecked(folder, changes, branch, error).
    Q_INVOKABLE bool checkWorktree(const QString &folder);
    /// `git worktree remove [--force] <folder>` (the project's worktree; its branch stays):
    /// worktreeRemoved(folder, ok, message).
    Q_INVOKABLE bool removeWorktree(const QString &id, const QString &folder, bool force);
    /// `git worktree add <folder> <branch>`: a removed worktree again from its branch:
    /// worktreeRestored(folder, ok, message).
    Q_INVOKABLE bool restoreWorktree(const QString &id, const QString &folder, const QString &branch);

    // ---- cloning a repository (codeclones.cpp) -------------------------------------------------------
    QString clonesRoot() const;
    void setClonesRoot(const QString &root);
    QVariantList clones() const;
    /// What `source` (as typed) names: {ok, url (what is cloned, without a login), name (the
    /// default folder's name), title ("owner/name" on GitHub, "host/path" elsewhere, the folder of a
    /// local one), error (why not ok; "" when nothing was typed)}.
    Q_INVOKABLE QVariantMap cloneSource(const QString &source) const;
    /// The default folder of a clone of `source` in `parent` (a path or a file: URL; "" clonesRoot()):
    /// <parent>/<name>, "<name>-2", "-3" … when it is taken (a folder that is not empty, a file, a
    /// clone running into it); `parent` itself when it is an empty folder of the repository's name
    /// — "" when `source` names nothing.
    Q_INVOKABLE QString cloneFolder(const QString &source, const QString &parent) const;
    /// The row of clones() of the clone into `folder` ({} none).
    Q_INVOKABLE QVariantMap cloneOf(const QString &folder) const;
    /// The folder `folder` (a path, "~/…" or a file: URL) as a clone's key: absolute, clean.
    Q_INVOKABLE QString cleanFolder(const QString &folder) const;
    /// Starts cloning `source` into `folder` (missing, or an empty folder): "" when it started —
    /// clones() lists it, cloned() says how it ended — else why not (nothing to clone, the folder
    /// is taken, a clone into it runs already, no git, at most MaxProjects projects).
    Q_INVOKABLE QString cloneRepository(const QString &source, const QString &folder);
    /// Stops the clone into `folder` (what it cloned so far is taken away; it leaves the list).
    Q_INVOKABLE void cancelClone(const QString &folder);
    /// A failed clone into `folder` started again / taken off the list.
    Q_INVOKABLE bool retryClone(const QString &folder);
    Q_INVOKABLE void dismissClone(const QString &folder);
    /// `gh repo list` of the user signed in to gh: repositoriesListed() (false: gh is not here).
    Q_INVOKABLE bool listRepositories();
    /// The repositories as last listed (none yet: empty).
    Q_INVOKABLE QVariantList knownRepositories() const { return m_repositories; }
    /// The clone picker's rows (BPalette items) of `repositories` (repositoriesListed()): {title
    /// ("owner/name"), subtitle (private, fork, archived and its description, in words), icon,
    /// source ("owner/name"), kind: "repo"}.
    Q_INVOKABLE QVariantList repositoryItems(const QVariantList &repositories) const;
    /// The picker's offer for what is typed: "Clone “x”" ({kind: "clone", source, title, subtitle:
    /// the address, icon}) when it names a repository none of `repositories` is; none else.
    Q_INVOKABLE QVariantList cloneOffers(const QString &query, const QVariantList &repositories) const;
    /// Why what is typed names no repository when it looks like an address (a '/', ':' or '@' in
    /// it): "Type the whole owner/repository name." …; "" for a search word or a good address.
    Q_INVOKABLE QString cloneProblem(const QString &query) const;

    // ---- a deleted thread's Undo -------------------------------------------------------------------
    /// The toast `toast` (AppController::toastWithAction) undoes the deletion of a thread: its pages
    /// back from the trash (`trashPaths`) and — when `removed` — its worktree made again from
    /// `branch` in `folder` (project `id`).
    Q_INVOKABLE void keepUndo(int toast, const QStringList &trashPaths, const QString &id, const QString &folder,
                              const QString &branch, bool removed);
    /// AppController's toast `toast` was pressed / is gone.
    void undo(int toast);
    void forgetUndo(int toast);

    // ---- "Open in…" (codeeditors.cpp) ----------------------------------------------------------------
    /// [{id, name, icon}]: the editors found on PATH, then "custom" (editorCommand) when set, then
    /// "files" (the file manager).
    QVariantList editors() const;
    /// Looks for the editors on PATH again.
    Q_INVOKABLE void rescanEditors();
    /// Where editors are looked for (tests; default: PATH).
    void setEditorSearchPaths(const QStringList &paths);
    QString preferredEditor() const { return m_preferredEditor; }
    void setPreferredEditor(const QString &id);
    QString effectiveEditor() const;
    /// The program and arguments that open `path` at `line` (1-based, < 1 none) with the known
    /// editor `editor` ("code", "zed", "idea" …: knownEditors()) whose program is `program`: pure.
    static QStringList editorArguments(const QString &editor, const QString &path, int line);
    /// The editors Brulion knows: {id, name, programs (the names looked for on PATH, in order)}.
    struct KnownEditor {
        QString id;
        QString name;
        QStringList programs;
    };
    static QList<KnownEditor> knownEditors();
    /// Opens `path` — a folder or a file, at `line` where the editor takes one — in the preferred
    /// editor (effectiveEditor()). False when nothing could be started.
    Q_INVOKABLE bool openInEditor(const QString &path, int line = -1) const;
    /// … in the editor `editor` (an id of editors()).
    Q_INVOKABLE bool openWith(const QString &editor, const QString &path, int line) const;
    /// Shows `folder` in the system's file manager (a file: its folder).
    Q_INVOKABLE bool openFolder(const QString &folder) const;
    /// `command` split as a shell would (quotes, backslashes), no shell run (tests).
    static QStringList splitCommand(const QString &command);
    /// `command` split as Windows programs read their command line (CommandLineToArgvW): a
    /// backslash is a backslash except before a quote — "C:\Program Files\…\Code.exe" -n.
    static QStringList splitWindowsCommand(const QString &command);
    /// cmd.exe's arguments that run the batch file `batch` with `arguments`:
    /// `/d /v:off /s /c ""<batch>" "<argument>" …"` — every one quoted, so & | < > ^ ( ) are text to
    /// cmd.exe, and trailing backslashes doubled for the program the batch file hands them to. ""
    /// when one of them holds what cmd.exe reads even inside quotes: a quote, %, a line break.
    static QString batchCommandLine(const QString &batch, const QStringList &arguments);
    /// Starts `program` with `arguments` in `workDir`, detached; Windows: a batch file through
    /// cmd.exe (batchCommandLine()). False when it did not start.
    static bool startEditor(const QString &program, const QStringList &arguments, const QString &workDir);

    /// "Mój Projekt (2)!" → "moj-projekt-2": lower-case ASCII letters, digits and dashes, at most
    /// 40 characters ("project" when nothing is left) — acp::projectSlug(), the same rule as the
    /// computer that shares its projects with the phone names them by.
    static QString slug(const QString &text);

Q_SIGNALS:
    void changed();
    void editorsChanged();
    /// createWorktree() ended: the worktree's folder and branch, or `error` ("" fine).
    void worktreeCreated(const QString &project, const QString &folder, const QString &branch, const QString &error);
    void worktreesListed(const QString &project, const QVariantList &worktrees, const QString &error);
    void branchesListed(const QString &folder, const QVariantList &branches, const QString &error);
    void branchSwitched(const QString &folder, const QString &branch, bool ok, const QString &message);
    /// checkoutPullRequest() ended: `folder` the new worktree ("" the project's own folder), or `error`.
    void pullRequestCheckedOut(const QString &project, int number, const QString &folder, const QString &branch,
                               const QString &title, const QString &error);
    /// checkWorktree(): how many files have uncommitted changes (-1 unknown: `error`), its branch.
    void worktreeChecked(const QString &folder, int changes, const QString &branch, const QString &error);
    void worktreeRemoved(const QString &folder, bool ok, const QString &message);
    void worktreeRestored(const QString &folder, bool ok, const QString &message);
    void clonesChanged();
    /// A clone into `folder` ended: `project` the new project's id, or `error` ("" fine; a
    /// stopped one: cancelled true, no error shown).
    void cloned(const QString &folder, const QString &project, const QString &error, bool cancelled);
    /// listRepositories() ended: [{nameWithOwner, description, url, sshUrl, isPrivate, isFork,
    /// isArchived, updatedAt}], or `error` (gh missing, signed out …).
    void repositoriesListed(const QVariantList &repositories, const QString &error);

private:
    QList<Project> m_projects;
    RemoteProjects *m_remote = nullptr;
    QHash<QString, Config> m_configs;
    QString m_worktreesRoot;
    QString m_editor;
    QString m_preferredEditor;
    QString m_branchPrefix = QStringLiteral("brulion/");
    QString m_deletedWorktrees = QStringLiteral("ask");
    QString m_newThreadEnv;
    QString m_threadSort = QStringLiteral("activity");
    std::function<QByteArray()> m_load;
    std::function<void(const QByteArray &)> m_save;
    std::function<void(const QStringList &)> m_restore;
    BranchNamer m_namer;
    int m_namerTimeoutMs = BranchNameTimeoutMs;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_configTimer;
    QStringList m_configDue;
    mutable gitcli::Client *m_git = nullptr;
    /// The editors found: {id, program}.
    mutable QList<std::pair<QString, QString>> m_found;
    mutable bool m_scanned = false;
    QStringList m_editorPaths;
    struct Undo {
        QStringList trash;
        QString project;
        QString folder;
        QString branch;
        bool removed = false;
    };
    QHash<int, Undo> m_undos;
    QString m_clonesRoot;
    struct Clone {
        QString folder;
        QString source;
        /// What git is given now (the other protocol on the second try).
        QString url;
        QString name;
        bool running = false;
        /// The other protocol was tried already.
        bool retried = false;
        bool cancelled = false;
        /// The folder was there (empty) before: it stays when the clone fails.
        bool existed = false;
        gitcli::CloneProgress progress;
        QString error;
    };
    QList<Clone> m_clones;
    QVariantList m_repositories;
    bool m_listing = false;
    /// clonesChanged() at most a few times a second while git reports progress.
    QTimer m_cloneTimer;

    int cloneIndex(const QString &folder) const;
    void startClone(int index);
    void cloneEnded(const QString &folder, const gitcli::Outcome &outcome);
    void cloneProgressed(const QString &folder, const gitcli::CloneProgress &progress);

    int indexOf(const QString &id) const;
    QList<Project> displayed() const;
    void load();
    void save();
    void watchConfigs();
    void readConfig(const QString &id);
    void configChanged(const QString &path);
    void scanEditors() const;
    gitcli::Client *git() const;
    /// The remote of `folder`'s repository a pull request is fetched from: "origin", else the first.
    void remoteOf(const QString &folder, std::function<void(const QString &remote, const QString &error)> done);
    /// A branch name of `base` (<branchPrefix><base>, -2, -3 …) no branch of `folder` has, and a
    /// folder name no file has in `dir`: done(branch, folderName).
    void freeName(const QString &folder, const QString &dir, const QString &base,
                  std::function<void(const QString &branch, const QString &name)> done);
    /// The new worktree's name for the thread `title`: the AI's (m_namer, at most m_namerTimeoutMs)
    /// or the title's slug — done(name) once, maybe at once.
    void nameWorktree(const QString &title, std::function<void(const QString &name)> done);
    /// createWorktree()'s part once named: the base fetched when it is a remote one, the worktree
    /// of a free branch and folder of `name` made; worktreeCreated() — `fail(error)` on failure.
    void makeWorktree(const QString &id, const QString &repo, const QString &from, const QString &name,
                      const std::function<void(const QString &error)> &fail);
    /// slug() without its fallback: "" when nothing is left.
    static QString slugOrEmpty(const QString &text);
};

} // namespace brulion::shell

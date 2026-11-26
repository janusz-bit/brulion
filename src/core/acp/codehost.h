#pragma once
// Code mode's threads on another computer (docs/CODE.md "Threads on the phone", docs/CLI.md "AI
// agents for other devices" → "Code mode's projects"): the projects a computer's user added in
// Code mode there are offered to the devices paired with its `brulion-cli acp serve`, and a thread
// of one of them runs its turns there — in the project's folder or a worktree of it, with a git
// checkpoint before each turn — while the device keeps the conversation.
//
//   computer        acp serve --projects <file>   [{id, name, path, worktreesRoot}] (the desktop app's
//                                                  agent sharing writes it from its CodeProjects)
//   device → host   GET  projects                              → {projects: [{id, name, path}]}
//                   POST projects/<id>/worktrees {title, base} → 201 {folder, branch}
//                   GET  projects/<id>/changes?worktree=&since= → {changes: [{path, oldPath, change, …}]}
//                   GET  projects/<id>/diff?worktree=&since=&path=&oldPath= → {path, oldText, newText, …}
//                   session/new | load | resume  params._meta["brulion/code"] = {project, worktree, branch}
//                                                — the host takes it out: the session's cwd is the
//                                                project's folder (or that worktree of it), its files
//                                                and terminals are answered there, its Brulion tools
//                                                have none of the notebook's
//   host → device   {"jsonrpc": "2.0", "method": "_brulion/checkpoint",
//                    "params": {sessionId, turn, checkpoint: {id, at}}}    before the turn's prompt
//                                                reaches the agent (and in the turn kept: RemoteTurn)
//
// A project id names a folder this computer's user added — never a folder the device names; a
// worktree is accepted only when it lies under the project's worktrees root (<root>/<id>/…) or `git
// worktree list` of the project lists it, compared as canonical paths (no "..", no link out).
//
// Pure (QtCore): the types, the rules, the messages; acp serve (src/cli/acphost.h) and the app
// (src/app/shell/remoteprojects.h, workspaceagents.h) carry them. The device's requests go through
// sync::HttpTransport as remote.h's do.

#include "acp/remote.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1StringView>
#include <QList>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::acp {

/// The feature a host names (GET info's `features`, a stream's answer) when it runs Code mode's
/// threads (acp serve --projects).
inline constexpr QLatin1StringView CodeFeature{"code"};
/// The key of a session's CodeSession in its request's params' `_meta`.
inline constexpr QLatin1StringView CodeMetaKey{"brulion/code"};
/// The notification of a turn's checkpoint (an extension method of the protocol).
inline constexpr QLatin1StringView CheckpointMethod{"_brulion/checkpoint"};
/// The projects a host offers at most (as a device keeps: CodeProjects::MaxProjects).
inline constexpr int MaxHostProjects = 100;
/// The longest title of a new worktree's thread, and the longest path the host takes.
inline constexpr qsizetype MaxWorktreeTitleChars = 200;
inline constexpr qsizetype MaxRemotePathChars = 4096;
/// The changes of one answer at most (more: `truncated`).
inline constexpr int MaxRemoteChanges = 1000;

/// "Mój Projekt (2)!" → "moj-projekt-2": lower-case ASCII letters, digits and dashes, Polish letters
/// without their marks, at most 40 characters ("project" when nothing is left) — the ids of
/// projects and the names of worktrees (the same rule as the app's CodeProjects::slug()).
QString projectSlug(QStringView text);
/// An id a project may have: what projectSlug() gives, with a "-<n>" after it (1–60 characters).
bool isProjectId(QStringView id);

/// A project of the computer (the server's --projects file).
struct HostProject {
    QString id;
    QString name;
    QString path;          ///< absolute, clean
    QString worktreesRoot; ///< where its new worktrees go ("" the default: defaultWorktreesRoot())
    friend bool operator==(const HostProject &, const HostProject &) = default;
};
/// The projects of the --projects file: a JSON list [{id, name, path, worktreesRoot}] (an entry
/// without a valid id or an absolute path, or of an id given before, left out; at most
/// MaxHostProjects). nullopt, with `error`: no JSON list.
std::optional<QList<HostProject>> parseHostProjects(const QByteArray &json, QString *error = nullptr);
QByteArray hostProjectsJson(const QList<HostProject> &projects);
/// ~/.brulion/worktrees (the app's default too).
QString defaultWorktreesRoot();

/// The folder and the branch of a new worktree for a thread `title` of project `id`:
/// <root>/<id>/<slug of title> (-2, -3 … while `taken` says a folder is there) and
/// "brulion/<that name>" — as the app's CodeProjects::worktreeFor().
struct WorktreePlace {
    QString folder;
    QString branch;
};
WorktreePlace worktreePlace(const QString &root, const QString &id, QStringView title,
                            const std::function<bool(const QString &folder)> &taken);

/// What a session of a thread names (its request's `_meta`).
struct CodeSession {
    QString project;  ///< the host's project id
    QString worktree; ///< an absolute folder on the host ("" the project's own)
    QString branch;   ///< the worktree's branch, as the thread's page says ("" none)
    bool isEmpty() const { return project.isEmpty(); }
    QJsonObject toJson() const;
    /// nullopt: no object, or a project id that is none, a worktree that is not an absolute path
    /// (or too long, or holds a NUL), a branch of a line break or too long.
    static std::optional<CodeSession> fromJson(const QJsonValue &value);
    friend bool operator==(const CodeSession &, const CodeSession &) = default;
};
/// `params` of a session's request with `session` in its `_meta` (other keys kept).
QJsonObject withCodeSession(QJsonObject params, const CodeSession &session);
/// The CodeSession of a session/new, session/load or session/resume message, taken out of it (its
/// `_meta` left without it, dropped when nothing is left). nullopt: none — `malformed` true when
/// there was one that did not read (taken out all the same: the host refuses the request).
std::optional<CodeSession> takeCodeSession(QJsonObject &message, bool *malformed = nullptr);

/// A checkpoint the host took before a turn of a thread (git: refs/brulion/checkpoints/<id>).
struct RemoteCheckpoint {
    QString session; ///< the agent's session
    QString turn;    ///< the turn's id (its prompt's _meta "brulion/turn"; "" none)
    QString id;      ///< the checkpoint's (gitcli::isValidCheckpointId())
    QString at;      ///< when (ISO 8601, UTC)
    friend bool operator==(const RemoteCheckpoint &, const RemoteCheckpoint &) = default;
};
/// The `_brulion/checkpoint` notification of `c` (a JSON-RPC message).
QJsonObject checkpointNotification(const RemoteCheckpoint &c);
/// The checkpoint of that notification's params; nullopt for one that is none (no session, an id
/// that is no checkpoint's).
std::optional<RemoteCheckpoint> readCheckpoint(const QJsonObject &params);

// ---- The device's side ---------------------------------------------------------------------------

/// A project the host offers (GET projects).
struct RemoteProject {
    QString id;
    QString name;
    QString path; ///< its folder on the host
    friend bool operator==(const RemoteProject &, const RemoteProject &) = default;
};
/// GET projects: the host's projects (entries that are none left out). A host without them (an
/// older one): RemoteError::Kind::Refused, status 404.
void fetchRemoteProjects(sync::HttpTransport *transport, const QUrl &base, const QString &token,
                         std::function<void(const QList<RemoteProject> &projects, const RemoteError &error)> done);

/// POST projects/<id>/worktrees {title, base}: a new worktree of the project made there for a thread
/// `title` (a new branch from `base`, "" the project's HEAD): its folder and branch there.
struct RemoteWorktree {
    QString folder;
    QString branch;
};
void createRemoteWorktree(sync::HttpTransport *transport, const QUrl &base, const QString &token,
                          const QString &project, const QString &title, const QString &from,
                          std::function<void(const RemoteWorktree &worktree, const RemoteError &error)> done);

/// One changed file of a project there (GET projects/<id>/changes).
struct RemoteChange {
    QString path;
    QString oldPath;  ///< a rename: where it came from
    QString change;   ///< gitcli::changeName(): "modified", "added", "deleted", "renamed", "untracked" …
    int additions = -1;
    int deletions = -1;
    bool binary = false;
    friend bool operator==(const RemoteChange &, const RemoteChange &) = default;
};
/// The files of the project's folder (or its worktree `worktree`) that changed since the checkpoint
/// `since` ("" against HEAD: every change not committed); `truncated`: more than MaxRemoteChanges.
void fetchRemoteChanges(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &project,
                        const QString &worktree, const QString &since,
                        std::function<void(const QList<RemoteChange> &changes, bool truncated, const RemoteError &error)>
                                done);

/// A file's two texts there (GET projects/<id>/diff).
struct RemoteDiff {
    QString path;
    QString oldText;
    QString newText;
    bool oldExists = false;
    bool newExists = false;
    bool binary = false;
    bool tooLarge = false;
};
void fetchRemoteDiff(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &project,
                     const QString &worktree, const QString &since, const QString &path, const QString &oldPath,
                     std::function<void(const RemoteDiff &diff, const RemoteError &error)> done);

} // namespace brulion::acp

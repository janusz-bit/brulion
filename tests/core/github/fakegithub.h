// An in-memory GitHub for the github module's tests (and the app's): the REST API endpoints
// RepoSync uses — repositories, branches (ETags, 304), matching refs, trees (recursive), the
// contents API (folder listings, the first file of an empty repository), blobs (raw), commits,
// references (fast-forward only), forks, pull requests — and raw.githubusercontent.com, answered
// from memory through sync::HttpTransport, asynchronously like the real transport. One object
// store holds the blobs, trees and commits of every repository (GitHub shares them within a fork
// network). Blob ids are Git's; trees and commits get ids of their own (unique, not Git's).
#pragma once

#include "sync/http.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace githubtest {

class FakeGitHub : public QObject, public brulion::sync::HttpTransport {
    Q_OBJECT
public:
    struct Entry {
        QString mode; ///< "100644", "100755", "120000", "160000", "040000"
        QString sha;
    };
    struct Repo {
        QString owner;
        QString name;
        QString defaultBranch = QStringLiteral("main");
        bool isPrivate = false;
        bool archived = false;
        QString parent;                  ///< "owner/name" of the repository it was forked from
        QMap<QString, QString> branches; ///< name → commit
        QStringList collaborators;       ///< logins that may push (the owner may always)
    };
    struct PullRequest {
        int number = 0;
        QString repo; ///< "owner/name" it was opened in
        QString title;
        QString body;
        QString head; ///< "branch" or "owner:branch"
        QString base;
        QString state = QStringLiteral("open");
        bool merged = false;
    };
    struct LoggedRequest {
        QByteArray method;
        QString host;
        QString path; ///< decoded
        QString query;
        bool authorized = false;
        QByteArray body;
    };
    /// A hook may answer instead of the fake (std::nullopt: as usual).
    using Hook = std::function<std::optional<brulion::sync::HttpResponse>(const brulion::sync::HttpRequest &,
                                                                          const LoggedRequest &)>;

    explicit FakeGitHub(QObject *parent = nullptr);
    ~FakeGitHub() override;

    void send(const brulion::sync::HttpRequest &request, brulion::sync::HttpCallback done) override;

    // ---- accounts ---------------------------------------------------------------------------
    /// The token the fake accepts and whose user it is.
    QString token = QStringLiteral("ghp_TestTokenŹdźbło1234567890abcdefghij");
    QString login = QStringLiteral("jan");
    /// The X-OAuth-Scopes header of /user ("" none: a fine-grained token).
    QString scopes = QStringLiteral("repo");

    // ---- repositories -------------------------------------------------------------------------
    /// A repository with one commit holding `files` (none: an empty repository without branches).
    void createRepo(const QString &owner, const QString &name, const QMap<QString, QByteArray> &files = {},
                    const QString &branch = QStringLiteral("main"), bool isPrivate = false);
    Repo *repo(const QString &fullName);
    /// Someone else's push: a commit on `branch` (a new one starts from the default branch)
    /// changing `changes` (std::nullopt removes a file; `modes` sets a file's mode); returns its id.
    QString
    push(const QString &fullName, const QString &branch, const QMap<QString, std::optional<QByteArray>> &changes,
         const QString &message = QStringLiteral("A change on GitHub"), const QMap<QString, QString> &modes = {});
    /// A file of a branch's newest commit (std::nullopt: none).
    std::optional<QByteArray> file(const QString &fullName, const QString &branch, const QString &path) const;
    /// Every file of a branch (path → content) and their modes.
    QMap<QString, QByteArray> files(const QString &fullName, const QString &branch) const;
    QString modeOf(const QString &fullName, const QString &branch, const QString &path) const;
    QString head(const QString &fullName, const QString &branch) const;
    /// A commit's message and parents.
    QString messageOf(const QString &commit) const;
    QStringList parentsOf(const QString &commit) const;
    /// Merge a pull request into its base branch (its head's newest tree becomes the base's).
    void merge(int number);
    void close(int number);

    QList<PullRequest> pullRequests;

    // ---- behaviour ----------------------------------------------------------------------------
    /// Requests without a token answer 403 "API rate limit exceeded" once this many were made
    /// (-1: no limit). raw.githubusercontent.com is never limited.
    int anonymousLimit = -1;
    /// A new fork answers 404 to this many requests before it is ready.
    int forkDelay = 0;
    /// raw.githubusercontent.com and the blobs endpoint answer these bytes instead (path or blob id → bytes).
    QHash<QString, QByteArray> corrupt;
    /// The next request is logged and never answered (a stalled connection).
    bool stallNext = false;
    Hook hook;

    // ---- what happened -------------------------------------------------------------------------
    QList<LoggedRequest> requests;
    int count(const QByteArray &method, const QString &pathPart = {}, const QString &host = {}) const;
    void clearLog() { requests.clear(); }

private:
    struct Commit {
        QString tree;
        QStringList parents;
        QString message;
    };
    brulion::sync::HttpResponse answer(const brulion::sync::HttpRequest &request, LoggedRequest &logged);
    brulion::sync::HttpResponse api(const brulion::sync::HttpRequest &request, const LoggedRequest &logged);
    brulion::sync::HttpResponse raw(const LoggedRequest &logged);
    QString writeTree(const QMap<QString, Entry> &flat);
    QMap<QString, Entry> flatten(const QString &treeSha) const;
    QString makeCommit(const QString &tree, const QStringList &parents, const QString &message);
    bool isAncestor(const QString &ancestor, const QString &commit) const;
    bool canPush(const Repo &repo, bool authorized) const;
    QByteArray repoJson(const Repo &repo, bool authorized) const;
    QByteArray pullJson(const PullRequest &pr) const;

    QHash<QString, Repo> m_repos;
    QHash<QString, QByteArray> m_blobs;
    QHash<QString, QMap<QString, Entry>> m_trees; ///< tree id → its entries by name
    QHash<QString, Commit> m_commits;
    QHash<QString, int> m_forkWaits; ///< a fork → requests left before it answers
    int m_commitCounter = 0;
    int m_anonymous = 0;
};

} // namespace githubtest

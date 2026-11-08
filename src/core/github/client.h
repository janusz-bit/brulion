// The GitHub REST API (https://docs.github.com/rest, version 2022-11-28) as Brulion uses it:
// repositories, branches, trees, blobs, commits, references, forks, pull requests and the
// contents API, plus raw.githubusercontent.com for files of public repositories without a token.
// Part of the github module — see github/github.h for the overview.
#pragma once

#include "log/redact.h"
#include "sync/http.h"

#include <QByteArray>
#include <QHash>
#include <QJsonDocument>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

namespace brulion::github {

/// A failed request, explained by its kind; the app translates the code.
struct GitHubError {
    enum class Code {
        None,
        Auth,            ///< 401: the token is wrong, expired or revoked
        Forbidden,       ///< 403: the token may not do this (scopes, a fine-grained token's repositories, SSO)
        RateLimited,     ///< 403 / 429 with the limit used up, or a secondary limit (retryAt)
        NotFound,        ///< 404: no such repository, branch, file — or a private one the token cannot see
        EmptyRepository, ///< 409 "Git Repository is empty.": no commit yet
        Conflict,        ///< any other 409
        NotFastForward,  ///< 422: the branch moved meanwhile (a reference update that is no fast-forward)
        Validation,      ///< any other 422 (a pull request that exists, a branch name taken, …)
        Server,          ///< 5xx
        Network,         ///< no HTTP answer (offline, DNS, TLS, refused, CORS in the browser)
        Timeout,         ///< no answer in time
        Parse,           ///< an answer that is not what the API sends
        Integrity,       ///< downloaded bytes that are not the blob the tree names
        TooLarge,        ///< a tree or file beyond what the API returns (or Brulion takes)
        Cancelled,
    };
    Code code = Code::None;
    int status = 0;  ///< HTTP status (0: none)
    QString message; ///< technical (English, redacted): GitHub's message or the transport's
    /// RateLimited: when the limit resets (X-RateLimit-Reset), ms since epoch (0 = unknown) …
    qint64 resetAt = 0;
    /// … or how long to wait (Retry-After: a secondary limit), seconds (-1 = unknown).
    int retryAfterSeconds = -1;

    bool isError() const { return code != Code::None; }
    /// Failures after which continuing is pointless: Auth, RateLimited, Network, Timeout, Cancelled.
    bool isFatal() const;
    static GitHubError make(Code code, const QString &message, int status = 0);
};

/// "auth", "forbidden", "rate-limited", "not-found", "empty-repository", "conflict",
/// "not-fast-forward", "validation", "server", "network", "timeout", "parse", "integrity",
/// "too-large", "cancelled"; "" for None.
QString errorCodeName(GitHubError::Code code);

/// owner/name of a repository.
struct RepoId {
    QString owner;
    QString name;
    bool isValid() const { return !owner.isEmpty() && !name.isEmpty(); }
    QString fullName() const { return owner + u'/' + name; }
    friend bool operator==(const RepoId &, const RepoId &) = default;
};

struct Repository {
    RepoId id;
    QString defaultBranch;
    bool isPrivate = false;
    bool isFork = false;
    bool archived = false;
    /// What the token may do here (GitHub sends `permissions` only to a signed-in user).
    bool permissionsKnown = false;
    bool canPush = false;
    QUrl htmlUrl;
};

/// A branch's newest commit.
struct BranchHead {
    QString name;
    QString commit; ///< sha of the commit
    QString tree;   ///< sha of its root tree
    QString etag;   ///< of the answer, for the next request's If-None-Match
};

struct TreeEntry {
    QString path; ///< relative to the tree asked for
    QString mode; ///< "100644", "100755", "120000", "160000", "040000"
    QString type; ///< "blob", "tree", "commit"
    QString sha;
    qint64 size = -1; ///< blobs; -1 unknown
};

struct Tree {
    QString sha;
    QList<TreeEntry> entries;
    bool truncated = false; ///< GitHub left entries out (above 100 000 entries or 7 MB)
};

struct User {
    QString login;
    QString name;
    /// The classic token's scopes (X-OAuth-Scopes); a fine-grained token sends none.
    QStringList scopes;
    bool scopesKnown = false;
};

struct PullRequest {
    int number = 0;
    QUrl htmlUrl;
    QString title;
    QString state; ///< "open" | "closed"
    bool merged = false;
    QString headRef;   ///< the proposal's branch
    QString headOwner; ///< the owner of the repository holding it
};

/// One entry of a new tree: a file written (content inline for text, else an uploaded blob's sha)
/// or removed (neither).
struct NewTreeEntry {
    QString path; ///< from the repository's root
    QString mode = QStringLiteral("100644");
    std::optional<QString> sha;        ///< a blob (createBlob())
    std::optional<QByteArray> content; ///< UTF-8 text GitHub stores as the file
    bool isDeletion() const { return !sha && !content; }
};

/// The rate limit of the last answer that named one.
struct RateLimit {
    int limit = -1;
    int remaining = -1;
    qint64 resetAt = 0; ///< ms since epoch
};

/// Asynchronous calls of the API; every callback runs exactly once from the event loop (never
/// inside the call), unless the client is destroyed first; cancel() answers every open call with
/// Cancelled. Requests go to api.github.com and raw.githubusercontent.com only.
class GitHubClient : public QObject {
    Q_OBJECT
public:
    template<typename T>
    using Done = std::function<void(const GitHubError &, const T &)>;
    using Finished = std::function<void(const GitHubError &)>;

    /// `transport` is not owned and must outlive the client.
    explicit GitHubClient(sync::HttpTransport *transport, QObject *parent = nullptr);
    ~GitHubClient() override;
    Q_DISABLE_COPY_MOVE(GitHubClient)

    /// A personal access token ("" = none: public repositories only, 60 requests an hour). Kept
    /// in memory, masked in every log line.
    void setToken(const QString &token);
    bool hasToken() const { return !m_token.isEmpty(); }
    /// In a browser: no User-Agent (the browser sends its own and refuses one set by a page).
    void setBrowser(bool browser) { m_browser = browser; }
    /// Time limit of one request (default 60 s).
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    /// Requests sent so far; the rate limit last reported.
    int requestCount() const { return m_requests; }
    RateLimit rateLimit() const { return m_rateLimit; }
    /// Answer every open call with Cancelled (their late answers are dropped).
    void cancel();

    void user(Done<User> done);
    void repository(const RepoId &repo, Done<Repository> done);
    /// The branch's head; with `etag` an unchanged branch answers `notModified` (free of the rate
    /// limit) — the head is then empty.
    void branch(const RepoId &repo, const QString &name, const QString &etag,
                std::function<void(const GitHubError &, const BranchHead &, bool notModified)> done);
    /// Branch names starting with `prefix` (whole names; "" = every branch, at most 100).
    void branchNames(const RepoId &repo, const QString &prefix, Done<QStringList> done);
    void tree(const RepoId &repo, const QString &sha, bool recursive, Done<Tree> done);
    /// The entries of a folder at a commit (the contents API): its subfolders' tree shas.
    void folder(const RepoId &repo, const QString &path, const QString &ref, Done<QList<TreeEntry>> done);
    /// A blob's bytes (the API; counts against the limit).
    void blob(const RepoId &repo, const QString &sha, Done<QByteArray> done);
    /// A file of a commit from raw.githubusercontent.com (public repositories; not rate limited
    /// like the API).
    void rawFile(const RepoId &repo, const QString &commit, const QString &path, Done<QByteArray> done);
    void createBlob(const RepoId &repo, const QByteArray &content, Done<QString> done);
    void createTree(const RepoId &repo, const QString &baseTree, const QList<NewTreeEntry> &entries,
                    Done<QString> done);
    /// A commit of `tree` with `parents`; its author is the token's user.
    void createCommit(const RepoId &repo, const QString &message, const QString &tree, const QStringList &parents,
                      Done<QString> done);
    /// Move a branch to `sha` (never forced: NotFastForward when it moved meanwhile).
    void updateBranch(const RepoId &repo, const QString &branch, const QString &sha, Finished done);
    void createBranch(const RepoId &repo, const QString &branch, const QString &sha, Finished done);
    /// The user's fork of `repo` (created, or the existing one); it may take a moment to be ready.
    void createFork(const RepoId &repo, Done<Repository> done);
    void createPullRequest(const RepoId &repo, const QString &title, const QString &body, const QString &head,
                           const QString &base, Done<PullRequest> done);
    void pullRequest(const RepoId &repo, int number, Done<PullRequest> done);
    /// The contents API's write of one file (the first commit of an empty repository); the new
    /// commit's sha.
    void putFile(const RepoId &repo, const QString &path, const QString &branch, const QString &message,
                 const QByteArray &content, Done<QString> done);

    /// The error of a finished request (status, headers, GitHub's JSON message).
    static GitHubError errorOf(const sync::HttpResponse &response);

private:
    using Json = std::function<void(const GitHubError &, const QJsonDocument &, const sync::HttpResponse &)>;
    void send(const QByteArray &method, const QUrl &url, const QByteArray &body,
              const QList<QPair<QByteArray, QByteArray>> &headers,
              std::function<void(const sync::HttpResponse &)> done);
    void api(const QByteArray &method, const QString &path, const QJsonDocument &body, Json done,
             const QList<QPair<QByteArray, QByteArray>> &headers = {});
    QUrl apiUrl(const QString &encodedPath) const;
    void noteRateLimit(const sync::HttpResponse &response);

    sync::HttpTransport *m_transport;
    QString m_token;
    logging::ScopedSecret m_secret;
    bool m_browser = false;
    int m_timeoutMs = 60000;
    int m_requests = 0;
    RateLimit m_rateLimit;
    /// The open calls' handlers by call number: cancel() answers them.
    QHash<int, std::function<void(const sync::HttpResponse &)>> m_open;
    int m_nextCall = 0;
};

} // namespace brulion::github

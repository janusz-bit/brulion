#include "github/client.h"

#include "github/address.h"
#include "log/categories.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::github {

namespace {

/// What a cancelled call answers (no transport sends a negative status).
constexpr int kCancelledStatus = -1;

QString repoPath(const RepoId &repo)
{
    return u"repos/"_s + encodePath(repo.owner) + u'/' + encodePath(repo.name);
}

QString queryValue(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QUrl apiUrl(const QString &encodedPath)
{
    return QUrl::fromEncoded(QString(u"https://"_s + ApiHost + u'/' + encodedPath).toLatin1(), QUrl::StrictMode);
}

/// X-RateLimit-Reset (seconds since the epoch) in ms since the epoch; 0 when missing or out of
/// range (no overflow on a bogus header).
qint64 rateLimitResetMs(const sync::HttpResponse &response)
{
    bool ok = false;
    const qint64 seconds = response.header("x-ratelimit-reset").trimmed().toLongLong(&ok);
    return ok && seconds > 0 && seconds <= std::numeric_limits<qint64>::max() / 1000 ? seconds * 1000 : 0;
}

/// GitHub's explanation of a failed request: "message" and the "errors" it lists.
QString messageOf(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return QString::fromUtf8(body.left(200)).simplified();
    const QJsonObject object = doc.object();
    QString message = object.value(u"message"_s).toString();
    const QJsonArray errors = object.value(u"errors"_s).toArray();
    for (const QJsonValue error : errors) {
        const QJsonObject details = error.toObject();
        const QString text = error.isObject() ? details.value(u"message"_s).toString() : error.toString();
        const QString part = text.isEmpty() ? details.value(u"code"_s).toString() : text;
        if (!part.isEmpty())
            message += (message.isEmpty() ? QString() : u": "_s) + part;
    }
    return message;
}

Repository repositoryOf(const QJsonObject &object)
{
    Repository repo;
    repo.id.owner = object.value(u"owner"_s).toObject().value(u"login"_s).toString();
    repo.id.name = object.value(u"name"_s).toString();
    repo.defaultBranch = object.value(u"default_branch"_s).toString();
    repo.isPrivate = object.value(u"private"_s).toBool();
    repo.isFork = object.value(u"fork"_s).toBool();
    repo.archived = object.value(u"archived"_s).toBool();
    const QJsonValue permissions = object.value(u"permissions"_s);
    repo.permissionsKnown = permissions.isObject();
    repo.canPush = permissions.toObject().value(u"push"_s).toBool();
    repo.htmlUrl = QUrl(object.value(u"html_url"_s).toString());
    return repo;
}

PullRequest pullRequestOf(const QJsonObject &object)
{
    PullRequest pr;
    pr.number = object.value(u"number"_s).toInt();
    pr.htmlUrl = QUrl(object.value(u"html_url"_s).toString());
    pr.title = object.value(u"title"_s).toString();
    pr.state = object.value(u"state"_s).toString();
    pr.merged = object.value(u"merged"_s).toBool() || !object.value(u"merged_at"_s).toString().isEmpty();
    const QJsonObject head = object.value(u"head"_s).toObject();
    pr.headRef = head.value(u"ref"_s).toString();
    pr.headOwner = head.value(u"repo"_s).toObject().value(u"owner"_s).toObject().value(u"login"_s).toString();
    return pr;
}

TreeEntry treeEntryOf(const QJsonObject &object)
{
    TreeEntry entry;
    entry.path = object.value(u"path"_s).toString();
    entry.mode = object.value(u"mode"_s).toString();
    entry.type = object.value(u"type"_s).toString();
    entry.sha = object.value(u"sha"_s).toString();
    entry.size = object.value(u"size"_s).toInteger(-1);
    return entry;
}

Tree treeOf(const QJsonObject &object)
{
    Tree tree;
    tree.sha = object.value(u"sha"_s).toString();
    tree.truncated = object.value(u"truncated"_s).toBool();
    const QJsonArray entries = object.value(u"tree"_s).toArray();
    tree.entries.reserve(entries.size());
    for (const QJsonValue entry : entries)
        tree.entries.append(treeEntryOf(entry.toObject()));
    return tree;
}

/// The id of what a call of the Git database API made.
QString shaOf(const QJsonObject &object)
{
    return object.value(u"sha"_s).toString();
}

/// The id of the commit a write of the contents API made.
QString commitShaOf(const QJsonObject &object)
{
    return shaOf(object.value(u"commit"_s).toObject());
}

/// Whether an answer holds what was asked for (else it is a Parse error).
bool holdsAnswer(const Repository &repository)
{
    return repository.id.isValid();
}

bool holdsAnswer(const PullRequest &pr)
{
    return pr.number > 0;
}

bool holdsAnswer(const Tree &tree)
{
    return !tree.sha.isEmpty();
}

bool holdsAnswer(const QString &sha)
{
    return !sha.isEmpty();
}

/// The handler of an API call answering `done` with what `read` takes from GitHub's JSON object:
/// the request's error, or Parse with `missing` when the answer holds nothing (holdsAnswer()).
template<typename T>
auto answer(GitHubClient::Done<T> done, T (*read)(const QJsonObject &), const QString &missing)
{
    return [done = std::move(done), read, missing](const GitHubError &error, const QJsonDocument &doc,
                                                   const sync::HttpResponse &response) {
        if (error.isError()) {
            done(error, {});
            return;
        }
        const T value = read(doc.object());
        if (holdsAnswer(value))
            done({}, value);
        else
            done(GitHubError::make(GitHubError::Code::Parse, missing, response.status), value);
    };
}

/// The handler of a download answering `done` with the bytes, or the request's error.
auto bytesAnswer(GitHubClient::Done<QByteArray> done)
{
    return [done = std::move(done)](const sync::HttpResponse &response) {
        if (response.status < 200 || response.status >= 300) {
            done(GitHubClient::errorOf(response), {});
            return;
        }
        done({}, response.body);
    };
}

} // namespace

bool GitHubError::isFatal() const
{
    return code == Code::Auth || code == Code::RateLimited || code == Code::Network || code == Code::Timeout
           || code == Code::Cancelled;
}

GitHubError GitHubError::make(Code code, const QString &message, int status)
{
    GitHubError error;
    error.code = code;
    error.message = logging::redact(message);
    error.status = status;
    return error;
}

QString errorCodeName(GitHubError::Code code)
{
    using C = GitHubError::Code;
    switch (code) {
    case C::None: return {};
    case C::Auth: return u"auth"_s;
    case C::Forbidden: return u"forbidden"_s;
    case C::RateLimited: return u"rate-limited"_s;
    case C::NotFound: return u"not-found"_s;
    case C::EmptyRepository: return u"empty-repository"_s;
    case C::Conflict: return u"conflict"_s;
    case C::NotFastForward: return u"not-fast-forward"_s;
    case C::Validation: return u"validation"_s;
    case C::Server: return u"server"_s;
    case C::Network: return u"network"_s;
    case C::Timeout: return u"timeout"_s;
    case C::Parse: return u"parse"_s;
    case C::Integrity: return u"integrity"_s;
    case C::TooLarge: return u"too-large"_s;
    case C::Cancelled: return u"cancelled"_s;
    }
    return {};
}

GitHubClient::GitHubClient(sync::HttpTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport) { }

GitHubClient::~GitHubClient() = default;

void GitHubClient::setToken(const QString &token)
{
    m_token = token.trimmed();
    m_secret.reset(m_token);
}

void GitHubClient::cancel()
{
    const auto open = std::exchange(m_open, {});
    for (const auto &handler : open) {
        QMetaObject::invokeMethod(
                this,
                [handler] {
                    sync::HttpResponse response;
                    response.status = kCancelledStatus;
                    response.networkError = u"Cancelled"_s;
                    handler(response);
                },
                Qt::QueuedConnection);
    }
}

GitHubError GitHubClient::errorOf(const sync::HttpResponse &response)
{
    using C = GitHubError::Code;
    const int status = response.status;
    if (status == kCancelledStatus)
        return GitHubError::make(C::Cancelled, u"Cancelled"_s);
    if (status == 0)
        return GitHubError::make(response.timedOut ? C::Timeout : C::Network, response.networkError);
    const QString message = messageOf(response.body);
    if (status == 401)
        return GitHubError::make(C::Auth, message, status);
    if (status == 403 || status == 429) {
        const QByteArray remaining = response.header("x-ratelimit-remaining").trimmed();
        const QByteArray retryAfter = response.header("retry-after").trimmed();
        if (status == 429 || remaining == "0" || !retryAfter.isEmpty()
            || message.contains(u"rate limit"_s, Qt::CaseInsensitive)) {
            GitHubError error = GitHubError::make(C::RateLimited, message, status);
            if (remaining == "0")
                error.resetAt = rateLimitResetMs(response);
            bool ok = false;
            const int seconds = retryAfter.toInt(&ok);
            if (ok && seconds >= 0)
                error.retryAfterSeconds = seconds;
            return error;
        }
        return GitHubError::make(C::Forbidden, message, status);
    }
    if (status == 404 || status == 410)
        return GitHubError::make(C::NotFound, message, status);
    if (status == 409) {
        const bool empty = message.contains(u"is empty"_s, Qt::CaseInsensitive);
        return GitHubError::make(empty ? C::EmptyRepository : C::Conflict, message, status);
    }
    if (status == 422) {
        const bool fastForward = message.contains(u"fast forward"_s, Qt::CaseInsensitive)
                                 || message.contains(u"fast-forward"_s, Qt::CaseInsensitive);
        return GitHubError::make(fastForward ? C::NotFastForward : C::Validation, message, status);
    }
    if (status == 413)
        return GitHubError::make(C::TooLarge, message, status);
    if (status >= 500)
        return GitHubError::make(C::Server, message, status);
    return GitHubError::make(C::Validation, message, status);
}

void GitHubClient::noteRateLimit(const sync::HttpResponse &response)
{
    bool ok = false;
    const int limit = response.header("x-ratelimit-limit").trimmed().toInt(&ok);
    if (!ok)
        return;
    m_rateLimit.limit = limit;
    m_rateLimit.remaining = response.header("x-ratelimit-remaining").trimmed().toInt();
    m_rateLimit.resetAt = rateLimitResetMs(response);
}

void GitHubClient::send(const QByteArray &method, const QUrl &url, const QByteArray &body, const Headers &headers,
                        std::function<void(const sync::HttpResponse &)> done)
{
    sync::HttpRequest request;
    request.method = method;
    request.url = url;
    request.body = body;
    request.timeoutMs = m_timeoutMs;
    request.headers = headers;
    if (url.host() == ApiHost && !m_token.isEmpty())
        request.headers.emplaceBack("Authorization", "Bearer " + m_token.toUtf8());
    if (!m_browser)
        request.headers.emplaceBack("User-Agent", "Brulion");
    const int call = ++m_nextCall;
    m_open.insert(call, std::move(done));
    ++m_requests;
    qCDebug(logging::lcNet).noquote() << u"GitHub %1 %2"_s.arg(QString::fromLatin1(method),
                                                               url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery));
    QPointer<GitHubClient> self(this);
    m_transport->send(request, [self, call, method, url](const sync::HttpResponse &response) {
        if (!self)
            return;
        const auto handler = self->m_open.take(call);
        if (!handler)
            return; // cancelled: answered already
        self->noteRateLimit(response);
        if (response.status >= 400 || response.status == 0) {
            // One arg() call: a "%3F" of the URL is no placeholder.
            qCDebug(logging::lcNet).noquote() << u"GitHub %1 %2 -> %3 %4"_s.arg(
                    QString::fromLatin1(method), url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery),
                    QString::number(response.status), logging::redact(response.networkError));
        }
        handler(response);
    });
}

void GitHubClient::api(const QByteArray &method, const QString &path, const QJsonDocument &body, Json done,
                       const Headers &headers)
{
    Headers all = headers;
    const bool accept = std::ranges::any_of(
            headers, [](const auto &header) { return header.first.compare("Accept", Qt::CaseInsensitive) == 0; });
    if (!accept)
        all.emplaceBack("Accept", "application/vnd.github+json");
    all.emplaceBack("X-GitHub-Api-Version", "2022-11-28");
    if (!body.isNull())
        all.emplaceBack("Content-Type", "application/json");
    send(method, apiUrl(path), body.isNull() ? QByteArray() : body.toJson(QJsonDocument::Compact), all,
         [done = std::move(done)](const sync::HttpResponse &response) {
             if (response.status == 304) {
                 done({}, {}, response);
                 return;
             }
             if (response.status < 200 || response.status >= 300) {
                 done(errorOf(response), {}, response);
                 return;
             }
             if (response.body.trimmed().isEmpty()) {
                 done({}, {}, response);
                 return;
             }
             QJsonParseError parse{};
             const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parse);
             if (parse.error != QJsonParseError::NoError) {
                 done(GitHubError::make(GitHubError::Code::Parse, u"Not JSON: "_s + parse.errorString(),
                                        response.status),
                      {}, response);
                 return;
             }
             done({}, doc, response);
         });
}

void GitHubClient::user(Done<User> done)
{
    api("GET", u"user"_s, {},
        [done = std::move(done)](const GitHubError &error, const QJsonDocument &doc, const sync::HttpResponse &r) {
            User user;
            if (error.isError()) {
                done(error, user);
                return;
            }
            const QJsonObject object = doc.object();
            user.login = object.value(u"login"_s).toString();
            user.name = object.value(u"name"_s).toString();
            if (r.headers.contains("x-oauth-scopes")) {
                user.scopesKnown = true;
                const QList<QByteArray> scopes = r.header("x-oauth-scopes").split(',');
                for (const QByteArray &scope : scopes) {
                    if (const QByteArray name = scope.trimmed(); !name.isEmpty())
                        user.scopes.append(QString::fromLatin1(name));
                }
            }
            if (user.login.isEmpty()) {
                done(GitHubError::make(GitHubError::Code::Parse, u"No login in the answer"_s, r.status), user);
                return;
            }
            done({}, user);
        });
}

void GitHubClient::repository(const RepoId &repo, Done<Repository> done)
{
    api("GET", repoPath(repo), {}, answer(std::move(done), &repositoryOf, u"No repository in the answer"_s));
}

void GitHubClient::branch(const RepoId &repo, const QString &name, const QString &etag,
                          std::function<void(const GitHubError &, const BranchHead &, bool)> done)
{
    Headers headers;
    if (!etag.isEmpty())
        headers.emplaceBack("If-None-Match", etag.toUtf8());
    api(
            "GET", repoPath(repo) + u"/branches/"_s + encodePath(name), {},
            [done = std::move(done), name](const GitHubError &error, const QJsonDocument &doc,
                                           const sync::HttpResponse &r) {
                BranchHead head;
                if (error.isError()) {
                    done(error, head, false);
                    return;
                }
                if (r.status == 304) {
                    done({}, head, true);
                    return;
                }
                const QJsonObject object = doc.object();
                const QJsonObject commit = object.value(u"commit"_s).toObject();
                head.name = object.value(u"name"_s).toString(name);
                head.commit = commit.value(u"sha"_s).toString();
                head.tree = commit.value(u"commit"_s).toObject().value(u"tree"_s).toObject().value(u"sha"_s).toString();
                head.etag = QString::fromLatin1(r.header("etag"));
                if (head.commit.isEmpty() || head.tree.isEmpty()) {
                    done(GitHubError::make(GitHubError::Code::Parse, u"No commit in the branch's answer"_s, r.status),
                         head, false);
                    return;
                }
                done({}, head, false);
            },
            headers);
}

void GitHubClient::branchNames(const RepoId &repo, const QString &prefix, Done<QStringList> done)
{
    const QString filter = prefix.isEmpty() ? QString() : QString(u'/' + encodePath(prefix));
    api("GET", repoPath(repo) + u"/git/matching-refs/heads"_s + filter + u"?per_page=100"_s, {},
        [done = std::move(done)](const GitHubError &error, const QJsonDocument &doc, const sync::HttpResponse &) {
            if (error.code == GitHubError::Code::EmptyRepository) {
                done({}, {}); // no commit: no branch either
                return;
            }
            if (error.isError()) {
                done(error, {});
                return;
            }
            static constexpr QStringView heads = u"refs/heads/";
            QStringList names;
            const QJsonArray refs = doc.array();
            for (const QJsonValue ref : refs) {
                const QString full = ref.toObject().value(u"ref"_s).toString();
                if (full.startsWith(heads))
                    names.append(full.sliced(heads.size()));
            }
            done({}, names);
        });
}

void GitHubClient::tree(const RepoId &repo, const QString &sha, bool recursive, Done<Tree> done)
{
    api("GET", repoPath(repo) + u"/git/trees/"_s + encodePath(sha) + (recursive ? u"?recursive=1"_s : QString()), {},
        answer(std::move(done), &treeOf, u"No tree in the answer"_s));
}

void GitHubClient::folder(const RepoId &repo, const QString &path, const QString &ref, Done<QList<TreeEntry>> done)
{
    const QString where = path.isEmpty() ? QString() : QString(u'/' + encodePath(path));
    api("GET", repoPath(repo) + u"/contents"_s + where + u"?ref="_s + queryValue(ref), {},
        [done = std::move(done)](const GitHubError &error, const QJsonDocument &doc, const sync::HttpResponse &r) {
            if (error.isError()) {
                done(error, {});
                return;
            }
            if (!doc.isArray()) { // a file
                done(GitHubError::make(GitHubError::Code::NotFound, u"Not a folder"_s, r.status), {});
                return;
            }
            QList<TreeEntry> entries;
            const QJsonArray listing = doc.array();
            for (const QJsonValue value : listing) {
                const QJsonObject object = value.toObject();
                TreeEntry entry;
                entry.path = object.value(u"name"_s).toString();
                entry.sha = object.value(u"sha"_s).toString();
                entry.size = object.value(u"size"_s).toInteger(-1);
                const QString type = object.value(u"type"_s).toString();
                entry.type = type == u"dir"_s ? u"tree"_s : type == u"submodule"_s ? u"commit"_s : u"blob"_s;
                entries.append(entry);
            }
            done({}, entries);
        });
}

void GitHubClient::blob(const RepoId &repo, const QString &sha, Done<QByteArray> done)
{
    const Headers headers = {{"Accept", "application/vnd.github.raw+json"}, {"X-GitHub-Api-Version", "2022-11-28"}};
    send("GET", apiUrl(repoPath(repo) + u"/git/blobs/"_s + encodePath(sha)), {}, headers, bytesAnswer(std::move(done)));
}

void GitHubClient::rawFile(const RepoId &repo, const QString &commit, const QString &path, Done<QByteArray> done)
{
    const QString text = u"https://"_s + RawHost + u'/' + encodePath(repo.owner) + u'/' + encodePath(repo.name) + u'/'
                         + encodePath(commit) + u'/' + encodePath(path);
    send("GET", QUrl::fromEncoded(text.toLatin1(), QUrl::StrictMode), {}, {}, bytesAnswer(std::move(done)));
}

void GitHubClient::createBlob(const RepoId &repo, const QByteArray &content, Done<QString> done)
{
    const QJsonObject body{{u"content"_s, QString::fromLatin1(content.toBase64())}, {u"encoding"_s, u"base64"_s}};
    api("POST", repoPath(repo) + u"/git/blobs"_s, QJsonDocument(body),
        answer(std::move(done), &shaOf, u"No blob in the answer"_s));
}

void GitHubClient::createTree(const RepoId &repo, const QString &baseTree, const QList<NewTreeEntry> &entries,
                              Done<QString> done)
{
    QJsonArray tree;
    for (const NewTreeEntry &entry : entries) {
        QJsonObject object{{u"path"_s, entry.path}, {u"mode"_s, entry.mode}, {u"type"_s, u"blob"_s}};
        if (entry.content)
            object.insert(u"content"_s, QString::fromUtf8(*entry.content));
        else if (entry.sha)
            object.insert(u"sha"_s, *entry.sha);
        else
            object.insert(u"sha"_s, QJsonValue::Null); // removed
        tree.append(object);
    }
    QJsonObject body{{u"tree"_s, tree}};
    if (!baseTree.isEmpty())
        body.insert(u"base_tree"_s, baseTree);
    api("POST", repoPath(repo) + u"/git/trees"_s, QJsonDocument(body),
        answer(std::move(done), &shaOf, u"No tree in the answer"_s));
}

void GitHubClient::createCommit(const RepoId &repo, const QString &message, const QString &tree,
                                const QStringList &parents, Done<QString> done)
{
    const QJsonObject body{
            {u"message"_s, message}, {u"tree"_s, tree}, {u"parents"_s, QJsonArray::fromStringList(parents)}};
    api("POST", repoPath(repo) + u"/git/commits"_s, QJsonDocument(body),
        answer(std::move(done), &shaOf, u"No commit in the answer"_s));
}

void GitHubClient::updateBranch(const RepoId &repo, const QString &branch, const QString &sha, Finished done)
{
    const QJsonObject body{{u"sha"_s, sha}, {u"force"_s, false}};
    api("PATCH", repoPath(repo) + u"/git/refs/heads/"_s + encodePath(branch), QJsonDocument(body),
        [done = std::move(done)](const GitHubError &error, const QJsonDocument &, const sync::HttpResponse &) {
            done(error);
        });
}

void GitHubClient::createBranch(const RepoId &repo, const QString &branch, const QString &sha, Finished done)
{
    const QJsonObject body{{u"ref"_s, QString(u"refs/heads/"_s + branch)}, {u"sha"_s, sha}};
    api("POST", repoPath(repo) + u"/git/refs"_s, QJsonDocument(body),
        [done = std::move(done)](const GitHubError &error, const QJsonDocument &, const sync::HttpResponse &) {
            done(error);
        });
}

void GitHubClient::createFork(const RepoId &repo, Done<Repository> done)
{
    const QJsonObject body{{u"default_branch_only"_s, true}};
    api("POST", repoPath(repo) + u"/forks"_s, QJsonDocument(body),
        answer(std::move(done), &repositoryOf, u"No fork in the answer"_s));
}

void GitHubClient::createPullRequest(const RepoId &repo, const QString &title, const QString &body, const QString &head,
                                     const QString &base, Done<PullRequest> done)
{
    const QJsonObject request{{u"title"_s, title},
                              {u"body"_s, body},
                              {u"head"_s, head},
                              {u"base"_s, base},
                              {u"maintainer_can_modify"_s, true}};
    api("POST", repoPath(repo) + u"/pulls"_s, QJsonDocument(request),
        answer(std::move(done), &pullRequestOf, u"No pull request in the answer"_s));
}

void GitHubClient::pullRequest(const RepoId &repo, int number, Done<PullRequest> done)
{
    api("GET", repoPath(repo) + u"/pulls/"_s + QString::number(number), {},
        answer(std::move(done), &pullRequestOf, u"No pull request in the answer"_s));
}

void GitHubClient::putFile(const RepoId &repo, const QString &path, const QString &branch, const QString &message,
                           const QByteArray &content, Done<QString> done)
{
    QJsonObject body{{u"message"_s, message}, {u"content"_s, QString::fromLatin1(content.toBase64())}};
    if (!branch.isEmpty())
        body.insert(u"branch"_s, branch);
    api("PUT", repoPath(repo) + u"/contents/"_s + encodePath(path), QJsonDocument(body),
        answer(std::move(done), &commitShaOf, u"No commit in the answer"_s));
}

} // namespace brulion::github

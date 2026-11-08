#include "fakegithub.h"

#include "github/gitobjects.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

using namespace Qt::StringLiterals;
using brulion::sync::HttpRequest;
using brulion::sync::HttpResponse;

namespace githubtest {

namespace {

HttpResponse json(int status, const QJsonValue &value)
{
    HttpResponse response;
    response.status = status;
    response.headers.insert("content-type", "application/json; charset=utf-8");
    response.body = value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                    : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    return response;
}

HttpResponse message(int status, const QString &text)
{
    return json(status, QJsonObject{{u"message"_s, text}, {u"documentation_url"_s, u"https://docs.github.com/rest"_s}});
}

QString sha1Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
}

QString fullName(const QString &owner, const QString &name)
{
    return owner + u'/' + name;
}

} // namespace

FakeGitHub::FakeGitHub(QObject *parent) : QObject(parent) { }

FakeGitHub::~FakeGitHub() = default;

void FakeGitHub::send(const HttpRequest &request, brulion::sync::HttpCallback done)
{
    LoggedRequest logged;
    logged.method = request.method;
    logged.host = request.url.host();
    logged.path = request.url.path(QUrl::FullyDecoded);
    logged.query = request.url.query(QUrl::FullyDecoded);
    logged.authorized = request.header("Authorization") == "Bearer " + token.toUtf8();
    logged.body = request.body;
    if (stallNext) {
        stallNext = false;
        requests.append(logged);
        return; // never answered
    }
    HttpResponse response;
    if (hook) {
        if (const std::optional<HttpResponse> hooked = hook(request, logged)) {
            requests.append(logged);
            response = *hooked;
            QTimer::singleShot(0, this, [done, response] { done(response); });
            return;
        }
    }
    response = answer(request, logged);
    requests.append(logged);
    QTimer::singleShot(0, this, [done, response] { done(response); });
}

int FakeGitHub::count(const QByteArray &method, const QString &pathPart, const QString &host) const
{
    int n = 0;
    for (const LoggedRequest &r : requests) {
        if ((method.isEmpty() || r.method == method) && (pathPart.isEmpty() || r.path.contains(pathPart))
            && (host.isEmpty() || r.host == host)) {
            ++n;
        }
    }
    return n;
}

// ---- the object store ---------------------------------------------------------------------------------

QString FakeGitHub::writeTree(const QMap<QString, Entry> &flat)
{
    // Folders first: group the paths by their first segment.
    QMap<QString, QMap<QString, Entry>> folders;
    QMap<QString, Entry> entries;
    for (auto it = flat.cbegin(); it != flat.cend(); ++it) {
        const qsizetype slash = it.key().indexOf(u'/');
        if (slash < 0)
            entries.insert(it.key(), it.value());
        else
            folders[it.key().left(slash)].insert(it.key().mid(slash + 1), it.value());
    }
    for (auto it = folders.cbegin(); it != folders.cend(); ++it)
        entries.insert(it.key(), Entry{u"040000"_s, writeTree(it.value())});
    QByteArray canonical = "tree\n";
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        canonical += it.value().mode.toUtf8() + ' ' + it.value().sha.toUtf8() + ' ' + it.key().toUtf8() + '\n';
    const QString sha = sha1Hex(canonical);
    m_trees.insert(sha, entries);
    return sha;
}

QMap<QString, FakeGitHub::Entry> FakeGitHub::flatten(const QString &treeSha) const
{
    QMap<QString, Entry> out;
    const auto tree = m_trees.constFind(treeSha);
    if (tree == m_trees.cend())
        return out;
    for (auto it = tree->cbegin(); it != tree->cend(); ++it) {
        if (it.value().mode == u"040000"_s) {
            const QMap<QString, Entry> inner = flatten(it.value().sha);
            for (auto in = inner.cbegin(); in != inner.cend(); ++in)
                out.insert(it.key() + u'/' + in.key(), in.value());
        } else {
            out.insert(it.key(), it.value());
        }
    }
    return out;
}

QString FakeGitHub::makeCommit(const QString &tree, const QStringList &parents, const QString &text)
{
    const QString sha = sha1Hex((u"commit "_s + QString::number(++m_commitCounter) + u'\n' + tree + u'\n'
                                 + parents.join(u' ') + u'\n' + text)
                                        .toUtf8());
    m_commits.insert(sha, Commit{tree, parents, text});
    return sha;
}

bool FakeGitHub::isAncestor(const QString &ancestor, const QString &commit) const
{
    if (ancestor == commit)
        return true;
    QStringList todo{commit};
    QSet<QString> seen;
    while (!todo.isEmpty()) {
        const QString c = todo.takeLast();
        if (c == ancestor)
            return true;
        if (seen.contains(c))
            continue;
        seen.insert(c);
        todo += m_commits.value(c).parents;
    }
    return false;
}

void FakeGitHub::createRepo(const QString &owner, const QString &name, const QMap<QString, QByteArray> &files,
                            const QString &branch, bool isPrivate)
{
    Repo r;
    r.owner = owner;
    r.name = name;
    r.defaultBranch = branch;
    r.isPrivate = isPrivate;
    if (!files.isEmpty()) {
        QMap<QString, Entry> flat;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString sha = brulion::github::blobSha(it.value());
            m_blobs.insert(sha, it.value());
            flat.insert(it.key(), Entry{u"100644"_s, sha});
        }
        r.branches.insert(branch, makeCommit(writeTree(flat), {}, u"Initial commit"_s));
    }
    m_repos.insert(fullName(owner, name), r);
}

FakeGitHub::Repo *FakeGitHub::repo(const QString &name)
{
    const auto it = m_repos.find(name);
    return it == m_repos.end() ? nullptr : &it.value();
}

QString FakeGitHub::push(const QString &name, const QString &branch,
                         const QMap<QString, std::optional<QByteArray>> &changes, const QString &text,
                         const QMap<QString, QString> &modes)
{
    Repo *r = repo(name);
    if (!r)
        return {};
    // A new branch starts from the default branch's head (as one made on GitHub).
    const QString parent = r->branches.value(branch, r->branches.value(r->defaultBranch));
    QMap<QString, Entry> flat = parent.isEmpty() ? QMap<QString, Entry>() : flatten(m_commits.value(parent).tree);
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (!it.value()) {
            flat.remove(it.key());
            continue;
        }
        const QString sha = brulion::github::blobSha(*it.value());
        m_blobs.insert(sha, *it.value());
        flat.insert(it.key(),
                    Entry{modes.value(it.key(),
                                      flat.value(it.key()).mode.isEmpty() ? u"100644"_s : flat.value(it.key()).mode),
                          sha});
    }
    for (auto it = modes.cbegin(); it != modes.cend(); ++it) {
        if (flat.contains(it.key()))
            flat[it.key()].mode = it.value();
    }
    const QString commit = makeCommit(writeTree(flat), parent.isEmpty() ? QStringList() : QStringList{parent}, text);
    r->branches.insert(branch, commit);
    return commit;
}

std::optional<QByteArray> FakeGitHub::file(const QString &name, const QString &branch, const QString &path) const
{
    const QMap<QString, QByteArray> all = files(name, branch);
    if (!all.contains(path))
        return std::nullopt;
    return all.value(path);
}

QMap<QString, QByteArray> FakeGitHub::files(const QString &name, const QString &branch) const
{
    QMap<QString, QByteArray> out;
    const auto r = m_repos.constFind(name);
    if (r == m_repos.cend() || !r->branches.contains(branch))
        return out;
    const QMap<QString, Entry> flat = flatten(m_commits.value(r->branches.value(branch)).tree);
    for (auto it = flat.cbegin(); it != flat.cend(); ++it)
        out.insert(it.key(), m_blobs.value(it.value().sha));
    return out;
}

QString FakeGitHub::modeOf(const QString &name, const QString &branch, const QString &path) const
{
    const auto r = m_repos.constFind(name);
    if (r == m_repos.cend())
        return {};
    return flatten(m_commits.value(r->branches.value(branch)).tree).value(path).mode;
}

QString FakeGitHub::head(const QString &name, const QString &branch) const
{
    const auto r = m_repos.constFind(name);
    return r == m_repos.cend() ? QString() : r->branches.value(branch);
}

QString FakeGitHub::messageOf(const QString &commit) const
{
    return m_commits.value(commit).message;
}

QStringList FakeGitHub::parentsOf(const QString &commit) const
{
    return m_commits.value(commit).parents;
}

void FakeGitHub::merge(int number)
{
    for (PullRequest &pr : pullRequests) {
        if (pr.number != number)
            continue;
        const qsizetype colon = pr.head.indexOf(u':');
        const QString headRepo = colon < 0 ? pr.repo : pr.head.left(colon) + u'/' + pr.repo.section(u'/', 1);
        const QString headBranch = colon < 0 ? pr.head : pr.head.mid(colon + 1);
        Repo *headR = repo(headRepo);
        Repo *base = repo(pr.repo);
        if (!headR || !base)
            return;
        const QString headCommit = headR->branches.value(headBranch);
        const QString baseCommit = base->branches.value(pr.base);
        // The base's files with the head's changes (relative to the head's first parent chain's base).
        base->branches.insert(pr.base, makeCommit(m_commits.value(headCommit).tree, {baseCommit, headCommit},
                                                  u"Merge pull request #%1"_s.arg(number)));
        pr.state = u"closed"_s;
        pr.merged = true;
    }
}

void FakeGitHub::close(int number)
{
    for (PullRequest &pr : pullRequests) {
        if (pr.number == number)
            pr.state = u"closed"_s;
    }
}

bool FakeGitHub::canPush(const Repo &r, bool authorized) const
{
    return authorized && !r.archived && (r.owner == login || r.collaborators.contains(login));
}

QByteArray FakeGitHub::repoJson(const Repo &r, bool authorized) const
{
    QJsonObject object{{u"name"_s, r.name},
                       {u"full_name"_s, fullName(r.owner, r.name)},
                       {u"owner"_s, QJsonObject{{u"login"_s, r.owner}}},
                       {u"default_branch"_s, r.defaultBranch},
                       {u"private"_s, r.isPrivate},
                       {u"fork"_s, !r.parent.isEmpty()},
                       {u"archived"_s, r.archived},
                       {u"html_url"_s, QString(u"https://github.com/"_s + fullName(r.owner, r.name))}};
    if (authorized) {
        const bool push = canPush(r, authorized);
        object.insert(u"permissions"_s, QJsonObject{{u"admin"_s, push}, {u"push"_s, push}, {u"pull"_s, true}});
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray FakeGitHub::pullJson(const PullRequest &pr) const
{
    const qsizetype colon = pr.head.indexOf(u':');
    const QString headOwner = colon < 0 ? pr.repo.section(u'/', 0, 0) : pr.head.left(colon);
    const QString headRef = colon < 0 ? pr.head : pr.head.mid(colon + 1);
    const QJsonObject object{
            {u"number"_s, pr.number},
            {u"html_url"_s, u"https://github.com/%1/pull/%2"_s.arg(pr.repo).arg(pr.number)},
            {u"title"_s, pr.title},
            {u"body"_s, pr.body},
            {u"state"_s, pr.state},
            {u"merged"_s, pr.merged},
            {u"head"_s, QJsonObject{{u"ref"_s, headRef},
                                    {u"repo"_s, QJsonObject{{u"owner"_s, QJsonObject{{u"login"_s, headOwner}}}}}}},
            {u"base"_s, QJsonObject{{u"ref"_s, pr.base}}}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// ---- answers ------------------------------------------------------------------------------------------

HttpResponse FakeGitHub::answer(const HttpRequest &request, LoggedRequest &logged)
{
    if (logged.host == u"raw.githubusercontent.com"_s)
        return raw(logged);
    if (logged.host != u"api.github.com"_s) {
        HttpResponse none;
        none.networkError = u"Host not found"_s;
        return none;
    }
    const QByteArray auth = request.header("Authorization");
    if (!auth.isEmpty() && !logged.authorized)
        return message(401, u"Bad credentials"_s);
    if (!logged.authorized) {
        ++m_anonymous;
        if (anonymousLimit >= 0 && m_anonymous > anonymousLimit) {
            HttpResponse limited = message(403, u"API rate limit exceeded for 192.0.2.1."_s);
            limited.headers.insert("x-ratelimit-limit", "60");
            limited.headers.insert("x-ratelimit-remaining", "0");
            limited.headers.insert("x-ratelimit-reset", "1793955600");
            return limited;
        }
    }
    HttpResponse response = api(request, logged);
    response.headers.insert("x-ratelimit-limit", logged.authorized ? "5000" : "60");
    response.headers.insert("x-ratelimit-remaining",
                            logged.authorized ? "4999" : QByteArray::number(qMax(0, 60 - m_anonymous)));
    response.headers.insert("x-ratelimit-reset", "1793955600");
    return response;
}

HttpResponse FakeGitHub::raw(const LoggedRequest &logged)
{
    // /owner/name/<commit>/<path>
    const QStringList parts = logged.path.mid(1).split(u'/');
    if (parts.size() < 4)
        return message(404, u"404: Not Found"_s);
    const QString name = parts.at(0) + u'/' + parts.at(1);
    const Repo *r = m_repos.contains(name) ? &m_repos[name] : nullptr;
    if (!r || r->isPrivate)
        return message(404, u"404: Not Found"_s);
    QString commit = parts.at(2);
    if (!m_commits.contains(commit))
        commit = r->branches.value(commit);
    const QString path = parts.mid(3).join(u'/');
    const QMap<QString, Entry> flat = flatten(m_commits.value(commit).tree);
    if (!flat.contains(path))
        return message(404, u"404: Not Found"_s);
    HttpResponse response;
    response.status = 200;
    response.headers.insert("content-type", "text/plain; charset=utf-8");
    response.headers.insert("access-control-allow-origin", "*");
    response.body = corrupt.contains(path) ? corrupt.value(path) : m_blobs.value(flat.value(path).sha);
    return response;
}

HttpResponse FakeGitHub::api(const HttpRequest &request, const LoggedRequest &logged)
{
    const QByteArray method = request.method;
    const QStringList parts = logged.path.mid(1).split(u'/');
    if (logged.path == u"/user"_s) {
        if (!logged.authorized)
            return message(401, u"Requires authentication"_s);
        HttpResponse response = json(200, QJsonObject{{u"login"_s, login}, {u"name"_s, u"Jan Kowalski"_s}});
        if (!scopes.isNull())
            response.headers.insert("x-oauth-scopes", scopes.toUtf8());
        return response;
    }
    if (parts.size() < 3 || parts.at(0) != u"repos"_s)
        return message(404, u"Not Found"_s);
    const QString name = parts.at(1) + u'/' + parts.at(2);
    if (!m_repos.contains(name))
        return message(404, u"Not Found"_s);
    if (const auto wait = m_forkWaits.find(name); wait != m_forkWaits.end() && *wait > 0) {
        --*wait;
        return message(404, u"Not Found"_s);
    }
    Repo &r = m_repos[name];
    if (r.isPrivate && !(logged.authorized && (r.owner == login || r.collaborators.contains(login))))
        return message(404, u"Not Found"_s);
    const QStringList rest = parts.mid(3);
    const QString tail = rest.join(u'/');
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    const bool mayPush = canPush(r, logged.authorized);

    if (rest.isEmpty() && method == "GET") {
        HttpResponse response;
        response.status = 200;
        response.body = repoJson(r, logged.authorized);
        return response;
    }
    if (rest.value(0) == u"branches"_s && method == "GET") {
        const QString branch = rest.mid(1).join(u'/');
        if (!r.branches.contains(branch))
            return message(404, u"Branch not found"_s);
        const QString commit = r.branches.value(branch);
        const QByteArray etag = "W/\"" + commit.toUtf8() + '"';
        if (request.header("If-None-Match") == etag) {
            HttpResponse notModified;
            notModified.status = 304;
            notModified.headers.insert("etag", etag);
            return notModified;
        }
        HttpResponse response = json(
                200,
                QJsonObject{{u"name"_s, branch},
                            {u"commit"_s,
                             QJsonObject{{u"sha"_s, commit},
                                         {u"commit"_s,
                                          QJsonObject{{u"tree"_s,
                                                       QJsonObject{{u"sha"_s, m_commits.value(commit).tree}}}}}}}});
        response.headers.insert("etag", etag);
        return response;
    }
    if (tail.startsWith(u"git/matching-refs/heads"_s) && method == "GET") {
        if (r.branches.isEmpty())
            return message(409, u"Git Repository is empty."_s);
        const QString prefix = tail.mid(QStringLiteral("git/matching-refs/heads").size()).section(u'/', 1);
        QJsonArray refs;
        for (auto it = r.branches.cbegin(); it != r.branches.cend(); ++it) {
            if (prefix.isEmpty() || it.key().startsWith(prefix))
                refs.append(QJsonObject{{u"ref"_s, QString(u"refs/heads/"_s + it.key())},
                                        {u"object"_s, QJsonObject{{u"sha"_s, it.value()}}}});
        }
        return json(200, refs);
    }
    if (rest.value(0) == u"git"_s && rest.value(1) == u"trees"_s && method == "GET") {
        if (r.branches.isEmpty())
            return message(409, u"Git Repository is empty."_s);
        const QString sha = rest.value(2);
        if (!m_trees.contains(sha))
            return message(404, u"Not Found"_s);
        QJsonArray entries;
        const bool recursive = logged.query.contains(u"recursive=1"_s);
        if (recursive) {
            // Folders too, as GitHub lists them.
            QSet<QString> folders;
            const QMap<QString, Entry> flat = flatten(sha);
            for (auto it = flat.cbegin(); it != flat.cend(); ++it) {
                QString folder = it.key().section(u'/', 0, -2);
                while (!folder.isEmpty() && !folders.contains(folder)) {
                    folders.insert(folder);
                    entries.append(QJsonObject{{u"path"_s, folder},
                                               {u"mode"_s, u"040000"_s},
                                               {u"type"_s, u"tree"_s},
                                               {u"sha"_s, u"0000"_s}});
                    folder = folder.section(u'/', 0, -2);
                }
                const bool submodule = it.value().mode == u"160000"_s;
                QJsonObject entry{{u"path"_s, it.key()},
                                  {u"mode"_s, it.value().mode},
                                  {u"type"_s, submodule ? u"commit"_s : u"blob"_s},
                                  {u"sha"_s, it.value().sha}};
                if (!submodule)
                    entry.insert(u"size"_s, m_blobs.value(it.value().sha).size());
                entries.append(entry);
            }
        } else {
            const QMap<QString, Entry> &tree = m_trees[sha];
            for (auto it = tree.cbegin(); it != tree.cend(); ++it) {
                const bool folder = it.value().mode == u"040000"_s;
                entries.append(QJsonObject{{u"path"_s, it.key()},
                                           {u"mode"_s, it.value().mode},
                                           {u"type"_s, folder ? u"tree"_s : u"blob"_s},
                                           {u"sha"_s, it.value().sha}});
            }
        }
        return json(200, QJsonObject{{u"sha"_s, sha}, {u"tree"_s, entries}, {u"truncated"_s, false}});
    }
    if (rest.value(0) == u"contents"_s && method == "GET") {
        if (r.branches.isEmpty())
            return message(404, u"This repository is empty."_s);
        QString ref = logged.query.section(u"ref="_s, 1).section(u'&', 0, 0);
        const QString commit = m_commits.contains(ref) ? ref : r.branches.value(ref.isEmpty() ? r.defaultBranch : ref);
        const QString path = rest.mid(1).join(u'/');
        QString tree = m_commits.value(commit).tree;
        for (const QString &segment : path.split(u'/', Qt::SkipEmptyParts)) {
            const Entry entry = m_trees.value(tree).value(segment);
            if (entry.mode != u"040000"_s)
                return message(404, u"Not Found"_s);
            tree = entry.sha;
        }
        QJsonArray entries;
        const QMap<QString, Entry> &listing = m_trees[tree];
        for (auto it = listing.cbegin(); it != listing.cend(); ++it) {
            const bool folder = it.value().mode == u"040000"_s;
            entries.append(QJsonObject{{u"name"_s, it.key()},
                                       {u"path"_s, path.isEmpty() ? it.key() : QString(path + u'/' + it.key())},
                                       {u"sha"_s, it.value().sha},
                                       {u"size"_s, folder ? 0 : m_blobs.value(it.value().sha).size()},
                                       {u"type"_s, folder ? u"dir"_s : u"file"_s}});
        }
        return json(200, entries);
    }
    if (rest.value(0) == u"contents"_s && method == "PUT") {
        if (!mayPush)
            return message(403, u"Resource not accessible by personal access token"_s);
        const QString path = rest.mid(1).join(u'/');
        const QByteArray content = QByteArray::fromBase64(body.value(u"content"_s).toString().toLatin1());
        const QString branch = body.value(u"branch"_s).toString(r.defaultBranch);
        if (!r.branches.isEmpty() && !r.branches.contains(branch))
            return message(404, u"Branch %1 not found"_s.arg(branch));
        const QString commit = push(name, branch, {{path, content}}, body.value(u"message"_s).toString());
        return json(201, QJsonObject{{u"commit"_s, QJsonObject{{u"sha"_s, commit}}}});
    }
    if (rest.value(0) == u"git"_s && rest.value(1) == u"blobs"_s) {
        if (method == "GET") {
            const QString sha = rest.value(2);
            if (!m_blobs.contains(sha))
                return message(404, u"Not Found"_s);
            HttpResponse response;
            response.status = 200;
            response.headers.insert("content-type", "application/vnd.github.raw+json");
            response.body = corrupt.contains(sha) ? corrupt.value(sha) : m_blobs.value(sha);
            return response;
        }
        if (method == "POST") {
            if (r.branches.isEmpty())
                return message(409, u"Git Repository is empty."_s);
            if (!mayPush)
                return message(403, u"Resource not accessible by personal access token"_s);
            const QByteArray content = body.value(u"encoding"_s).toString() == u"base64"_s
                                               ? QByteArray::fromBase64(body.value(u"content"_s).toString().toLatin1())
                                               : body.value(u"content"_s).toString().toUtf8();
            const QString sha = brulion::github::blobSha(content);
            m_blobs.insert(sha, content);
            return json(201, QJsonObject{{u"sha"_s, sha}});
        }
    }
    if (tail == u"git/trees"_s && method == "POST") {
        if (r.branches.isEmpty())
            return message(409, u"Git Repository is empty."_s);
        if (!mayPush)
            return message(403, u"Resource not accessible by personal access token"_s);
        const QString base = body.value(u"base_tree"_s).toString();
        if (!base.isEmpty() && !m_trees.contains(base))
            return message(422, u"base_tree is not a valid tree"_s);
        QMap<QString, Entry> flat = base.isEmpty() ? QMap<QString, Entry>() : flatten(base);
        for (const QJsonValue &value : body.value(u"tree"_s).toArray()) {
            const QJsonObject entry = value.toObject();
            const QString path = entry.value(u"path"_s).toString();
            const QString mode = entry.value(u"mode"_s).toString();
            if (entry.contains(u"content"_s)) {
                const QByteArray content = entry.value(u"content"_s).toString().toUtf8();
                const QString sha = brulion::github::blobSha(content);
                m_blobs.insert(sha, content);
                flat.insert(path, Entry{mode, sha});
            } else if (entry.value(u"sha"_s).isNull()) {
                if (!flat.contains(path))
                    return message(422, u"GitRPC::BadObjectState"_s);
                flat.remove(path);
            } else {
                const QString sha = entry.value(u"sha"_s).toString();
                if (!m_blobs.contains(sha))
                    return message(422, u"tree.sha %1 is not a valid blob"_s.arg(sha));
                flat.insert(path, Entry{mode, sha});
            }
        }
        const QString sha = writeTree(flat);
        return json(201, QJsonObject{{u"sha"_s, sha}});
    }
    if (tail == u"git/commits"_s && method == "POST") {
        if (!mayPush)
            return message(403, u"Resource not accessible by personal access token"_s);
        const QString tree = body.value(u"tree"_s).toString();
        if (!m_trees.contains(tree))
            return message(422, u"Tree SHA does not exist"_s);
        QStringList parents;
        for (const QJsonValue &parent : body.value(u"parents"_s).toArray()) {
            if (!m_commits.contains(parent.toString()))
                return message(422, u"Parent SHA does not exist or is not a commit object"_s);
            parents.append(parent.toString());
        }
        return json(201, QJsonObject{{u"sha"_s, makeCommit(tree, parents, body.value(u"message"_s).toString())}});
    }
    if (rest.value(0) == u"git"_s && rest.value(1) == u"refs"_s && rest.value(2) == u"heads"_s && method == "PATCH") {
        if (!mayPush)
            return message(403, u"Resource not accessible by personal access token"_s);
        const QString branch = rest.mid(3).join(u'/');
        if (!r.branches.contains(branch))
            return message(422, u"Reference does not exist"_s);
        const QString sha = body.value(u"sha"_s).toString();
        if (!m_commits.contains(sha))
            return message(422, u"Object does not exist"_s);
        if (!body.value(u"force"_s).toBool() && !isAncestor(r.branches.value(branch), sha))
            return message(422, u"Update is not a fast forward"_s);
        r.branches.insert(branch, sha);
        return json(200, QJsonObject{{u"ref"_s, QString(u"refs/heads/"_s + branch)},
                                     {u"object"_s, QJsonObject{{u"sha"_s, sha}}}});
    }
    if (tail == u"git/refs"_s && method == "POST") {
        if (!mayPush)
            return message(403, u"Resource not accessible by personal access token"_s);
        const QString ref = body.value(u"ref"_s).toString();
        const QString sha = body.value(u"sha"_s).toString();
        if (!ref.startsWith(u"refs/heads/"_s))
            return message(422, u"Reference name is not valid"_s);
        const QString branch = ref.mid(11);
        if (r.branches.contains(branch))
            return message(422, u"Reference already exists"_s);
        if (!m_commits.contains(sha))
            return message(422, u"Object does not exist"_s);
        r.branches.insert(branch, sha);
        return json(201, QJsonObject{{u"ref"_s, ref}, {u"object"_s, QJsonObject{{u"sha"_s, sha}}}});
    }
    if (tail == u"forks"_s && method == "POST") {
        if (!logged.authorized)
            return message(401, u"Requires authentication"_s);
        const QString forkName = fullName(login, r.name);
        if (!m_repos.contains(forkName)) {
            Repo fork;
            fork.owner = login;
            fork.name = r.name;
            fork.defaultBranch = r.defaultBranch;
            fork.parent = name;
            if (r.branches.contains(r.defaultBranch))
                fork.branches.insert(r.defaultBranch, r.branches.value(r.defaultBranch));
            m_repos.insert(forkName, fork);
            m_forkWaits.insert(forkName, forkDelay);
        }
        HttpResponse response;
        response.status = 202;
        response.body = repoJson(m_repos.value(forkName), true);
        return response;
    }
    if (tail == u"pulls"_s && method == "POST") {
        if (!logged.authorized)
            return message(401, u"Requires authentication"_s);
        const QString head = body.value(u"head"_s).toString();
        const QString base = body.value(u"base"_s).toString();
        for (const PullRequest &pr : std::as_const(pullRequests)) {
            if (pr.repo == name && pr.head == head && pr.state == u"open"_s)
                return message(422, u"Validation Failed: A pull request already exists for %1."_s.arg(head));
        }
        const qsizetype colon = head.indexOf(u':');
        const QString headRepo = colon < 0 ? name : head.left(colon) + u'/' + r.name;
        const QString headBranch = colon < 0 ? head : head.mid(colon + 1);
        if (!m_repos.contains(headRepo) || !m_repos.value(headRepo).branches.contains(headBranch))
            return message(422, u"Validation Failed: head invalid"_s);
        if (!r.branches.contains(base))
            return message(422, u"Validation Failed: base invalid"_s);
        PullRequest pr;
        pr.number = int(pullRequests.size()) + 1;
        pr.repo = name;
        pr.title = body.value(u"title"_s).toString();
        pr.body = body.value(u"body"_s).toString();
        pr.head = head;
        pr.base = base;
        pullRequests.append(pr);
        HttpResponse response;
        response.status = 201;
        response.body = pullJson(pr);
        return response;
    }
    if (rest.value(0) == u"pulls"_s && rest.size() == 2 && method == "GET") {
        for (const PullRequest &pr : std::as_const(pullRequests)) {
            if (pr.repo == name && pr.number == rest.value(1).toInt()) {
                HttpResponse response;
                response.status = 200;
                response.body = pullJson(pr);
                return response;
            }
        }
        return message(404, u"Not Found"_s);
    }
    return message(404, u"Not Found"_s);
}

} // namespace githubtest

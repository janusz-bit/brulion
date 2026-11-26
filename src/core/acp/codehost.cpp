#include "acp/codehost.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::acp {

namespace {

/// A checkpoint's id: 1–200 of [A-Za-z0-9_-], '/'-separated parts (none empty) — gitcli's rule.
bool isCheckpointId(QStringView id)
{
    if (id.isEmpty() || id.size() > 200 || id.startsWith(u'/') || id.endsWith(u'/') || id.contains(u"//"))
        return false;
    for (const QChar c : id) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'_'
                        || u == u'-' || u == u'/';
        if (!ok)
            return false;
    }
    return true;
}

/// An absolute path the host may be named: clean, without a NUL, not too long.
bool absolutePath(const QString &path)
{
    return !path.isEmpty() && path.size() <= MaxRemotePathChars && !path.contains(QChar(u'\0'))
           && QDir::isAbsolutePath(path);
}

/// A value of the address's query (percent-encoded: a path's spaces, "&", "#" …).
QString queryValue(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

} // namespace

QString projectSlug(QStringView text)
{
    // (Letters without their marks: "żółw" → "zolw"; "ł" has no decomposition.)
    QString out;
    bool dash = false;
    for (const QChar c : text.toString().normalized(QString::NormalizationForm_KD)) {
        QChar a = c;
        if (c == u'ł' || c == u'Ł')
            a = u'l';
        if (a.isLetterOrNumber() && a.unicode() < 128) {
            if (dash && !out.isEmpty())
                out += u'-';
            out += a.toLower();
            dash = false;
        } else if (a.category() != QChar::Mark_NonSpacing) {
            dash = true;
        }
        if (out.size() >= 40)
            break;
    }
    while (out.endsWith(u'-'))
        out.chop(1);
    return out.isEmpty() ? u"project"_s : out;
}

bool isProjectId(QStringView id)
{
    if (id.isEmpty() || id.size() > 60 || id.startsWith(u'-') || id.endsWith(u'-') || id.contains(u"--"))
        return false;
    for (const QChar c : id) {
        const char16_t u = c.unicode();
        if (!((u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'-'))
            return false;
    }
    return true;
}

std::optional<QList<HostProject>> parseHostProjects(const QByteArray &json, QString *error)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (!doc.isArray()) {
        if (error)
            *error = parse.error != QJsonParseError::NoError ? parse.errorString()
                                                             : u"no JSON list of projects ([{id, name, path}])"_s;
        return std::nullopt;
    }
    QList<HostProject> out;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        HostProject p;
        p.id = o.value(u"id"_s).toString();
        p.name = o.value(u"name"_s).toString().simplified().left(80);
        const QString path = o.value(u"path"_s).toString();
        const QString root = o.value(u"worktreesRoot"_s).toString();
        if (!isProjectId(p.id) || !absolutePath(path) || (!root.isEmpty() && !absolutePath(root))
            || std::ranges::any_of(out, [&p](const HostProject &q) { return q.id == p.id; }))
            continue;
        p.path = QDir::cleanPath(path);
        p.worktreesRoot = root.isEmpty() ? QString() : QDir::cleanPath(root);
        if (p.name.isEmpty())
            p.name = p.id;
        out.append(p);
        if (out.size() >= MaxHostProjects)
            break;
    }
    return out;
}

QByteArray hostProjectsJson(const QList<HostProject> &projects)
{
    QJsonArray list;
    for (const HostProject &p : projects) {
        QJsonObject o{{u"id"_s, p.id}, {u"name"_s, p.name}, {u"path"_s, p.path}};
        if (!p.worktreesRoot.isEmpty())
            o.insert(u"worktreesRoot"_s, p.worktreesRoot);
        list.append(o);
    }
    return QJsonDocument(list).toJson();
}

QString defaultWorktreesRoot()
{
    return QDir::home().filePath(u".brulion/worktrees"_s);
}

WorktreePlace worktreePlace(const QString &root, const QString &id, QStringView title,
                            const std::function<bool(const QString &)> &taken)
{
    const QString base = projectSlug(title);
    const QDir dir(QDir(root.isEmpty() ? defaultWorktreesRoot() : root).filePath(id));
    QString name = base;
    for (int n = 2; taken && taken(dir.filePath(name)); ++n)
        name = base + u'-' + QString::number(n);
    return {QDir::cleanPath(dir.filePath(name)), u"brulion/"_s + name};
}

QJsonObject CodeSession::toJson() const
{
    QJsonObject o{{u"project"_s, project}};
    if (!worktree.isEmpty())
        o.insert(u"worktree"_s, worktree);
    if (!branch.isEmpty())
        o.insert(u"branch"_s, branch);
    return o;
}

std::optional<CodeSession> CodeSession::fromJson(const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject o = value.toObject();
    CodeSession s;
    s.project = o.value(u"project"_s).toString();
    s.worktree = o.value(u"worktree"_s).toString();
    s.branch = o.value(u"branch"_s).toString();
    if (!isProjectId(s.project) || (!s.worktree.isEmpty() && !absolutePath(s.worktree)) || s.branch.size() > 250
        || s.branch.contains(u'\n') || s.branch.contains(u'\r') || s.branch.contains(QChar(u'\0')))
        return std::nullopt;
    if (!s.worktree.isEmpty())
        s.worktree = QDir::cleanPath(s.worktree);
    return s;
}

QJsonObject withCodeSession(QJsonObject params, const CodeSession &session)
{
    QJsonObject meta = params.value(u"_meta"_s).toObject();
    meta.insert(CodeMetaKey, session.toJson());
    params.insert(u"_meta"_s, meta);
    return params;
}

std::optional<CodeSession> takeCodeSession(QJsonObject &message, bool *malformed)
{
    if (malformed)
        *malformed = false;
    const QString method = message.value(u"method"_s).toString();
    if (method != u"session/new" && method != u"session/load" && method != u"session/resume")
        return std::nullopt;
    QJsonObject params = message.value(u"params"_s).toObject();
    QJsonObject meta = params.value(u"_meta"_s).toObject();
    if (!meta.contains(CodeMetaKey))
        return std::nullopt;
    const std::optional<CodeSession> session = CodeSession::fromJson(meta.value(CodeMetaKey));
    if (!session && malformed)
        *malformed = true;
    meta.remove(CodeMetaKey);
    if (meta.isEmpty())
        params.remove(u"_meta"_s);
    else
        params.insert(u"_meta"_s, meta);
    message.insert(u"params"_s, params);
    return session;
}

QJsonObject checkpointNotification(const RemoteCheckpoint &c)
{
    QJsonObject params{{u"sessionId"_s, c.session},
                       {u"checkpoint"_s, QJsonObject{{u"id"_s, c.id}, {u"at"_s, c.at}}}};
    if (!c.turn.isEmpty())
        params.insert(u"turn"_s, c.turn);
    return QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, QString(CheckpointMethod)}, {u"params"_s, params}};
}

std::optional<RemoteCheckpoint> readCheckpoint(const QJsonObject &params)
{
    const QJsonObject checkpoint = params.value(u"checkpoint"_s).toObject();
    RemoteCheckpoint c;
    c.session = params.value(u"sessionId"_s).toString().left(500);
    c.turn = params.value(u"turn"_s).toString().left(200);
    c.id = checkpoint.value(u"id"_s).toString();
    c.at = checkpoint.value(u"at"_s).toString().left(40);
    if (c.session.isEmpty() || !isCheckpointId(c.id))
        return std::nullopt;
    return c;
}

// ---- The device's side ---------------------------------------------------------------------------

void fetchRemoteProjects(sync::HttpTransport *transport, const QUrl &base, const QString &token,
                         std::function<void(const QList<RemoteProject> &, const RemoteError &)> done)
{
    transport->send(remoteRequest("GET", base, u"projects"_s, token, nullptr),
                    [done](const sync::HttpResponse &response) {
                        QJsonObject o;
                        const RemoteError e = remoteErrorOf(response, &o);
                        QList<RemoteProject> projects;
                        if (!e.failed()) {
                            for (const QJsonValue &v : o.value(u"projects"_s).toArray()) {
                                const QJsonObject p = v.toObject();
                                RemoteProject r{p.value(u"id"_s).toString(),
                                                p.value(u"name"_s).toString().simplified().left(80),
                                                p.value(u"path"_s).toString()};
                                if (!isProjectId(r.id) || !absolutePath(r.path)
                                    || std::ranges::any_of(projects,
                                                           [&r](const RemoteProject &q) { return q.id == r.id; }))
                                    continue;
                                if (r.name.isEmpty())
                                    r.name = r.id;
                                projects.append(r);
                                if (projects.size() >= MaxHostProjects)
                                    break;
                            }
                        }
                        done(projects, e);
                    });
}

void createRemoteWorktree(sync::HttpTransport *transport, const QUrl &base, const QString &token,
                          const QString &project, const QString &title, const QString &from,
                          std::function<void(const RemoteWorktree &, const RemoteError &)> done)
{
    QJsonObject body{{u"title"_s, title.left(MaxWorktreeTitleChars)}};
    if (!from.isEmpty())
        body.insert(u"base"_s, from);
    sync::HttpRequest request =
            remoteRequest("POST", base, u"projects/%1/worktrees"_s.arg(queryValue(project)), token, &body);
    request.timeoutMs += 60000; // (git makes the worktree: a big repository takes a while)
    transport->send(request, [done](const sync::HttpResponse &response) {
        QJsonObject o;
        RemoteError e = remoteErrorOf(response, &o);
        RemoteWorktree w;
        if (!e.failed()) {
            w.folder = o.value(u"folder"_s).toString();
            w.branch = o.value(u"branch"_s).toString();
            if (!absolutePath(w.folder))
                e = RemoteError{RemoteError::Kind::Malformed, response.status, QString(), u"no folder in the answer"_s};
        }
        done(w, e);
    });
}

void fetchRemoteChanges(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &project,
                        const QString &worktree, const QString &since,
                        std::function<void(const QList<RemoteChange> &, bool, const RemoteError &)> done)
{
    QString endpoint = u"projects/%1/changes?worktree=%2&since=%3"_s.arg(queryValue(project), queryValue(worktree),
                                                                          queryValue(since));
    transport->send(remoteRequest("GET", base, endpoint, token, nullptr), [done](const sync::HttpResponse &response) {
        QJsonObject o;
        const RemoteError e = remoteErrorOf(response, &o);
        QList<RemoteChange> changes;
        if (!e.failed()) {
            for (const QJsonValue &v : o.value(u"changes"_s).toArray()) {
                const QJsonObject c = v.toObject();
                RemoteChange r{c.value(u"path"_s).toString(), c.value(u"oldPath"_s).toString(),
                               c.value(u"change"_s).toString(),  c.value(u"additions"_s).toInt(-1),
                               c.value(u"deletions"_s).toInt(-1), c.value(u"binary"_s).toBool()};
                if (r.path.isEmpty() || r.path.size() > MaxRemotePathChars)
                    continue;
                changes.append(r);
                if (changes.size() >= MaxRemoteChanges)
                    break;
            }
        }
        done(changes, o.value(u"truncated"_s).toBool(), e);
    });
}

void fetchRemoteDiff(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &project,
                     const QString &worktree, const QString &since, const QString &path, const QString &oldPath,
                     std::function<void(const RemoteDiff &, const RemoteError &)> done)
{
    const QString endpoint = u"projects/%1/diff?worktree=%2&since=%3&path=%4&oldPath=%5"_s.arg(
            queryValue(project), queryValue(worktree), queryValue(since), queryValue(path), queryValue(oldPath));
    transport->send(remoteRequest("GET", base, endpoint, token, nullptr), [done](const sync::HttpResponse &response) {
        QJsonObject o;
        const RemoteError e = remoteErrorOf(response, &o);
        RemoteDiff d;
        if (!e.failed()) {
            d.path = o.value(u"path"_s).toString();
            d.oldText = o.value(u"oldText"_s).toString();
            d.newText = o.value(u"newText"_s).toString();
            d.oldExists = o.value(u"oldExists"_s).toBool();
            d.newExists = o.value(u"newExists"_s).toBool();
            d.binary = o.value(u"binary"_s).toBool();
            d.tooLarge = o.value(u"tooLarge"_s).toBool();
        }
        done(d, e);
    });
}

} // namespace brulion::acp

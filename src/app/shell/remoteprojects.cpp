#include "remoteprojects.h"

#include "log/categories.h"
#include "sync/http.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// The letter the changes panel shows for a change's name (gitcli::changeLetter()'s).
QString letterOf(const QString &change)
{
    if (change == u"added")
        return u"A"_s;
    if (change == u"deleted")
        return u"D"_s;
    if (change == u"renamed")
        return u"R"_s;
    if (change == u"untracked")
        return u"U"_s;
    if (change == u"conflicted")
        return u"C"_s;
    return u"M"_s;
}

QString baseOf(const QString &url)
{
    return acp::normalizeRemoteBase(url).toString();
}

} // namespace

RemoteProjects::RemoteProjects(QObject *parent) : QObject(parent) { }

RemoteProjects::~RemoteProjects()
{
    *m_alive = false;
}

void RemoteProjects::setStorage(std::function<QByteArray()> load, std::function<void(const QByteArray &)> save)
{
    m_load = std::move(load);
    m_save = std::move(save);
    m_loaded = false;
    this->load();
}

void RemoteProjects::load()
{
    if (m_loaded || !m_load)
        return;
    const QByteArray json = m_load();
    if (json.isEmpty())
        return; // (no notebook's state yet: asked again later)
    m_loaded = true;
    const QJsonObject all = QJsonDocument::fromJson(json).object();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (m_lists.contains(it.key()))
            continue; // (what a computer said since is newer)
        QList<acp::RemoteProject> list;
        for (const QJsonValue &v : it.value().toArray()) {
            const QJsonObject o = v.toObject();
            const acp::RemoteProject p{o.value(u"id"_s).toString(), o.value(u"name"_s).toString(),
                                       o.value(u"path"_s).toString()};
            if (acp::isProjectId(p.id) && list.size() < acp::MaxHostProjects)
                list.append(p);
        }
        m_lists.insert(it.key(), list);
    }
}

void RemoteProjects::save()
{
    if (!m_save)
        return;
    QJsonObject all;
    for (const Computer &c : std::as_const(m_computers)) {
        QJsonArray list;
        for (const acp::RemoteProject &p : m_lists.value(c.url))
            list.append(QJsonObject{{u"id"_s, p.id}, {u"name"_s, p.name}, {u"path"_s, p.path}});
        all.insert(c.url, list);
    }
    m_save(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

void RemoteProjects::setComputers(const QList<Computer> &computers)
{
    QList<Computer> clean;
    for (const Computer &c : computers) {
        Computer n = c;
        n.url = baseOf(c.url);
        if (n.url.isEmpty() || n.token.isEmpty()
            || std::ranges::any_of(clean, [&n](const Computer &o) { return o.url == n.url; }))
            continue;
        clean.append(n);
    }
    if (clean == m_computers)
        return;
    m_computers = clean;
    // (A computer no longer paired: its projects go.)
    for (const QString &url : m_lists.keys()) {
        if (std::ranges::none_of(m_computers, [&url](const Computer &c) { return c.url == url; }))
            m_lists.remove(url);
    }
    load();
    Q_EMIT changed();
    refresh();
}

QString RemoteProjects::unreachable() const
{
    QStringList names;
    for (const Computer &c : m_computers) {
        if (m_failed.contains(c.url))
            names << (c.host.isEmpty() ? QUrl(c.url).host() : c.host);
    }
    return names.join(u", "_s);
}

QList<RemoteProjects::Project> RemoteProjects::projects() const
{
    QList<Project> out;
    for (const Computer &c : m_computers) {
        for (const acp::RemoteProject &p : m_lists.value(c.url)) {
            if (std::ranges::any_of(out, [&p](const Project &q) { return q.id == p.id; }))
                continue;
            out.append({p.id, p.name, p.path, c.host.isEmpty() ? QUrl(c.url).host() : c.host, c.url});
        }
    }
    return out;
}

std::optional<RemoteProjects::Project> RemoteProjects::find(const QString &id) const
{
    for (const Project &p : projects()) {
        if (p.id == id)
            return p;
    }
    return std::nullopt;
}

std::optional<RemoteProjects::Computer> RemoteProjects::computerOf(const QString &id) const
{
    const std::optional<Project> p = find(id);
    if (!p)
        return std::nullopt;
    for (const Computer &c : m_computers) {
        if (c.url == p->url)
            return c;
    }
    return std::nullopt;
}

void RemoteProjects::refresh()
{
    load();
    for (const Computer &c : std::as_const(m_computers))
        ask(c);
}

void RemoteProjects::ask(const Computer &computer)
{
    sync::HttpTransport *transport = m_transport ? m_transport() : nullptr;
    if (!transport || m_asking.contains(computer.url))
        return;
    m_asking.insert(computer.url);
    Q_EMIT changed();
    const std::weak_ptr<bool> alive = m_alive;
    const QString url = computer.url;
    acp::fetchRemoteProjects(transport, QUrl(url), computer.token,
                             [this, alive, url](const QList<acp::RemoteProject> &list, const acp::RemoteError &error) {
                                 if (alive.expired())
                                     return;
                                 m_asking.remove(url);
                                 const bool known =
                                         std::ranges::any_of(m_computers, [&url](const Computer &c) { return c.url == url; });
                                 if (!known) {
                                     Q_EMIT changed();
                                     return;
                                 }
                                 if (error.failed() && error.status == 404) {
                                     // (A computer that runs no threads: an older one, or without projects.)
                                     m_failed.remove(url);
                                     m_lists.insert(url, {});
                                 } else if (error.failed()) {
                                     // (Not reached: what it said last stays.)
                                     qCInfo(logging::lcApp, "Code mode: the projects of %s not read: %s",
                                            qUtf8Printable(url), qUtf8Printable(error.detail));
                                     m_failed.insert(url);
                                 } else {
                                     m_failed.remove(url);
                                     m_lists.insert(url, list);
                                 }
                                 save();
                                 Q_EMIT changed();
                             });
}

QString RemoteProjects::errorText(const Computer &computer, const acp::RemoteError &error) const
{
    const QString host = computer.host.isEmpty() ? QUrl(computer.url).host() : computer.host;
    switch (error.kind) {
    case acp::RemoteError::Kind::Network:
        return tr("%1 cannot be reached: %2").arg(host, error.detail);
    case acp::RemoteError::Kind::Unauthorized:
        return tr("This device is no longer paired with %1: pair it again in Settings → Extensions → AI agents.")
                .arg(host);
    default:
        return error.detail.isEmpty() ? tr("%1 refused it.").arg(host) : tr("%1: %2").arg(host, error.detail);
    }
}

bool RemoteProjects::createWorktree(const QString &id, const QString &title)
{
    const std::optional<Computer> computer = computerOf(id);
    sync::HttpTransport *transport = m_transport ? m_transport() : nullptr;
    if (!computer || !transport)
        return false;
    const std::weak_ptr<bool> alive = m_alive;
    const Computer c = *computer;
    acp::createRemoteWorktree(transport, QUrl(c.url), c.token, id, title, QString(),
                              [this, alive, id, c](const acp::RemoteWorktree &w, const acp::RemoteError &error) {
                                  if (alive.expired())
                                      return;
                                  if (error.failed())
                                      Q_EMIT worktreeCreated(id, QString(), QString(), errorText(c, error));
                                  else
                                      Q_EMIT worktreeCreated(id, w.folder, w.branch, QString());
                              });
    return true;
}

int RemoteProjects::requestChanges(const QString &id, const QString &worktree, const QString &since)
{
    const int serial = ++m_serial;
    const std::optional<Computer> computer = computerOf(id);
    sync::HttpTransport *transport = m_transport ? m_transport() : nullptr;
    if (!computer || !transport) {
        // (After this call returns, as an answer would come.)
        QMetaObject::invokeMethod(
                this, [this, serial] { Q_EMIT changesReady(serial, {}, false, tr("No computer of yours has this project.")); },
                Qt::QueuedConnection);
        return serial;
    }
    const std::weak_ptr<bool> alive = m_alive;
    const Computer c = *computer;
    acp::fetchRemoteChanges(
            transport, QUrl(c.url), c.token, id, worktree, since,
            [this, alive, serial, c](const QList<acp::RemoteChange> &changes, bool truncated, const acp::RemoteError &error) {
                if (alive.expired())
                    return;
                QVariantList list;
                for (const acp::RemoteChange &r : changes) {
                    // (The name and its folder apart: the list shows them so.)
                    const qsizetype slash = r.path.lastIndexOf(u'/');
                    list << QVariantMap{{u"path"_s, r.path},
                                        {u"oldPath"_s, r.oldPath},
                                        {u"change"_s, r.change},
                                        {u"letter"_s, letterOf(r.change)},
                                        {u"name"_s, slash < 0 ? r.path : r.path.mid(slash + 1)},
                                        {u"dir"_s, slash < 0 ? QString() : r.path.left(slash)},
                                        {u"additions"_s, r.additions},
                                        {u"deletions"_s, r.deletions},
                                        {u"binary"_s, r.binary}};
                }
                Q_EMIT changesReady(serial, list, truncated, error.failed() ? errorText(c, error) : QString());
            });
    return serial;
}

int RemoteProjects::requestDiff(const QString &id, const QString &worktree, const QString &since, const QString &path,
                                const QString &oldPath)
{
    const int serial = ++m_serial;
    const std::optional<Computer> computer = computerOf(id);
    sync::HttpTransport *transport = m_transport ? m_transport() : nullptr;
    if (!computer || !transport) {
        QMetaObject::invokeMethod(
                this,
                [this, serial, path] {
                    Q_EMIT diffReady(serial, path, {}, {}, false, false, tr("No computer of yours has this project."));
                },
                Qt::QueuedConnection);
        return serial;
    }
    const std::weak_ptr<bool> alive = m_alive;
    const Computer c = *computer;
    acp::fetchRemoteDiff(transport, QUrl(c.url), c.token, id, worktree, since, path, oldPath,
                         [this, alive, serial, c](const acp::RemoteDiff &d, const acp::RemoteError &error) {
                             if (alive.expired())
                                 return;
                             Q_EMIT diffReady(serial, d.path, d.oldText, d.newText, d.binary, d.tooLarge,
                                              error.failed() ? errorText(c, error) : QString());
                         });
    return serial;
}

// ---- Git there ----------------------------------------------------------------------------------------

QString RemoteProjects::subjectOf(const QString &message)
{
    const QString trimmed = message.trimmed();
    return trimmed.left(trimmed.indexOf(u'\n')).trimmed();
}

void RemoteProjects::gitFinished(int serial, const QString &action, bool ok, const QString &message, const QString &url)
{
    // (After the call that asked returns, as an answer from there would come.)
    QMetaObject::invokeMethod(
            this, [this, serial, action, ok, message, url] { Q_EMIT gitDone(serial, action, ok, message, url); },
            Qt::QueuedConnection);
}

void RemoteProjects::askGit(int serial, const QString &id, const QString &action, const acp::RemoteGitRequest &request,
                            const QString &failure, std::function<void(const acp::RemoteGitResult &)> then)
{
    const std::optional<Computer> computer = computerOf(id);
    sync::HttpTransport *transport = m_transport ? m_transport() : nullptr;
    if (!computer || !transport) {
        gitFinished(serial, action, false, tr("No computer of yours has this project."));
        return;
    }
    const std::weak_ptr<bool> alive = m_alive;
    const Computer c = *computer;
    acp::remoteGit(transport, QUrl(c.url), c.token, id, request,
                   [this, alive, serial, action, failure, c, then](const acp::RemoteGitResult &result,
                                                                    const acp::RemoteError &error) {
                       if (alive.expired())
                           return;
                       if (error.failed()) {
                           const QString why = errorText(c, error);
                           gitFinished(serial, action, false, failure.isEmpty() ? why : failure.arg(why));
                           return;
                       }
                       then(result);
                   });
}

int RemoteProjects::commit(const QString &id, const QString &worktree, const QString &message, const QStringList &files,
                           bool push, bool createPr, const QString &prTitle, const QString &prBody, bool draft)
{
    const int serial = ++m_serial;
    acp::RemoteGitRequest request;
    request.worktree = worktree;
    request.message = message;
    request.files = files;
    if (request.problem() == u"bad_message") {
        gitFinished(serial, u"commit"_s, false, tr("Write a commit message first."));
        return serial;
    }
    askGit(serial, id, u"commit"_s, request, QString(),
           [this, serial, id, worktree, message, push, createPr, prTitle, prBody, draft](const acp::RemoteGitResult &c) {
               const QString commit = c.commit.left(7);
               if (!push && !createPr) {
                   gitFinished(serial, u"commit"_s, true,
                               c.branch.isEmpty() ? tr("Committed %1").arg(commit)
                                                  : tr("Committed %1 on %2").arg(commit, c.branch));
                   return;
               }
               acp::RemoteGitRequest next;
               next.worktree = worktree;
               if (!createPr) {
                   next.kind = acp::RemoteGitRequest::Kind::Push;
                   askGit(serial, id, u"commit"_s, next, tr("Committed, but the push failed: %1"),
                          [this, serial](const acp::RemoteGitResult &p) {
                              gitFinished(serial, u"commit"_s, true, tr("Committed and pushed to %1").arg(p.remote));
                          });
                   return;
               }
               next.kind = acp::RemoteGitRequest::Kind::PullRequest;
               next.title = prTitle.trimmed().isEmpty() ? subjectOf(message) : prTitle.trimmed();
               next.body = prBody;
               next.draft = draft;
               askGit(serial, id, u"commit"_s, next, tr("Committed, but no pull request: %1"),
                      [this, serial](const acp::RemoteGitResult &p) {
                          gitFinished(serial, u"commit"_s, true,
                                      p.number > 0 ? tr("Committed, pushed and opened pull request #%1").arg(p.number)
                                                   : tr("Committed, pushed and opened the pull request"),
                                      p.url);
                      });
           });
    return serial;
}

int RemoteProjects::push(const QString &id, const QString &worktree)
{
    const int serial = ++m_serial;
    acp::RemoteGitRequest request;
    request.kind = acp::RemoteGitRequest::Kind::Push;
    request.worktree = worktree;
    askGit(serial, id, u"push"_s, request, QString(), [this, serial](const acp::RemoteGitResult &p) {
        gitFinished(serial, u"push"_s, true, tr("Pushed %1 to %2").arg(p.branch, p.remote));
    });
    return serial;
}

int RemoteProjects::createPullRequest(const QString &id, const QString &worktree, const QString &title,
                                      const QString &body, const QString &base, bool draft)
{
    const int serial = ++m_serial;
    acp::RemoteGitRequest request;
    request.kind = acp::RemoteGitRequest::Kind::PullRequest;
    request.worktree = worktree;
    request.title = title.trimmed();
    request.body = body;
    request.base = base.trimmed();
    request.draft = draft;
    if (request.problem() == u"bad_title") {
        gitFinished(serial, u"pr"_s, false, tr("A pull request needs a title."));
        return serial;
    }
    askGit(serial, id, u"pr"_s, request, QString(), [this, serial](const acp::RemoteGitResult &p) {
        gitFinished(serial, u"pr"_s, true,
                    p.number > 0 ? tr("Opened pull request #%1").arg(p.number) : tr("Opened the pull request"), p.url);
    });
    return serial;
}

int RemoteProjects::revertTurn(const QString &id, const QString &worktree, const QString &checkpoint)
{
    const int serial = ++m_serial;
    acp::RemoteGitRequest request;
    request.kind = acp::RemoteGitRequest::Kind::Revert;
    request.worktree = worktree;
    request.checkpoint = checkpoint;
    askGit(serial, id, u"revert"_s, request, QString(), [this, serial](const acp::RemoteGitResult &r) {
        gitFinished(serial, u"revert"_s, true,
                    tr("The turn was reverted: %n file(s) put back", "", int(r.restored.size() + r.removed.size())));
    });
    return serial;
}

} // namespace brulion::shell

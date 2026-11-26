// CodeProjects: git and gh for Code mode's threads (docs/CODE.md "Worktrees, branches, pull
// requests") — a thread's worktree made from a base branch, the branches of a folder, switching and
// creating them, a pull request checked out locally or in a new worktree, a worktree checked for
// uncommitted changes, removed and made again. Everything through gitcli::Client: QProcess with
// argument lists (never a shell), its environment rules (docs/CODE-GIT.md "Security"),
// asynchronous — each ends with a signal.

#include "codeprojects.h"

#include "gitcli/client.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// `out` of a run as one trimmed line (the first).
QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out).section(u'\n', 0, 0).trimmed();
}

} // namespace

gitcli::Client *CodeProjects::git() const
{
    if (!m_git)
        m_git = new gitcli::Client(const_cast<CodeProjects *>(this));
    return m_git;
}

bool CodeProjects::gitAvailable() const
{
    return gitcli::Client::processesAvailable() && !git()->gitProgram().isEmpty();
}

bool CodeProjects::ghAvailable() const
{
    return gitcli::Client::processesAvailable() && !git()->ghProgram().isEmpty();
}

void CodeProjects::setPrograms(const QString &gitProgram, const QString &gh)
{
    git()->setGitProgram(gitProgram);
    git()->setGhProgram(gh);
}

bool CodeProjects::validBranchName(const QString &name) const
{
    return gitcli::isValidBranchName(name);
}

int CodeProjects::parsePullRequest(const QString &text)
{
    const QString t = text.trimmed();
    static const QRegularExpression bare(u"^#?([1-9][0-9]{0,8})$"_s);
    if (const QRegularExpressionMatch m = bare.match(t); m.hasMatch())
        return m.captured(1).toInt();
    // https://github.com/owner/repo/pull/123, …/pull/123/files, ?query or #fragment after it.
    static const QRegularExpression url(u"^https?://[^\\s/]+/[^\\s]*?/pull/([1-9][0-9]{0,8})(?:[/?#][^\\s]*)?$"_s);
    if (const QRegularExpressionMatch m = url.match(t); m.hasMatch())
        return m.captured(1).toInt();
    return 0;
}

QVariantMap CodeProjects::worktreeFor(const QString &id, const QString &title) const
{
    if (indexOf(id) < 0)
        return {};
    const QString base = slug(title);
    const QDir dir(QDir(worktreesRoot()).filePath(id));
    QString name = base;
    for (int n = 2; QFileInfo::exists(dir.filePath(name)); ++n)
        name = base + u'-' + QString::number(n);
    return {{u"folder"_s, dir.filePath(name)}, {u"branch"_s, QString(m_branchPrefix + name)}};
}

void CodeProjects::freeName(const QString &folder, const QString &dir, const QString &base,
                            std::function<void(const QString &, const QString &)> done)
{
    gitcli::RunOptions ro;
    ro.readOnly = true;
    git()->git(folder, {u"for-each-ref"_s, u"--format=%(refname:short)"_s, u"refs/heads/"_s}, ro,
               [this, dir, base, done = std::move(done)](const gitcli::RunResult &r) {
                   QSet<QString> taken;
                   for (const QString &line : QString::fromUtf8(r.out).split(u'\n', Qt::SkipEmptyParts))
                       taken.insert(line.trimmed());
                   QString name = base;
                   for (int n = 2; QFileInfo::exists(QDir(dir).filePath(name)) || taken.contains(m_branchPrefix + name);
                        ++n)
                       name = base + u'-' + QString::number(n);
                   done(m_branchPrefix + name, name);
               });
}

bool CodeProjects::createWorktree(const QString &id, const QString &title, const QString &base)
{
    const QString repo = folderOf(id);
    if (repo.isEmpty() || !gitAvailable())
        return false;
    const QString from = base.trimmed();
    const QString dir = QDir(worktreesRoot()).filePath(id);
    const QString name = slug(title);
    const auto fail = [this, id](const QString &error) { Q_EMIT worktreeCreated(id, QString(), QString(), error); };
    // (Never from inside the call: the caller sets up its wait after it returned.)
    const auto failLater = [this, fail](const QString &error) {
        QMetaObject::invokeMethod(this, [fail, error] { fail(error); }, Qt::QueuedConnection);
    };
    // Made once the base is known (a remote-tracking one fetched first: the newest of it).
    const auto make = [this, id, repo, from, dir, name, fail] {
        freeName(repo, dir, name, [this, id, repo, from, dir, fail](const QString &branch, const QString &folderName) {
            QDir().mkpath(dir);
            const QString folder = QDir(dir).filePath(folderName);
            git()->addWorktree(repo, folder, branch, from.isEmpty() ? u"HEAD"_s : from,
                               [this, id, folder, branch, fail](const gitcli::Outcome &o) {
                                   if (!o.ok)
                                       fail(o.message.isEmpty() ? tr("git could not make the worktree.")
                                                                : o.message.right(500));
                                   else
                                       Q_EMIT worktreeCreated(id, folder, branch, QString());
                               });
        });
    };
    if (from.isEmpty() || from.startsWith(u'-')) {
        if (from.startsWith(u'-')) {
            failLater(tr("“%1” is no branch.").arg(from));
            return true;
        }
        make();
        return true;
    }
    gitcli::RunOptions ro;
    ro.readOnly = true;
    git()->git(repo, {u"show-ref"_s, u"--verify"_s, u"--quiet"_s, u"refs/remotes/"_s + from}, ro,
               [this, repo, from, make](const gitcli::RunResult &remote) {
                   if (!remote.ok() || !from.contains(u'/')) {
                       make();
                       return;
                   }
                   // (Fetched first; offline it goes on from what is known of it.)
                   gitcli::RunOptions net;
                   net.timeoutMs = git()->networkTimeoutMs();
                   git()->git(repo, {u"fetch"_s, u"--"_s, from.section(u'/', 0, 0), from.section(u'/', 1)}, net,
                              [make](const gitcli::RunResult &) { make(); });
               });
    return true;
}

bool CodeProjects::listWorktrees(const QString &id)
{
    const QString repo = folderOf(id);
    if (repo.isEmpty() || !gitAvailable())
        return false;
    git()->worktrees(repo, [this, id, repo](const gitcli::WorktreeList &list) {
        QVariantList out;
        const QString own = QFileInfo(repo).canonicalFilePath();
        for (const gitcli::Worktree &w : list.worktrees) {
            if (w.bare || QDir::cleanPath(w.path) == QDir::cleanPath(repo)
                || (!own.isEmpty() && QFileInfo(w.path).canonicalFilePath() == own))
                continue;
            out << QVariantMap{{u"folder"_s, QDir::cleanPath(w.path)},
                               {u"branch"_s, w.branch},
                               {u"detached"_s, w.detached},
                               {u"exists"_s, QFileInfo(w.path).isDir()}};
        }
        Q_EMIT worktreesListed(id, out, list.ok ? QString() : list.error);
    });
    return true;
}

bool CodeProjects::listBranches(const QString &folder)
{
    if (folder.isEmpty() || !QFileInfo(folder).isDir() || !gitAvailable())
        return false;
    gitcli::RunOptions ro;
    ro.readOnly = true;
    // The default branch first (origin's HEAD), then the branches.
    git()->git(folder, {u"symbolic-ref"_s, u"--quiet"_s, u"--short"_s, u"refs/remotes/origin/HEAD"_s}, ro,
               [this, folder](const gitcli::RunResult &head) {
                   const QString originHead = head.ok() ? firstLine(head.out) : QString();
                   git()->branches(folder, [this, folder, originHead](const gitcli::BranchList &list) {
                       QString localDefault = originHead.section(u'/', 1);
                       if (localDefault.isEmpty()) {
                           for (const QString &name : {u"main"_s, u"master"_s, u"trunk"_s})
                               if (std::ranges::any_of(list.branches, [&name](const gitcli::Branch &b) {
                                       return !b.remote && b.name == name;
                                   })) {
                                   localDefault = name;
                                   break;
                               }
                       }
                       QSet<QString> tracked;
                       for (const gitcli::Branch &b : list.branches)
                           if (!b.remote && !b.upstream.isEmpty())
                               tracked.insert(b.upstream);
                       QVariantList local, remote;
                       for (const gitcli::Branch &b : list.branches) {
                           // (A remote one a local branch tracks: that local one stands for it.)
                           if (b.remote && tracked.contains(b.name))
                               continue;
                           const QVariantMap row{
                                   {u"name"_s, b.name},
                                   {u"remote"_s, b.remote},
                                   {u"current"_s, b.current},
                                   {u"isDefault"_s, b.remote ? b.name == originHead : b.name == localDefault},
                                   {u"worktree"_s, b.worktreePath.isEmpty() ? QString() : QDir::cleanPath(b.worktreePath)},
                                   {u"upstream"_s, b.upstream},
                                   {u"ahead"_s, b.ahead},
                                   {u"behind"_s, b.behind}};
                           (b.remote ? remote : local) << row;
                       }
                       Q_EMIT branchesListed(folder, local + remote, list.ok ? QString() : list.error);
                   });
               });
    return true;
}

bool CodeProjects::hasBranch(const QVariantList &branches, const QString &name) const
{
    const QString wanted = name.trimmed();
    return std::ranges::any_of(branches, [&wanted](const QVariant &v) {
        const QVariantMap m = v.toMap();
        const QString n = m.value(u"name"_s).toString();
        return n == wanted || (m.value(u"remote"_s).toBool() && n.section(u'/', 1) == wanted);
    });
}

QVariantList CodeProjects::branchItems(const QVariantList &branches) const
{
    QVariantList out;
    for (const QVariant &v : branches) {
        const QVariantMap b = v.toMap();
        QStringList badges;
        if (b.value(u"current"_s).toBool())
            badges << tr("current");
        if (b.value(u"isDefault"_s).toBool())
            badges << tr("default");
        if (b.value(u"remote"_s).toBool())
            badges << tr("remote");
        const QString worktree = b.value(u"worktree"_s).toString();
        if (!worktree.isEmpty() && !b.value(u"current"_s).toBool())
            badges << tr("in the worktree %1").arg(QFileInfo(worktree).fileName());
        const int ahead = b.value(u"ahead"_s).toInt(), behind = b.value(u"behind"_s).toInt();
        if (ahead > 0)
            badges << u"↑%1"_s.arg(ahead);
        if (behind > 0)
            badges << u"↓%1"_s.arg(behind);
        out << QVariantMap{{u"title"_s, b.value(u"name"_s)},
                           {u"subtitle"_s, badges.join(u" · "_s)},
                           {u"icon"_s, b.value(u"remote"_s).toBool() ? u"globe"_s : u"git-branch"_s},
                           {u"name"_s, b.value(u"name"_s)},
                           {u"remote"_s, b.value(u"remote"_s)},
                           {u"current"_s, b.value(u"current"_s)},
                           {u"worktree"_s, worktree},
                           {u"kind"_s, u"branch"_s}};
    }
    return out;
}

QVariantList CodeProjects::branchOffers(const QString &query, const QVariantList &branches, bool canCreate) const
{
    const QString text = query.trimmed();
    if (text.isEmpty())
        return {};
    if (const int number = parsePullRequest(text); number > 0)
        return {QVariantMap{{u"title"_s, tr("Check out pull request #%1").arg(number)},
                            {u"subtitle"_s, tr("Locally or in a new worktree")},
                            {u"icon"_s, u"git-pull-request"_s},
                            {u"kind"_s, u"pr"_s},
                            {u"number"_s, number}}};
    if (!canCreate || !validBranchName(text) || hasBranch(branches, text))
        return {};
    return {QVariantMap{{u"title"_s, tr("Create branch “%1”").arg(text)},
                        {u"subtitle"_s, tr("From the checked-out commit, and switch to it")},
                        {u"icon"_s, u"plus"_s},
                        {u"kind"_s, u"create"_s},
                        {u"name"_s, text}}};
}

bool CodeProjects::switchBranch(const QString &folder, const QString &name)
{
    if (folder.isEmpty() || !gitAvailable())
        return false;
    git()->checkout(folder, name, [this, folder, name](const gitcli::Outcome &o) {
        // (A remote-tracking one: the local branch made for it.)
        const QString local = name.contains(u'/') && o.ok ? name.section(u'/', 1) : name;
        Q_EMIT branchSwitched(folder, o.ok ? local : name, o.ok, o.message);
    });
    return true;
}

bool CodeProjects::createBranch(const QString &folder, const QString &name)
{
    if (folder.isEmpty() || !gitAvailable())
        return false;
    git()->createBranch(folder, name.trimmed(), [this, folder, name](const gitcli::Outcome &o) {
        Q_EMIT branchSwitched(folder, name.trimmed(), o.ok, o.message);
    });
    return true;
}

void CodeProjects::remoteOf(const QString &folder, std::function<void(const QString &, const QString &)> done)
{
    gitcli::RunOptions ro;
    ro.readOnly = true;
    git()->git(folder, {u"remote"_s}, ro, [done = std::move(done)](const gitcli::RunResult &r) {
        const QStringList remotes = QString::fromUtf8(r.out).split(u'\n', Qt::SkipEmptyParts);
        if (!r.ok() || remotes.isEmpty()) {
            done(QString(), r.ok() ? tr("The repository has no remote to fetch the pull request from.") : r.message());
            return;
        }
        done(remotes.contains(u"origin"_s) ? u"origin"_s : remotes.first().trimmed(), QString());
    });
}

bool CodeProjects::checkoutPullRequest(const QString &id, int number, bool inWorktree)
{
    const QString repo = folderOf(id);
    if (repo.isEmpty() || number <= 0 || !gitAvailable())
        return false;
    const bool gh = ghAvailable();
    const QString n = QString::number(number);
    const auto fail = [this, id, number](const QString &error) {
        Q_EMIT pullRequestCheckedOut(id, number, QString(), QString(), QString(), error);
    };
    // Its branch, then its title (where gh is), then done.
    const auto finish = [this, id, number, n, gh](const QString &folder, const QString &where) {
        gitcli::RunOptions ro;
        ro.readOnly = true;
        git()->git(where, {u"branch"_s, u"--show-current"_s}, ro,
                   [this, id, number, n, gh, folder, where](const gitcli::RunResult &b) {
                       const QString branch = firstLine(b.out);
                       if (!gh) {
                           Q_EMIT pullRequestCheckedOut(id, number, folder, branch, QString(), QString());
                           return;
                       }
                       gitcli::RunOptions net;
                       net.timeoutMs = git()->networkTimeoutMs();
                       git()->gh(where, {u"pr"_s, u"view"_s, n, u"--json"_s, u"title"_s}, net,
                                 [this, id, number, folder, branch](const gitcli::RunResult &v) {
                                     const QString title = QJsonDocument::fromJson(v.out)
                                                                   .object()
                                                                   .value(u"title"_s)
                                                                   .toString()
                                                                   .simplified()
                                                                   .left(200);
                                     Q_EMIT pullRequestCheckedOut(id, number, folder, branch, title, QString());
                                 });
                   });
    };
    gitcli::RunOptions net;
    net.timeoutMs = git()->networkTimeoutMs();
    if (!inWorktree) {
        // In the project's folder: not over uncommitted changes.
        git()->status(repo, [this, repo, n, gh, net, fail, finish](const gitcli::StatusReport &s) {
            if (!s.ok) {
                fail(s.error);
                return;
            }
            const bool dirty = std::ranges::any_of(s.changes, [](const gitcli::FileChange &c) {
                return c.change != gitcli::Change::Untracked && c.change != gitcli::Change::Ignored;
            });
            if (dirty) {
                fail(tr("The project's folder has uncommitted changes: commit them first, or check the pull request "
                        "out in a new worktree."));
                return;
            }
            if (gh) {
                git()->gh(repo, {u"pr"_s, u"checkout"_s, n}, net, [repo, fail, finish](const gitcli::RunResult &r) {
                    if (!r.ok())
                        fail(r.message());
                    else
                        finish(QString(), repo);
                });
                return;
            }
            remoteOf(repo, [this, repo, n, net, fail, finish](const QString &remote, const QString &error) {
                if (remote.isEmpty()) {
                    fail(error);
                    return;
                }
                const QString branch = m_branchPrefix + u"pr-"_s + n;
                git()->git(repo, {u"fetch"_s, u"--"_s, remote, u"pull/"_s + n + u"/head:"_s + branch}, net,
                           [this, repo, branch, fail, finish](const gitcli::RunResult &r) {
                               if (!r.ok()) {
                                   fail(r.message());
                                   return;
                               }
                               git()->checkout(repo, branch, [repo, fail, finish](const gitcli::Outcome &o) {
                                   if (!o.ok)
                                       fail(o.message);
                                   else
                                       finish(QString(), repo);
                               });
                           });
            });
        });
        return true;
    }
    // In a new worktree of its own.
    const QString dir = QDir(worktreesRoot()).filePath(id);
    freeName(repo, dir, u"pr-"_s + n, [this, repo, dir, n, gh, net, fail, finish](const QString &branch,
                                                                               const QString &name) {
        QDir().mkpath(dir);
        const QString folder = QDir(dir).filePath(name);
        if (gh) {
            // A detached worktree, then gh checks the pull request's branch out in it (its
            // remote and tracking set up as gh does).
            git()->git(repo, {u"worktree"_s, u"add"_s, u"--detach"_s, u"--"_s, folder, u"HEAD"_s}, {},
                       [this, repo, folder, n, net, fail, finish](const gitcli::RunResult &added) {
                           if (!added.ok()) {
                               fail(added.message());
                               return;
                           }
                           git()->gh(folder, {u"pr"_s, u"checkout"_s, n}, net,
                                     [this, repo, folder, fail, finish](const gitcli::RunResult &r) {
                                         if (r.ok()) {
                                             finish(folder, folder);
                                             return;
                                         }
                                         // (Nothing left behind: the empty worktree goes.)
                                         const QString why = r.message();
                                         git()->removeWorktree(repo, folder, true,
                                                               [fail, why](const gitcli::Outcome &) { fail(why); });
                                     });
                       });
            return;
        }
        remoteOf(repo, [this, repo, folder, branch, n, net, fail, finish](const QString &remote, const QString &error) {
            if (remote.isEmpty()) {
                fail(error);
                return;
            }
            git()->git(repo, {u"fetch"_s, u"--"_s, remote, u"pull/"_s + n + u"/head:"_s + branch}, net,
                       [this, repo, folder, branch, fail, finish](const gitcli::RunResult &r) {
                           if (!r.ok()) {
                               fail(r.message());
                               return;
                           }
                           git()->addWorktree(repo, folder, QString(), branch,
                                              [folder, fail, finish](const gitcli::Outcome &o) {
                                                  if (!o.ok)
                                                      fail(o.message);
                                                  else
                                                      finish(folder, folder);
                                              });
                       });
        });
    });
    return true;
}

bool CodeProjects::checkWorktree(const QString &folder)
{
    if (folder.isEmpty() || !gitAvailable())
        return false;
    if (!QFileInfo(folder).isDir()) {
        // (Gone already: nothing in it to lose.)
        QMetaObject::invokeMethod(
                this, [this, folder] { Q_EMIT worktreeChecked(folder, 0, QString(), QString()); }, Qt::QueuedConnection);
        return true;
    }
    git()->status(folder, [this, folder](const gitcli::StatusReport &s) {
        int changes = 0;
        for (const gitcli::FileChange &c : s.changes)
            if (c.change != gitcli::Change::Ignored)
                ++changes;
        Q_EMIT worktreeChecked(folder, s.ok ? changes : -1, s.status.branch.head, s.ok ? QString() : s.error);
    });
    return true;
}

bool CodeProjects::removeWorktree(const QString &id, const QString &folder, bool force)
{
    const QString repo = folderOf(id);
    // (Only a worktree, never the project's own folder.)
    if (repo.isEmpty() || folder.isEmpty() || QDir::cleanPath(folder) == QDir::cleanPath(repo) || !gitAvailable())
        return false;
    git()->removeWorktree(repo, folder, force, [this, folder](const gitcli::Outcome &o) {
        Q_EMIT worktreeRemoved(folder, o.ok, o.message);
    });
    return true;
}

bool CodeProjects::restoreWorktree(const QString &id, const QString &folder, const QString &branch)
{
    const QString repo = folderOf(id);
    if (repo.isEmpty() || folder.isEmpty() || branch.isEmpty() || !gitAvailable())
        return false;
    if (QFileInfo(folder).isDir()) {
        QMetaObject::invokeMethod(
                this, [this, folder] { Q_EMIT worktreeRestored(folder, true, QString()); }, Qt::QueuedConnection);
        return true;
    }
    QDir().mkpath(QFileInfo(folder).absolutePath());
    // (A worktree removed by hand leaves git's record of it: pruned first.)
    git()->git(repo, {u"worktree"_s, u"prune"_s}, {}, [this, repo, folder, branch](const gitcli::RunResult &) {
        git()->addWorktree(repo, folder, QString(), branch, [this, folder](const gitcli::Outcome &o) {
            Q_EMIT worktreeRestored(folder, o.ok, o.message);
        });
    });
    return true;
}

} // namespace brulion::shell

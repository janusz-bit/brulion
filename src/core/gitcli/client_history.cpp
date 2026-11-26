// Client (client.h): history, the default branch, remotes, pull requests through gh, a new
// repository and publishing one — docs/CODE-GIT.md "The client".
#include "gitcli/client.h"

#include "gitcli/messages_p.h"

#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

using detail::GitMessages;

namespace {

Outcome failedWith(const QString &message)
{
    Outcome o;
    o.message = message;
    return o;
}

/// gh's error for the user: how to sign in when it is that, else its own words.
QString ghMessage(const RunResult &r)
{
    if (r.failure.isEmpty()
        && (r.err.contains(u"gh auth login"_s) || r.err.contains(u"not logged in"_s, Qt::CaseInsensitive)
            || r.err.contains(u"GH_TOKEN"_s)))
        return GitMessages::tr("Sign in to GitHub first: run “gh auth login” in a terminal");
    return r.message();
}

} // namespace

void Client::log(const QString &folder, const QString &revision, int max, std::function<void(const CommitList &)> done)
{
    const QString rev = revision.isEmpty() ? u"HEAD"_s : revision;
    if (!isSafeRevision(rev)) {
        later([done = std::move(done), rev] {
            CommitList list;
            list.error = GitMessages::tr("Not a revision: %1").arg(rev);
            done(list);
        });
        return;
    }
    RunOptions ro;
    ro.readOnly = true;
    git(folder,
        {u"log"_s, u"--no-color"_s, u"-n"_s, QString::number(qBound(1, max, 10000)), u"--format="_s + logFormat(), rev,
         u"--"_s},
        ro, [done = std::move(done), revision](const RunResult &r) {
            CommitList list;
            // (A new repository: no commits, not an error.)
            if (!r.ok() && r.failure.isEmpty() && revision.isEmpty()
                && (r.err.contains(u"does not have any commits"_s) || r.err.contains(u"bad revision 'HEAD'"_s)
                    || r.err.contains(u"unknown revision"_s))) {
                list.ok = true;
                done(list);
                return;
            }
            list.ok = r.ok();
            if (!r.ok())
                list.error = r.message();
            else
                list.commits = parseLog(r.out);
            done(list);
        });
}

void Client::mergeBase(const QString &folder, const QString &a, const QString &b, std::function<void(const Outcome &)> done)
{
    if (!isSafeRevision(a) || !isSafeRevision(b)) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("Not a revision to compare with"))); });
        return;
    }
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"merge-base"_s, a, b}, ro, [done = std::move(done), a, b](const RunResult &r) {
        Outcome o;
        o.value = QString::fromUtf8(r.out).trimmed();
        o.ok = r.ok() && !o.value.isEmpty();
        if (!o.ok)
            o.message = r.failure.isEmpty() && r.exitCode == 1
                                ? GitMessages::tr("%1 and %2 have no commit in common").arg(a, b)
                                : r.message();
        done(o);
    });
}

void Client::defaultBranch(const QString &folder, std::function<void(const DefaultBranch &)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"symbolic-ref"_s, u"-q"_s, u"--short"_s, u"refs/remotes/origin/HEAD"_s}, ro,
        [this, folder, ro, done = std::move(done)](const RunResult &head) {
            const QString remoteHead = QString::fromUtf8(head.out).trimmed(); // "origin/main"
            if (head.ok() && remoteHead.startsWith(u"origin/"_s) && remoteHead.size() > 7) {
                DefaultBranch d;
                d.ok = true;
                d.name = remoteHead.sliced(7);
                d.ref = remoteHead;
                done(d);
                return;
            }
            // main / master, the remote-tracking ones first, and init.defaultBranch's name.
            git(folder,
                {u"for-each-ref"_s, u"--format=%(refname)"_s, u"refs/heads/main"_s, u"refs/heads/master"_s,
                 u"refs/remotes/origin/main"_s, u"refs/remotes/origin/master"_s},
                ro, [this, folder, ro, done](const RunResult &refs) {
                    const QStringList found = QString::fromUtf8(refs.out).split(u'\n', Qt::SkipEmptyParts);
                    for (const QString &name : {u"main"_s, u"master"_s}) {
                        const bool remote = found.contains(u"refs/remotes/origin/"_s + name);
                        if (remote || found.contains(u"refs/heads/"_s + name)) {
                            DefaultBranch d;
                            d.ok = true;
                            d.name = name;
                            d.ref = remote ? u"origin/"_s + name : name;
                            done(d);
                            return;
                        }
                    }
                    git(folder, {u"config"_s, u"--get"_s, u"init.defaultBranch"_s}, ro,
                        [this, folder, ro, done](const RunResult &config) {
                            const QString name = QString::fromUtf8(config.out).trimmed();
                            if (!config.ok() || !isValidBranchName(name)) {
                                done(DefaultBranch());
                                return;
                            }
                            git(folder, {u"show-ref"_s, u"--verify"_s, u"--quiet"_s, u"refs/heads/"_s + name}, ro,
                                [name, done](const RunResult &exists) {
                                    DefaultBranch d;
                                    d.ok = exists.ok();
                                    if (d.ok) {
                                        d.name = name;
                                        d.ref = name;
                                    }
                                    done(d);
                                });
                        });
                });
        });
}

void Client::remotes(const QString &folder, std::function<void(const RemoteList &)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"remote"_s, u"-v"_s}, ro, [done = std::move(done)](const RunResult &r) {
        RemoteList list;
        list.ok = r.ok();
        if (!r.ok())
            list.error = r.message();
        else
            list.remotes = parseRemotes(r.out);
        done(list);
    });
}

void Client::pullRequestTarget(const QString &folder, const QString &branch, std::function<void(const ForkTarget &)> done)
{
    if (!isValidBranchName(branch)) {
        later([done = std::move(done)] { done(ForkTarget()); });
        return;
    }
    remotes(folder, [this, folder, branch, done = std::move(done)](const RemoteList &list) {
        if (!list.ok || list.remotes.size() < 2) {
            done(ForkTarget());
            return;
        }
        RunOptions ro;
        ro.readOnly = true;
        // The remote the branch is pushed to, by git's rules (branch.<name>.pushRemote,
        // remote.pushDefault, branch.<name>.remote); "" none set: origin.
        git(folder, {u"for-each-ref"_s, u"--format=%(push:remotename)"_s, u"refs/heads/"_s + branch}, ro,
            [list, branch, done](const RunResult &p) {
                const QString remote = p.ok() ? QString::fromUtf8(p.out).trimmed() : QString();
                done(forkTarget(list.remotes, remote, branch));
            });
    });
}

void Client::pullRequestFor(const QString &folder, const QString &head, const QString &repo,
                            std::function<void(const PullRequestStatus &)> done)
{
    if (m_gh.isEmpty()) {
        later([done = std::move(done)] {
            PullRequestStatus s;
            s.error = GitMessages::tr("The GitHub CLI (gh) is not installed");
            done(s);
        });
        return;
    }
    QStringList args{u"pr"_s, u"view"_s};
    if (!head.isEmpty()) {
        if (head.startsWith(u'-')) {
            later([done = std::move(done)] { done(PullRequestStatus()); });
            return;
        }
        args << head;
    }
    if (!repo.isEmpty())
        args << u"--repo"_s << repo;
    args << u"--json"_s << u"number,url,state,isDraft,title,baseRefName"_s;
    gh(folder, args, {}, [done = std::move(done)](const RunResult &r) {
        PullRequestStatus s;
        if (r.ok()) {
            s.pr = parsePullRequestView(r.out);
            s.ok = true;
            s.found = s.pr.isValid();
            done(s);
            return;
        }
        // No pull request for the branch: an answer, not a failure.
        if (r.failure.isEmpty() && r.err.contains(u"no pull requests found"_s, Qt::CaseInsensitive)) {
            s.ok = true;
            done(s);
            return;
        }
        s.error = ghMessage(r);
        done(s);
    });
}

void Client::initRepository(const QString &folder, std::function<void(const Outcome &)> done)
{
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("The folder does not exist"))); });
        return;
    }
    git(folder, {u"init"_s}, {}, [done = std::move(done)](const RunResult &r) {
        Outcome o;
        o.ok = r.ok();
        o.message = r.ok() ? GitMessages::tr("Made a git repository") : r.message();
        done(o);
    });
}

void Client::publish(const QString &folder, const QString &name, bool isPublic, std::function<void(const Outcome &)> done)
{
    if (!isValidRepositoryName(name)) {
        later([done = std::move(done), name] {
            done(failedWith(GitMessages::tr("“%1” cannot be a repository's name").arg(name)));
        });
        return;
    }
    if (m_gh.isEmpty()) {
        later([done = std::move(done)] {
            done(failedWith(GitMessages::tr("The GitHub CLI (gh) is not installed: install it to publish repositories")));
        });
        return;
    }
    const QStringList args{u"repo"_s,     u"create"_s, name,        isPublic ? u"--public"_s : u"--private"_s,
                           u"--source"_s, folder,      u"--remote"_s, u"origin"_s,
                           u"--push"_s};
    gh(folder, args, {}, [done = std::move(done)](const RunResult &r) {
        Outcome o;
        o.ok = r.ok();
        if (r.ok()) {
            o.message = GitMessages::tr("Published the repository on GitHub");
            // gh prints the new repository's address ("https://github.com/owner/name").
            const QString all = QString::fromUtf8(r.out) + u'\n' + r.err;
            for (const QString &line : all.split(u'\n')) {
                const qsizetype at = line.indexOf(u"https://"_s);
                if (at >= 0 && o.url.isEmpty())
                    o.url = line.sliced(at).section(u' ', 0, 0).trimmed();
            }
        } else {
            o.message = ghMessage(r);
        }
        done(o);
    });
}

} // namespace brulion::gitcli

// Client (client.h): a merge, rebase, cherry-pick, revert or `git am` in progress — found, aborted,
// continued — and the stashes — docs/CODE-GIT.md "The client".
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

Outcome succeededWith(const QString &message)
{
    Outcome o;
    o.ok = true;
    o.message = message;
    return o;
}

/// The command of an operation ("merge", "rebase", "cherry-pick", "revert", "am").
QString commandOf(Operation operation)
{
    switch (operation) {
    case Operation::Merge:
        return u"merge"_s;
    case Operation::Rebase:
        return u"rebase"_s;
    case Operation::CherryPick:
        return u"cherry-pick"_s;
    case Operation::Revert:
        return u"revert"_s;
    case Operation::Apply:
        return u"am"_s;
    case Operation::None:
        break;
    }
    return {};
}

/// The newest index of the stash whose commit is `oid` (-1 none).
int stashIndexOf(const QList<Stash> &stashes, const QString &oid)
{
    for (const Stash &s : stashes) {
        if (s.oid == oid)
            return s.index;
    }
    return -1;
}

} // namespace

Operation Client::operationIn(const QString &gitDir)
{
    if (gitDir.isEmpty())
        return Operation::None;
    const auto has = [&gitDir](const QString &name) { return QFileInfo::exists(gitDir + u'/' + name); };
    if (has(u"rebase-merge"_s))
        return Operation::Rebase;
    if (has(u"rebase-apply"_s))
        return has(u"rebase-apply/applying"_s) ? Operation::Apply : Operation::Rebase;
    if (has(u"MERGE_HEAD"_s))
        return Operation::Merge;
    if (has(u"CHERRY_PICK_HEAD"_s))
        return Operation::CherryPick;
    if (has(u"REVERT_HEAD"_s))
        return Operation::Revert;
    return Operation::None;
}

QString Client::operationName(Operation operation)
{
    switch (operation) {
    case Operation::Merge:
        return u"merge"_s;
    case Operation::Rebase:
        return u"rebase"_s;
    case Operation::CherryPick:
        return u"cherryPick"_s;
    case Operation::Revert:
        return u"revert"_s;
    case Operation::Apply:
        return u"apply"_s;
    case Operation::None:
        break;
    }
    return {};
}

Operation Client::operationFromName(const QString &name)
{
    for (const Operation o : {Operation::Merge, Operation::Rebase, Operation::CherryPick, Operation::Revert,
                              Operation::Apply}) {
        if (operationName(o) == name)
            return o;
    }
    return Operation::None;
}

void Client::abortOperation(const QString &folder, Operation operation, std::function<void(const Outcome &)> done)
{
    const QString command = commandOf(operation);
    if (command.isEmpty()) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("Nothing is in progress"))); });
        return;
    }
    git(folder, {command, u"--abort"_s}, {}, [done = std::move(done)](const RunResult &r) {
        done(r.ok() ? succeededWith(GitMessages::tr("Aborted: the files are as they were before")) : failedWith(r.message()));
    });
}

void Client::continueOperation(const QString &folder, Operation operation, std::function<void(const Outcome &)> done)
{
    const QString command = commandOf(operation);
    if (command.isEmpty()) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("Nothing is in progress"))); });
        return;
    }
    // Never an editor: the messages git prepared are taken as they are.
    RunOptions c;
    c.env = {{u"GIT_EDITOR"_s, u"true"_s}, {u"GIT_SEQUENCE_EDITOR"_s, u"true"_s}};
    c.timeoutMs = m_commitTimeoutMs;
    const QStringList args = operation == Operation::Merge ? QStringList{u"commit"_s, u"--no-edit"_s}
                                                           : QStringList{command, u"--continue"_s};
    git(folder, args, c, [done = std::move(done), operation](const RunResult &r) {
        if (r.ok()) {
            done(succeededWith(operation == Operation::Merge ? GitMessages::tr("Merge committed")
                                                             : GitMessages::tr("Continued")));
            return;
        }
        const QString all = QString::fromUtf8(r.out) + u'\n' + r.err;
        if (r.failure.isEmpty() && (all.contains(u"unmerged"_s, Qt::CaseInsensitive) || all.contains(u"conflict"_s)))
            done(failedWith(GitMessages::tr("Resolve every conflicted file first: %1").arg(r.message())));
        else
            done(failedWith(r.message()));
    });
}

void Client::skipOperation(const QString &folder, Operation operation, std::function<void(const Outcome &)> done)
{
    const QString command = commandOf(operation);
    if (command.isEmpty() || operation == Operation::Merge) {
        const QString why = command.isEmpty() ? GitMessages::tr("Nothing is in progress")
                                              : GitMessages::tr("A merge has no commit to skip: abort it instead");
        later([done = std::move(done), why] { done(failedWith(why)); });
        return;
    }
    RunOptions c;
    c.env = {{u"GIT_EDITOR"_s, u"true"_s}, {u"GIT_SEQUENCE_EDITOR"_s, u"true"_s}};
    c.timeoutMs = m_commitTimeoutMs;
    git(folder, {command, u"--skip"_s}, c, [done = std::move(done)](const RunResult &r) {
        done(r.ok() ? succeededWith(GitMessages::tr("Skipped: the operation goes on without that commit"))
                    : failedWith(r.message()));
    });
}

void Client::stashes(const QString &folder, std::function<void(const StashList &)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"stash"_s, u"list"_s, u"--format="_s + stashFormat()}, ro, [done = std::move(done)](const RunResult &r) {
        StashList list;
        list.ok = r.ok();
        if (!r.ok())
            list.error = r.message();
        else
            list.stashes = parseStashes(r.out);
        done(list);
    });
}

void Client::stashPush(const QString &folder, const QString &message, std::function<void(const Outcome &)> done)
{
    QStringList args{u"stash"_s, u"push"_s, u"--include-untracked"_s};
    const QString m = message.simplified();
    if (!m.isEmpty())
        args << u"--message"_s << m;
    git(folder, args, {}, [done = std::move(done)](const RunResult &r) {
        if (r.ok() && QString::fromUtf8(r.out).contains(u"No local changes to save"_s)) {
            done(failedWith(GitMessages::tr("No changes to stash")));
            return;
        }
        done(r.ok() ? succeededWith(GitMessages::tr("Stashed the changes")) : failedWith(r.message()));
    });
}

void Client::stashApply(const QString &folder, const QString &oid, bool drop, std::function<void(const Outcome &)> done)
{
    if (!isSafeRevision(oid)) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("Not a stash"))); });
        return;
    }
    git(folder, {u"stash"_s, u"apply"_s, oid}, {}, [this, folder, oid, drop, done = std::move(done)](const RunResult &r) {
        if (!r.ok()) {
            const QString all = QString::fromUtf8(r.out) + u'\n' + r.err;
            done(failedWith(r.failure.isEmpty() && all.contains(u"CONFLICT"_s)
                                    ? GitMessages::tr("The stash met conflicts: it is kept, resolve the files")
                                    : r.message()));
            return;
        }
        if (!drop) {
            done(succeededWith(GitMessages::tr("Applied the stash")));
            return;
        }
        stashDrop(folder, oid, [done](const Outcome &d) {
            done(d.ok ? succeededWith(GitMessages::tr("Applied and dropped the stash"))
                      : failedWith(GitMessages::tr("Applied the stash, but it was not dropped: %1").arg(d.message)));
        });
    });
}

void Client::stashDrop(const QString &folder, const QString &oid, std::function<void(const Outcome &)> done)
{
    stashes(folder, [this, folder, oid, done = std::move(done)](const StashList &list) {
        if (!list.ok) {
            done(failedWith(list.error));
            return;
        }
        const int index = stashIndexOf(list.stashes, oid);
        if (index < 0) {
            done(failedWith(GitMessages::tr("The stash is gone")));
            return;
        }
        git(folder, {u"stash"_s, u"drop"_s, u"stash@{%1}"_s.arg(index)}, {}, [done](const RunResult &r) {
            done(r.ok() ? succeededWith(GitMessages::tr("Dropped the stash")) : failedWith(r.message()));
        });
    });
}

} // namespace brulion::gitcli

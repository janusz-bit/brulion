// Client (client.h): a file's changes staged, unstaged or thrown away, a conflict settled, single
// hunks through `git apply`, conflict markers found and the commit of some files —
// docs/CODE-GIT.md "The client".
#include "gitcli/client.h"

#include "gitcli/messages_p.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

using detail::GitMessages;
using detail::inTree;
using detail::shortOid;

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

QByteArray nulPaths(const QStringList &paths)
{
    QByteArray out;
    for (const QString &p : paths) {
        out += p.toUtf8();
        out += '\0';
    }
    return out;
}

/// The paths read as literal pathspecs from standard input (`--pathspec-from-file=-`).
RunOptions pathspecInput(const QStringList &paths)
{
    RunOptions o;
    o.input = nulPaths(paths);
    o.env = {{u"GIT_LITERAL_PATHSPECS"_s, u"1"_s}};
    return o;
}

const QStringList &pathspecFromInput()
{
    static const QStringList args{u"--pathspec-from-file=-"_s, u"--pathspec-file-nul"_s};
    return args;
}

/// The first path that is not inside the work tree ("" when all are).
QString unsafePath(const QStringList &paths)
{
    for (const QString &p : paths) {
        if (!isSafeRelativePath(p))
            return p.isEmpty() ? u"\"\""_s : p;
    }
    return {};
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash);
}

/// No folder on the way to `path` is a symbolic link (a deletion never leaves the work tree).
bool plainWay(const QString &root, const QString &path)
{
    QString dir = parentOf(path);
    while (!dir.isEmpty()) {
        if (QFileInfo(inTree(root, dir)).isSymbolicLink())
            return false;
        dir = parentOf(dir);
    }
    return true;
}

/// The file (or symbolic link) `path` of the work tree into the system's trash, else deleted:
/// true when it is gone.
bool trashFile(const QString &root, const QString &path)
{
    if (!isSafeRelativePath(path) || !plainWay(root, path))
        return false;
    const QString full = inTree(root, path);
    const QFileInfo info(full);
    if (!info.isSymbolicLink() && !info.exists())
        return true;
    if (!info.isSymbolicLink() && !info.isFile())
        return false; // (a folder: never)
    if (QFile::moveToTrash(full))
        return true;
    return QFile::remove(full);
}

} // namespace

void Client::stage(const QString &folder, const QStringList &paths, std::function<void(const Outcome &)> done)
{
    const QString bad = unsafePath(paths);
    if (paths.isEmpty() || !bad.isEmpty()) {
        later([done = std::move(done), bad] {
            done(failedWith(bad.isEmpty() ? GitMessages::tr("No file to stage")
                                          : GitMessages::tr("Not a path inside the repository: %1").arg(bad)));
        });
        return;
    }
    git(folder, QStringList{u"add"_s, u"-A"_s} + pathspecFromInput(), pathspecInput(paths),
        [done = std::move(done), paths](const RunResult &r) {
            done(r.ok() ? succeededWith(GitMessages::tr("Staged %n file(s)", "", int(paths.size())))
                        : failedWith(r.message()));
        });
}

void Client::unstage(const QString &folder, const QStringList &paths, std::function<void(const Outcome &)> done)
{
    const QString bad = unsafePath(paths);
    if (paths.isEmpty() || !bad.isEmpty()) {
        later([done = std::move(done), bad] {
            done(failedWith(bad.isEmpty() ? GitMessages::tr("No file to unstage")
                                          : GitMessages::tr("Not a path inside the repository: %1").arg(bad)));
        });
        return;
    }
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"rev-parse"_s, u"-q"_s, u"--verify"_s, u"HEAD^{commit}"_s}, ro,
        [this, folder, paths, done = std::move(done)](const RunResult &head) {
            // Before the first commit there is no HEAD to restore the index from.
            const QStringList args = head.ok()
                                             ? QStringList{u"restore"_s, u"--staged"_s} + pathspecFromInput()
                                             : QStringList{u"rm"_s, u"--cached"_s, u"-q"_s, u"-r"_s,
                                                           u"--ignore-unmatch"_s} + pathspecFromInput();
            git(folder, args, pathspecInput(paths), [done, paths](const RunResult &r) {
                done(r.ok() ? succeededWith(GitMessages::tr("Unstaged %n file(s)", "", int(paths.size())))
                            : failedWith(r.message()));
            });
        });
}

void Client::discard(const QString &folder, const QString &path, const QString &oldPath,
                     std::function<void(const Outcome &)> done)
{
    QStringList paths{path};
    if (!oldPath.isEmpty() && oldPath != path)
        paths.append(oldPath);
    const QString bad = unsafePath(paths);
    QString refused;
    if (!bad.isEmpty())
        refused = GitMessages::tr("Not a path inside the repository: %1").arg(bad);
    for (const QString &p : std::as_const(paths)) {
        const QFileInfo info(inTree(folder, p));
        if (refused.isEmpty() && (!plainWay(folder, p) || (info.isDir() && !info.isSymbolicLink())))
            refused = GitMessages::tr("%1 is a folder or lies behind a link: its changes are not thrown away here").arg(p);
    }
    if (!refused.isEmpty()) {
        later([done = std::move(done), refused] { done(failedWith(refused)); });
        return;
    }
    RunOptions ro;
    ro.readOnly = true;
    // Conflicted? Then it is settled (resolve()), not thrown away.
    git(folder, {u"ls-files"_s, u"-u"_s, u"-z"_s, u"--"_s, u":(literal)"_s + paths.first()}, ro,
        [this, folder, paths, ro, done = std::move(done)](const RunResult &u) {
            if (u.ok() && !u.out.isEmpty()) {
                done(failedWith(GitMessages::tr("%1 has a conflict: take one side or mark it resolved").arg(paths.first())));
                return;
            }
            // Which of the paths HEAD has (restored from it) and which not (out of the index,
            // into the trash).
            // (ls-tree takes no pathspec magic: literal through the environment.)
            const QStringList lsTree =
                    QStringList{u"ls-tree"_s, u"-z"_s, u"--name-only"_s, u"--full-tree"_s, u"HEAD"_s, u"--"_s} + paths;
            RunOptions literal = ro;
            literal.env = {{u"GIT_LITERAL_PATHSPECS"_s, u"1"_s}};
            git(folder, lsTree, literal, [this, folder, paths, done](const RunResult &t) {
                QStringList inHead;
                QStringList notInHead;
                QSet<QString> listed;
                if (t.ok()) {
                    for (const QByteArray &name : t.out.split('\0')) {
                        if (!name.isEmpty())
                            listed.insert(QString::fromUtf8(name));
                    }
                }
                for (const QString &p : paths)
                    (listed.contains(p) ? inHead : notInHead).append(p);
                const auto removeNew = [this, folder, notInHead, done, paths] {
                    if (notInHead.isEmpty()) {
                        done(succeededWith(GitMessages::tr("Threw away the changes of %1").arg(paths.first())));
                        return;
                    }
                    git(folder,
                        QStringList{u"rm"_s, u"--cached"_s, u"-q"_s, u"--ignore-unmatch"_s} + pathspecFromInput(),
                        pathspecInput(notInHead), [folder, notInHead, done, paths](const RunResult &rm) {
                            if (!rm.ok()) {
                                done(failedWith(rm.message()));
                                return;
                            }
                            QStringList left;
                            for (const QString &p : notInHead) {
                                if (!trashFile(folder, p))
                                    left.append(p);
                            }
                            done(left.isEmpty() ? succeededWith(GitMessages::tr("Threw away the changes of %1")
                                                                        .arg(paths.first()))
                                                : failedWith(GitMessages::tr("%1 could not be moved to the trash")
                                                                     .arg(left.join(u", "_s))));
                        });
                };
                if (inHead.isEmpty()) {
                    removeNew();
                    return;
                }
                git(folder,
                    QStringList{u"restore"_s, u"--source=HEAD"_s, u"--staged"_s, u"--worktree"_s} + pathspecFromInput(),
                    pathspecInput(inHead), [removeNew, done](const RunResult &r) {
                        if (!r.ok()) {
                            done(failedWith(r.message()));
                            return;
                        }
                        removeNew();
                    });
            });
        });
}

void Client::resolve(const QString &folder, const QString &path, Resolution how, std::function<void(const Outcome &)> done)
{
    if (!isSafeRelativePath(path)) {
        later([done = std::move(done), path] {
            done(failedWith(GitMessages::tr("Not a path inside the repository: %1").arg(path)));
        });
        return;
    }
    const auto add = [this, folder, path, done](const QString &message) {
        git(folder, QStringList{u"add"_s, u"-A"_s} + pathspecFromInput(), pathspecInput({path}),
            [done, message](const RunResult &a) { done(a.ok() ? succeededWith(message) : failedWith(a.message())); });
    };
    if (how == Resolution::Resolved) {
        conflictMarkers(folder, {path}, [add, done, path](const QStringList &found) {
            if (!found.isEmpty()) {
                done(failedWith(GitMessages::tr("%1 still has conflict markers (<<<<<<< / >>>>>>>)").arg(path)));
                return;
            }
            add(GitMessages::tr("Marked %1 resolved").arg(path));
        });
        return;
    }
    // The stages git keeps of the conflict: 2 ours, 3 theirs — a side without one deleted it.
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"ls-files"_s, u"-u"_s, u"-z"_s, u"--"_s, u":(literal)"_s + path}, ro,
        [this, folder, path, how, add, done](const RunResult &u) {
            if (!u.ok() || u.out.isEmpty()) {
                done(failedWith(u.ok() ? GitMessages::tr("%1 has no conflict").arg(path) : u.message()));
                return;
            }
            const char wanted = how == Resolution::Ours ? '2' : '3';
            bool has = false;
            for (const QByteArray &entry : u.out.split('\0')) {
                // "<mode> <oid> <stage>\t<path>"
                const qsizetype tab = entry.indexOf('\t');
                if (tab >= 2 && entry.at(tab - 1) == wanted)
                    has = true;
            }
            const QString side = how == Resolution::Ours ? u"--ours"_s : u"--theirs"_s;
            const QString message = how == Resolution::Ours ? GitMessages::tr("Took our side of %1").arg(path)
                                                            : GitMessages::tr("Took their side of %1").arg(path);
            if (!has) {
                // That side deleted the file.
                git(folder, QStringList{u"rm"_s, u"-q"_s, u"--ignore-unmatch"_s} + pathspecFromInput(),
                    pathspecInput({path}), [done, message](const RunResult &rm) {
                        done(rm.ok() ? succeededWith(message) : failedWith(rm.message()));
                    });
                return;
            }
            git(folder, QStringList{u"checkout"_s, side} + pathspecFromInput(), pathspecInput({path}),
                [add, done, message](const RunResult &c) {
                    if (!c.ok()) {
                        done(failedWith(c.message()));
                        return;
                    }
                    add(message);
                });
        });
}

void Client::applyHunk(const QString &folder, const QString &path, HunkAction action, int first, int last,
                       std::function<void(const Outcome &)> done)
{
    if (first <= 0 || last <= 0) {
        later([done = std::move(done), path] {
            done(failedWith(GitMessages::tr("Not a path inside the repository: %1").arg(path)));
        });
        return;
    }
    applyPart(folder, path, action, [action, first, last](const FilePatch &patch) -> PatchPart {
        const QList<int> hunks = hunksTouching(patch, first, last, action != HunkAction::Unstage);
        const int n = int(hunks.size());
        return {hunks.isEmpty() ? QByteArray() : patchOf(patch, hunks, action != HunkAction::Stage),
                action == HunkAction::Stage     ? GitMessages::tr("Staged %n hunk(s)", "", n)
                : action == HunkAction::Unstage ? GitMessages::tr("Unstaged %n hunk(s)", "", n)
                                                : GitMessages::tr("Threw away %n hunk(s)", "", n)};
    }, std::move(done));
}

void Client::revertHunk(const QString &folder, const QString &path, const QString &base, const QString &target,
                        int first, int last, std::function<void(const Outcome &)> done)
{
    // (Revisions as git reads them, never an option.)
    if (base.isEmpty() || base.startsWith(u'-') || target.startsWith(u'-') || first <= 0 || last <= 0) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("No such change to revert"))); });
        return;
    }
    QStringList range{base};
    if (!target.isEmpty())
        range << target;
    applyPart(folder, path, HunkAction::Discard, [first, last](const FilePatch &patch) -> PatchPart {
        const QList<int> hunks = hunksTouching(patch, first, last, true);
        return {hunks.isEmpty() ? QByteArray() : patchOf(patch, hunks, true),
                GitMessages::tr("Reverted %n hunk(s)", "", int(hunks.size()))};
    }, std::move(done), range);
}

void Client::applyLines(const QString &folder, const QString &path, HunkAction action, int oldFirst, int oldLast,
                        int newFirst, int newLast, std::function<void(const Outcome &)> done)
{
    applyPart(folder, path, action, [action, oldFirst, oldLast, newFirst, newLast](const FilePatch &patch) -> PatchPart {
        const bool reverse = action != HunkAction::Stage;
        const FilePatch lines = linesOnly(patch, std::min(oldFirst, oldLast), std::max(oldFirst, oldLast),
                                          std::min(newFirst, newLast), std::max(newFirst, newLast), reverse);
        QList<int> all;
        for (int i = 0; i < lines.hunks.size(); ++i)
            all << i;
        return {all.isEmpty() ? QByteArray() : patchOf(lines, all, reverse),
                action == HunkAction::Stage     ? GitMessages::tr("Staged the lines")
                : action == HunkAction::Unstage ? GitMessages::tr("Unstaged the lines")
                                                : GitMessages::tr("Threw the lines away")};
    }, std::move(done));
}

void Client::applyPart(const QString &folder, const QString &path, HunkAction action,
                       std::function<PatchPart(const FilePatch &)> part, std::function<void(const Outcome &)> done,
                       const QStringList &range)
{
    if (!isSafeRelativePath(path)) {
        later([done = std::move(done), path] {
            done(failedWith(GitMessages::tr("Not a path inside the repository: %1").arg(path)));
        });
        return;
    }
    // git's own diff of the file: index → work tree (Stage, Discard), HEAD → index (Unstage).
    QStringList diff{u"diff"_s, u"--no-color"_s, u"--no-ext-diff"_s, u"--no-textconv"_s, u"--no-renames"_s,
                     u"-U3"_s};
    if (action == HunkAction::Unstage)
        diff << u"--cached"_s;
    diff << range << u"--"_s << u":(literal)"_s + path;
    RunOptions ro;
    ro.readOnly = true;
    git(folder, diff, ro, [this, folder, path, action, range, part = std::move(part), done = std::move(done)](const RunResult &d) {
        if (!d.ok()) {
            done(failedWith(d.message()));
            return;
        }
        const FilePatch patch = parsePatch(d.out);
        if (patch.binary) {
            done(failedWith(GitMessages::tr("%1 is binary: stage or discard the whole file").arg(path)));
            return;
        }
        const PatchPart chosen = part(patch);
        if (chosen.patch.isEmpty()) {
            done(failedWith(!range.isEmpty()                ? GitMessages::tr("These lines have no change to revert")
                            : action == HunkAction::Unstage ? GitMessages::tr("Nothing of these lines is staged")
                                                            : GitMessages::tr("These lines have no unstaged changes")));
            return;
        }
        applyChosen(folder, action, chosen, done);
    });
}

void Client::applyChosen(const QString &folder, HunkAction action, const PatchPart &chosen,
                         std::function<void(const Outcome &)> done)
{
    RunOptions a;
    a.noHooks = true;
    a.input = chosen.patch;
    QStringList args{u"apply"_s, u"--whitespace=nowarn"_s};
    if (action != HunkAction::Discard)
        args << u"--cached"_s;
    if (action != HunkAction::Stage)
        args << u"-R"_s;
    args << u"-"_s;
    git(folder, args, a, [done = std::move(done), message = chosen.message](const RunResult &r) {
        done(r.ok() ? succeededWith(message) : failedWith(r.message()));
    });
}

void Client::conflictMarkers(const QString &folder, const QStringList &paths, std::function<void(const QStringList &)> done)
{
    scanMarkers(folder, paths, 0, std::make_shared<QStringList>(), std::move(done));
}

void Client::scanMarkers(const QString &folder, const QStringList &paths, qsizetype next,
                         std::shared_ptr<QStringList> found, std::function<void(const QStringList &)> done)
{
    // A slice at a time (16 files or 4 MiB), the rest after the event loop had its turn.
    later([this, folder, paths, next, found, done = std::move(done)]() mutable {
        qsizetype i = next;
        qint64 read = 0;
        for (int files = 0; i < paths.size() && files < 16 && read < 4 * 1024 * 1024; ++i, ++files) {
            const QString &p = paths.at(i);
            if (!isSafeRelativePath(p))
                continue;
            const QString full = inTree(folder, p);
            const QFileInfo info(full);
            if (!info.isFile() || info.isSymbolicLink() || info.size() > MaxTextBytes)
                continue;
            QFile file(full);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QByteArray bytes = file.read(MaxTextBytes);
            read += bytes.size();
            if (!looksBinary(bytes) && hasConflictMarkers(bytes))
                found->append(p);
        }
        if (i >= paths.size()) {
            done(*found);
            return;
        }
        scanMarkers(folder, paths, i, found, std::move(done));
    });
}

void Client::commit(const QString &folder, const CommitRequest &request, std::function<void(const Outcome &)> done)
{
    if (subjectOf(request.message).isEmpty()) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("Write a commit message first"))); });
        return;
    }
    if (request.mode != u"all"_s && request.mode != u"staged"_s && request.mode != u"paths"_s) {
        later([done = std::move(done)] { done(failedWith(GitMessages::tr("Not a way to commit"))); });
        return;
    }
    const QString bad = unsafePath(request.paths + request.untracked);
    if (!bad.isEmpty() || (request.mode == u"paths"_s && request.paths.isEmpty())
        || (!request.newBranch.isEmpty() && !isValidBranchName(request.newBranch))) {
        const QString why = !bad.isEmpty() ? GitMessages::tr("Not a path inside the repository: %1").arg(bad)
                            : request.paths.isEmpty() && request.mode == u"paths"_s
                                    ? GitMessages::tr("Choose the files to commit")
                                    : GitMessages::tr("“%1” cannot be a branch's name").arg(request.newBranch);
        later([done = std::move(done), why] { done(failedWith(why)); });
        return;
    }
    // Conflict markers in what goes into the commit: refused, naming the files. ("all" and
    // "staged" check the files the caller named — the panel names every changed file.)
    conflictMarkers(folder, request.paths,
                    [this, folder, request, done = std::move(done)](const QStringList &found) {
                        if (!found.isEmpty()) {
                            done(failedWith(GitMessages::tr("Resolve the conflicts first — conflict markers in: %1")
                                                    .arg(found.join(u", "_s))));
                            return;
                        }
                        if (request.newBranch.isEmpty()) {
                            commitChecked(folder, request, done);
                            return;
                        }
                        createBranch(folder, request.newBranch, [this, folder, request, done](const Outcome &b) {
                            if (!b.ok) {
                                done(b);
                                return;
                            }
                            commitChecked(folder, request, done);
                        });
                    });
}

void Client::commitChecked(const QString &folder, const CommitRequest &request, std::function<void(const Outcome &)> done)
{
    const auto finished = [this, folder](const RunResult &r, const std::function<void(const Outcome &)> &d) {
        if (!r.ok()) {
            if (r.failure.isEmpty() && QString::fromUtf8(r.out).contains(u"nothing to commit"_s))
                d(failedWith(GitMessages::tr("Nothing to commit")));
            else if (r.failure.isEmpty() && QString::fromUtf8(r.out).contains(u"no changes added to commit"_s))
                d(failedWith(GitMessages::tr("Nothing is staged to commit")));
            else
                d(failedWith(r.message()));
            return;
        }
        RunOptions ro;
        ro.readOnly = true;
        git(folder, {u"rev-parse"_s, u"HEAD"_s}, ro, [d](const RunResult &h) {
            Outcome o = succeededWith(QString());
            o.value = QString::fromUtf8(h.out).trimmed();
            o.message = o.value.isEmpty() ? GitMessages::tr("Committed") : GitMessages::tr("Committed %1").arg(shortOid(o.value));
            d(o);
        });
    };
    RunOptions c;
    c.input = request.message.toUtf8();
    c.timeoutMs = m_commitTimeoutMs;
    if (request.mode == u"staged"_s) {
        git(folder, {u"commit"_s, u"--file=-"_s}, c, [finished, done](const RunResult &r) { finished(r, done); });
        return;
    }
    if (request.mode == u"all"_s) {
        git(folder, {u"add"_s, u"-A"_s}, {}, [this, folder, c, finished, done](const RunResult &a) {
            if (!a.ok()) {
                done(failedWith(a.message()));
                return;
            }
            git(folder, {u"commit"_s, u"--file=-"_s}, c, [finished, done](const RunResult &r) { finished(r, done); });
        });
        return;
    }
    // Only these paths, as they are in the work tree: `git commit --only` (the pathspecs from a
    // file, the message on standard input); what is staged of other files stays staged.
    auto temp = std::make_shared<QTemporaryDir>();
    const QString specs = temp->isValid() ? temp->filePath(u"paths"_s) : QString();
    QFile file(specs);
    if (specs.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(nulPaths(request.paths)) < 0) {
        later([done] { done(failedWith(GitMessages::tr("No temporary folder for the commit"))); });
        return;
    }
    file.close();
    c.env = {{u"GIT_LITERAL_PATHSPECS"_s, u"1"_s}};
    const auto commitOnly = [this, folder, c, specs, temp, finished, request, done](bool added) {
        git(folder,
            {u"commit"_s, u"--only"_s, u"--file=-"_s, u"--pathspec-from-file="_s + specs, u"--pathspec-file-nul"_s}, c,
            [this, folder, temp, finished, request, added, done](const RunResult &r) {
                if (r.ok() || !added) {
                    finished(r, done);
                    return;
                }
                // The new files added for it go out of the index again (they were untracked).
                git(folder,
                    QStringList{u"rm"_s, u"--cached"_s, u"-q"_s, u"--ignore-unmatch"_s} + pathspecFromInput(),
                    pathspecInput(request.untracked), [finished, r, done](const RunResult &) { finished(r, done); });
            });
    };
    if (request.untracked.isEmpty()) {
        commitOnly(false);
        return;
    }
    git(folder, QStringList{u"add"_s} + pathspecFromInput(), pathspecInput(request.untracked),
        [commitOnly, done](const RunResult &a) {
            if (!a.ok()) {
                done(failedWith(a.message()));
                return;
            }
            commitOnly(true);
        });
}

} // namespace brulion::gitcli

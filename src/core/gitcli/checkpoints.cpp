// Checkpoints of the work tree (client.h, docs/CODE-GIT.md "Checkpoints"): snapshots through a
// temporary index, kept by refs/brulion/checkpoints/<id>; the changes since one; the work tree put
// back to one.
#include "gitcli/client.h"

#include "gitcli/messages_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

using detail::GitMessages;
using detail::inTree;

namespace {

/// The identity of the snapshot commits (not the user's: a repository without user.name
/// still gets checkpoints, and they are not the user's commits).
const QList<QPair<QString, QString>> &snapshotIdentity()
{
    static const QList<QPair<QString, QString>> identity{
            {u"GIT_AUTHOR_NAME"_s, u"Brulion"_s},
            {u"GIT_AUTHOR_EMAIL"_s, u"brulion@localhost"_s},
            {u"GIT_COMMITTER_NAME"_s, u"Brulion"_s},
            {u"GIT_COMMITTER_EMAIL"_s, u"brulion@localhost"_s},
    };
    return identity;
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

/// Deletes the file (or symbolic link) `path` of the work tree: true when it is gone.
bool deleteFile(const QString &root, const QString &path)
{
    if (!isSafeRelativePath(path) || !plainWay(root, path))
        return false;
    const QString full = inTree(root, path);
    const QFileInfo info(full);
    if (info.isSymbolicLink() || info.isFile())
        return QFile::remove(full);
    return !info.exists();
}

/// The folders on the way to `path` that are empty now, deleted (the deepest first, never the
/// work tree's own folder).
void pruneEmptyFolders(const QString &root, const QString &path)
{
    QString dir = parentOf(path);
    while (!dir.isEmpty()) {
        const QString full = inTree(root, dir);
        const QFileInfo info(full);
        if (info.isSymbolicLink() || !info.isDir())
            return;
        if (!QDir(full).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            return;
        if (!QDir().rmdir(full))
            return;
        dir = parentOf(dir);
    }
}

QByteArray nulList(const QStringList &paths)
{
    QByteArray out;
    for (const QString &p : paths) {
        out += p.toUtf8();
        out += '\0';
    }
    return out;
}

} // namespace

QString Client::newCheckpointId()
{
    quint32 words[4];
    QRandomGenerator::system()->fillRange(words);
    QString id;
    for (const quint32 w : words)
        id += u"%1"_s.arg(w, 8, 16, u'0');
    return id;
}

QString Client::checkpointRef(const QString &id)
{
    return u"refs/brulion/checkpoints/"_s + id;
}

void Client::snapshotTree(const QString &folder, std::function<void(const QString &tree, const QString &error)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"rev-parse"_s, u"--path-format=absolute"_s, u"--git-path"_s, u"index"_s}, ro,
        [this, folder, done = std::move(done)](const RunResult &p) {
            if (!p.ok()) {
                done({}, p.message());
                return;
            }
            auto temp = std::make_shared<QTemporaryDir>();
            if (!temp->isValid()) {
                done({}, GitMessages::tr("No temporary folder: %1").arg(temp->errorString()));
                return;
            }
            // A copy of the real index: `git add -A` then only hashes what changed since.
            const QString indexPath = QString::fromUtf8(p.out).trimmed();
            const QString tempIndex = temp->filePath(u"index"_s);
            if (QFile::exists(indexPath) && !QFile::copy(indexPath, tempIndex))
                QFile::remove(tempIndex);
            RunOptions o;
            o.noHooks = true;
            o.env = {{u"GIT_INDEX_FILE"_s, tempIndex}};
            git(folder, {u"-c"_s, u"core.splitIndex=false"_s, u"add"_s, u"-A"_s}, o,
                [this, folder, o, temp, done](const RunResult &a) {
                    if (!a.ok()) {
                        done({}, a.message());
                        return;
                    }
                    git(folder, {u"-c"_s, u"core.splitIndex=false"_s, u"write-tree"_s}, o,
                        [temp, done](const RunResult &w) {
                            const QString tree = QString::fromUtf8(w.out).trimmed();
                            if (!w.ok() || tree.isEmpty())
                                done({}, w.message());
                            else
                                done(tree, {});
                        });
                });
        });
}

void Client::checkpoint(const QString &folder, const QString &id, std::function<void(const Outcome &)> done)
{
    if (!isValidCheckpointId(id)) {
        later([done = std::move(done), id] {
            Outcome o;
            o.message = GitMessages::tr("Not a checkpoint's id: %1").arg(id);
            done(o);
        });
        return;
    }
    snapshotTree(folder, [this, folder, id, done = std::move(done)](const QString &tree, const QString &error) {
        if (tree.isEmpty()) {
            Outcome o;
            o.message = error;
            done(o);
            return;
        }
        RunOptions ro;
        ro.readOnly = true;
        git(folder, {u"rev-parse"_s, u"-q"_s, u"--verify"_s, u"HEAD^{commit}"_s}, ro,
            [this, folder, id, tree, done](const RunResult &h) {
                const QString parent = h.ok() ? QString::fromUtf8(h.out).trimmed() : QString();
                QStringList args{u"commit-tree"_s, u"--no-gpg-sign"_s, tree};
                if (!parent.isEmpty())
                    args << u"-p"_s << parent;
                args << u"-m"_s << u"Brulion checkpoint "_s + id;
                RunOptions c;
                c.noHooks = true;
                c.env = snapshotIdentity();
                git(folder, args, c, [this, folder, id, done](const RunResult &commit) {
                    const QString oid = QString::fromUtf8(commit.out).trimmed();
                    if (!commit.ok() || oid.isEmpty()) {
                        Outcome o;
                        o.message = commit.message();
                        done(o);
                        return;
                    }
                    RunOptions u;
                    u.noHooks = true;
                    git(folder, {u"update-ref"_s, u"-m"_s, u"Brulion checkpoint"_s, checkpointRef(id), oid}, u,
                        [oid, done](const RunResult &ref) {
                            Outcome o;
                            o.ok = ref.ok();
                            o.message = ref.ok() ? GitMessages::tr("Checkpoint made") : ref.message();
                            o.value = oid;
                            done(o);
                        });
                });
            });
    });
}

void Client::changesSince(const QString &folder, const QString &id, std::function<void(const ChangesReport &)> done)
{
    if (!isValidCheckpointId(id)) {
        later([done = std::move(done), id] {
            ChangesReport r;
            r.error = GitMessages::tr("Not a checkpoint's id: %1").arg(id);
            done(r);
        });
        return;
    }
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"rev-parse"_s, u"-q"_s, u"--verify"_s, checkpointRef(id) + u"^{tree}"_s}, ro,
        [this, folder, ro, done = std::move(done)](const RunResult &c) {
            const QString base = QString::fromUtf8(c.out).trimmed();
            if (!c.ok() || base.isEmpty()) {
                ChangesReport r;
                r.error = c.failure.isEmpty() ? GitMessages::tr("The checkpoint does not exist") : c.failure;
                done(r);
                return;
            }
            snapshotTree(folder, [this, folder, ro, base, done](const QString &now, const QString &error) {
                if (now.isEmpty()) {
                    ChangesReport r;
                    r.error = error;
                    done(r);
                    return;
                }
                git(folder,
                    {u"diff-tree"_s, u"-r"_s, u"-z"_s, u"-M"_s, u"--raw"_s, u"--numstat"_s, u"--no-ext-diff"_s,
                     u"--no-textconv"_s, base, now},
                    ro, [done](const RunResult &d) {
                        ChangesReport r;
                        if (!d.ok()) {
                            r.error = d.message();
                            done(r);
                            return;
                        }
                        r.ok = true;
                        const DiffRecords records = parseDiff(d.out);
                        QHash<QString, const NumstatEntry *> counts;
                        for (const NumstatEntry &n : records.numstat)
                            counts.insert(n.path, &n);
                        for (const RawEntry &e : records.raw) {
                            FileChange f;
                            f.path = e.path;
                            f.oldPath = e.oldPath;
                            f.change = changeOfRawStatus(e.status);
                            f.unstaged = true;
                            f.oldMode = e.oldMode;
                            f.newMode = e.newMode;
                            if (const NumstatEntry *n = counts.value(e.path)) {
                                f.binary = n->binary;
                                f.additions = n->binary ? -1 : n->additions;
                                f.deletions = n->binary ? -1 : n->deletions;
                            }
                            r.changes.append(f);
                        }
                        done(r);
                    });
            });
        });
}

void Client::restore(const QString &folder, const QString &id, std::function<void(const RestoreReport &)> done)
{
    if (!isValidCheckpointId(id)) {
        later([done = std::move(done), id] {
            RestoreReport r;
            r.error = GitMessages::tr("Not a checkpoint's id: %1").arg(id);
            done(r);
        });
        return;
    }
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"rev-parse"_s, u"-q"_s, u"--verify"_s, checkpointRef(id) + u"^{tree}"_s}, ro,
        [this, folder, ro, done = std::move(done)](const RunResult &c) {
            const QString base = QString::fromUtf8(c.out).trimmed();
            if (!c.ok() || base.isEmpty()) {
                RestoreReport r;
                r.error = c.failure.isEmpty() ? GitMessages::tr("The checkpoint does not exist") : c.failure;
                done(r);
                return;
            }
            snapshotTree(folder, [this, folder, ro, base, done](const QString &now, const QString &error) {
                if (now.isEmpty()) {
                    RestoreReport r;
                    r.error = error;
                    done(r);
                    return;
                }
                git(folder, {u"diff-tree"_s, u"-r"_s, u"-z"_s, u"--no-renames"_s, u"--raw"_s, base, now}, ro,
                    [this, folder, ro, base, done](const RunResult &d) {
                        auto report = std::make_shared<RestoreReport>();
                        if (!d.ok()) {
                            report->error = d.message();
                            done(*report);
                            return;
                        }
                        // What to write back (changed or deleted since) and what to delete
                        // (added since); submodules stay as they are.
                        QStringList restore;
                        QStringList added;
                        for (const RawEntry &e : parseDiff(d.out).raw) {
                            if (!isSafeRelativePath(e.path)) {
                                report->kept.append(e.path);
                                continue;
                            }
                            if (e.status == 'A') {
                                if (e.newMode == GitlinkMode)
                                    report->kept.append(e.path);
                                else
                                    added.append(e.path);
                            } else if (e.oldMode == GitlinkMode || e.newMode == GitlinkMode) {
                                report->kept.append(e.path);
                            } else {
                                restore.append(e.path);
                            }
                        }
                        // Files added since that stand where a file comes back: a file where a
                        // folder was ("x" over "x/inner"), files in a folder where a file was.
                        QSet<QString> blockers;
                        for (const QString &a : std::as_const(added)) {
                            for (const QString &p : std::as_const(restore)) {
                                if (p.startsWith(a + u'/') || a.startsWith(p + u'/')) {
                                    blockers.insert(a);
                                    break;
                                }
                            }
                        }
                        QStringList rest;
                        for (const QString &a : std::as_const(added)) {
                            if (!blockers.contains(a)) {
                                rest.append(a);
                                continue;
                            }
                            if (deleteFile(folder, a))
                                report->removed.append(a);
                            else
                                report->kept.append(a);
                        }
                        for (const QString &a : std::as_const(report->removed))
                            pruneEmptyFolders(folder, a);
                        // A folder still where a file comes back (it holds ignored files): left.
                        QStringList writable;
                        for (const QString &p : std::as_const(restore)) {
                            const QFileInfo info(inTree(folder, p));
                            if (!plainWay(folder, p) || (info.isDir() && !info.isSymbolicLink()))
                                report->kept.append(p);
                            else
                                writable.append(p);
                        }
                        const auto removeRest = [this, folder, ro, rest, report, done] {
                            if (rest.isEmpty()) {
                                report->ok = report->error.isEmpty();
                                done(*report);
                                return;
                            }
                            // Ignored now (by the .gitignore just written back): it may have been
                            // there before the checkpoint, ignored — left alone.
                            RunOptions ci = ro;
                            ci.input = nulList(rest);
                            git(folder, {u"check-ignore"_s, u"-z"_s, u"--stdin"_s}, ci,
                                [folder, rest, report, done](const RunResult &ig) {
                                    if (!ig.failure.isEmpty() || (ig.exitCode != 0 && ig.exitCode != 1)) {
                                        report->error = ig.message();
                                        done(*report);
                                        return;
                                    }
                                    QSet<QString> ignored;
                                    for (const QByteArray &p : ig.out.split('\0')) {
                                        if (!p.isEmpty())
                                            ignored.insert(QString::fromUtf8(p));
                                    }
                                    for (const QString &a : rest) {
                                        if (ignored.contains(a)) {
                                            report->kept.append(a);
                                        } else if (deleteFile(folder, a)) {
                                            report->removed.append(a);
                                            pruneEmptyFolders(folder, a);
                                        } else {
                                            report->kept.append(a);
                                        }
                                    }
                                    report->ok = report->error.isEmpty();
                                    done(*report);
                                });
                        };
                        if (writable.isEmpty()) {
                            removeRest();
                            return;
                        }
                        // The checkpoint's files written out of a temporary index of its tree:
                        // the real index is not read or written.
                        auto temp = std::make_shared<QTemporaryDir>();
                        if (!temp->isValid()) {
                            report->error = GitMessages::tr("No temporary folder: %1").arg(temp->errorString());
                            done(*report);
                            return;
                        }
                        RunOptions t;
                        t.noHooks = true;
                        t.env = {{u"GIT_INDEX_FILE"_s, temp->filePath(u"index"_s)}};
                        git(folder, {u"read-tree"_s, base}, t,
                            [this, folder, t, temp, writable, report, done, removeRest](const RunResult &rt) {
                                if (!rt.ok()) {
                                    report->error = rt.message();
                                    done(*report);
                                    return;
                                }
                                RunOptions co = t;
                                co.input = nulList(writable);
                                git(folder, {u"checkout-index"_s, u"-f"_s, u"-z"_s, u"--stdin"_s}, co,
                                    [temp, writable, report, removeRest](const RunResult &out) {
                                        if (!out.ok())
                                            report->error = out.message();
                                        report->restored = writable;
                                        removeRest();
                                    });
                            });
                    });
            });
        });
}

void Client::dropCheckpoints(const QString &folder, const QStringList &ids, std::function<void(const Outcome &)> done)
{
    QByteArray input;
    for (const QString &id : ids) {
        if (isValidCheckpointId(id))
            input += "delete " + checkpointRef(id).toUtf8() + '\n';
    }
    if (input.isEmpty()) {
        later([done = std::move(done)] {
            Outcome o;
            o.ok = true;
            done(o);
        });
        return;
    }
    RunOptions o;
    o.noHooks = true;
    o.input = input;
    git(folder, {u"update-ref"_s, u"--stdin"_s}, o, [done = std::move(done)](const RunResult &r) {
        Outcome out;
        out.ok = r.ok();
        out.message = r.ok() ? GitMessages::tr("Checkpoints deleted") : r.message();
        done(out);
    });
}

void Client::checkpoints(const QString &folder,
                         std::function<void(bool ok, const QStringList &ids, const QString &error)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    git(folder, {u"for-each-ref"_s, u"--format=%(refname)"_s, u"refs/brulion/checkpoints/"_s}, ro,
        [done = std::move(done)](const RunResult &r) {
            if (!r.ok()) {
                done(false, {}, r.message());
                return;
            }
            QStringList ids;
            const QString prefix = checkpointRef(QString());
            for (const QString &line : QString::fromUtf8(r.out).split(u'\n', Qt::SkipEmptyParts)) {
                if (line.startsWith(prefix))
                    ids.append(line.sliced(prefix.size()));
            }
            done(true, ids, {});
        });
}

} // namespace brulion::gitcli

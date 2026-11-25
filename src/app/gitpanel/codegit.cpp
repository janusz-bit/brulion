#include "codegit.h"

#include "gitcli/gitcli.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace brulion::gitpanel {

namespace {

/// One row of `changes` / `turnChanges` (the keys the panel's delegates require).
QVariantMap changeRow(const gitcli::FileChange &c)
{
    // (An untracked folder — a repository inside — comes as "dir/".)
    QString bare = c.path;
    while (bare.endsWith(u'/'))
        bare.chop(1);
    const qsizetype slash = bare.lastIndexOf(u'/');
    QVariantMap row;
    row.insert(u"path"_s, c.path);
    row.insert(u"oldPath"_s, c.oldPath);
    row.insert(u"status"_s, gitcli::changeName(c.change));
    row.insert(u"letter"_s, gitcli::changeLetter(c.change));
    row.insert(u"staged"_s, c.staged);
    row.insert(u"unstaged"_s, c.unstaged);
    row.insert(u"additions"_s, c.additions);
    row.insert(u"deletions"_s, c.deletions);
    row.insert(u"binary"_s, c.binary);
    row.insert(u"name"_s, slash < 0 ? bare : bare.sliced(slash + 1));
    row.insert(u"dir"_s, slash < 0 ? QString() : bare.left(slash));
    return row;
}

QVariantList changeRows(const QList<gitcli::FileChange> &changes)
{
    QVariantList rows;
    rows.reserve(qMin<qsizetype>(changes.size(), CodeGit::MaxRows));
    for (const gitcli::FileChange &c : changes) {
        if (rows.size() >= CodeGit::MaxRows)
            break;
        rows.append(changeRow(c));
    }
    return rows;
}

QVariantMap branchRow(const gitcli::Branch &b)
{
    QVariantMap row;
    row.insert(u"name"_s, b.name);
    row.insert(u"remote"_s, b.remote);
    row.insert(u"current"_s, b.current);
    row.insert(u"upstream"_s, b.upstream);
    row.insert(u"ahead"_s, b.ahead);
    row.insert(u"behind"_s, b.behind);
    row.insert(u"gone"_s, b.upstreamGone);
    row.insert(u"worktreePath"_s, b.worktreePath);
    return row;
}

/// The row of `changes` (or `turnChanges`) for `path`.
QVariantMap rowFor(const QVariantList &rows, const QString &path)
{
    for (const QVariant &v : rows) {
        const QVariantMap row = v.toMap();
        if (row.value(u"path"_s).toString() == path)
            return row;
    }
    return {};
}

} // namespace

CodeGit::CodeGit(QObject *parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_debounce, &QTimer::timeout, this, &CodeGit::refresh);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (!m_refreshRunning)
            refresh();
    });
    const auto changed = [this] {
        if (!m_debounce.isActive())
            m_debounce.start();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
}

CodeGit::~CodeGit() = default;

void CodeGit::setGitProgram(const QString &program)
{
    m_client.setGitProgram(program);
    Q_EMIT toolsChanged();
}

void CodeGit::setGhProgram(const QString &program)
{
    m_client.setGhProgram(program);
    Q_EMIT toolsChanged();
}

void CodeGit::bump()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

// ---- Inputs ------------------------------------------------------------------------------------

void CodeGit::setFolder(const QString &folder)
{
    const QString cleaned = folder.isEmpty() ? QString() : QDir::cleanPath(folder);
    if (cleaned == m_folder)
        return;
    m_folder = cleaned;
    ++m_generation;
    // What waits for the old folder is dropped; an action running there ends there.
    const QList<Task> dropped = m_queue;
    m_queue.clear();
    m_refreshRunning = false;
    m_refreshAgain = false;
    m_info = gitcli::RepoInfo();
    m_infoFolder.clear();
    m_loaded = false;
    m_branch.clear();
    m_upstream.clear();
    m_ahead = m_behind = 0;
    m_detached = false;
    m_hasCommits = false;
    m_headOid.clear();
    m_changes.clear();
    m_changeCount = 0;
    m_turnChanges.clear();
    m_turnError.clear();
    m_turnLoaded = false;
    m_branchList.clear();
    m_worktreeList.clear();
    m_refreshError.clear();
    m_actionError.clear();
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    Q_EMIT folderChanged();
    Q_EMIT stateChanged();
    Q_EMIT turnChangesChanged();
    Q_EMIT branchListChanged();
    Q_EMIT worktreeListChanged();
    Q_EMIT refreshingChanged();
    Q_EMIT lastErrorChanged();
    Q_EMIT busyChanged();
    for (const Task &t : dropped) {
        const QString why = tr("The folder changed before it ran");
        if (!t.checkpointId.isEmpty())
            Q_EMIT checkpointMade(t.checkpointId, false, why);
        Q_EMIT actionFinished(t.action, false, why, QString());
    }
    bump();
    refresh();
}

void CodeGit::setTurnCheckpoint(const QString &id)
{
    if (id == m_turnCheckpoint)
        return;
    m_turnCheckpoint = id;
    m_turnChanges.clear();
    m_turnError.clear();
    m_turnLoaded = false;
    Q_EMIT turnCheckpointChanged();
    Q_EMIT turnChangesChanged();
    bump();
    if (!id.isEmpty() && !m_folder.isEmpty())
        changesSince(id);
}

void CodeGit::setPollInterval(int ms)
{
    ms = qMax(0, ms);
    if (ms == m_pollInterval)
        return;
    m_pollInterval = ms;
    if (ms > 0)
        m_poll.start(ms);
    else
        m_poll.stop();
    Q_EMIT pollIntervalChanged();
}

// ---- Refresh -----------------------------------------------------------------------------------

void CodeGit::withRepo(std::function<void(bool isRepo)> done)
{
    // Known — and its git folder still there (a deleted repository is resolved again).
    if (m_infoFolder == m_folder && m_info.isRepo && QFileInfo::exists(m_info.gitDir)) {
        done(true);
        return;
    }
    const int generation = m_generation;
    const QString folder = m_folder;
    m_client.repository(folder, [this, generation, folder, done = std::move(done)](const gitcli::RepoInfo &info) {
        if (generation != m_generation) {
            done(false); // (the caller sees the generation: an action ends, the rest drops it)
            return;
        }
        const bool was = m_info.isRepo;
        const QString top = m_info.topLevel;
        m_info = info;
        m_infoFolder = folder;
        if (!info.isRepo) {
            setNotRepo(info.error, was);
        } else if (!was || top != info.topLevel) {
            Q_EMIT stateChanged();
            bump();
        }
        updateWatches();
        done(info.isRepo);
    });
}

void CodeGit::refresh()
{
    if (m_folder.isEmpty()) {
        const bool changed = !m_loaded || m_info.isRepo;
        m_info = gitcli::RepoInfo();
        m_loaded = true;
        if (changed) {
            Q_EMIT stateChanged();
            bump();
        }
        return;
    }
    if (m_refreshRunning) {
        m_refreshAgain = true;
        return;
    }
    m_refreshRunning = true;
    Q_EMIT refreshingChanged();
    const int generation = m_generation;
    withRepo([this, generation](bool isRepo) {
        if (generation != m_generation)
            return;
        if (!isRepo) {
            endRefresh();
            return;
        }
        m_client.status(m_info.topLevel, [this, generation](const gitcli::StatusReport &report) {
            if (generation != m_generation)
                return;
            if (!report.ok) {
                // Gone since (the folder deleted, the repository removed): found out again next time.
                if (!QFileInfo::exists(m_info.gitDir) || !QFileInfo(m_info.topLevel).isDir()) {
                    m_infoFolder.clear();
                    setNotRepo(report.error, true);
                } else {
                    setRefreshError(report.error);
                }
                endRefresh();
                return;
            }
            applyStatus(report);
            if (m_turnCheckpoint.isEmpty()) {
                endRefresh();
                return;
            }
            const QString checkpoint = m_turnCheckpoint;
            m_client.changesSince(m_info.topLevel, checkpoint,
                                  [this, generation, checkpoint](const gitcli::ChangesReport &turn) {
                                      if (generation != m_generation)
                                          return;
                                      applyTurn(checkpoint, turn);
                                      endRefresh();
                                  });
        });
    });
}

void CodeGit::endRefresh()
{
    m_refreshRunning = false;
    Q_EMIT refreshingChanged();
    updateWatches();
    if (m_refreshAgain) {
        m_refreshAgain = false;
        refresh();
    }
}

void CodeGit::applyStatus(const gitcli::StatusReport &report)
{
    const gitcli::BranchStatus &b = report.status.branch;
    const QVariantList rows = changeRows(report.changes);
    const bool changed = !m_loaded || !m_info.isRepo || m_branch != b.head || m_upstream != b.upstream
                         || m_ahead != b.ahead || m_behind != b.behind || m_detached != b.detached
                         || m_hasCommits == b.initial || m_headOid != b.oid || m_changeCount != report.changes.size()
                         || m_changes != rows;
    m_loaded = true;
    m_branch = b.head;
    m_upstream = b.upstream;
    m_ahead = b.ahead;
    m_behind = b.behind;
    m_detached = b.detached;
    m_hasCommits = !b.initial;
    m_headOid = b.oid;
    m_changes = rows;
    m_changeCount = int(report.changes.size());
    setRefreshError(QString());
    if (changed) {
        Q_EMIT stateChanged();
        bump();
    }
}

void CodeGit::applyTurn(const QString &checkpoint, const gitcli::ChangesReport &report)
{
    const QVariantList rows = report.ok ? changeRows(report.changes) : QVariantList();
    Q_EMIT changesSinceReady(checkpoint, rows);
    if (checkpoint != m_turnCheckpoint)
        return;
    const QString error = report.ok ? QString() : report.error;
    if (m_turnLoaded && rows == m_turnChanges && error == m_turnError)
        return;
    m_turnChanges = rows;
    m_turnError = error;
    m_turnLoaded = true;
    Q_EMIT turnChangesChanged();
    bump();
}

void CodeGit::setNotRepo(const QString &error, bool wasRepo)
{
    const bool changed = !m_loaded || wasRepo || m_info.isRepo || !m_changes.isEmpty() || !m_branch.isEmpty();
    m_info.isRepo = false;
    m_loaded = true;
    m_branch.clear();
    m_upstream.clear();
    m_ahead = m_behind = 0;
    m_detached = false;
    m_hasCommits = false;
    m_headOid.clear();
    m_changes.clear();
    m_changeCount = 0;
    setRefreshError(error);
    if (changed) {
        Q_EMIT stateChanged();
        bump();
    }
}

void CodeGit::setRefreshError(const QString &error)
{
    if (error == m_refreshError)
        return;
    const QString before = lastError();
    m_refreshError = error;
    if (lastError() != before)
        Q_EMIT lastErrorChanged();
}

void CodeGit::setActionError(const QString &error)
{
    if (error == m_actionError)
        return;
    const QString before = lastError();
    m_actionError = error;
    if (lastError() != before)
        Q_EMIT lastErrorChanged();
}

void CodeGit::updateWatches()
{
    QStringList wanted;
    const auto want = [&wanted](const QString &path) {
        if (!path.isEmpty() && QFileInfo::exists(path))
            wanted.append(path);
    };
    if (m_info.isRepo) {
        // HEAD and the index are replaced (renamed over) by git: the folder that holds them sees
        // that too, and the files are watched again after each refresh.
        want(m_info.gitDir);
        want(m_info.gitDir + u"/HEAD"_s);
        want(m_info.gitDir + u"/index"_s);
        want(m_info.topLevel);
    } else if (!m_folder.isEmpty()) {
        want(m_folder); // a `git init` there
    }
    const QStringList current = m_watcher.files() + m_watcher.directories();
    QStringList stale;
    for (const QString &p : current) {
        if (!wanted.contains(p))
            stale.append(p);
    }
    if (!stale.isEmpty())
        m_watcher.removePaths(stale);
    QStringList missing;
    for (const QString &p : std::as_const(wanted)) {
        if (!current.contains(p))
            missing.append(p);
    }
    if (!missing.isEmpty())
        m_watcher.addPaths(missing);
}

// ---- Reading -----------------------------------------------------------------------------------

void CodeGit::requestDiff(const QString &path, const QString &checkpoint)
{
    const int generation = m_generation;
    withRepo([this, generation, path, checkpoint](bool isRepo) {
        if (generation != m_generation)
            return;
        if (!isRepo) {
            Q_EMIT diffFailed(path, tr("Not a git repository"));
            return;
        }
        const QVariantMap row =
                rowFor(!checkpoint.isEmpty() && checkpoint == m_turnCheckpoint ? m_turnChanges : m_changes, path);
        gitcli::FileTextsRequest request;
        request.path = path;
        if (row.value(u"status"_s).toString() == u"renamed"_s)
            request.oldPath = row.value(u"oldPath"_s).toString();
        if (!checkpoint.isEmpty())
            request.base = gitcli::Client::checkpointRef(checkpoint);
        else if (!m_hasCommits)
            request.base.clear();
        m_client.fileTexts(m_info.topLevel, request, [this, generation, path](const gitcli::FileTexts &t) {
            if (generation != m_generation)
                return;
            if (!t.ok)
                Q_EMIT diffFailed(path, t.error);
            else
                Q_EMIT diffReady(path, t.oldText, t.newText, t.binary, t.tooLarge);
        });
    });
}

void CodeGit::branches()
{
    const int generation = m_generation;
    withRepo([this, generation](bool isRepo) {
        if (generation != m_generation || !isRepo)
            return;
        m_client.branches(m_info.topLevel, [this, generation](const gitcli::BranchList &list) {
            if (generation != m_generation)
                return;
            if (!list.ok) {
                setRefreshError(list.error);
                return;
            }
            QSet<QString> tracked;
            for (const gitcli::Branch &b : list.branches) {
                if (!b.remote && !b.upstream.isEmpty())
                    tracked.insert(b.upstream);
            }
            QVariantList current;
            QVariantList local;
            QVariantList remote;
            for (const gitcli::Branch &b : list.branches) {
                if (b.current)
                    current.append(branchRow(b));
                else if (!b.remote)
                    local.append(branchRow(b));
                else if (!tracked.contains(b.name))
                    remote.append(branchRow(b));
            }
            m_branchList = current + local + remote;
            Q_EMIT branchListChanged();
            bump();
            Q_EMIT branchesReady(m_branchList);
        });
    });
}

void CodeGit::worktrees()
{
    const int generation = m_generation;
    withRepo([this, generation](bool isRepo) {
        if (generation != m_generation || !isRepo)
            return;
        m_client.worktrees(m_info.topLevel, [this, generation](const gitcli::WorktreeList &list) {
            if (generation != m_generation)
                return;
            if (!list.ok) {
                setRefreshError(list.error);
                return;
            }
            QVariantList rows;
            const QString here = QFileInfo(m_info.topLevel).canonicalFilePath();
            for (const gitcli::Worktree &w : list.worktrees) {
                QVariantMap row;
                row.insert(u"path"_s, w.path);
                row.insert(u"branch"_s, w.branch);
                row.insert(u"head"_s, w.head);
                row.insert(u"bare"_s, w.bare);
                row.insert(u"detached"_s, w.detached);
                row.insert(u"locked"_s, w.locked);
                row.insert(u"prunable"_s, w.prunable);
                row.insert(u"current"_s, w.path == m_info.topLevel || QFileInfo(w.path).canonicalFilePath() == here);
                rows.append(row);
            }
            m_worktreeList = rows;
            Q_EMIT worktreeListChanged();
            bump();
            Q_EMIT worktreesReady(rows);
        });
    });
}

void CodeGit::changesSince(const QString &checkpoint)
{
    const int generation = m_generation;
    withRepo([this, generation, checkpoint](bool isRepo) {
        if (generation != m_generation)
            return;
        if (!isRepo) {
            gitcli::ChangesReport none;
            none.error = tr("Not a git repository");
            applyTurn(checkpoint, none);
            return;
        }
        m_client.changesSince(m_info.topLevel, checkpoint,
                              [this, generation, checkpoint](const gitcli::ChangesReport &report) {
                                  if (generation == m_generation)
                                      applyTurn(checkpoint, report);
                              });
    });
}

// ---- Actions -----------------------------------------------------------------------------------

void CodeGit::enqueue(const QString &action, std::function<void()> run, const QString &checkpointId)
{
    m_queue.append(Task{action, std::move(run), checkpointId});
    Q_EMIT busyChanged();
    if (!m_taskRunning)
        startNext();
}

void CodeGit::startNext()
{
    if (m_taskRunning || m_queue.isEmpty())
        return;
    const Task task = m_queue.takeFirst();
    m_taskRunning = true;
    m_currentAction = task.action;
    Q_EMIT busyChanged();
    task.run();
}

void CodeGit::taskDone()
{
    m_taskRunning = false;
    m_currentAction.clear();
    Q_EMIT busyChanged();
    startNext();
}

void CodeGit::finishAction(const QString &action, bool ok, const QString &message, const QString &url)
{
    setActionError(ok ? QString() : message);
    Q_EMIT actionFinished(action, ok, message, url);
    taskDone();
    refresh();
}

void CodeGit::runAction(const QString &action, std::function<void(const QString &root)> body)
{
    enqueue(action, [this, action, body = std::move(body)] {
        const int generation = m_generation;
        withRepo([this, action, body, generation](bool isRepo) {
            if (generation != m_generation)
                finishAction(action, false, tr("The folder changed before it ran"), QString());
            else if (!isRepo)
                finishAction(action, false, tr("Not a git repository"), QString());
            else
                body(m_info.topLevel);
        });
    });
}

void CodeGit::commit(const QString &message, bool push, bool createPr, const QString &prTitle, const QString &prBody,
                     bool draft)
{
    runAction(u"commit"_s, [this, message, push, createPr, prTitle, prBody, draft](const QString &root) {
        m_client.commitAll(
                root, message,
                [this, root, message, push, createPr, prTitle, prBody, draft](const gitcli::Outcome &committed) {
                    if (!committed.ok || (!push && !createPr)) {
                        finishAction(u"commit"_s, committed.ok, committed.message, QString());
                        return;
                    }
                    m_client.push(root, [this, root, message, createPr, prTitle, prBody,
                                         draft](const gitcli::Outcome &pushed) {
                        if (!pushed.ok) {
                            finishAction(u"commit"_s, false,
                                         tr("Committed, but the push failed: %1").arg(pushed.message), QString());
                            return;
                        }
                        if (!createPr) {
                            finishAction(u"commit"_s, true, tr("Committed and pushed"), QString());
                            return;
                        }
                        gitcli::PullRequest pr;
                        pr.title = prTitle.trimmed().isEmpty() ? gitcli::subjectOf(message) : prTitle.trimmed();
                        pr.body = prBody;
                        pr.draft = draft;
                        pr.head = m_branch;
                        m_client.createPullRequest(root, pr, [this](const gitcli::Outcome &opened) {
                            finishAction(
                                    u"commit"_s, opened.ok,
                                    opened.ok ? opened.message
                                              : tr("Committed and pushed, but no pull request: %1").arg(opened.message),
                                    opened.url);
                        });
                    });
                });
    });
}

void CodeGit::push()
{
    runAction(u"push"_s, [this](const QString &root) {
        m_client.push(root, [this](const gitcli::Outcome &o) { finishAction(u"push"_s, o.ok, o.message, QString()); });
    });
}

void CodeGit::pull()
{
    runAction(u"pull"_s, [this](const QString &root) {
        m_client.pull(root, [this](const gitcli::Outcome &o) { finishAction(u"pull"_s, o.ok, o.message, QString()); });
    });
}

void CodeGit::createBranch(const QString &name)
{
    runAction(u"createBranch"_s, [this, name](const QString &root) {
        m_client.createBranch(root, name, [this](const gitcli::Outcome &o) {
            finishAction(u"createBranch"_s, o.ok, o.message, QString());
        });
    });
}

void CodeGit::checkout(const QString &name)
{
    runAction(u"checkout"_s, [this, name](const QString &root) {
        m_client.checkout(root, name, [this](const gitcli::Outcome &o) {
            finishAction(u"checkout"_s, o.ok, o.message, QString());
        });
    });
}

void CodeGit::createPullRequest(const QString &title, const QString &body, bool draft)
{
    runAction(u"pullRequest"_s, [this, title, body, draft](const QString &root) {
        gitcli::PullRequest pr;
        pr.title = title;
        pr.body = body;
        pr.draft = draft;
        pr.head = m_branch;
        const auto open = [this, root, pr] {
            m_client.createPullRequest(root, pr, [this](const gitcli::Outcome &o) {
                finishAction(u"pullRequest"_s, o.ok, o.message, o.url);
            });
        };
        if (pr.title.trimmed().isEmpty() || (!m_upstream.isEmpty() && m_ahead == 0)) {
            open();
            return;
        }
        // gh needs the branch on the remote: pushed first.
        m_client.push(root, [this, open](const gitcli::Outcome &pushed) {
            if (!pushed.ok) {
                finishAction(u"pullRequest"_s, false,
                             tr("The push before the pull request failed: %1").arg(pushed.message), QString());
                return;
            }
            open();
        });
    });
}

QString CodeGit::checkpoint()
{
    const QString id = gitcli::Client::newCheckpointId();
    enqueue(
            u"checkpoint"_s,
            [this, id] {
                const int generation = m_generation;
                const auto done = [this, id](bool ok, const QString &message) {
                    setActionError(ok ? QString() : message);
                    Q_EMIT checkpointMade(id, ok, message);
                    if (!ok)
                        Q_EMIT actionFinished(u"checkpoint"_s, false, message, QString());
                    taskDone();
                };
                withRepo([this, id, generation, done](bool isRepo) {
                    if (generation != m_generation) {
                        done(false, tr("The folder changed before it ran"));
                        return;
                    }
                    if (!isRepo) {
                        done(false, tr("Not a git repository"));
                        return;
                    }
                    m_client.checkpoint(m_info.topLevel, id,
                                        [done](const gitcli::Outcome &o) { done(o.ok, o.message); });
                });
            },
            id);
    return id;
}

void CodeGit::restore(const QString &checkpoint)
{
    runAction(u"restore"_s, [this, checkpoint](const QString &root) {
        m_client.restore(root, checkpoint, [this](const gitcli::RestoreReport &r) {
            const int files = int(r.restored.size() + r.removed.size());
            QString message;
            if (!r.ok)
                message = r.error;
            else if (files == 0)
                message = tr("Nothing to revert: the files are as they were");
            else
                message = tr("Reverted %n file(s)", "", files);
            finishAction(u"restore"_s, r.ok, message, QString());
        });
    });
}

void CodeGit::dropCheckpoints(const QStringList &ids)
{
    runAction(u"dropCheckpoints"_s, [this, ids](const QString &root) {
        m_client.dropCheckpoints(root, ids, [this](const gitcli::Outcome &o) {
            finishAction(u"dropCheckpoints"_s, o.ok, o.message, QString());
        });
    });
}

void CodeGit::addWorktree(const QString &path, const QString &branch, const QString &base)
{
    runAction(u"addWorktree"_s, [this, path, branch, base](const QString &root) {
        m_client.addWorktree(root, path, branch, base, [this](const gitcli::Outcome &o) {
            finishAction(u"addWorktree"_s, o.ok, o.message, o.ok ? o.value : QString());
            worktrees();
        });
    });
}

void CodeGit::removeWorktree(const QString &path, bool force)
{
    runAction(u"removeWorktree"_s, [this, path, force](const QString &root) {
        m_client.removeWorktree(root, path, force, [this](const gitcli::Outcome &o) {
            finishAction(u"removeWorktree"_s, o.ok, o.message, QString());
            worktrees();
        });
    });
}

QString CodeGit::subjectOf(const QString &message) const
{
    return gitcli::subjectOf(message);
}

bool CodeGit::isValidBranchName(const QString &name) const
{
    return gitcli::isValidBranchName(name);
}

} // namespace brulion::gitpanel

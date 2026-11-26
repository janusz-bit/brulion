#include "codegit.h"

#include "gitcli/gitcli.h"

#include "gitdiffrows.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
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
    // (The keys of treeRows()' rows: the list's delegate reads them in both layouts.)
    row.insert(u"kind"_s, u"file"_s);
    row.insert(u"depth"_s, 0);
    row.insert(u"collapsed"_s, false);
    row.insert(u"fileCount"_s, 0);
    return row;
}

/// "just now", "5 minutes ago" … for a time in seconds since 1970.
QString whenText(qint64 time)
{
    const qint64 ago = QDateTime::currentSecsSinceEpoch() - time;
    if (time <= 0)
        return {};
    if (ago < 60)
        return CodeGit::tr("just now");
    if (ago < 3600)
        return CodeGit::tr("%n minute(s) ago", "", int(ago / 60));
    if (ago < 86400)
        return CodeGit::tr("%n hour(s) ago", "", int(ago / 3600));
    if (ago < 30 * 86400)
        return CodeGit::tr("%n day(s) ago", "", int(ago / 86400));
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(time).date(), QLocale::ShortFormat);
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
    m_changePaths.clear();
    m_stagedPaths.clear();
    m_conflictPaths.clear();
    m_operation.clear();
    m_defaultBranch.clear();
    m_defaultBranchRef.clear();
    m_remoteNames.clear();
    m_factsBranch.clear();
    m_factsKnown = false;
    m_prBranch.clear();
    m_pullRequest.clear();
    ++m_prSerial;
    m_comparisonBase.clear();
    m_compareChanges.clear();
    m_compareLoaded = false;
    m_compareError.clear();
    ++m_compareSerial;
    m_commitList.clear();
    m_stashList.clear();
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
    Q_EMIT pullRequestChanged();
    Q_EMIT compareChanged();
    Q_EMIT commitListChanged();
    Q_EMIT stashListChanged();
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
            loadCompare([this, generation] {
                if (generation != m_generation)
                    return;
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
    QStringList paths;
    QStringList staged;
    QStringList conflicts;
    for (const gitcli::FileChange &c : report.changes) {
        paths.append(c.path);
        if (c.staged)
            staged.append(c.path);
        if (c.change == gitcli::Change::Conflicted)
            conflicts.append(c.path);
    }
    const QString operation = gitcli::Client::operationName(gitcli::Client::operationIn(m_info.gitDir));
    const bool more = paths != m_changePaths || staged != m_stagedPaths || conflicts != m_conflictPaths
                      || operation != m_operation;
    m_changePaths = paths;
    m_stagedPaths = staged;
    m_conflictPaths = conflicts;
    m_operation = operation;
    setRefreshError(QString());
    if (changed || more) {
        Q_EMIT stateChanged();
        bump();
    }
    // The default branch and the remotes once and on a switch of branch; the pull request when
    // the branch or its upstream changes (gh goes to the network: not on every refresh).
    // (The remotes live in the repository's config, origin/HEAD in a ref: their times tell.)
    const QString common = m_info.commonDir.isEmpty() ? m_info.gitDir : m_info.commonDir;
    const auto stamp = [&common](const QString &name) {
        const QFileInfo f(common + u'/' + name);
        return f.exists() ? QString::number(f.lastModified().toMSecsSinceEpoch()) : QString();
    };
    const QString branchKey = m_branch + u'\n' + m_upstream;
    const QString facts = branchKey + u'\n' + stamp(u"config"_s) + u'\n' + stamp(u"refs/remotes/origin/HEAD"_s);
    if (!m_factsKnown || facts != m_factsBranch) {
        m_factsBranch = facts;
        m_factsKnown = true;
        loadRepoFacts();
    }
    if (branchKey != m_prBranch) {
        m_prBranch = branchKey;
        refreshPullRequest();
    }
}

void CodeGit::loadRepoFacts()
{
    const int generation = m_generation;
    const QString root = m_info.topLevel;
    m_client.defaultBranch(root, [this, generation, root](const gitcli::DefaultBranch &d) {
        if (generation != m_generation)
            return;
        m_client.remotes(root, [this, generation, d](const gitcli::RemoteList &list) {
            if (generation != m_generation)
                return;
            QStringList names;
            for (const gitcli::Remote &r : list.remotes)
                names.append(r.name);
            if (d.name == m_defaultBranch && d.ref == m_defaultBranchRef && names == m_remoteNames)
                return;
            m_defaultBranch = d.name;
            m_defaultBranchRef = d.ref;
            m_remoteNames = names;
            Q_EMIT stateChanged();
            bump();
        });
    });
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
    m_changePaths.clear();
    m_stagedPaths.clear();
    m_conflictPaths.clear();
    m_operation.clear();
    m_factsKnown = false;
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
        gitcli::CommitRequest request;
        request.message = message;
        request.paths = m_changePaths;
        m_client.commit(root, request,
                        [this, root, message, push, createPr, prTitle, prBody, draft](const gitcli::Outcome &committed) {
                            afterCommit(root, committed, push, createPr, message, prTitle, prBody, draft, QString());
                        });
    });
}

void CodeGit::afterCommit(const QString &root, const gitcli::Outcome &committed, bool push, bool createPr,
                          const QString &message, const QString &prTitle, const QString &prBody, bool draft,
                          const QString &prBase)
{
    if (!committed.ok || (!push && !createPr)) {
        finishAction(u"commit"_s, committed.ok, committed.message, QString());
        return;
    }
    m_client.push(root, [this, root, message, createPr, prTitle, prBody, draft, prBase](const gitcli::Outcome &pushed) {
        if (!pushed.ok) {
            finishAction(u"commit"_s, false, tr("Committed, but the push failed: %1").arg(pushed.message), QString());
            return;
        }
        if (!createPr) {
            finishAction(u"commit"_s, true, tr("Committed and pushed"), QString());
            refreshPullRequest();
            return;
        }
        gitcli::PullRequest pr;
        pr.title = prTitle.trimmed().isEmpty() ? gitcli::subjectOf(message) : prTitle.trimmed();
        pr.body = prBody;
        pr.draft = draft;
        pr.base = prBase;
        openPullRequest(u"commit"_s, root, pr, tr("Committed and pushed, but no pull request: %1"));
    });
}

void CodeGit::openPullRequest(const QString &action, const QString &root, const gitcli::PullRequest &request,
                              const QString &failurePrefix)
{
    // The branch name of HEAD now (a commit on a new branch switched to it).
    const QString branch = m_branch;
    gitcli::RunOptions ro;
    ro.readOnly = true;
    m_client.git(root, {u"symbolic-ref"_s, u"-q"_s, u"--short"_s, u"HEAD"_s}, ro,
                 [this, action, root, request, failurePrefix, branch](const gitcli::RunResult &h) {
                     const QString head = h.ok() ? QString::fromUtf8(h.out).trimmed() : branch;
                     m_client.pullRequestTarget(
                             root, head, [this, action, root, request, failurePrefix, head](const gitcli::ForkTarget &fork) {
                                 gitcli::PullRequest pr = request;
                                 pr.head = fork.head.isEmpty() ? head : fork.head;
                                 pr.repo = fork.repo;
                                 m_client.createPullRequest(
                                         root, pr, [this, action, failurePrefix](const gitcli::Outcome &opened) {
                                             finishAction(action, opened.ok,
                                                          opened.ok || failurePrefix.isEmpty()
                                                                  ? opened.message
                                                                  : failurePrefix.arg(opened.message),
                                                          opened.url);
                                             refreshPullRequest();
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
    createPullRequestTo(title, body, draft, QString());
}

void CodeGit::createPullRequestTo(const QString &title, const QString &body, bool draft, const QString &base)
{
    runAction(u"pullRequest"_s, [this, title, body, draft, base](const QString &root) {
        gitcli::PullRequest pr;
        pr.title = title;
        pr.body = body;
        pr.draft = draft;
        pr.base = base;
        if (pr.title.trimmed().isEmpty() || (!m_upstream.isEmpty() && m_ahead == 0)) {
            openPullRequest(u"pullRequest"_s, root, pr, QString());
            return;
        }
        // gh needs the branch on the remote: pushed first.
        m_client.push(root, [this, root, pr](const gitcli::Outcome &pushed) {
            if (!pushed.ok) {
                finishAction(u"pullRequest"_s, false,
                             tr("The push before the pull request failed: %1").arg(pushed.message), QString());
                return;
            }
            openPullRequest(u"pullRequest"_s, root, pr, QString());
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

bool CodeGit::isValidRepositoryName(const QString &name) const
{
    return gitcli::isValidRepositoryName(name);
}

QString CodeGit::branchNameFor(const QString &message) const
{
    // The subject's words, lower case, joined with '-'; letters of any language kept.
    QString out;
    bool dash = false;
    for (const QChar c : gitcli::subjectOf(message).toLower()) {
        if (c.isLetterOrNumber()) {
            if (dash && !out.isEmpty())
                out += u'-';
            out += c;
            dash = false;
        } else {
            dash = true;
        }
        if (out.size() >= 48)
            break;
    }
    while (out.endsWith(u'-'))
        out.chop(1);
    return gitcli::isValidBranchName(out) ? out : QString();
}

QVariantMap CodeGit::rowOf(const QVariantList &rows, const QString &path) const
{
    return gitpanel::rowFor(rows, path);
}

QString CodeGit::commitModeFor(const QStringList &paths) const
{
    const QSet<QString> chosen(paths.cbegin(), paths.cend());
    if (chosen.isEmpty())
        return {};
    // (A merge is committed whole: git refuses a partial commit then.)
    if (chosen == QSet<QString>(m_changePaths.cbegin(), m_changePaths.cend()) || m_operation == u"merge"_s)
        return u"all"_s;
    if (!m_stagedPaths.isEmpty() && chosen == QSet<QString>(m_stagedPaths.cbegin(), m_stagedPaths.cend()))
        return u"staged"_s;
    return u"paths"_s;
}

QString CodeGit::primaryAction() const
{
    if (!m_loaded || m_folder.isEmpty())
        return {};
    if (!m_info.isRepo)
        return gitAvailable() && QFileInfo(m_folder).isDir() ? u"init"_s : QString();
    if (!m_operation.isEmpty())
        return {};
    if (m_changeCount > 0)
        return u"commit"_s;
    if (!m_hasCommits || m_detached)
        return {};
    if (!hasRemote())
        return ghAvailable() ? u"publish"_s : QString();
    if (m_upstream.isEmpty())
        return u"push"_s;
    if (m_behind > 0)
        return u"pull"_s;
    if (m_ahead > 0)
        return u"push"_s;
    const QString state = m_pullRequest.value(u"state"_s).toString();
    if (!m_pullRequest.isEmpty() && (state == u"open"_s || state == u"draft"_s))
        return u"openPr"_s;
    if (ghAvailable() && !onDefaultBranch())
        return u"createPr"_s;
    return {};
}

QString CodeGit::unavailableReason(const QString &action) const
{
    if (busy())
        return tr("Wait for the action that runs");
    if (action == u"init"_s)
        return m_info.isRepo ? tr("This is a git repository already")
               : !gitAvailable() ? tr("git is not installed")
                                 : QString();
    if (!m_info.isRepo)
        return tr("Not a git repository");
    if (action == u"commit"_s) {
        if (m_changeCount == 0)
            return tr("There are no changes to commit");
        if (m_operation == u"rebase"_s || m_operation == u"cherryPick"_s || m_operation == u"revert"_s)
            return tr("Finish or abort what is in progress first");
        return {};
    }
    if (action == u"stash"_s)
        return m_changeCount == 0 ? tr("There are no changes to stash") : QString();
    if (action == u"push"_s) {
        if (m_detached)
            return tr("HEAD is detached: switch to a branch to push it");
        if (!m_hasCommits)
            return tr("Nothing is committed yet");
        if (!hasRemote())
            return tr("The repository has no remote: publish it first");
        if (!m_upstream.isEmpty() && m_behind > 0)
            return tr("The remote has %n new commit(s): pull first", "", m_behind);
        if (!m_upstream.isEmpty() && m_ahead == 0)
            return tr("Everything is pushed");
        return {};
    }
    if (action == u"pull"_s)
        return m_upstream.isEmpty() ? tr("The branch has no upstream to pull from") : QString();
    if (action == u"createPr"_s) {
        if (!ghAvailable())
            return tr("Install the GitHub CLI (gh) to create pull requests");
        if (m_detached)
            return tr("HEAD is detached: switch to a branch first");
        if (!m_hasCommits)
            return tr("Nothing is committed yet");
        if (!hasRemote())
            return tr("The repository has no remote: publish it first");
        if (onDefaultBranch())
            return tr("This is the default branch: make a branch for the pull request");
        const QString state = m_pullRequest.value(u"state"_s).toString();
        if (state == u"open"_s || state == u"draft"_s)
            return tr("The branch has a pull request: #%1").arg(m_pullRequest.value(u"number"_s).toInt());
        return {};
    }
    if (action == u"publish"_s) {
        if (!ghAvailable())
            return tr("Install the GitHub CLI (gh) to publish repositories");
        if (hasRemote())
            return tr("The repository has a remote already");
        if (!m_hasCommits)
            return tr("Commit something first");
        return {};
    }
    return {};
}

// ---- Diff sources -------------------------------------------------------------------------------

void CodeGit::compare(bool on, const QString &base, const QString &target, bool mergeBase)
{
    if (on == m_compareOn && base == m_compareBase && target == m_compareTarget && mergeBase == m_compareMergeBase)
        return;
    m_compareOn = on;
    m_compareBase = base;
    m_compareTarget = target;
    m_compareMergeBase = mergeBase;
    m_comparisonBase = mergeBase ? QString() : base;
    m_compareChanges.clear();
    m_compareLoaded = false;
    m_compareError.clear();
    ++m_compareSerial;
    Q_EMIT compareChanged();
    bump();
    if (on && !m_folder.isEmpty())
        loadCompare({});
}

void CodeGit::loadCompare(std::function<void()> then)
{
    if (!m_compareOn) {
        if (then)
            then();
        return;
    }
    const int generation = m_generation;
    const int serial = m_compareSerial;
    const auto apply = [this, generation, serial, then](const QString &base, const gitcli::ChangesReport &report) {
        if (generation != m_generation)
            return;
        if (serial == m_compareSerial) {
            const QVariantList rows = report.ok ? changeRows(report.changes) : QVariantList();
            const QString error = report.ok ? QString() : report.error;
            if (!m_compareLoaded || rows != m_compareChanges || error != m_compareError || base != m_comparisonBase) {
                m_compareChanges = rows;
                m_compareError = error;
                m_comparisonBase = base;
                m_compareLoaded = true;
                Q_EMIT compareChanged();
                bump();
            }
        }
        if (then)
            then();
    };
    withRepo([this, generation, apply](bool isRepo) {
        if (generation != m_generation)
            return;
        if (!isRepo) {
            gitcli::ChangesReport none;
            none.error = tr("Not a git repository");
            apply(QString(), none);
            return;
        }
        const QString root = m_info.topLevel;
        const QString target = m_compareTarget;
        if (!m_compareMergeBase) {
            const QString base = m_compareBase;
            m_client.changesBetween(root, base, target,
                                    [apply, base](const gitcli::ChangesReport &r) { apply(base, r); });
            return;
        }
        m_client.mergeBase(root, m_compareBase, u"HEAD"_s, [this, root, target, apply](const gitcli::Outcome &m) {
            if (!m.ok) {
                gitcli::ChangesReport none;
                none.error = m.message;
                apply(QString(), none);
                return;
            }
            const QString base = m.value;
            m_client.changesBetween(root, base, target, [apply, base](const gitcli::ChangesReport &r) { apply(base, r); });
        });
    });
}

void CodeGit::requestDiffBetween(const QString &path, const QString &oldPath, const QString &base, const QString &target)
{
    const int generation = m_generation;
    withRepo([this, generation, path, oldPath, base, target](bool isRepo) {
        if (generation != m_generation)
            return;
        if (!isRepo) {
            Q_EMIT diffFailed(path, tr("Not a git repository"));
            return;
        }
        gitcli::FileTextsRequest request;
        request.path = path;
        request.oldPath = oldPath;
        request.base = base;
        request.target = target;
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

void CodeGit::commits()
{
    const int generation = m_generation;
    withRepo([this, generation](bool isRepo) {
        if (generation != m_generation || !isRepo)
            return;
        m_client.log(m_info.topLevel, QString(), 100, [this, generation](const gitcli::CommitList &list) {
            if (generation != m_generation)
                return;
            if (!list.ok) {
                setRefreshError(list.error);
                return;
            }
            QVariantList rows;
            for (const gitcli::Commit &c : list.commits) {
                rows.append(QVariantMap{{u"oid"_s, c.oid},
                                        {u"shortOid"_s, c.oid.left(7)},
                                        {u"subject"_s, c.subject},
                                        {u"author"_s, c.authorName},
                                        {u"time"_s, c.authorTime},
                                        {u"when"_s, whenText(c.authorTime)},
                                        {u"parentOid"_s, c.parents.value(0)}});
            }
            m_commitList = rows;
            Q_EMIT commitListChanged();
            bump();
        });
    });
}

// ---- Files and hunks ---------------------------------------------------------------------------

void CodeGit::simpleAction(const QString &action,
                           std::function<void(const QString &root, std::function<void(const gitcli::Outcome &)> done)> body)
{
    runAction(action, [this, action, body = std::move(body)](const QString &root) {
        body(root, [this, action](const gitcli::Outcome &o) { finishAction(action, o.ok, o.message, o.url); });
    });
}

void CodeGit::stage(const QStringList &paths)
{
    simpleAction(u"stage"_s, [this, paths](const QString &root, auto done) { m_client.stage(root, paths, done); });
}

void CodeGit::unstage(const QStringList &paths)
{
    simpleAction(u"unstage"_s, [this, paths](const QString &root, auto done) { m_client.unstage(root, paths, done); });
}

void CodeGit::discard(const QString &path, const QString &oldPath)
{
    simpleAction(u"discard"_s,
                 [this, path, oldPath](const QString &root, auto done) { m_client.discard(root, path, oldPath, done); });
}

void CodeGit::resolve(const QString &path, const QString &how)
{
    const gitcli::Resolution r = how == u"ours"_s     ? gitcli::Resolution::Ours
                                 : how == u"theirs"_s ? gitcli::Resolution::Theirs
                                                      : gitcli::Resolution::Resolved;
    simpleAction(u"resolve"_s, [this, path, r](const QString &root, auto done) { m_client.resolve(root, path, r, done); });
}

void CodeGit::stageHunk(const QString &path, int first, int last)
{
    simpleAction(u"stageHunk"_s, [this, path, first, last](const QString &root, auto done) {
        m_client.applyHunk(root, path, gitcli::HunkAction::Stage, first, last, done);
    });
}

void CodeGit::unstageHunk(const QString &path, int first, int last)
{
    simpleAction(u"unstageHunk"_s, [this, path, first, last](const QString &root, auto done) {
        m_client.applyHunk(root, path, gitcli::HunkAction::Unstage, first, last, done);
    });
}

void CodeGit::discardHunk(const QString &path, int first, int last)
{
    simpleAction(u"discardHunk"_s, [this, path, first, last](const QString &root, auto done) {
        m_client.applyHunk(root, path, gitcli::HunkAction::Discard, first, last, done);
    });
}

// ---- Commits, pull requests, publishing ---------------------------------------------------------

void CodeGit::commitFiles(const QString &message, const QStringList &paths, const QString &newBranch, bool push,
                          bool createPr, const QString &prTitle, const QString &prBody, bool draft, const QString &prBase)
{
    runAction(u"commit"_s, [this, message, paths, newBranch, push, createPr, prTitle, prBody, draft,
                            prBase](const QString &root) {
        // Which way: everything, the index as it is, or these files alone.
        const QSet<QString> chosen(paths.cbegin(), paths.cend());
        gitcli::CommitRequest request;
        request.message = message;
        request.newBranch = newBranch;
        request.mode = commitModeFor(paths);
        if (request.mode.isEmpty()) {
            finishAction(u"commit"_s, false, tr("Choose the files to commit"), QString());
            return;
        }
        if (request.mode == u"all"_s) {
            request.paths = m_changePaths;
        } else if (request.mode == u"staged"_s) {
            request.paths = m_stagedPaths;
        } else {
            request.mode = u"paths"_s;
            for (const QVariant &v : std::as_const(m_changes)) {
                const QVariantMap row = v.toMap();
                const QString path = row.value(u"path"_s).toString();
                if (!chosen.contains(path))
                    continue;
                request.paths.append(path);
                const QString old = row.value(u"oldPath"_s).toString();
                if (!old.isEmpty())
                    request.paths.append(old);
                if (row.value(u"status"_s).toString() == u"untracked"_s)
                    request.untracked.append(path);
            }
            // (Rows beyond maxRows: their paths as they are.)
            for (const QString &p : paths) {
                if (!request.paths.contains(p))
                    request.paths.append(p);
            }
        }
        m_client.commit(root, request, [this, root, message, push, createPr, prTitle, prBody, draft,
                                        prBase](const gitcli::Outcome &committed) {
            afterCommit(root, committed, push, createPr, message, prTitle, prBody, draft, prBase);
        });
    });
}

void CodeGit::refreshPullRequest()
{
    const int serial = ++m_prSerial;
    const int generation = m_generation;
    const auto set = [this](const QVariantMap &pr) {
        if (pr == m_pullRequest)
            return;
        m_pullRequest = pr;
        Q_EMIT pullRequestChanged();
        bump();
    };
    if (!ghAvailable() || !m_info.isRepo || m_branch.isEmpty() || m_detached) {
        set({});
        return;
    }
    const QString root = m_info.topLevel;
    const QString branch = m_branch;
    m_client.pullRequestTarget(root, branch, [this, serial, generation, root, set](const gitcli::ForkTarget &fork) {
        if (generation != m_generation || serial != m_prSerial)
            return;
        m_client.pullRequestFor(root, fork.head, fork.repo,
                                [this, serial, generation, set](const gitcli::PullRequestStatus &s) {
                                    if (generation != m_generation || serial != m_prSerial)
                                        return;
                                    if (!s.ok) // (gh failed — offline, signed out: what was known stays)
                                        return;
                                    if (!s.found) {
                                        set({});
                                        return;
                                    }
                                    set(QVariantMap{{u"number"_s, s.pr.number},
                                                    {u"url"_s, s.pr.url},
                                                    {u"state"_s, s.pr.state},
                                                    {u"title"_s, s.pr.title},
                                                    {u"baseRefName"_s, s.pr.baseRefName}});
                                });
    });
}

void CodeGit::initRepository()
{
    const QString folder = m_folder;
    enqueue(u"init"_s, [this, folder] {
        m_client.initRepository(folder, [this](const gitcli::Outcome &o) {
            // (The folder is a repository now: found out again.)
            m_infoFolder.clear();
            finishAction(u"init"_s, o.ok, o.message, QString());
        });
    });
}

void CodeGit::publish(const QString &name, bool isPublic)
{
    simpleAction(u"publish"_s, [this, name, isPublic](const QString &root, auto done) {
        m_client.publish(root, name, isPublic, [this, done](const gitcli::Outcome &o) {
            done(o);
            loadRepoFacts();
        });
    });
}

void CodeGit::revertBetween(const QString &base, const QString &target)
{
    simpleAction(u"revertTurn"_s, [this, base, target](const QString &root, auto done) {
        m_client.revertBetween(root, base, target, done);
    });
}

// ---- Merges, rebases, stashes -------------------------------------------------------------------

void CodeGit::abortOperation()
{
    const gitcli::Operation op = gitcli::Client::operationFromName(m_operation);
    simpleAction(u"abort"_s, [this, op](const QString &root, auto done) { m_client.abortOperation(root, op, done); });
}

void CodeGit::continueOperation()
{
    const gitcli::Operation op = gitcli::Client::operationFromName(m_operation);
    simpleAction(u"continue"_s, [this, op](const QString &root, auto done) { m_client.continueOperation(root, op, done); });
}

void CodeGit::stashes()
{
    const int generation = m_generation;
    withRepo([this, generation](bool isRepo) {
        if (generation != m_generation || !isRepo)
            return;
        m_client.stashes(m_info.topLevel, [this, generation](const gitcli::StashList &list) {
            if (generation != m_generation)
                return;
            if (!list.ok) {
                setRefreshError(list.error);
                return;
            }
            QVariantList rows;
            for (const gitcli::Stash &st : list.stashes) {
                rows.append(QVariantMap{{u"index"_s, st.index},
                                        {u"oid"_s, st.oid},
                                        {u"message"_s, st.message},
                                        {u"branch"_s, st.branch},
                                        {u"time"_s, st.time},
                                        {u"when"_s, whenText(st.time)}});
            }
            if (rows == m_stashList)
                return;
            m_stashList = rows;
            Q_EMIT stashListChanged();
            bump();
        });
    });
}

void CodeGit::stashPush(const QString &message)
{
    simpleAction(u"stash"_s, [this, message](const QString &root, auto done) {
        m_client.stashPush(root, message, [this, done](const gitcli::Outcome &o) {
            done(o);
            stashes();
        });
    });
}

void CodeGit::stashApply(const QString &oid)
{
    simpleAction(u"stashApply"_s, [this, oid](const QString &root, auto done) {
        m_client.stashApply(root, oid, false, [this, done](const gitcli::Outcome &o) {
            done(o);
            stashes();
        });
    });
}

void CodeGit::stashPop(const QString &oid)
{
    simpleAction(u"stashPop"_s, [this, oid](const QString &root, auto done) {
        m_client.stashApply(root, oid, true, [this, done](const gitcli::Outcome &o) {
            done(o);
            stashes();
        });
    });
}

void CodeGit::stashDrop(const QString &oid)
{
    simpleAction(u"stashDrop"_s, [this, oid](const QString &root, auto done) {
        m_client.stashDrop(root, oid, [this, done](const gitcli::Outcome &o) {
            done(o);
            stashes();
        });
    });
}

// ---- The diff view's rows -----------------------------------------------------------------------

QVariantList CodeGit::turnRows(const QVariantList &checkpoints) const
{
    QVariantList out;
    for (qsizetype i = checkpoints.size() - 1; i >= 0; --i) {
        const QVariantMap c = checkpoints.at(i).toMap();
        const QString id = c.value(u"id"_s).toString();
        if (!gitcli::isValidCheckpointId(id))
            continue;
        const int message = c.value(u"message"_s).toInt();
        const int number = message > 0 ? message : int(i) + 1;
        const QString next = i + 1 < checkpoints.size() ? checkpoints.at(i + 1).toMap().value(u"id"_s).toString() : QString();
        out.append(QVariantMap{{u"index"_s, int(i)},
                               {u"id"_s, id},
                               {u"number"_s, number},
                               {u"label"_s, tr("Turn %1").arg(number)},
                               {u"at"_s, c.value(u"at"_s).toString()},
                               {u"latest"_s, i == checkpoints.size() - 1},
                               {u"base"_s, gitcli::Client::checkpointRef(id)},
                               {u"target"_s, next.isEmpty() ? QString() : gitcli::Client::checkpointRef(next)}});
    }
    return out;
}

QVariantList CodeGit::diffRows(const QString &oldText, const QString &newText, bool split, bool ignoreWhitespace,
                               int context) const
{
    DiffOptions options;
    options.split = split;
    options.ignoreWhitespace = ignoreWhitespace;
    options.context = context;
    return gitpanel::diffRows(oldText, newText, options);
}

QList<int> CodeGit::findRows(const QVariantList &rows, const QString &query) const
{
    return gitpanel::findRows(rows, query);
}

int CodeGit::longestLine(const QVariantList &rows) const
{
    return gitpanel::longestLine(rows);
}

QString CodeGit::lineHtml(const QString &text, const QVariantList &segs, const QString &changeColor, const QString &query,
                          const QString &findColor, bool wrap) const
{
    return gitpanel::lineHtml(text, segs, changeColor, query, findColor, wrap);
}

QVariantList CodeGit::treeRows(const QVariantList &changes, const QStringList &collapsed) const
{
    return gitpanel::treeRows(changes, collapsed);
}

QString CodeGit::reviewComment(const QString &path, const QVariantList &rows, int from, int to,
                               const QString &comment) const
{
    return gitpanel::reviewComment(path, rows, from, to, comment);
}

} // namespace brulion::gitpanel

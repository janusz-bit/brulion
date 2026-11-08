// RepoSync's commits and proposals (pull requests); pulling is reposync.cpp.
#include "github/reposync.h"

#include "github/address.h"
#include "github/gitobjects.h"
#include "github/reposync_p.h"

#include "log/categories.h"
#include "sync/conflicts.h"
#include "sync/syncengine.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::github {

namespace {

using logging::lcSync;

/// UTF-8 text without a byte order mark or NUL, small enough: inline in the new tree (GitHub
/// writes the string as the blob). Everything else is uploaded as a blob, byte for byte.
bool inlineable(const QByteArray &bytes)
{
    return bytes.size() <= RepoSync::InlineTextLimit && !bytes.startsWith("\xEF\xBB\xBF") && !bytes.contains('\0')
           && sync::decodeSyncText(bytes).has_value();
}

} // namespace

void RepoSync::commit(const QString &message, const QStringList &paths, std::function<void(const CommitReport &)> done)
{
    if (!start(Operation::Commit)) {
        CommitReport report;
        report.refused = u"busy"_s;
        QTimer::singleShot(0, this, [done, report] {
            if (done)
                done(report);
        });
        return;
    }
    auto run = std::make_shared<Run>();
    run->operation = Operation::Commit;
    run->generation = ++m_generation;
    run->commitDone = std::move(done);
    run->message = message.trimmed().isEmpty() ? u"Update from Brulion"_s : message.trimmed();
    run->paths = paths;
    run->timer.start();
    m_run = run;
    QTimer::singleShot(0, this, [this, run] {
        if (run->generation == m_generation)
            startCommit(run);
    });
}

void RepoSync::propose(const QString &title, const QString &body, const QStringList &paths,
                       std::function<void(const CommitReport &)> done)
{
    if (!start(Operation::Propose)) {
        CommitReport report;
        report.refused = u"busy"_s;
        QTimer::singleShot(0, this, [done, report] {
            if (done)
                done(report);
        });
        return;
    }
    auto run = std::make_shared<Run>();
    run->operation = Operation::Propose;
    run->generation = ++m_generation;
    run->commitDone = std::move(done);
    run->title = title.trimmed().isEmpty() ? u"Changes from Brulion"_s : title.trimmed();
    run->body = body.trimmed();
    run->message = run->body.isEmpty() ? run->title : run->title + u"\n\n"_s + run->body;
    run->paths = paths;
    run->timer.start();
    m_run = run;
    QTimer::singleShot(0, this, [this, run] {
        if (run->generation == m_generation)
            startCommit(run);
    });
}

void RepoSync::startCommit(std::shared_ptr<Run> run)
{
    if (!m_client || !m_client->hasToken()) {
        // Without a token GitHub takes no commit: said before anything is asked.
        run->commit.refused = u"no-token"_s;
        reportCommit(run);
        return;
    }
    // Always on the branch's newest version: a pull first (endPull() continues here).
    startPull(run);
}

void RepoSync::storeRepository(const Repository &repository)
{
    if (!m_store)
        return;
    m_store->set(repositoryKey(), QJsonObject{{u"defaultBranch"_s, repository.defaultBranch},
                                              {u"private"_s, repository.isPrivate},
                                              {u"fork"_s, repository.isFork},
                                              {u"archived"_s, repository.archived},
                                              {u"permissionsKnown"_s, repository.permissionsKnown},
                                              {u"canPush"_s, repository.canPush},
                                              {u"url"_s, repository.htmlUrl.toString()}});
    Q_EMIT stateChanged();
}

void RepoSync::afterPullForCommit(std::shared_ptr<Run> run)
{
    CommitReport &report = run->commit;
    report.pull = run->pull;
    if (run->pull.failed()) {
        report.refused = run->pull.refused;
        reportCommit(run);
        return;
    }
    if (!run->pull.errors.isEmpty()) {
        // A file that could not be brought up to date must not be committed over GitHub's newer one.
        report.refused = u"pull-incomplete"_s;
        reportCommit(run);
        return;
    }
    if (run->operation == Operation::Propose) {
        checkProposal(run);
        return;
    }
    if (!selectTargets(run))
        return;
    Q_EMIT progress(u"commit"_s, 0, 0);
    m_client->repository(m_link.repo, [this, run](const GitHubError &error, const Repository &repository) {
        if (!alive(run->generation))
            return;
        if (error.isError()) {
            failCommit(run, error);
            return;
        }
        storeRepository(repository);
        if ((repository.permissionsKnown && !repository.canPush) || repository.archived) {
            run->commit.refused = u"read-only"_s;
            reportCommit(run);
            return;
        }
        run->target = m_link.repo;
        run->defaultBranch = repository.defaultBranch;
        if (run->state.commit.isEmpty()) {
            commitFirstFile(run);
            return;
        }
        prepareEntries(run, [this, run] { writeCommit(run); });
    });
}

bool RepoSync::selectTargets(std::shared_ptr<Run> run)
{
    CommitReport &report = run->commit;
    run->unknown.clear();
    run->local = localShas(&run->unknown);
    const QList<LocalChange> changes = localChanges();
    QSet<VaultPath> wanted(run->paths.cbegin(), run->paths.cend());
    if (run->proposal) {
        // Updating a proposal: every file it changes goes along (its version here).
        for (auto it = run->proposal->files.keyBegin(); it != run->proposal->files.keyEnd(); ++it)
            wanted.insert(*it);
    }
    // Files whose GitHub version waits in a conflict copy: merged first, or the commit would
    // replace GitHub's change with this device's version.
    QSet<VaultPath> waiting;
    for (const FileStat &file : m_vault->files()) {
        const std::optional<sync::BrulionConflict> copy = sync::parseBrulionConflict(file.path);
        if (copy && copy->device == ConflictDevice)
            waiting.insert(copy->original);
    }
    run->targets.clear();
    report.conflictsPending.clear();
    for (const LocalChange &change : changes) {
        if (!run->paths.isEmpty() && !wanted.contains(change.path))
            continue;
        if (waiting.contains(change.path))
            report.conflictsPending.append(change.path);
        run->targets.append(change.path);
    }
    if (!report.conflictsPending.isEmpty()) {
        report.refused = u"conflicts"_s;
        reportCommit(run);
        return false;
    }
    if (run->targets.isEmpty()) {
        report.nothingToCommit = true;
        reportCommit(run);
        return false;
    }
    const State &state = run->state;
    const bool anyLeft = std::any_of(state.files.keyBegin(), state.files.keyEnd(), [&run](const VaultPath &path) {
        return run->local.contains(path) || run->unknown.contains(path);
    });
    if (state.files.size() >= 2 && !anyLeft) {
        // Every file pulled before is gone here: a lost storage, not a wish to empty the folder.
        report.refused = u"local-empty"_s;
        reportCommit(run);
        return false;
    }
    return true;
}

void RepoSync::commitFirstFile(std::shared_ptr<Run> run)
{
    // An empty repository: the Git database API writes nothing into it, the contents API makes
    // its first commit. The first new file goes that way, the others in a commit after it.
    VaultPath first;
    for (const VaultPath &path : std::as_const(run->targets)) {
        if (run->local.contains(path)) {
            first = path;
            break;
        }
    }
    bool ok = false;
    const QByteArray bytes = first.isEmpty() ? QByteArray() : m_vault->readBinary(first, &ok);
    if (first.isEmpty() || !ok) {
        run->commit.nothingToCommit = first.isEmpty();
        if (!first.isEmpty())
            run->commit.error = GitHubError::make(GitHubError::Code::Validation, m_vault->errorString());
        reportCommit(run);
        return;
    }
    const QString branch =
            run->defaultBranch.isEmpty() || run->defaultBranch == m_link.branch ? QString() : m_link.branch;
    m_client->putFile(
            m_link.repo, m_link.repoPath(first), branch, run->message, bytes,
            [this, run, first, bytes](const GitHubError &error, const QString &) {
                if (!alive(run->generation))
                    return;
                if (error.isError()) {
                    failCommit(run, error);
                    return;
                }
                m_client->branch(m_link.repo, m_link.branch, {},
                                 [this, run, first, bytes](const GitHubError &headError, const BranchHead &head, bool) {
                                     if (!alive(run->generation))
                                         return;
                                     if (headError.isError()) {
                                         failCommit(run, headError);
                                         return;
                                     }
                                     State &state = run->state;
                                     state.commit = head.commit;
                                     state.tree = head.tree;
                                     state.files.insert(first, blobSha(bytes));
                                     state.emptyRepository = false;
                                     state.folderMissing = false;
                                     state.complete = true;
                                     saveState(state);
                                     run->firstCommitted.append(first);
                                     run->targets.removeAll(first);
                                     if (run->targets.isEmpty()) {
                                         afterCommitted(run, head.commit, head.tree);
                                         return;
                                     }
                                     prepareEntries(run, [this, run] { writeCommit(run); });
                                 });
            });
}

void RepoSync::prepareEntries(std::shared_ptr<Run> run, std::function<void()> then)
{
    run->entries.clear();
    run->blobs.clear();
    run->nextBlob = 0;
    run->activeBlobs = 0;
    run->newShas.clear();
    run->commit.committed = run->firstCommitted;
    const State &state = run->state;
    for (const VaultPath &path : std::as_const(run->targets)) {
        const QString base = state.files.value(path);
        const std::optional<QString> current = localSha(path);
        if (!current || *current == base)
            continue; // unreadable here (left out), or the same as GitHub's
        NewTreeEntry entry;
        entry.path = m_link.repoPath(state.names.value(path, path));
        entry.mode = state.executable.contains(path) ? ExecutableMode.toString() : FileMode.toString();
        if (current->isEmpty()) {
            if (base.isEmpty())
                continue;
            run->entries.append(entry); // removed here
            run->newShas.insert(path, QString());
            run->commit.committed.append(path);
            continue;
        }
        bool ok = false;
        const QByteArray bytes = m_vault->readBinary(path, &ok);
        if (!ok)
            continue;
        if (inlineable(bytes))
            entry.content = bytes;
        else
            run->blobs.append({run->entries.size(), bytes});
        run->entries.append(entry);
        run->newShas.insert(path, blobSha(bytes));
        run->commit.committed.append(path);
    }
    if (run->entries.isEmpty()) {
        if (!run->firstCommitted.isEmpty()) {
            afterCommitted(run, run->state.commit, run->state.tree);
            return;
        }
        run->commit.nothingToCommit = true;
        reportCommit(run);
        return;
    }
    uploadBlobs(run, std::move(then));
}

void RepoSync::uploadBlobs(std::shared_ptr<Run> run, std::function<void()> then)
{
    const qsizetype total = run->blobs.size();
    while (!run->commit.error.isError() && run->activeBlobs < m_concurrency && run->nextBlob < total) {
        const qsizetype index = run->nextBlob++;
        ++run->activeBlobs;
        Q_EMIT progress(u"upload"_s, int(index), int(total));
        const qsizetype entry = run->blobs.at(index).first;
        const QByteArray &bytes = run->blobs.at(index).second;
        const QString expected = blobSha(bytes);
        m_client->createBlob(
                run->target, bytes, [this, run, entry, expected, then](const GitHubError &error, const QString &sha) {
                    if (!alive(run->generation))
                        return;
                    --run->activeBlobs;
                    if (error.isError()) {
                        if (!run->commit.error.isError())
                            run->commit.error = error;
                    } else if (sha != expected) {
                        if (!run->commit.error.isError())
                            run->commit.error =
                                    GitHubError::make(GitHubError::Code::Integrity,
                                                      u"GitHub stored blob %1, not %2"_s.arg(sha, expected));
                    } else {
                        run->entries[entry].sha = sha;
                    }
                    uploadBlobs(run, then);
                });
    }
    if (run->activeBlobs > 0)
        return; // the last answer goes on
    if (run->commit.error.isError()) {
        failCommit(run, run->commit.error);
        return;
    }
    if (run->nextBlob >= total)
        then();
}

void RepoSync::writeCommit(std::shared_ptr<Run> run)
{
    Q_EMIT progress(u"commit"_s, 0, 0);
    m_client->createTree(
            run->target, run->state.tree, run->entries, [this, run](const GitHubError &error, const QString &tree) {
                if (!alive(run->generation))
                    return;
                if (error.isError()) {
                    failCommit(run, error);
                    return;
                }
                m_client->createCommit(run->target, run->message, tree, {run->state.commit},
                                       [this, run, tree](const GitHubError &commitError, const QString &commit) {
                                           if (!alive(run->generation))
                                               return;
                                           if (commitError.isError()) {
                                               failCommit(run, commitError);
                                               return;
                                           }
                                           moveBranch(run, commit, tree);
                                       });
            });
}

void RepoSync::moveBranch(std::shared_ptr<Run> run, const QString &commit, const QString &tree)
{
    m_client->updateBranch(m_link.repo, m_link.branch, commit, [this, run, commit, tree](const GitHubError &error) {
        if (!alive(run->generation))
            return;
        if (error.code == GitHubError::Code::NotFastForward && run->attempts < 1) {
            // Someone pushed meanwhile: their version pulled (and merged), the commit made again.
            ++run->attempts;
            qCInfo(lcSync).noquote() << u"GitHub commit on %1@%2: the branch moved meanwhile, pulling again"_s.arg(
                    m_link.repo.fullName(), m_link.branch);
            startPull(run);
            return;
        }
        if (error.isError()) {
            failCommit(run, error);
            return;
        }
        afterCommitted(run, commit, tree);
    });
}

void RepoSync::afterCommitted(std::shared_ptr<Run> run, const QString &commit, const QString &tree)
{
    State &state = run->state;
    state.commit = commit;
    state.tree = tree;
    // The folder's tree changed with the commit; the branch's next answer has a new ETag.
    state.folderTree.clear();
    state.etag.clear();
    state.folderMissing = false;
    state.emptyRepository = false;
    for (auto it = run->newShas.cbegin(); it != run->newShas.cend(); ++it) {
        if (it.value().isEmpty())
            state.files.remove(it.key());
        else
            state.files.insert(it.key(), it.value());
    }
    saveState(state);
    run->commit.commit = commit;
    if (run->commit.committed.isEmpty())
        run->commit.committed = run->firstCommitted;
    qCInfo(lcSync).noquote() << u"GitHub commit %1 on %2@%3: %4 file(s)"_s
                                        .arg(shortSha(commit), m_link.repo.fullName(), m_link.branch)
                                        .arg(run->commit.committed.size());
    reportCommit(run);
}

// ---- Proposals --------------------------------------------------------------------------------------

void RepoSync::checkProposal(std::shared_ptr<Run> run)
{
    const std::optional<Proposal> open = proposal();
    if (!open || !open->isOpen()) {
        run->proposal.reset();
        if (selectTargets(run))
            newProposalHead(run);
        return;
    }
    // Still open on GitHub? A merged or closed one is not updated: a new one starts.
    Q_EMIT progress(u"proposal"_s, 0, 0);
    const Proposal current = *open;
    m_client->pullRequest(m_link.repo, current.number,
                          [this, run, current](const GitHubError &error, const PullRequest &pr) {
                              if (!alive(run->generation))
                                  return;
                              if (error.isError() && error.code != GitHubError::Code::NotFound) {
                                  failCommit(run, error);
                                  return;
                              }
                              if (error.isError() || pr.merged || pr.state != u"open"_s) {
                                  Proposal ended = current;
                                  ended.state = error.isError() ? u"closed"_s : pr.merged ? u"merged"_s : pr.state;
                                  m_store->set(proposalKey(), ended.toJson());
                                  Q_EMIT stateChanged();
                                  run->proposal.reset();
                                  if (selectTargets(run))
                                      newProposalHead(run);
                                  return;
                              }
                              run->proposal = current;
                              run->target = current.head;
                              if (selectTargets(run))
                                  prepareEntries(run, [this, run] { writeProposal(run); });
                          });
}

void RepoSync::newProposalHead(std::shared_ptr<Run> run)
{
    Q_EMIT progress(u"fork"_s, 0, 0);
    m_client->repository(m_link.repo, [this, run](const GitHubError &error, const Repository &repository) {
        if (!alive(run->generation))
            return;
        if (error.isError()) {
            failCommit(run, error);
            return;
        }
        storeRepository(repository);
        if (repository.canPush && !repository.archived) {
            // A branch of the repository itself.
            run->target = m_link.repo;
            prepareEntries(run, [this, run] { writeProposal(run); });
            return;
        }
        // No push: a branch of the user's fork (GitHub answers with the existing one, if any).
        m_client->createFork(m_link.repo, [this, run](const GitHubError &forkError, const Repository &fork) {
            if (!alive(run->generation))
                return;
            if (forkError.isError()) {
                failCommit(run, forkError);
                return;
            }
            run->target = fork.id;
            run->commit.forked = true;
            qCInfo(lcSync).noquote() << u"GitHub: proposing from the fork %1"_s.arg(fork.id.fullName());
            waitForFork(run, 0);
        });
    });
}

void RepoSync::waitForFork(std::shared_ptr<Run> run, int attempt)
{
    // A new fork takes a moment: ready once it has its branches.
    m_client->branchNames(run->target, {}, [this, run, attempt](const GitHubError &error, const QStringList &names) {
        if (!alive(run->generation))
            return;
        if (!error.isError() && !names.isEmpty()) {
            prepareEntries(run, [this, run] { writeProposal(run); });
            return;
        }
        if (error.isFatal()) {
            failCommit(run, error);
            return;
        }
        if (attempt + 1 >= m_forkAttempts) {
            failCommit(run, GitHubError::make(GitHubError::Code::Timeout,
                                              u"The fork %1 is not ready yet"_s.arg(run->target.fullName())));
            return;
        }
        QTimer::singleShot(m_forkWaitMs, this, [this, run, attempt] {
            if (alive(run->generation))
                waitForFork(run, attempt + 1);
        });
    });
}

void RepoSync::writeProposal(std::shared_ptr<Run> run)
{
    Q_EMIT progress(u"commit"_s, 0, 0);
    QStringList parents;
    if (run->proposal) {
        // One more commit on the proposal's branch; a merge commit when the notebook's branch moved
        // meanwhile (the tree is that branch's newest plus this device's files).
        parents.append(run->proposal->commit);
        if (run->proposal->base != run->state.commit)
            parents.append(run->state.commit);
    } else {
        parents.append(run->state.commit);
    }
    m_client->createTree(run->target, run->state.tree, run->entries,
                         [this, run, parents](const GitHubError &error, const QString &tree) {
                             if (!alive(run->generation))
                                 return;
                             if (error.isError()) {
                                 failCommit(run, error);
                                 return;
                             }
                             m_client->createCommit(
                                     run->target, run->message, tree, parents,
                                     [this, run](const GitHubError &commitError, const QString &commit) {
                                         if (!alive(run->generation))
                                             return;
                                         if (commitError.isError()) {
                                             failCommit(run, commitError);
                                             return;
                                         }
                                         if (run->proposal) {
                                             m_client->updateBranch(run->target, run->proposal->branch, commit,
                                                                    [this, run, commit](const GitHubError &moveError) {
                                                                        if (!alive(run->generation))
                                                                            return;
                                                                        if (moveError.code
                                                                            == GitHubError::Code::NotFastForward) {
                                                                            // Someone else added commits to the
                                                                            // proposal on GitHub: nothing overwritten.
                                                                            run->commit.refused = u"proposal-moved"_s;
                                                                            run->commit.proposal = run->proposal;
                                                                            reportCommit(run);
                                                                            return;
                                                                        }
                                                                        if (moveError.isError()) {
                                                                            failCommit(run, moveError);
                                                                            return;
                                                                        }
                                                                        finishProposal(run, commit);
                                                                    });
                                             return;
                                         }
                                         run->branchName = u"brulion/"_s + now().toString(u"yyyyMMdd-HHmmss"_s);
                                         createProposalBranch(run, commit, 0);
                                     });
                         });
}

void RepoSync::createProposalBranch(std::shared_ptr<Run> run, const QString &commit, int attempt)
{
    const QString name =
            attempt == 0 ? run->branchName : QString(run->branchName + u'-' + QString::number(attempt + 1));
    m_client->createBranch(run->target, name, commit, [this, run, commit, attempt, name](const GitHubError &error) {
        if (!alive(run->generation))
            return;
        if (error.code == GitHubError::Code::Validation && attempt < 5) {
            createProposalBranch(run, commit, attempt + 1); // the name is taken
            return;
        }
        if (error.isError()) {
            failCommit(run, error);
            return;
        }
        run->branchName = name;
        openPullRequest(run, commit, 0);
    });
}

void RepoSync::openPullRequest(std::shared_ptr<Run> run, const QString &commit, int attempt)
{
    Q_EMIT progress(u"proposal"_s, 0, 0);
    const QString head =
            run->target == m_link.repo ? run->branchName : QString(run->target.owner + u':' + run->branchName);
    m_client->createPullRequest(m_link.repo, run->title, run->body, head, m_link.branch,
                                [this, run, commit, attempt](const GitHubError &error, const PullRequest &pr) {
                                    if (!alive(run->generation))
                                        return;
                                    if (error.code == GitHubError::Code::Validation && run->commit.forked
                                        && attempt < 3) {
                                        // A branch of a fork made a moment ago is not always seen at once.
                                        QTimer::singleShot(m_forkWaitMs, this, [this, run, commit, attempt] {
                                            if (alive(run->generation))
                                                openPullRequest(run, commit, attempt + 1);
                                        });
                                        return;
                                    }
                                    if (error.isError()) {
                                        failCommit(run, error);
                                        return;
                                    }
                                    Proposal proposal;
                                    proposal.number = pr.number;
                                    proposal.url = pr.htmlUrl;
                                    proposal.title = pr.title.isEmpty() ? run->title : pr.title;
                                    proposal.state = u"open"_s;
                                    proposal.head = run->target;
                                    proposal.branch = run->branchName;
                                    run->proposal = proposal;
                                    finishProposal(run, commit);
                                });
}

void RepoSync::finishProposal(std::shared_ptr<Run> run, const QString &commit)
{
    Proposal proposal = *run->proposal;
    proposal.commit = commit;
    proposal.base = run->state.commit;
    proposal.files.clear();
    for (auto it = run->newShas.cbegin(); it != run->newShas.cend(); ++it)
        proposal.files.insert(it.key(), it.value());
    m_store->set(proposalKey(), proposal.toJson());
    Q_EMIT stateChanged();
    run->commit.proposal = proposal;
    run->commit.commit = commit;
    qCInfo(lcSync).noquote() << u"GitHub proposal #%1 on %2 (%3): %4 file(s)"_s.arg(proposal.number)
                                        .arg(m_link.repo.fullName(),
                                             QString(proposal.head.fullName() + u':' + proposal.branch))
                                        .arg(run->commit.committed.size());
    reportCommit(run);
}

void RepoSync::failCommit(std::shared_ptr<Run> run, const GitHubError &error)
{
    run->commit.error = error;
    qCWarning(lcSync).noquote() << u"GitHub %1 on %2@%3 failed (%4: %5)"_s.arg(
            run->operation == Operation::Propose ? u"proposal"_s : u"commit"_s, m_link.repo.fullName(), m_link.branch,
            errorCodeName(error.code), error.message);
    reportCommit(run);
}

void RepoSync::reportCommit(std::shared_ptr<Run> run)
{
    run->commit.pull = run->pull;
    const CommitReport report = run->commit;
    const auto done = run->commitDone;
    finishOperation();
    Q_EMIT progress(u"done"_s, 0, 0);
    if (done)
        done(report);
}

} // namespace brulion::github

#include "github/reposync.h"

#include "github/address.h"
#include "github/gitobjects.h"
#include "github/reposync_p.h"

#include "drawing/drawingfile.h"
#include "drawing/merge.h"
#include "log/categories.h"
#include "sync/conflicts.h"
#include "sync/merge.h"
#include "sync/syncengine.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"
#include "vault/vaultconfig.h"

#include <QDir>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::github {

namespace {

const QString kLinkKey = u"github/link"_s;
const QString kStateKey = u"github/state"_s;
const QString kFilesKey = u"github/files"_s;
const QString kExecutableKey = u"github/executable"_s;
const QString kNamesKey = u"github/names"_s;
const QString kLfsKey = u"github/lfs"_s;
const QString kProposalKey = u"github/proposal"_s;
const QString kRepositoryKey = u"github/repository"_s;

using logging::lcSync;

QJsonObject toJson(const QHash<VaultPath, QString> &map)
{
    QJsonObject out;
    for (auto [path, value] : map.asKeyValueRange())
        out.insert(path, value);
    return out;
}

QHash<VaultPath, QString> mapFromJson(const QJsonValue &value)
{
    QHash<VaultPath, QString> out;
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.value().isString())
            out.insert(it.key(), it.value().toString());
    }
    return out;
}

/// The paths of both maps, sorted.
QStringList sortedPaths(const QHash<VaultPath, QString> &a, const QHash<VaultPath, QString> &b)
{
    QSet<VaultPath> all(a.keyBegin(), a.keyEnd());
    for (auto it = b.keyBegin(); it != b.keyEnd(); ++it)
        all.insert(*it);
    QStringList sorted(all.cbegin(), all.cend());
    std::ranges::sort(sorted);
    return sorted;
}

/// A drawing changed on both sides is merged element by element, never line by line (as
/// Nextcloud's sync does: sync/syncrun.cpp).
bool isDrawingText(const VaultPath &path, const QString &text)
{
    const QByteArray head = QStringView(text).first(std::min<qsizetype>(text.size(), drawing::HeadBytes)).toUtf8();
    return drawing::looksLikeDrawingFile(path, head);
}

bool isJsonObject(const QString &text)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    return error.error == QJsonParseError::NoError && doc.isObject();
}

/// The three versions merged — the config key by key, drawings element by element, other text
/// line by line —; std::nullopt when they cannot be merged without the user.
std::optional<QString> mergeTexts(const VaultPath &path, const QString &base, const QString &local,
                                  const QString &remote, qint64 nowMs)
{
    if (path == vaultConfigPath()) {
        const sync::MergeResult lines = sync::merge3(base, local, remote);
        if (lines.clean && isJsonObject(lines.text))
            return lines.text;
        return sync::mergeJson3(base, local, remote);
    }
    if (isDrawingText(path, local) || isDrawingText(path, remote)) {
        if (remote == base)
            return local;
        if (local == base)
            return remote;
        const std::optional<QByteArray> merged =
                drawing::mergeDrawingFiles(base.toUtf8(), local.toUtf8(), remote.toUtf8(), nowMs);
        if (!merged)
            return std::nullopt;
        return sync::decodeSyncText(*merged);
    }
    const sync::MergeResult result = sync::merge3(base, local, remote);
    return result.clean ? std::optional<QString>(result.text) : std::nullopt;
}

GitHubError busyError()
{
    return GitHubError::make(GitHubError::Code::Validation, u"Another GitHub operation is running"_s);
}

} // namespace

// ---- RepoLink, Proposal, helpers ----------------------------------------------------------------

QString RepoLink::repoPath(const VaultPath &path) const
{
    return folder.isEmpty() ? path : folder + u'/' + path;
}

QJsonObject RepoLink::toJson() const
{
    return {{u"owner"_s, repo.owner}, {u"name"_s, repo.name}, {u"branch"_s, branch}, {u"folder"_s, folder}};
}

RepoLink RepoLink::fromJson(const QJsonObject &json)
{
    RepoLink link;
    link.repo.owner = json.value(u"owner"_s).toString();
    link.repo.name = json.value(u"name"_s).toString();
    link.branch = json.value(u"branch"_s).toString();
    link.folder = json.value(u"folder"_s).toString();
    return link;
}

bool isNotebookPath(const VaultPath &path)
{
    if (path == vaultConfigPath())
        return true;
    // The vault takes a backslash for a separator (paths::normalize): "a\..\.x" would write ".x".
    if (path.isEmpty() || path.contains(u'\\') || !sync::isSyncablePath(path))
        return false;
    return std::ranges::none_of(QStringView(path).tokenize(u'/'),
                                [](QStringView segment) { return segment.isEmpty() || segment.startsWith(u'.'); });
}

QString changeKindName(LocalChange::Kind kind)
{
    switch (kind) {
    case LocalChange::Kind::Added: return u"added"_s;
    case LocalChange::Kind::Modified: return u"modified"_s;
    case LocalChange::Kind::Deleted: return u"deleted"_s;
    }
    return {};
}

QJsonObject Proposal::toJson() const
{
    QJsonObject fileVersions;
    for (auto [path, version] : files.asKeyValueRange())
        fileVersions.insert(path, version);
    return {{u"number"_s, number},     {u"url"_s, url.toString()},   {u"title"_s, title},
            {u"state"_s, state},       {u"headOwner"_s, head.owner}, {u"headName"_s, head.name},
            {u"branch"_s, branch},     {u"commit"_s, commit},        {u"base"_s, base},
            {u"files"_s, fileVersions}};
}

std::optional<Proposal> Proposal::fromJson(const QJsonObject &json)
{
    Proposal p;
    p.number = json.value(u"number"_s).toInt();
    if (p.number <= 0)
        return std::nullopt;
    p.url = QUrl(json.value(u"url"_s).toString());
    p.title = json.value(u"title"_s).toString();
    p.state = json.value(u"state"_s).toString();
    p.head.owner = json.value(u"headOwner"_s).toString();
    p.head.name = json.value(u"headName"_s).toString();
    p.branch = json.value(u"branch"_s).toString();
    p.commit = json.value(u"commit"_s).toString();
    p.base = json.value(u"base"_s).toString();
    const QJsonObject fileVersions = json.value(u"files"_s).toObject();
    for (auto it = fileVersions.constBegin(); it != fileVersions.constEnd(); ++it)
        p.files.insert(it.key(), it.value().toString());
    return p;
}

// ---- RepoSync: bookkeeping --------------------------------------------------------------------------

QString RepoSync::linkKey()
{
    return kLinkKey;
}

QString RepoSync::stateKey()
{
    return kStateKey;
}

QString RepoSync::filesKey()
{
    return kFilesKey;
}

QString RepoSync::proposalKey()
{
    return kProposalKey;
}

QString RepoSync::repositoryKey()
{
    return kRepositoryKey;
}

RepoSync::RepoSync(Vault *vault, GitHubClient *client, LocalStore *store, QObject *parent)
    : QObject(parent), m_vault(vault), m_client(client), m_store(store)
{
    if (vault)
        connect(vault, &Vault::changed, this, &RepoSync::onVaultChanged);
}

// Callbacks still waiting for GitHub end without a report: their run's owner is gone (alive()).
RepoSync::~RepoSync() = default;

void RepoSync::setLink(const RepoLink &link)
{
    m_link = link;
    if (!m_store || !link.isValid())
        return;
    const RepoLink stored = RepoLink::fromJson(m_store->get(kLinkKey).toObject());
    if (stored == link)
        return;
    // Another repository, branch or folder: the bookkeeping of the old one means nothing there.
    if (stored.isValid())
        qCInfo(lcSync).noquote() << u"GitHub: the notebook's link changed (%1 → %2): bookkeeping reset"_s.arg(
                QString(stored.repo.fullName() + u'@' + stored.branch + u':' + stored.folder),
                QString(link.repo.fullName() + u'@' + link.branch + u':' + link.folder));
    resetState();
    m_store->set(kLinkKey, link.toJson());
}

void RepoSync::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime RepoSync::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTime();
}

void RepoSync::setForkWait(int ms, int attempts)
{
    m_forkWaitMs = std::max(0, ms);
    m_forkAttempts = std::max(1, attempts);
}

RepoSync::State RepoSync::loadState() const
{
    State state;
    if (!m_store)
        return state;
    state.link = RepoLink::fromJson(m_store->get(kLinkKey).toObject());
    const QJsonObject meta = m_store->get(kStateKey).toObject();
    state.commit = meta.value(u"commit"_s).toString();
    state.tree = meta.value(u"tree"_s).toString();
    state.folderTree = meta.value(u"folderTree"_s).toString();
    state.etag = meta.value(u"etag"_s).toString();
    state.complete = meta.value(u"complete"_s).toBool();
    state.folderMissing = meta.value(u"folderMissing"_s).toBool();
    state.emptyRepository = meta.value(u"emptyRepository"_s).toBool();
    state.pulledAt = meta.value(u"pulledAt"_s).toInteger();
    state.files = mapFromJson(m_store->get(kFilesKey));
    const QJsonArray executable = m_store->get(kExecutableKey).toArray();
    for (const QJsonValue path : executable)
        state.executable.insert(path.toString());
    state.names = mapFromJson(m_store->get(kNamesKey));
    state.lfs = mapFromJson(m_store->get(kLfsKey));
    return state;
}

void RepoSync::saveState(const State &state)
{
    if (!m_store)
        return;
    m_store->set(kStateKey, QJsonObject{{u"commit"_s, state.commit},
                                        {u"tree"_s, state.tree},
                                        {u"folderTree"_s, state.folderTree},
                                        {u"etag"_s, state.etag},
                                        {u"complete"_s, state.complete},
                                        {u"folderMissing"_s, state.folderMissing},
                                        {u"emptyRepository"_s, state.emptyRepository},
                                        {u"pulledAt"_s, state.pulledAt}});
    m_store->set(kFilesKey, toJson(state.files));
    QStringList executable(state.executable.cbegin(), state.executable.cend());
    executable.sort();
    const QJsonValue none(QJsonValue::Undefined); // removes the key
    m_store->set(kExecutableKey, executable.isEmpty() ? none : QJsonValue(QJsonArray::fromStringList(executable)));
    m_store->set(kNamesKey, state.names.isEmpty() ? none : QJsonValue(toJson(state.names)));
    m_store->set(kLfsKey, state.lfs.isEmpty() ? none : QJsonValue(toJson(state.lfs)));
    Q_EMIT stateChanged();
}

void RepoSync::resetState()
{
    if (!m_store)
        return;
    m_store->removePrefix(u"github/"_s);
    if (m_link.isValid())
        m_store->set(kLinkKey, m_link.toJson());
    Q_EMIT stateChanged();
}

bool RepoSync::hasPulled() const
{
    return !headCommit().isEmpty();
}

QString RepoSync::headCommit() const
{
    return m_store ? m_store->get(kStateKey).toObject().value(u"commit"_s).toString() : QString();
}

qint64 RepoSync::lastPullMs() const
{
    return m_store ? m_store->get(kStateKey).toObject().value(u"pulledAt"_s).toInteger() : 0;
}

std::optional<Proposal> RepoSync::proposal() const
{
    if (!m_store)
        return std::nullopt;
    return Proposal::fromJson(m_store->get(kProposalKey).toObject());
}

void RepoSync::forgetProposal()
{
    if (m_store && m_store->remove(kProposalKey))
        Q_EMIT stateChanged();
}

std::optional<Repository> RepoSync::repository() const
{
    if (!m_store)
        return std::nullopt;
    const QJsonObject json = m_store->get(kRepositoryKey).toObject();
    if (json.isEmpty())
        return std::nullopt;
    Repository repo;
    repo.id = m_link.repo;
    repo.defaultBranch = json.value(u"defaultBranch"_s).toString();
    repo.isPrivate = json.value(u"private"_s).toBool();
    repo.isFork = json.value(u"fork"_s).toBool();
    repo.archived = json.value(u"archived"_s).toBool();
    repo.permissionsKnown = json.value(u"permissionsKnown"_s).toBool();
    repo.canPush = json.value(u"canPush"_s).toBool();
    repo.htmlUrl = QUrl(json.value(u"url"_s).toString());
    return repo;
}

// ---- Local files --------------------------------------------------------------------------------

std::optional<QString> RepoSync::localSha(const VaultPath &path)
{
    if (!m_vault)
        return std::nullopt;
    const std::optional<FileStat> stat = m_vault->stat(path);
    if (!stat || stat->isDir)
        return QString(); // missing
    if (stat->size > m_maxFileSize)
        return std::nullopt;
    if (const auto it = m_shas.constFind(path);
        it != m_shas.cend() && it->size == stat->size && it->mtime == stat->mtime)
        return it->sha;
    bool ok = false;
    const QByteArray bytes = m_vault->readBinary(path, &ok);
    if (!ok)
        return std::nullopt;
    const QString sha = blobSha(bytes);
    m_shas.insert(path, {.size = stat->size, .mtime = stat->mtime, .sha = sha});
    return sha;
}

QHash<VaultPath, QString> RepoSync::localShas(QSet<VaultPath> *unknown)
{
    QHash<VaultPath, QString> out;
    if (!m_vault)
        return out;
    QList<FileStat> files = m_vault->files();
    if (const std::optional<FileStat> config = m_vault->stat(vaultConfigPath()); config && !config->isDir)
        files.append(*config);
    for (const FileStat &file : std::as_const(files)) {
        if (file.isDir || !isNotebookPath(file.path))
            continue;
        const std::optional<QString> sha = localSha(file.path);
        if (sha && !sha->isEmpty())
            out.insert(file.path, *sha);
        else if (unknown)
            unknown->insert(file.path); // unreadable or too large: never taken for removed
    }
    return out;
}

void RepoSync::onVaultChanged(const QList<brulion::VaultChange> &changes)
{
    for (const VaultChange &change : changes) {
        m_shas.remove(change.path);
        if (!change.oldPath.isEmpty())
            m_shas.remove(change.oldPath);
    }
}

QList<LocalChange> RepoSync::localChanges()
{
    QList<LocalChange> out;
    if (!m_vault || !m_store)
        return out;
    const State state = loadState();
    const QHash<VaultPath, QString> local = localShas();
    const std::optional<Proposal> open = proposal();
    // The files the open proposal changes, with their versions there ("" = removed).
    const QMap<VaultPath, QString> proposed = open && open->isOpen() ? open->files : QMap<VaultPath, QString>();
    const QStringList sorted = sortedPaths(state.files, local);
    for (const VaultPath &path : sorted) {
        const QString base = state.files.value(path);
        const auto here = local.constFind(path);
        if (here == local.cend()) {
            // Gone here — unless it could not be read (then it is not known to be gone).
            if (base.isEmpty() || m_vault->exists(path))
                continue;
            out.append({path, LocalChange::Kind::Deleted, proposed.contains(path) && proposed.value(path).isEmpty()});
            continue;
        }
        if (*here == base)
            continue;
        if (base.isEmpty() && paths::isConflictCopy(path))
            continue; // a conflict copy stays on this device
        out.append({path, base.isEmpty() ? LocalChange::Kind::Added : LocalChange::Kind::Modified,
                    proposed.value(path) == *here});
    }
    return out;
}

// ---- Operations -----------------------------------------------------------------------------------

std::shared_ptr<RepoSync::Run> RepoSync::beginRun(Operation operation)
{
    if (busy())
        return nullptr;
    m_operation = operation;
    auto run = std::make_shared<Run>();
    run->owner = this;
    run->operation = operation;
    run->generation = ++m_generation;
    run->timer.start();
    m_run = run;
    return run;
}

void RepoSync::finishOperation()
{
    m_operation = Operation::None;
    m_run.reset();
}

bool RepoSync::alive(const Run &run)
{
    const RepoSync *self = run.owner.data();
    return self && run.generation == self->m_generation && self->m_run && self->m_vault && self->m_client
           && self->m_store;
}

void RepoSync::cancel()
{
    if (m_client && busy())
        m_client->cancel();
}

void RepoSync::pull(std::function<void(const PullReport &)> done)
{
    const auto run = beginRun(Operation::Pull);
    if (!run) {
        PullReport report;
        report.refused = u"busy"_s;
        report.error = busyError();
        QTimer::singleShot(0, this, [done = std::move(done), report] {
            if (done)
                done(report);
        });
        return;
    }
    run->pullDone = std::move(done);
    QTimer::singleShot(0, this, [this, run] {
        if (run->generation == m_generation)
            startPull(run);
    });
}

void RepoSync::startPull(std::shared_ptr<Run> run)
{
    run->pull = {};
    run->remote.clear();
    run->remoteNames.clear();
    run->remoteExecutable.clear();
    run->local.clear();
    run->unknown.clear();
    run->jobs.clear();
    run->nextJob = 0;
    run->activeJobs = 0;
    run->doneJobs = 0;
    run->emptiedFolders.clear();
    run->head = {};
    run->folderSha.clear();
    if (!m_vault || !m_client || !m_store || !m_link.isValid()) {
        run->pull.refused = u"not-linked"_s;
        endPull(run);
        return;
    }
    if (!m_vault->isStorageAvailable()) {
        run->pull.refused = u"vault-unavailable"_s;
        endPull(run);
        return;
    }
    run->state = loadState();
    run->before = run->state;
    Q_EMIT progress(u"branch"_s, 0, 0);
    pullBranch(run);
}

void RepoSync::pullBranch(std::shared_ptr<Run> run)
{
    const State &state = run->state;
    const QString etag = state.complete && !state.commit.isEmpty() ? state.etag : QString();
    m_client->branch(
            m_link.repo, m_link.branch, etag,
            [this, run](const GitHubError &error, const BranchHead &head, bool notModified) {
                if (!alive(*run))
                    return;
                const auto emptyRepository = [this, run] {
                    run->pull.emptyRepository = true;
                    run->pull.folderMissing = true;
                    planPull(run, {});
                };
                if (error.code == GitHubError::Code::EmptyRepository) {
                    emptyRepository();
                    return;
                }
                if (error.code == GitHubError::Code::NotFound) {
                    // No such branch — or no commit at all yet (an empty repository has no branch).
                    m_client->branchNames(m_link.repo, {},
                                          [this, run, error, emptyRepository](const GitHubError &listError,
                                                                              const QStringList &names) {
                                              if (!alive(*run))
                                                  return;
                                              if (!listError.isError() && names.isEmpty()) {
                                                  emptyRepository();
                                                  return;
                                              }
                                              run->pull.error = error;
                                              endPull(run);
                                          });
                    return;
                }
                if (error.isError()) {
                    run->pull.error = error;
                    endPull(run);
                    return;
                }
                if (notModified
                    || (run->state.complete && head.commit == run->state.commit && !run->state.commit.isEmpty())) {
                    run->pull.upToDate = true;
                    run->pull.commit = run->state.commit;
                    if (!notModified && !head.etag.isEmpty())
                        run->state.etag = head.etag;
                    run->head = {};
                    endPull(run);
                    return;
                }
                run->head = head;
                run->pull.commit = head.commit;
                Q_EMIT progress(u"tree"_s, 0, 0);
                if (m_link.folder.isEmpty())
                    pullTree(run, head.tree);
                else
                    pullFolder(run);
            });
}

void RepoSync::pullFolder(std::shared_ptr<Run> run)
{
    // The folder's tree: its entry in the listing of the folder holding it.
    const QString name = paths::basename(m_link.folder);
    m_client->folder(m_link.repo, paths::dirname(m_link.folder), run->head.commit,
                     [this, run, name](const GitHubError &error, const QList<TreeEntry> &entries) {
                         if (!alive(*run))
                             return;
                         if (error.isError() && error.code != GitHubError::Code::NotFound) {
                             run->pull.error = error;
                             endPull(run);
                             return;
                         }
                         const auto folder = std::ranges::find_if(entries, [&name](const TreeEntry &entry) {
                             return entry.path == name && entry.type == u"tree"_s;
                         });
                         if (folder == entries.cend() || folder->sha.isEmpty()) { // NotFound: no entries
                             run->pull.folderMissing = true;
                             planPull(run, {});
                             return;
                         }
                         pullTree(run, folder->sha);
                     });
}

void RepoSync::pullTree(std::shared_ptr<Run> run, const QString &folderSha)
{
    run->folderSha = folderSha;
    const State &state = run->state;
    if (state.complete && !state.folderTree.isEmpty() && folderSha == state.folderTree) {
        // The folder is as it was at the last complete pull: its files are the bookkeeping's.
        QList<TreeEntry> entries;
        const auto add = [&entries, &state](const VaultPath &path, const QString &sha, QStringView mode) {
            entries.append({state.names.value(path, path), mode.toString(), u"blob"_s, sha});
        };
        for (auto [path, sha] : state.files.asKeyValueRange())
            add(path, sha, state.executable.contains(path) ? ExecutableMode : FileMode);
        for (auto [path, sha] : state.lfs.asKeyValueRange())
            add(path, sha, FileMode);
        planPull(run, entries);
        return;
    }
    m_client->tree(m_link.repo, folderSha, true, [this, run](const GitHubError &error, const Tree &tree) {
        if (!alive(*run))
            return;
        if (error.isError()) {
            run->pull.error = error;
            endPull(run);
            return;
        }
        if (tree.truncated) {
            run->pull.error = GitHubError::make(GitHubError::Code::TooLarge,
                                                u"The folder has more files than GitHub lists at once"_s);
            endPull(run);
            return;
        }
        planPull(run, tree.entries);
    });
}

void RepoSync::planPull(std::shared_ptr<Run> run, const QList<TreeEntry> &entries)
{
    State &state = run->state;
    PullReport &report = run->pull;
    QHash<QString, VaultPath> lowerCase;
    QHash<VaultPath, QString> lfs;
    for (const TreeEntry &entry : entries) {
        if (entry.type == u"tree"_s)
            continue;
        const VaultPath path = entry.path.normalized(QString::NormalizationForm_C);
        if (!isNotebookPath(path))
            continue; // the repository's own (hidden files, .github, …): never touched
        if (entry.type == u"commit"_s || entry.mode == SubmoduleMode) {
            report.skipped.append({path, u"submodule"_s, u"A submodule (another repository)"_s});
            continue;
        }
        if (entry.mode == SymlinkMode) {
            report.skipped.append({path, u"symlink"_s, u"A symbolic link"_s});
            continue;
        }
        if (entry.size > m_maxFileSize) {
            report.skipped.append({path, u"too-large"_s, u"%1 bytes"_s.arg(entry.size)});
            continue;
        }
        if (run->remote.contains(path)) {
            report.skipped.append({path, u"name"_s, u"Two names that are one in composed Unicode"_s});
            continue;
        }
        const QString lower = path.toLower();
        if (lowerCase.contains(lower)) {
            report.skipped.append({path, u"case-clash"_s, u"Also named %1"_s.arg(lowerCase.value(lower))});
            continue;
        }
        lowerCase.insert(lower, path);
        if (state.lfs.value(path) == entry.sha) {
            lfs.insert(path, entry.sha);
            report.skipped.append({path, u"lfs"_s, u"Kept in Git LFS"_s});
            continue;
        }
        run->remote.insert(path, entry.sha);
        if (path != entry.path)
            run->remoteNames.insert(path, entry.path);
        if (entry.mode == ExecutableMode)
            run->remoteExecutable.insert(path);
    }
    state.lfs = lfs;

    if (run->remote.isEmpty() && lfs.isEmpty() && !state.files.isEmpty()) {
        // Every file pulled before is gone from GitHub (a deleted branch, a moved folder): nothing
        // is removed here — the user decides (link the notebook again, or commit to fill it).
        qCWarning(lcSync).noquote() << u"GitHub pull %1@%2: no files in the folder any more, %3 were there: nothing "
                                       "removed here"_s.arg(m_link.repo.fullName(), m_link.branch,
                                                            QString::number(state.files.size()));
        report.refused = u"remote-empty"_s;
        endPull(run);
        return;
    }
    run->local = localShas(&run->unknown);
    const QStringList sorted = sortedPaths(run->remote, state.files);
    for (const VaultPath &path : sorted) {
        if (run->unknown.contains(path))
            continue; // cannot be read here: left as it is
        const QString remote = run->remote.value(path);
        const QString base = state.files.value(path);
        const QString local = run->local.value(path);
        if (remote == local) {
            if (remote.isEmpty())
                state.files.remove(path);
            else
                state.files.insert(path, remote);
            continue;
        }
        if (remote.isEmpty()) {
            if (base.isEmpty())
                continue; // only here: a change to commit
            if (local == base) {
                run->jobs.append({Job::Kind::Delete, path, {}, base, local, {}});
                continue;
            }
            // Changed here, removed on GitHub: kept (a commit adds it again).
            state.files.remove(path);
            ++report.kept;
            continue;
        }
        if (local == base) {
            run->jobs.append({Job::Kind::Download, path, remote, base, local, {}});
            continue;
        }
        if (remote == base)
            continue; // changed or removed here only
        if (local.isEmpty()) {
            run->jobs.append({Job::Kind::Download, path, remote, base, local, {}}); // removed here, changed there
            continue;
        }
        run->jobs.append({Job::Kind::Merge, path, remote, base, local, {}});
    }
    // Renames on GitHub: a file removed there (unchanged here) whose content arrives under a new
    // name that is free here is moved, one to one by its blob id (several files with that
    // content: left as they are — downloaded and removed).
    QHash<QString, QList<qsizetype>> removedBySha;
    QHash<QString, QList<qsizetype>> addedBySha;
    for (qsizetype i = 0; i < run->jobs.size(); ++i) {
        const Job &job = run->jobs.at(i);
        if (job.kind == Job::Kind::Delete)
            removedBySha[job.base].append(i);
        else if (job.kind == Job::Kind::Download && job.base.isEmpty() && job.local.isEmpty())
            addedBySha[job.remote].append(i);
    }
    QList<qsizetype> dropped;
    for (auto it = addedBySha.cbegin(); it != addedBySha.cend(); ++it) {
        const QList<qsizetype> removed = removedBySha.value(it.key());
        if (it.value().size() != 1 || removed.size() != 1)
            continue;
        Job &added = run->jobs[it.value().first()];
        const Job &gone = run->jobs.at(removed.first());
        added.kind = Job::Kind::Rename;
        added.from = gone.path;
        added.local = gone.local;
        dropped.append(removed.first());
    }
    std::ranges::sort(dropped, std::greater<qsizetype>());
    for (const qsizetype i : std::as_const(dropped))
        run->jobs.removeAt(i);
    // The names and executable bits of the files as GitHub has them now.
    state.names = run->remoteNames;
    state.executable = run->remoteExecutable;
    runJobs(run);
}

void RepoSync::runJobs(std::shared_ptr<Run> run)
{
    const int total = int(run->jobs.size());
    if (run->nextJob >= run->jobs.size() && run->activeJobs == 0) {
        endPull(run);
        return;
    }
    while (run->activeJobs < m_concurrency && run->nextJob < run->jobs.size()) {
        const int index = int(run->nextJob++);
        ++run->activeJobs;
        Q_EMIT progress(u"download"_s, run->doneJobs, total);
        runJob(run, index);
    }
}

void RepoSync::fetchContent(std::shared_ptr<Run> run, const VaultPath &path, const QString &sha, const QString &commit,
                            std::function<void(const GitHubError &, const QByteArray &)> done)
{
    const auto verified = [sha, done](const GitHubError &error, const QByteArray &bytes) {
        if (!error.isError() && blobSha(bytes) != sha) {
            done(GitHubError::make(GitHubError::Code::Integrity, u"The downloaded bytes are not blob %1"_s.arg(sha)),
                 {});
            return;
        }
        done(error, bytes);
    };
    if (m_client->hasToken() || commit.isEmpty()) {
        m_client->blob(m_link.repo, sha, verified);
        return;
    }
    // Without a token the API allows 60 requests an hour: files of a public repository come from
    // raw.githubusercontent.com, by the commit (a branch name is cached there for minutes). A file
    // that is not that blob there (a merge base kept from an older commit) comes through the API.
    const QString repoPath = m_link.repoPath(run->state.names.value(path, run->remoteNames.value(path, path)));
    m_client->rawFile(
            m_link.repo, commit, repoPath,
            [this, run, sha, verified, done = std::move(done)](const GitHubError &error, const QByteArray &bytes) {
                if (!alive(*run))
                    return;
                if (error.isFatal() && error.code != GitHubError::Code::RateLimited) {
                    done(error, bytes);
                    return;
                }
                if (!error.isError() && blobSha(bytes) == sha) {
                    done({}, bytes); // verified: hashed once
                    return;
                }
                m_client->blob(m_link.repo, sha, verified);
            });
}

void RepoSync::runJob(std::shared_ptr<Run> run, int index)
{
    const auto finished = [this, run] {
        --run->activeJobs;
        ++run->doneJobs;
        runJobs(run);
    };
    const Job &job = run->jobs.at(index);
    if (job.kind == Job::Kind::Rename || job.kind == Job::Kind::Delete) {
        if (job.kind == Job::Kind::Rename) {
            // The old file still the version GitHub moved, the new name still free here?
            const std::optional<QString> current = localSha(job.from);
            const bool free = !m_vault->exists(job.path);
            if (current && *current == job.local && free && m_vault->rename(job.from, job.path, ChangeSource::Sync)) {
                run->state.files.remove(job.from);
                run->state.files.insert(job.path, job.remote);
                ++run->pull.renamed;
                run->pull.written.append(job.from);
                run->pull.written.append(job.path);
                run->emptiedFolders.insert(paths::dirname(job.from));
            } else {
                // Changed or taken meanwhile: compared again by the next pull (nothing lost).
                run->pull.errors.append({job.path, u"changed-meanwhile"_s, u"Not moved from %1"_s.arg(job.from)});
            }
        } else {
            // Still the version GitHub removed? (Edited a moment ago: kept, a commit adds it again.)
            const std::optional<QString> current = localSha(job.path);
            if (!current || *current != job.local) {
                run->state.files.remove(job.path);
                ++run->pull.kept;
            } else if (m_vault->remove(job.path, ChangeSource::Sync)) {
                run->state.files.remove(job.path);
                ++run->pull.deletedLocal;
                run->pull.written.append(job.path);
                run->emptiedFolders.insert(paths::dirname(job.path));
            } else {
                run->pull.errors.append({job.path, u"write-failed"_s, m_vault->errorString()});
            }
        }
        QTimer::singleShot(0, this, [run, finished] {
            if (alive(*run))
                finished();
        });
        return;
    }
    fetchContent(run, job.path, job.remote, run->head.commit,
                 [this, run, index, finished](const GitHubError &error, const QByteArray &remote) {
                     if (!alive(*run))
                         return;
                     const Job &fetched = run->jobs.at(index);
                     if (error.isError()) {
                         if (error.isFatal()) {
                             if (!run->pull.error.isError())
                                 run->pull.error = error;
                             run->nextJob = run->jobs.size(); // no more downloads
                         } else {
                             run->pull.errors.append({fetched.path,
                                                      error.code == GitHubError::Code::Integrity ? u"integrity"_s
                                                                                                 : u"download-failed"_s,
                                                      error.message});
                         }
                         finished();
                         return;
                     }
                     if (isLfsPointer(remote)) {
                         run->state.lfs.insert(fetched.path, fetched.remote);
                         run->state.files.remove(fetched.path);
                         run->pull.skipped.append({fetched.path, u"lfs"_s, u"Kept in Git LFS"_s});
                         finished();
                         return;
                     }
                     if (fetched.kind == Job::Kind::Download || fetched.base.isEmpty()) {
                         applyDownload(*run, fetched, remote, nullptr);
                         finished();
                         return;
                     }
                     // Changed on both sides: the version both had, then the merge.
                     fetchContent(run, fetched.path, fetched.base, run->before.commit,
                                  [this, run, index, remote, finished](const GitHubError &baseError,
                                                                       const QByteArray &base) {
                                      if (!alive(*run))
                                          return;
                                      if (baseError.isFatal()) {
                                          if (!run->pull.error.isError())
                                              run->pull.error = baseError;
                                          run->nextJob = run->jobs.size();
                                      } else {
                                          // No base to be had (removed meanwhile): both versions kept.
                                          applyDownload(*run, run->jobs.at(index), remote,
                                                        baseError.isError() ? nullptr : &base);
                                      }
                                      finished();
                                  });
                 });
}

void RepoSync::applyDownload(Run &run, const Job &job, const QByteArray &remote, const QByteArray *base)
{
    PullReport &report = run.pull;
    // The file must still be what the plan saw: an edit made meanwhile is merged next time.
    const std::optional<QString> current = localSha(job.path);
    if (!current) {
        report.errors.append({job.path, u"read-failed"_s, m_vault->errorString()});
        return;
    }
    if (*current == job.remote) {
        run.state.files.insert(job.path, job.remote);
        return;
    }
    if (*current != job.local) {
        report.errors.append({job.path, u"changed-meanwhile"_s, u"Changed here while it was downloaded"_s});
        return;
    }
    const bool overwrite = job.kind == Job::Kind::Download || job.local.isEmpty();
    if (overwrite) {
        if (!m_vault->writeBinary(job.path, remote, ChangeSource::Sync)) {
            report.errors.append({job.path, u"write-failed"_s, m_vault->errorString()});
            return;
        }
        run.state.files.insert(job.path, job.remote);
        ++report.downloaded;
        report.written.append(job.path);
        return;
    }
    // Changed on both sides.
    bool readOk = false;
    const QByteArray localBytes = m_vault->readBinary(job.path, &readOk);
    if (!readOk) {
        report.errors.append({job.path, u"read-failed"_s, m_vault->errorString()});
        return;
    }
    if (base && sync::isSyncTextPath(job.path)) {
        const std::optional<QString> baseText = sync::decodeSyncText(*base);
        const std::optional<QString> localText = sync::decodeSyncText(localBytes);
        const std::optional<QString> remoteText = sync::decodeSyncText(remote);
        if (baseText && localText && remoteText) {
            if (const std::optional<QString> merged =
                        mergeTexts(job.path, *baseText, *localText, *remoteText, now().toMSecsSinceEpoch())) {
                const WriteOutcome outcome =
                        *merged == *localText
                                ? WriteOutcome::Written
                                : m_vault->writeTextIfUnchanged(job.path, *merged, *localText, ChangeSource::Sync);
                if (outcome == WriteOutcome::Changed) {
                    report.errors.append({job.path, u"changed-meanwhile"_s, u"Changed here while it was merged"_s});
                    return;
                }
                if (outcome == WriteOutcome::Failed) {
                    report.errors.append({job.path, u"write-failed"_s, m_vault->errorString()});
                    return;
                }
                run.state.files.insert(job.path, job.remote);
                ++report.merged;
                report.written.append(job.path);
                return;
            }
        }
    }
    // Not merged: this device's version stays, GitHub's is saved beside it (once).
    if (!hasConflictCopy(job.path, remote)) {
        const VaultPath copy = m_vault->createBinary(sync::conflictCopyPath(job.path, ConflictDevice.toString(), now()),
                                                     remote, ChangeSource::Sync);
        if (copy.isEmpty()) {
            report.errors.append({job.path, u"conflict-copy-failed"_s, m_vault->errorString()});
            return;
        }
        report.conflictCopies.append(copy);
    }
    run.state.files.insert(job.path, job.remote);
    report.conflicts.append(job.path);
}

bool RepoSync::hasConflictCopy(const VaultPath &path, const QByteArray &content)
{
    std::optional<QString> sha; // hashed for the first copy of the same size only
    const QList<FileStat> files = m_vault->files();
    for (const FileStat &file : files) {
        if (file.isDir || file.size != content.size())
            continue;
        const std::optional<sync::BrulionConflict> parsed = sync::parseBrulionConflict(file.path);
        if (!parsed || parsed->original != path)
            continue;
        if (!sha)
            sha = blobSha(content);
        if (localSha(file.path) == sha)
            return true;
    }
    return false;
}

void RepoSync::pruneEmptyFolders(const QSet<QString> &folders)
{
    // Git keeps no empty folders: the ones whose last file GitHub removed go too — only when
    // nothing at all is left in them on disk.
    QSet<QString> candidates;
    for (QString folder : folders) {
        while (!folder.isEmpty()) {
            candidates.insert(folder);
            folder = paths::dirname(folder);
        }
    }
    QStringList sorted(candidates.cbegin(), candidates.cend());
    std::ranges::sort(sorted, [](const QString &a, const QString &b) {
        return a.count(u'/') > b.count(u'/') || (a.count(u'/') == b.count(u'/') && a < b);
    });
    for (const QString &folder : std::as_const(sorted)) {
        const std::optional<FileStat> stat = m_vault->stat(folder);
        if (!stat || !stat->isDir)
            continue;
        const QDir dir(m_vault->absolutePath(folder));
        if (!dir.isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
            continue;
        m_vault->removePermanently(folder, ChangeSource::Sync);
    }
}

void RepoSync::endPull(std::shared_ptr<Run> run)
{
    PullReport &report = run->pull;
    if (report.refused.isEmpty() && m_store && m_vault) {
        State &state = run->state;
        const bool moved = !run->head.commit.isEmpty();
        if (moved || report.emptyRepository) {
            state.commit = run->head.commit;
            state.tree = run->head.tree;
            state.etag = run->head.etag;
            state.folderTree = run->folderSha;
            state.folderMissing = report.folderMissing;
            state.emptyRepository = report.emptyRepository;
            // A pull that stopped halfway or failed for some files records the files it did, but
            // the next one compares everything again (no If-None-Match, the folder listed again).
            state.complete = !report.error.isError() && report.errors.isEmpty();
            if (!state.complete) {
                state.etag.clear();
                state.folderTree.clear();
            }
        }
        if (!report.error.isError())
            state.pulledAt = now().toMSecsSinceEpoch();
        saveState(state);
        pruneEmptyFolders(run->emptiedFolders);
    }
    report.durationMs = run->timer.elapsed();
    if (report.refused.isEmpty()) {
        const QString line = u"GitHub pull %1@%2: %3"_s.arg(
                m_link.repo.fullName(), m_link.branch,
                report.error.isError()
                        ? u"failed (%1: %2)"_s.arg(errorCodeName(report.error.code), report.error.message)
                : report.upToDate ? u"up to date"_s
                                  : u"%1 downloaded, %2 removed, %3 renamed, %4 "
                                    "merged, %5 conflicts, %6 errors"_s.arg(report.downloaded)
                                            .arg(report.deletedLocal)
                                            .arg(report.renamed)
                                            .arg(report.merged)
                                            .arg(report.conflicts.size())
                                            .arg(report.errors.size()));
        if (report.error.isError() || !report.errors.isEmpty())
            qCWarning(lcSync).noquote() << line;
        else
            qCInfo(lcSync).noquote() << line;
    }
    // An open proposal's state changes when the branch moves (merged) — or on GitHub alone (closed).
    const std::optional<Proposal> open = proposal();
    if (run->operation == Operation::Pull && !report.failed() && !report.upToDate && open && open->isOpen()) {
        m_client->pullRequest(m_link.repo, open->number,
                              [this, run, current = *open](const GitHubError &error, const PullRequest &pr) {
                                  if (!alive(*run))
                                      return;
                                  if (!error.isError())
                                      run->pull.proposal = updateProposal(current, pr);
                                  reportPull(run);
                              });
        return;
    }
    if (run->operation == Operation::Pull) {
        reportPull(run);
        return;
    }
    afterPullForCommit(run);
}

Proposal RepoSync::updateProposal(const Proposal &current, const PullRequest &pr)
{
    Proposal updated = current;
    updated.state = pr.merged ? u"merged"_s : pr.state;
    updated.title = pr.title.isEmpty() ? current.title : pr.title;
    if (updated != current) {
        m_store->set(kProposalKey, updated.toJson());
        Q_EMIT stateChanged();
    }
    return updated;
}

void RepoSync::reportPull(std::shared_ptr<Run> run)
{
    finishOperation();
    Q_EMIT progress(u"done"_s, 0, 0);
    if (run->pullDone)
        run->pullDone(run->pull);
}

void RepoSync::refreshProposal(std::function<void(const GitHubError &, const std::optional<Proposal> &)> done)
{
    const std::optional<Proposal> current = proposal();
    if (!current || !m_client || busy()) {
        QTimer::singleShot(0, this, [done = std::move(done), current, wasBusy = busy()] {
            if (done)
                done(wasBusy ? busyError() : GitHubError{}, current);
        });
        return;
    }
    const auto run = beginRun(Operation::Refresh);
    m_client->pullRequest(
            m_link.repo, current->number,
            [this, run, before = *current, done = std::move(done)](const GitHubError &error, const PullRequest &pr) {
                if (!alive(*run))
                    return;
                const Proposal updated = error.isError() ? before : updateProposal(before, pr);
                finishOperation();
                if (done)
                    done(error, updated);
            });
}

} // namespace brulion::github

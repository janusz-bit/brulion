// GitHub Pages: the site as one commit on the target's branch (docs/UI.md "Publishing"). The
// branch's tree is the target's state (blob ids compared with the site's: unchanged files are never
// sent, also from another device), text goes inline in the new tree, other files as blobs; a branch
// that is missing is made without history; the branch moves only as a fast-forward.
#include "publish/backends_p.h"

#include "github/address.h"
#include "github/client.h"

#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::publish::detail {

namespace {

using github::GitHubError;

/// Text GitHub takes inline in a tree ("content": a UTF-8 string, no NUL).
bool isInlineText(const QByteArray &data)
{
    return !data.contains('\0') && QString::fromUtf8(data).toUtf8() == data;
}

/// The inline text of one tree request at most (a large site takes several, each on the last).
constexpr qint64 kInlineBytesPerTree = 4 * 1024 * 1024;

QString errorCode(const GitHubError &error)
{
    using C = GitHubError::Code;
    switch (error.code) {
    case C::Auth: return u"auth"_s;
    case C::Forbidden: return u"forbidden"_s;
    case C::RateLimited: return u"rate-limited"_s;
    case C::NotFound: return u"not-found"_s;
    case C::NotFastForward: return u"not-fast-forward"_s;
    case C::Server: return u"server"_s;
    case C::Network: return u"network"_s;
    case C::Timeout: return u"timeout"_s;
    case C::TooLarge: return u"too-large"_s;
    case C::Cancelled: return u"cancelled"_s;
    case C::Parse: return u"parse"_s;
    case C::EmptyRepository:
    case C::Conflict:
    case C::Validation:
    case C::Integrity:
    case C::None: break;
    }
    return u"refused"_s;
}

class GitHubBackend final : public Backend {
public:
    GitHubBackend(sync::HttpTransport *transport, bool browser, int timeoutMs) : m_client(transport)
    {
        m_client.setBrowser(browser);
        m_client.setTimeoutMs(timeoutMs);
    }

    void run(const std::shared_ptr<Job> &job) override
    {
        m_job = job.get();
        const QString full = job->target.repository.trimmed();
        m_repo = github::RepoId{full.section(u'/', 0, 0), full.section(u'/', 1, 1)};
        m_branch = job->target.branch.trimmed();
        m_folder = github::normalizeFolder(job->target.folder).value_or(QString());
        m_client.setToken(job->credentials.token);
        if (!job->preview() && job->credentials.token.trimmed().isEmpty()) {
            fail(u"no-token"_s, {});
            return;
        }
        job->progress(u"checking"_s, 0, 0);
        QPointer<GitHubBackend> self(this);
        m_client.repository(m_repo, [self](const GitHubError &error, const github::Repository &repository) {
            if (self)
                self->onRepository(error, repository);
        });
    }

    void cancel() override { m_client.cancel(); }

private:
    Job &job() const { return *m_job; }

    void fail(const QString &code, const QString &message) { job().done(failed(job(), code, message, m_plan)); }

    void fail(const GitHubError &error) { fail(errorCode(error), error.message); }

    /// A dry run that cannot reach GitHub: the plan from the manifest alone.
    void previewOffline(const GitHubError &error)
    {
        PublishReport r = reportFor(job(), job().plan(std::nullopt, VersionKind::GitSha));
        r.remoteErrorCode = errorCode(error);
        r.remoteError = error.message;
        r.url = siteAddress(job().target, job().manifest.url);
        job().done(r);
    }

    void onRepository(const GitHubError &error, const github::Repository &repository)
    {
        if (error.isError()) {
            if (job().preview())
                previewOffline(error);
            else
                fail(error);
            return;
        }
        if (!job().preview() && repository.permissionsKnown && !repository.canPush) {
            fail(u"no-push"_s, {});
            return;
        }
        if (repository.archived && !job().preview()) {
            fail(u"archived"_s, {});
            return;
        }
        readBranch();
    }

    void readBranch()
    {
        QPointer<GitHubBackend> self(this);
        m_client.branch(m_repo, m_branch, {}, [self](const GitHubError &error, const github::BranchHead &head, bool) {
            if (self)
                self->onBranch(error, head);
        });
    }

    void onBranch(const GitHubError &error, const github::BranchHead &head)
    {
        if (error.code == GitHubError::Code::NotFound) {
            // No such branch yet (or an empty repository): the first publish makes it.
            m_branchMissing = true;
            m_head = {};
            m_remote.clear();
            onRemote();
            return;
        }
        if (error.isError()) {
            if (job().preview())
                previewOffline(error);
            else
                fail(error);
            return;
        }
        m_branchMissing = false;
        m_head = head;
        QPointer<GitHubBackend> self(this);
        m_client.tree(m_repo, head.tree, true, [self](const GitHubError &e, const github::Tree &tree) {
            if (self)
                self->onTree(e, tree);
        });
    }

    void onTree(const GitHubError &error, const github::Tree &tree)
    {
        if (error.isError()) {
            if (job().preview())
                previewOffline(error);
            else
                fail(error);
            return;
        }
        if (tree.truncated) {
            fail(u"too-large"_s, u"GitHub left entries of the tree out"_s);
            return;
        }
        m_remote.clear();
        const QString prefix = m_folder.isEmpty() ? QString() : m_folder + u'/';
        for (const github::TreeEntry &e : tree.entries) {
            if (e.type != u"blob"_s || !e.path.startsWith(prefix))
                continue;
            m_remote.insert(e.path.mid(prefix.size()), RemoteFile{e.sha, e.size});
        }
        onRemote();
    }

    void onRemote()
    {
        m_plan = job().plan(m_remote, VersionKind::GitSha);
        if (job().preview()) {
            PublishReport r = reportFor(job(), m_plan);
            r.url = siteAddress(job().target, job().manifest.url);
            job().done(r);
            return;
        }
        if (!m_plan.conflicts().isEmpty() && !job().force && !job().unpublish()) {
            fail(u"conflicts"_s, {});
            return;
        }
        if (!m_plan.hasChanges()) {
            m_commit = m_head.commit;
            afterCommit();
            return;
        }
        // The entries of the new tree.
        m_trees.clear();
        m_blobQueue.clear();
        QList<github::NewTreeEntry> current;
        qint64 inlineBytes = 0;
        const QString prefix = m_folder.isEmpty() ? QString() : m_folder + u'/';
        for (const PlannedFile &p : std::as_const(m_plan.files)) {
            if (p.action == FileAction::Delete) {
                if (!m_branchMissing)
                    current.append(github::NewTreeEntry{prefix + p.path, u"100644"_s, std::nullopt, std::nullopt});
                continue;
            }
            if (p.action != FileAction::Upload && p.action != FileAction::Update)
                continue;
            const SiteFile *f = job().site.file(p.path);
            if (!f)
                continue;
            if (isInlineText(f->data)) {
                if (inlineBytes > 0 && inlineBytes + f->data.size() > kInlineBytesPerTree) {
                    m_trees.append(current);
                    current.clear();
                    inlineBytes = 0;
                }
                inlineBytes += f->data.size();
                current.append(github::NewTreeEntry{prefix + p.path, u"100644"_s, std::nullopt, f->data});
            } else {
                m_blobQueue.append(p.path);
            }
        }
        m_trees.append(current);
        m_blobIndex = 0;
        m_blobTotal = int(m_blobQueue.size());
        nextBlob();
    }

    void nextBlob()
    {
        if (m_blobIndex >= m_blobQueue.size()) {
            m_treeIndex = 0;
            m_treeSha = m_branchMissing ? QString() : m_head.tree;
            nextTree();
            return;
        }
        const QString path = m_blobQueue.at(m_blobIndex++);
        const SiteFile *f = job().site.file(path);
        job().progress(u"uploading"_s, int(m_blobIndex), m_blobTotal);
        const QString prefix = m_folder.isEmpty() ? QString() : m_folder + u'/';
        if (const auto known = m_blobs.constFind(f->gitSha); known != m_blobs.constEnd()) {
            m_trees.last().append(github::NewTreeEntry{prefix + path, u"100644"_s, *known, std::nullopt});
            nextBlob();
            return;
        }
        QPointer<GitHubBackend> self(this);
        m_client.createBlob(
                m_repo, f->data, [self, path, prefix, git = f->gitSha](const GitHubError &error, const QString &sha) {
                    if (!self)
                        return;
                    if (error.code == GitHubError::Code::EmptyRepository) {
                        self->startEmptyRepository();
                        return;
                    }
                    if (error.isError()) {
                        self->fail(error);
                        return;
                    }
                    self->m_blobs.insert(git, sha);
                    self->m_trees.last().append(github::NewTreeEntry{prefix + path, u"100644"_s, sha, std::nullopt});
                    self->nextBlob();
                });
    }

    void nextTree()
    {
        if (m_treeIndex >= m_trees.size()) {
            makeCommit();
            return;
        }
        const QList<github::NewTreeEntry> entries = m_trees.at(m_treeIndex++);
        job().progress(u"committing"_s, int(m_treeIndex), int(m_trees.size()));
        QPointer<GitHubBackend> self(this);
        m_client.createTree(m_repo, m_treeSha, entries, [self](const GitHubError &error, const QString &sha) {
            if (!self)
                return;
            if (error.code == GitHubError::Code::EmptyRepository) {
                self->startEmptyRepository();
                return;
            }
            if (error.isError()) {
                self->fail(error);
                return;
            }
            self->m_treeSha = sha;
            self->nextTree();
        });
    }

    /// An empty repository takes no tree: its first file through the contents API, then again.
    void startEmptyRepository()
    {
        if (m_startedEmpty) {
            fail(u"refused"_s, u"The repository stays empty"_s);
            return;
        }
        m_startedEmpty = true;
        const QString path = (m_folder.isEmpty() ? QString() : m_folder + u'/') + u".nojekyll"_s;
        QPointer<GitHubBackend> self(this);
        m_client.putFile(m_repo, path, m_branch, u"Start the site (Brulion)"_s, {},
                         [self](const GitHubError &error, const QString &) {
                             if (!self)
                                 return;
                             if (error.isError()) {
                                 self->fail(error);
                                 return;
                             }
                             self->readBranch();
                         });
    }

    QString commitMessage() const
    {
        if (job().unpublish())
            return u"Unpublish the site (Brulion)"_s;
        const int added = m_plan.count(FileAction::Upload);
        const int changed = m_plan.count(FileAction::Update);
        int removed = m_plan.count(FileAction::Delete);
        if (m_branchMissing)
            removed = 0;
        QStringList parts;
        if (added > 0)
            parts.append(u"%1 new"_s.arg(added));
        if (changed > 0)
            parts.append(u"%1 changed"_s.arg(changed));
        if (removed > 0)
            parts.append(u"%1 removed"_s.arg(removed));
        return u"Publish from Brulion: "_s + parts.join(u", "_s);
    }

    void makeCommit()
    {
        const QStringList parents = m_branchMissing ? QStringList() : QStringList{m_head.commit};
        QPointer<GitHubBackend> self(this);
        m_client.createCommit(m_repo, commitMessage(), m_treeSha, parents,
                              [self](const GitHubError &error, const QString &sha) {
                                  if (!self)
                                      return;
                                  if (error.isError()) {
                                      self->fail(error);
                                      return;
                                  }
                                  self->moveBranch(sha);
                              });
    }

    void moveBranch(const QString &commit)
    {
        QPointer<GitHubBackend> self(this);
        const auto answered = [self, commit](const GitHubError &error) {
            if (!self)
                return;
            const bool moved = error.code == GitHubError::Code::NotFastForward
                               || (self->m_branchMissing && error.code == GitHubError::Code::Validation);
            if (moved && !self->m_retried) {
                // Someone pushed (or made the branch) meanwhile: the plan again on its newest version.
                self->m_retried = true;
                self->readBranch();
                return;
            }
            if (error.isError()) {
                self->fail(moved ? u"not-fast-forward"_s : errorCode(error), error.message);
                return;
            }
            self->m_commit = commit;
            self->m_committed = true;
            self->afterCommit();
        };
        if (m_branchMissing)
            m_client.createBranch(m_repo, m_branch, commit, answered);
        else
            m_client.updateBranch(m_repo, m_branch, commit, answered);
    }

    void afterCommit()
    {
        if (job().unpublish()) {
            finishOk();
            return;
        }
        // GitHub Pages: on for the branch's root or docs/ when it is off.
        job().progress(u"pages"_s, 0, 0);
        QPointer<GitHubBackend> self(this);
        m_client.pages(m_repo, [self](const GitHubError &error, const github::PagesSite &site) {
            if (self)
                self->onPages(error, site);
        });
    }

    void onPages(const GitHubError &error, const github::PagesSite &site)
    {
        const QString wanted = pagesPath(job().target);
        if (!error.isError()) {
            notePages(site);
            const bool sameBranch = site.branch == m_branch;
            const QString served = site.path == u"/"_s ? QString() : site.path.mid(1);
            const bool below = served.isEmpty() || m_folder == served || m_folder.startsWith(served + u'/');
            if (!sameBranch || !below)
                warn(u"pages-elsewhere"_s, site.branch + (site.path.isEmpty() ? QString() : u" "_s + site.path));
            finishOk();
            return;
        }
        if (error.code != GitHubError::Code::NotFound) {
            warn(u"pages-unknown"_s, error.message);
            finishOk();
            return;
        }
        if (wanted.isEmpty()) {
            warn(u"pages-folder"_s, m_folder);
            finishOk();
            return;
        }
        QPointer<GitHubBackend> self(this);
        m_client.enablePages(m_repo, m_branch, wanted, [self](const GitHubError &e, const github::PagesSite &made) {
            if (!self)
                return;
            if (e.isError())
                self->warn(u"pages-not-enabled"_s, e.message);
            else
                self->notePages(made);
            self->finishOk();
        });
    }

    void notePages(const github::PagesSite &site)
    {
        m_pagesUrl = site.htmlUrl.toString();
        m_pagesFolder = site.path;
    }

    void warn(const QString &code, const QString &detail) { m_warnings.append({code, detail}); }

    void finishOk()
    {
        PublishReport r = reportFor(job(), m_plan);
        QHash<QString, QString> written;
        QSet<QString> deleted;
        for (const PlannedFile &p : std::as_const(m_plan.files)) {
            if ((p.action == FileAction::Upload || p.action == FileAction::Update) && m_committed) {
                if (const SiteFile *f = job().site.file(p.path))
                    written.insert(p.path, f->gitSha);
            } else if (p.action == FileAction::Delete && m_committed) {
                deleted.insert(p.path);
            }
        }
        countDone(r, m_plan, written, deleted);
        r.manifest = manifestAfter(job(), m_plan, written, deleted, VersionKind::GitSha);
        r.manifest.publishedAt = job().now;
        r.commit = m_commit;
        if (!m_commit.isEmpty())
            r.commitUrl = github::commitUrl(m_repo.owner, m_repo.name, m_commit).toString();
        r.manifest.commit = m_commit;
        r.url = job().unpublish() ? QString() : siteAddress(job().target, m_pagesUrl, m_pagesFolder);
        r.manifest.url = r.url;
        r.manifestChanged = true;
        for (const auto &[code, detail] : std::as_const(m_warnings)) {
            r.warningCodes.append(code);
            r.warningDetails.append(detail);
        }
        r.ok = true;
        job().done(r);
    }

    github::GitHubClient m_client;
    Job *m_job = nullptr;
    github::RepoId m_repo;
    QString m_branch;
    QString m_folder;
    github::BranchHead m_head;
    bool m_branchMissing = false;
    bool m_retried = false;
    bool m_startedEmpty = false;
    bool m_committed = false;
    RemoteState m_remote;
    PublishPlan m_plan;
    QList<QList<github::NewTreeEntry>> m_trees;
    qsizetype m_treeIndex = 0;
    QString m_treeSha;
    QStringList m_blobQueue;
    qsizetype m_blobIndex = 0;
    int m_blobTotal = 0;
    QHash<QString, QString> m_blobs; ///< Git blob id → the blob made (the same id)
    QString m_commit;
    QString m_pagesUrl;
    QString m_pagesFolder;
    QList<std::pair<QString, QString>> m_warnings;
};

} // namespace

std::unique_ptr<Backend> makeGitHubBackend(sync::HttpTransport *transport, bool browser, int timeoutMs)
{
    return std::make_unique<GitHubBackend>(transport, browser, timeoutMs);
}

} // namespace brulion::publish::detail

// A WebDAV folder (and Nextcloud's: the user's folder plus its public link). The folder's listing
// is the target's state: a file whose ETag is the one the last publish left is ours, anything else
// is a conflict — PUT and DELETE carry If-Match (If-None-Match: * for new files), so a file changed
// meanwhile is never overwritten.
#include "publish/backends_p.h"

#include "publish/ocs.h"
#include "sync/url.h"
#include "sync/webdavclient.h"

#include <QCryptographicHash>
#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::publish::detail {

namespace {

using sync::WebDavError;
using sync::WebDavErrorCode;

/// Uploads and deletions at the same time.
constexpr int kParallel = 3;

QString errorCode(const WebDavError &error)
{
    switch (error.code) {
    case WebDavErrorCode::Auth: return u"auth"_s;
    case WebDavErrorCode::Forbidden: return u"forbidden"_s;
    case WebDavErrorCode::NotFound: return u"not-found"_s;
    case WebDavErrorCode::Network: return u"network"_s;
    case WebDavErrorCode::Timeout: return u"timeout"_s;
    case WebDavErrorCode::Precondition: return u"conflicts"_s;
    case WebDavErrorCode::InsufficientStorage: return u"no-space"_s;
    case WebDavErrorCode::TooLarge: return u"too-large"_s;
    case WebDavErrorCode::Server: return u"server"_s;
    case WebDavErrorCode::Cancelled: return u"cancelled"_s;
    default: break;
    }
    return u"refused"_s;
}

QString passwordHash(const QString &password)
{
    return password.isEmpty()
                   ? QString()
                   : QString::fromLatin1(
                             QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256).toHex());
}

class WebDavBackend final : public Backend {
public:
    WebDavBackend(sync::HttpTransport *transport, int timeoutMs) : m_transport(transport), m_timeoutMs(timeoutMs) { }

    void run(const std::shared_ptr<Job> &job) override
    {
        m_job = job.get();
        const PublishTarget &t = job->target;
        m_nextcloud = t.kind == TargetKind::Nextcloud;
        if (!job->preview() && m_nextcloud && job->credentials.password.isEmpty()) {
            fail(u"no-password"_s, {});
            return;
        }
        m_client = std::make_unique<sync::WebDavClient>(davUrl(t), t.username.trimmed(), job->credentials.password,
                                                        m_transport);
        m_client->setTimeout(m_timeoutMs);
        m_client->setLogTag(u"publish"_s);
        if (m_nextcloud)
            m_ocs = std::make_unique<OcsShareClient>(t.server, t.username.trimmed(), job->credentials.password,
                                                     m_transport);
        job->progress(u"checking"_s, 0, 0);
        QPointer<WebDavBackend> self(this);
        if (job->kind == Job::Kind::Publish) {
            m_client->ensureBase([self](const WebDavError &error) {
                if (!self)
                    return;
                if (error.isError()) {
                    self->fail(errorCode(error), error.message);
                    return;
                }
                self->list();
            });
            return;
        }
        list();
    }

    void cancel() override
    {
        m_cancelled = true;
        m_client.reset();
        m_ocs.reset();
    }

private:
    Job &job() const { return *m_job; }

    void fail(const QString &code, const QString &message) { job().done(failed(job(), code, message, m_plan)); }

    void list()
    {
        QPointer<WebDavBackend> self(this);
        sync::ListOptions options;
        // Hidden folders of the server (.well-known, a client's own) are none of the site's.
        options.skipDir = [](const VaultPath &path) { return path.section(u'/', -1).startsWith(u'.'); };
        m_client->list(options, [self](const sync::ListResult &result) {
            if (self)
                self->onList(result);
        });
    }

    void onList(const sync::ListResult &result)
    {
        if (m_refreshingEtags) {
            // The listing after the uploads: the ETags their answers did not carry.
            for (const sync::RemoteEntry &e : result.entries) {
                if (!e.isDir && m_written.contains(e.path) && m_written.value(e.path).isEmpty())
                    m_written.insert(e.path, e.etag);
            }
            afterEtags();
            return;
        }
        if (result.error.isError() && !result.error.is(WebDavErrorCode::NotFound)) {
            if (job().preview()) {
                PublishReport r = reportFor(job(), job().plan(std::nullopt, VersionKind::Opaque));
                r.remoteErrorCode = errorCode(result.error);
                r.remoteError = result.error.message;
                r.url = siteAddress(job().target, {}, {}, job().manifest.shareUrl);
                job().done(r);
            } else {
                fail(errorCode(result.error), result.error.message);
            }
            return;
        }
        m_remote.clear();
        m_dirs.clear();
        for (const sync::RemoteEntry &e : result.entries) {
            if (e.isDir)
                m_dirs.insert(e.path);
            else
                m_remote.insert(e.path, RemoteFile{e.etag, e.size});
        }
        m_plan = job().plan(m_remote, VersionKind::Opaque);
        if (job().preview()) {
            PublishReport r = reportFor(job(), m_plan);
            r.url = siteAddress(job().target, {}, {}, job().manifest.shareUrl);
            job().done(r);
            return;
        }
        if (!m_plan.conflicts().isEmpty() && !job().force && !job().unpublish()) {
            fail(u"conflicts"_s, {});
            return;
        }
        for (const PlannedFile &p : std::as_const(m_plan.files)) {
            if (p.action == FileAction::Upload || p.action == FileAction::Update)
                m_writes.append(p);
            else if (p.action == FileAction::Delete)
                m_deletes.append(p);
        }
        m_total = int(m_writes.size() + m_deletes.size());
        pump();
    }

    /// Keep kParallel requests going: the uploads, then the deletions, then the emptied folders.
    void pump()
    {
        if (m_cancelled)
            return;
        while (!m_fatal && m_inFlight < kParallel
               && (m_nextWrite < m_writes.size() || m_nextDelete < m_deletes.size())) {
            if (m_nextWrite < m_writes.size()) {
                startWrite(m_writes.at(m_nextWrite++));
            } else {
                // Deletions after every upload has answered: a page and the links to it change together.
                if (m_inFlight > 0)
                    break;
                startDelete(m_deletes.at(m_nextDelete++));
            }
        }
        if (m_inFlight > 0)
            return;
        if (m_fatal) {
            fail(m_fatalCode, m_fatalMessage);
            return;
        }
        if (m_nextWrite >= m_writes.size() && m_nextDelete >= m_deletes.size())
            pruneFolders();
    }

    void startWrite(const PlannedFile &p)
    {
        const SiteFile *f = job().site.file(p.path);
        if (!f)
            return;
        ++m_inFlight;
        job().progress(u"uploading"_s, ++m_done, m_total);
        sync::PutOptions options;
        if (p.action == FileAction::Upload || p.remote.isEmpty())
            options.ifNoneMatch = p.action == FileAction::Upload;
        else
            options.ifMatch = p.remote;
        QPointer<WebDavBackend> self(this);
        const QString path = p.path;
        m_client->put(path, f->data, options, [self, path](const sync::PutResult &result) {
            if (!self)
                return;
            --self->m_inFlight;
            self->onAnswer(path, result.error, false, result.etag);
            self->pump();
        });
    }

    void startDelete(const PlannedFile &p)
    {
        ++m_inFlight;
        job().progress(u"deleting"_s, ++m_done, m_total);
        sync::RemoveOptions options;
        options.ifMatch = p.remote;
        QPointer<WebDavBackend> self(this);
        const QString path = p.path;
        m_client->remove(path, options, [self, path](const WebDavError &error) {
            if (!self)
                return;
            --self->m_inFlight;
            self->onAnswer(path, error, true, {});
            self->pump();
        });
    }

    void onAnswer(const QString &path, const WebDavError &error, bool deletion, const QString &etag)
    {
        if (!error.isError()) {
            if (deletion)
                m_deleted.insert(path);
            else
                m_written.insert(path, sync::normalizeEtag(etag));
            return;
        }
        if (error.is(WebDavErrorCode::Precondition)) {
            // Changed at the target meanwhile (or made there): never overwritten.
            PlannedFile c{path, FileAction::Conflict, 0, deletion, {}};
            m_lateConflicts.append(c);
            return;
        }
        if (error.isFatal()) {
            m_fatal = true;
            m_fatalCode = errorCode(error);
            m_fatalMessage = error.message;
            return;
        }
        m_failures.append(FileFailure{path, errorCode(error), error.message});
    }

    /// The folders the deletions emptied, deepest first.
    void pruneFolders()
    {
        if (m_pruneQueue.isEmpty() && !m_pruned) {
            m_pruned = true;
            QSet<QString> remaining;
            for (auto it = m_remote.cbegin(); it != m_remote.cend(); ++it) {
                if (!m_deleted.contains(it.key()))
                    remaining.insert(it.key());
            }
            for (auto it = m_written.cbegin(); it != m_written.cend(); ++it)
                remaining.insert(it.key());
            QSet<QString> candidates;
            for (const QString &path : std::as_const(m_deleted)) {
                for (QString dir = path.section(u'/', 0, -2); !dir.isEmpty(); dir = dir.section(u'/', 0, -2))
                    candidates.insert(dir);
            }
            for (const QString &dir : std::as_const(candidates)) {
                const QString prefix = dir + u'/';
                const bool used = std::ranges::any_of(remaining, [&](const QString &p) { return p.startsWith(prefix); })
                                  || std::ranges::any_of(m_dirs, [&](const QString &d) {
                                         return d.startsWith(prefix) && !candidates.contains(d);
                                     });
                if (!used && m_dirs.contains(dir))
                    m_pruneQueue.append(dir);
            }
            std::ranges::sort(m_pruneQueue,
                              [](const QString &a, const QString &b) { return a.count(u'/') > b.count(u'/'); });
        }
        if (m_pruneQueue.isEmpty()) {
            afterFiles();
            return;
        }
        const QString dir = m_pruneQueue.takeFirst();
        QPointer<WebDavBackend> self(this);
        m_client->remove(dir, {}, [self](const WebDavError &) {
            if (self)
                self->pruneFolders();
        });
    }

    void afterFiles()
    {
        // Servers that give no ETag with a PUT: the listing tells it (else the next publish would
        // take our own files for changed ones).
        const bool missing = std::ranges::any_of(m_written, [](const QString &etag) { return etag.isEmpty(); });
        if (missing && !job().unpublish()) {
            m_refreshingEtags = true;
            list();
            return;
        }
        afterEtags();
    }

    void afterEtags()
    {
        if (m_nextcloud)
            shareStep();
        else
            finishOk();
    }

    // ---- Nextcloud's public link -------------------------------------------------------------

    void shareStep()
    {
        job().progress(u"sharing"_s, 0, 0);
        const PublishTarget &t = job().target;
        const QString id = job().manifest.shareId;
        QPointer<WebDavBackend> self(this);
        if (job().unpublish() || !t.share) {
            if (id.isEmpty()) {
                finishOk();
                return;
            }
            m_ocs->remove(id, [self](const std::optional<OcsError> &error) {
                if (!self)
                    return;
                if (error) {
                    self->m_shareError = *error;
                } else {
                    self->m_share = std::nullopt;
                    self->m_shareRemoved = true;
                }
                self->finishOk();
            });
            return;
        }
        const auto found = [self](const std::optional<OcsError> &error, const std::optional<Share> &share) {
            if (!self)
                return;
            if (error) {
                self->m_shareError = *error;
                self->finishOk();
                return;
            }
            if (share)
                self->adjustShare(*share);
            else
                self->findShare();
        };
        if (!id.isEmpty())
            m_ocs->get(id, found);
        else
            findShare();
    }

    void findShare()
    {
        QPointer<WebDavBackend> self(this);
        const QString path = sharePath(job().target.folder);
        m_ocs->find(path, [self, path](const std::optional<OcsError> &error, const std::optional<Share> &share) {
            if (!self)
                return;
            if (error) {
                self->m_shareError = *error;
                self->finishOk();
                return;
            }
            if (share) {
                self->adjustShare(*share);
                return;
            }
            const PublishTarget &t = self->job().target;
            const QString password = t.sharePassword ? self->job().credentials.sharePassword : QString();
            if (t.sharePassword && password.isEmpty()) {
                // Never a link without the password the target asks for.
                self->warn(u"no-share-password"_s, {});
                self->finishOk();
                return;
            }
            self->m_ocs->create(path, password, t.shareExpires,
                                [self, password](const std::optional<OcsError> &e, const Share &made) {
                                    if (!self)
                                        return;
                                    if (e) {
                                        self->m_shareError = *e;
                                    } else {
                                        self->m_share = made;
                                        self->m_sharePasswordHash = passwordHash(password);
                                        self->m_shareExpires = self->job().target.shareExpires;
                                    }
                                    self->finishOk();
                                });
        });
    }

    /// The link as the target says: its password and expiry date set when they differ.
    void adjustShare(const Share &share)
    {
        m_share = share;
        m_sharePasswordHash = job().manifest.sharePasswordHash;
        m_shareExpires = share.expires;
        const PublishTarget &t = job().target;
        const QString password = job().credentials.sharePassword;
        if (t.sharePassword && password.isEmpty() && !share.hasPassword)
            warn(u"no-share-password"_s, {});
        const QString wanted = t.sharePassword ? passwordHash(password) : QString();
        const bool passwordChanges =
                t.sharePassword ? (!password.isEmpty() && (wanted != m_sharePasswordHash || !share.hasPassword))
                                : share.hasPassword;
        QPointer<WebDavBackend> self(this);
        const auto expiry = [self]() {
            if (!self)
                return;
            const QDate wantedDate = self->job().target.shareExpires;
            if (wantedDate == self->m_shareExpires) {
                self->finishOk();
                return;
            }
            self->m_ocs->setExpires(self->m_share->id, wantedDate,
                                    [self, wantedDate](const std::optional<OcsError> &e) {
                                        if (!self)
                                            return;
                                        if (e)
                                            self->m_shareError = *e;
                                        else
                                            self->m_shareExpires = wantedDate;
                                        self->finishOk();
                                    });
        };
        if (!passwordChanges) {
            expiry();
            return;
        }
        const QString value = t.sharePassword ? password : QString();
        m_ocs->setPassword(share.id, value, [self, expiry, value](const std::optional<OcsError> &e) {
            if (!self)
                return;
            if (e) {
                self->m_shareError = *e;
                self->finishOk();
                return;
            }
            self->m_sharePasswordHash = passwordHash(value);
            expiry();
        });
    }

    void warn(const QString &code, const QString &detail) { m_warnings.append({code, detail}); }

    void finishOk()
    {
        PublishReport r = reportFor(job(), m_plan);
        // Conflicts the server found (a 412) count as such too.
        for (const PlannedFile &c : std::as_const(m_lateConflicts))
            r.conflicts.append(c);
        r.failures = m_failures;
        countDone(r, m_plan, m_written, m_deleted);
        r.manifest = manifestAfter(job(), m_plan, m_written, m_deleted, VersionKind::Opaque);
        r.manifest.publishedAt = job().now;
        if (m_share) {
            r.manifest.shareId = m_share->id;
            r.manifest.shareUrl = m_share->url;
            r.manifest.sharePasswordHash = m_sharePasswordHash;
            r.manifest.shareExpires = m_shareExpires;
        } else if (m_shareRemoved) {
            r.manifest.shareId.clear();
            r.manifest.shareUrl.clear();
            r.manifest.sharePasswordHash.clear();
            r.manifest.shareExpires = {};
        }
        r.url = job().unpublish() ? QString() : siteAddress(job().target, {}, {}, r.manifest.shareUrl);
        r.manifest.url = r.url;
        r.manifestChanged = true;
        for (const auto &[code, detail] : std::as_const(m_warnings)) {
            r.warningCodes.append(code);
            r.warningDetails.append(detail);
        }
        r.ok = m_failures.isEmpty() && m_lateConflicts.isEmpty() && !m_shareError;
        if (m_shareError) {
            r.errorCode = m_shareError->code() == u"share"_s || m_shareError->code() == u"not-found"_s
                                  ? u"share"_s
                                  : m_shareError->code();
            r.error = m_shareError->message;
        } else if (!m_lateConflicts.isEmpty()) {
            r.errorCode = u"conflicts"_s;
        } else if (!m_failures.isEmpty()) {
            r.errorCode = u"files-failed"_s;
        }
        job().done(r);
    }

    sync::HttpTransport *m_transport;
    int m_timeoutMs;
    Job *m_job = nullptr;
    bool m_nextcloud = false;
    bool m_cancelled = false;
    std::unique_ptr<sync::WebDavClient> m_client;
    std::unique_ptr<OcsShareClient> m_ocs;
    RemoteState m_remote;
    QSet<QString> m_dirs;
    PublishPlan m_plan;
    QList<PlannedFile> m_writes;
    QList<PlannedFile> m_deletes;
    qsizetype m_nextWrite = 0;
    qsizetype m_nextDelete = 0;
    int m_inFlight = 0;
    int m_done = 0;
    int m_total = 0;
    bool m_fatal = false;
    QString m_fatalCode;
    QString m_fatalMessage;
    QHash<QString, QString> m_written;
    QSet<QString> m_deleted;
    QList<PlannedFile> m_lateConflicts;
    QList<FileFailure> m_failures;
    QStringList m_pruneQueue;
    bool m_pruned = false;
    bool m_refreshingEtags = false;
    std::optional<Share> m_share;
    bool m_shareRemoved = false;
    QString m_sharePasswordHash;
    QDate m_shareExpires;
    std::optional<OcsError> m_shareError;
    QList<std::pair<QString, QString>> m_warnings;
};

} // namespace

std::unique_ptr<Backend> makeWebDavBackend(sync::HttpTransport *transport, int timeoutMs)
{
    return std::make_unique<WebDavBackend>(transport, timeoutMs);
}

} // namespace brulion::publish::detail

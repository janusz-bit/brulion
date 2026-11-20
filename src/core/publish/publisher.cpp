#include "publish/publisher.h"

#include "log/categories.h"
#include "log/redact.h"
#include "publish/backends_p.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace detail {

PublishPlan Job::plan(const std::optional<RemoteState> &remote, VersionKind versions) const
{
    return unpublish() ? planUnpublish(manifest, remote, versions, force)
                       : planPublish(site, manifest, remote, versions, force);
}

PublishReport reportFor(const Job &job, const PublishPlan &plan)
{
    PublishReport r;
    r.operation = job.preview() ? u"preview"_s : job.unpublish() ? u"unpublish"_s : u"publish"_s;
    r.plan = plan;
    r.manifest = job.manifest;
    r.conflicts = plan.conflicts();
    r.unchanged = plan.count(FileAction::Unchanged);
    if (job.preview()) {
        r.uploaded = plan.count(FileAction::Upload);
        r.updated = plan.count(FileAction::Update);
        r.deleted = plan.count(FileAction::Delete);
        r.ok = true;
    }
    return r;
}

PublishReport failed(const Job &job, const QString &code, const QString &message, const PublishPlan &plan)
{
    PublishReport r = reportFor(job, plan);
    r.ok = false;
    r.uploaded = r.updated = r.deleted = 0;
    r.errorCode = code;
    r.error = logging::redact(message);
    return r;
}

PublishManifest manifestAfter(const Job &job, const PublishPlan &plan, const QHash<QString, QString> &written,
                              const QSet<QString> &deleted, VersionKind versions)
{
    PublishManifest m = job.manifest;
    m.identity = identity(job.target);
    for (const PlannedFile &p : plan.files) {
        const SiteFile *f = job.site.file(p.path);
        switch (p.action) {
        case FileAction::Unchanged:
            if (f)
                m.files.insert(p.path, ManifestFile{f->sha256, f->data.size(),
                                                    p.remote.isEmpty() && versions != VersionKind::Opaque
                                                            ? (versions == VersionKind::GitSha ? f->gitSha : f->sha256)
                                                            : p.remote});
            break;
        case FileAction::Upload:
        case FileAction::Update:
            if (f && written.contains(p.path))
                m.files.insert(p.path, ManifestFile{f->sha256, f->data.size(), written.value(p.path)});
            break;
        case FileAction::Delete:
            if (deleted.contains(p.path))
                m.files.remove(p.path);
            break;
        case FileAction::Conflict: break;
        }
    }
    // A file the manifest had that is gone from the site and from the target: forgotten.
    if (!job.unpublish()) {
        for (auto it = m.files.begin(); it != m.files.end();) {
            const bool planned =
                    std::ranges::any_of(plan.files, [&](const PlannedFile &p) { return p.path == it.key(); });
            if (!planned && !job.site.file(it.key()))
                it = m.files.erase(it);
            else
                ++it;
        }
    } else {
        for (auto it = m.files.begin(); it != m.files.end();) {
            const bool planned =
                    std::ranges::any_of(plan.files, [&](const PlannedFile &p) { return p.path == it.key(); });
            it = planned ? std::next(it) : m.files.erase(it);
        }
    }
    return m;
}

void countDone(PublishReport &report, const PublishPlan &plan, const QHash<QString, QString> &written,
               const QSet<QString> &deleted)
{
    report.uploaded = report.updated = report.deleted = 0;
    for (const PlannedFile &p : plan.files) {
        if (p.action == FileAction::Upload && written.contains(p.path))
            ++report.uploaded;
        else if (p.action == FileAction::Update && written.contains(p.path))
            ++report.updated;
        else if (p.action == FileAction::Delete && deleted.contains(p.path))
            ++report.deleted;
    }
}

} // namespace detail

struct Publisher::Job : detail::Job {
    std::unique_ptr<detail::Backend> backend;
};

Publisher::Publisher(sync::HttpTransport *transport, QObject *parent) : QObject(parent), m_transport(transport) { }

Publisher::~Publisher()
{
    if (m_job && m_job->backend)
        m_job->backend->cancel();
}

bool Publisher::busy() const
{
    return m_job && !m_job->finished;
}

void Publisher::cancel()
{
    if (!busy())
        return;
    const std::shared_ptr<Job> job = m_job;
    if (job->backend)
        job->backend->cancel();
    // The backend's late answer is dropped (the job is finished).
    job->done(detail::failed(*job, u"cancelled"_s, u"Cancelled"_s));
}

void Publisher::preview(const PublishTarget &target, const SiteBuild &site, const PublishManifest &manifest,
                        const PublishCredentials &credentials, std::function<void(const PublishReport &)> done)
{
    auto job = std::make_shared<Job>();
    job->kind = Job::Kind::Preview;
    job->target = target;
    job->site = site;
    job->manifest = manifest;
    job->credentials = credentials;
    job->done = [done = std::move(done)](PublishReport r) { done(r); };
    start(job);
}

void Publisher::publish(const PublishTarget &target, const SiteBuild &site, const PublishManifest &manifest,
                        const PublishCredentials &credentials, bool force,
                        std::function<void(const PublishReport &)> done)
{
    auto job = std::make_shared<Job>();
    job->kind = Job::Kind::Publish;
    job->target = target;
    job->site = site;
    job->manifest = manifest;
    job->credentials = credentials;
    job->force = force;
    job->done = [done = std::move(done)](PublishReport r) { done(r); };
    start(job);
}

void Publisher::unpublish(const PublishTarget &target, const PublishManifest &manifest,
                          const PublishCredentials &credentials, bool force,
                          std::function<void(const PublishReport &)> done)
{
    auto job = std::make_shared<Job>();
    job->kind = Job::Kind::Unpublish;
    job->target = target;
    job->manifest = manifest;
    job->credentials = credentials;
    job->force = force;
    job->done = [done = std::move(done)](PublishReport r) { done(r); };
    start(job);
}

void Publisher::start(const std::shared_ptr<Job> &job)
{
    if (busy())
        cancel();
    job->now = m_clock ? m_clock() : QDateTime::currentDateTime();
    QStringList warningCodes;
    // A manifest of another place does not describe this one: everything is new here, and what
    // was published there stays there.
    if (job->manifest.identity != identity(job->target)) {
        if (!job->manifest.isEmpty())
            warningCodes.append(u"moved"_s);
        job->manifest = PublishManifest{};
        job->manifest.identity = identity(job->target);
    }
    QPointer<Publisher> self(this);
    job->progress = [self](const QString &phase, int done, int total) {
        if (self)
            Q_EMIT self->progress(phase, done, total);
    };
    const auto done = std::move(job->done);
    std::weak_ptr<Job> weak = job;
    job->done = [self, weak, done, warningCodes](PublishReport report) {
        const std::shared_ptr<Job> j = weak.lock();
        if (!self || !j || j->finished)
            return;
        for (const QString &code : warningCodes) {
            report.warningCodes.prepend(code);
            report.warningDetails.prepend(QString());
        }
        j->finished = true;
        // From the event loop, never inside a backend's call.
        QTimer::singleShot(0, self.data(), [self, j, report, done] {
            if (self && self->m_job == j)
                self->m_job.reset();
            qCInfo(logging::lcNet).noquote()
                    << u"Publishing (%1, %2): %3 — %4 new, %5 changed, %6 removed, %7 unchanged%8"_s.arg(
                               kindName(j->target.kind), report.operation,
                               report.ok ? u"done"_s : u"failed: "_s + report.errorCode,
                               QString::number(report.uploaded), QString::number(report.updated),
                               QString::number(report.deleted), QString::number(report.unchanged),
                               report.conflicts.isEmpty() ? QString()
                                                          : u", %1 conflict(s)"_s.arg(report.conflicts.size()));
            done(report);
        });
    };
    switch (job->target.kind) {
    case TargetKind::GitHubPages: job->backend = detail::makeGitHubBackend(m_transport, m_browser, m_timeoutMs); break;
    case TargetKind::WebDav:
    case TargetKind::Nextcloud: job->backend = detail::makeWebDavBackend(m_transport, m_timeoutMs); break;
    case TargetKind::Folder: job->backend = detail::makeFolderBackend(); break;
    }
    m_job = job;
    if (const QString problem = checkTarget(job->target); !problem.isEmpty()) {
        job->done(detail::failed(*job, problem, {}));
        return;
    }
    job->backend->run(job);
}

} // namespace brulion::publish

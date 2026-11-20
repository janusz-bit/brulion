// The publishers of each kind of target (Publisher picks one per operation). Internal to the
// publish module.
#pragma once

#include "publish/publisher.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>

namespace brulion::sync {
class HttpTransport;
}

namespace brulion::publish::detail {

/// One operation of a Publisher.
struct Job {
    enum class Kind { Preview, Publish, Unpublish };
    Kind kind = Kind::Preview;
    PublishTarget target;
    SiteBuild site;
    /// The manifest of this very place (another place's: empty, its identity set).
    PublishManifest manifest;
    PublishCredentials credentials;
    bool force = false;
    QDateTime now;
    ProgressFn progress;
    /// Called once with the result (the Publisher's finish()).
    std::function<void(PublishReport)> done;
    bool finished = false;

    bool preview() const { return kind == Kind::Preview; }
    bool unpublish() const { return kind == Kind::Unpublish; }
    /// The plan for `remote` (publish or unpublish by the kind).
    PublishPlan plan(const std::optional<RemoteState> &remote, VersionKind versions) const;
};

class Backend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    /// Carry `job` out; job->done is called once, from the event loop.
    virtual void run(const std::shared_ptr<Job> &job) = 0;
    /// Stop the requests in flight (their answers are dropped).
    virtual void cancel() { }
};

std::unique_ptr<Backend> makeFolderBackend();
std::unique_ptr<Backend> makeGitHubBackend(sync::HttpTransport *transport, bool browser, int timeoutMs);
/// WebDAV, and Nextcloud (WebDAV plus its public link).
std::unique_ptr<Backend> makeWebDavBackend(sync::HttpTransport *transport, int timeoutMs);

/// A report for `job` with `plan` (counts of a dry run: what would happen).
PublishReport reportFor(const Job &job, const PublishPlan &plan);

/// What the target holds after an operation: the manifest of `job` with every unchanged site file
/// (its version as seen), the written ones (`written`: path → the version the target now has),
/// the deleted ones gone; files that failed or conflicted keep their old entry.
PublishManifest manifestAfter(const Job &job, const PublishPlan &plan, const QHash<QString, QString> &written,
                              const QSet<QString> &deleted, VersionKind versions);

/// Fill the report's counts from what was done.
void countDone(PublishReport &report, const PublishPlan &plan, const QHash<QString, QString> &written,
               const QSet<QString> &deleted);

/// A failure report: `code` with the technical `message`.
PublishReport failed(const Job &job, const QString &code, const QString &message, const PublishPlan &plan = {});

} // namespace brulion::publish::detail

// Publisher — publishing a built site (publish/site.h) to a target, its dry run and unpublishing,
// asynchronously over sync::HttpTransport (NetworkHttpTransport in the app and the command line,
// in-memory servers in tests):
//   GitHub Pages  one commit per publish on the target's branch (made without history when
//                 missing; an empty repository gets its first file through the contents API): text
//                 inline in the new tree, other files as blobs, removed files as deletions; the
//                 branch moved only as a fast-forward — when someone pushed meanwhile, the plan is
//                 made again on the new head (once). Pages turned on for a branch's root or docs/
//                 when it is off; its address reported.
//   WebDAV        the folder made when missing; PUT with If-None-Match: * (new) or If-Match: the
//                 ETag last published (changed) — a 412 is a conflict, never overwritten; DELETE with
//                 If-Match; folders emptied by the removals removed.
//   Nextcloud     WebDAV into the user's folder, then its public link (OCS Share API): made when
//                 missing, its password and expiry date set as the target says.
//   Folder        files written with QSaveFile, compared by SHA-256.
// Every operation answers once (from the event loop) with a PublishReport; cancel() ends it with
// "cancelled". Part of the publish module — see publish/publish.h.
#pragma once

#include "publish/manifest.h"
#include "publish/plan.h"
#include "publish/site.h"
#include "publish/target.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace brulion::sync {
class HttpTransport;
}

namespace brulion::publish {

/// The secrets of a target (publish/store.h keeps them).
struct PublishCredentials {
    QString token;         ///< GitHub: a personal access token
    QString password;      ///< WebDAV, Nextcloud: the (app) password
    QString sharePassword; ///< Nextcloud: the public link's password
};

/// A file that failed.
struct FileFailure {
    QString path;
    QString code;    ///< publish/messages.h errorText()
    QString message; ///< technical (redacted)
};

struct PublishReport {
    QString operation; ///< "preview" | "publish" | "unpublish"
    bool ok = false;
    PublishPlan plan;
    int uploaded = 0; ///< new files
    int updated = 0;
    int deleted = 0;
    int unchanged = 0;
    QList<PlannedFile> conflicts;
    QList<FileFailure> failures;
    /// Where the site is (GitHub Pages' address, the Nextcloud link, the target's site address).
    QString url;
    QString commit;    ///< GitHub
    QString commitUrl; ///< GitHub
    /// The failure that stopped it: a code (publish/messages.h errorText(); "conflicts" when the
    /// plan has conflicts and force was off) and the technical message.
    QString errorCode;
    QString error;
    /// A dry run could not reach the target: the plan comes from the manifest alone.
    QString remoteErrorCode;
    QString remoteError;
    /// Things to tell the user that did not stop it ("pages-not-enabled", "pages-elsewhere",
    /// "share-failed", …: codes for messages.h warningText(), with the details).
    QStringList warningCodes;
    QStringList warningDetails;
    /// The manifest after the operation (a preview: the old one).
    PublishManifest manifest;
    bool manifestChanged = false;
};

/// Progress: a phase ("checking" | "uploading" | "deleting" | "committing" | "sharing" |
/// "pages"), done and total files (0 / 0 unknown).
using ProgressFn = std::function<void(const QString &phase, int done, int total)>;

class Publisher : public QObject {
    Q_OBJECT
public:
    /// `transport` is not owned and must outlive the publisher.
    explicit Publisher(sync::HttpTransport *transport, QObject *parent = nullptr);
    ~Publisher() override;
    Q_DISABLE_COPY_MOVE(Publisher)

    /// In a browser: no User-Agent of our own.
    void setBrowser(bool browser) { m_browser = browser; }
    /// Time limit of one request (default 60 s).
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    /// The local time of the operations (manifests, statuses; default: now).
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }

    /// What publish() would do: the target looked at (a target that cannot be reached: the plan
    /// from the manifest alone, remoteError set), nothing written.
    void preview(const PublishTarget &target, const SiteBuild &site, const PublishManifest &manifest,
                 const PublishCredentials &credentials, std::function<void(const PublishReport &)> done);
    /// Publish `site`: conflicts stop it before anything is written unless `force`.
    void publish(const PublishTarget &target, const SiteBuild &site, const PublishManifest &manifest,
                 const PublishCredentials &credentials, bool force, std::function<void(const PublishReport &)> done);
    /// Remove what `manifest` published (files changed there since stay unless `force`); a
    /// Nextcloud link goes too.
    void unpublish(const PublishTarget &target, const PublishManifest &manifest, const PublishCredentials &credentials,
                   bool force, std::function<void(const PublishReport &)> done);
    void cancel();
    bool busy() const;

Q_SIGNALS:
    void progress(const QString &phase, int done, int total);

private:
    struct Job;
    void start(const std::shared_ptr<Job> &job);

    sync::HttpTransport *m_transport;
    bool m_browser = false;
    int m_timeoutMs = 60000;
    std::function<QDateTime()> m_clock;
    std::shared_ptr<Job> m_job;
};

} // namespace brulion::publish

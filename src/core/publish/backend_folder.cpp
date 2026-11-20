// A folder on this device: its files compared by SHA-256, written with QSaveFile, removed ones
// deleted (and the folders they emptied). Hidden files and folders there (.git/, a server's own
// files) are never looked at.
#include "publish/backends_p.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::publish::detail {

namespace {

QString sha256Of(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&in);
    return QString::fromLatin1(hash.result().toHex());
}

/// Is `path` safe to write below the folder (no "..", no absolute path, no hidden segment)?
bool isSafeRelative(const QString &path)
{
    if (path.isEmpty() || path.startsWith(u'/') || path.contains(u'\\') || path.contains(u':'))
        return false;
    const QStringList segments = path.split(u'/');
    return std::ranges::none_of(segments, [](const QString &s) {
        return s.isEmpty() || s == u"." || s == u".." || (s.startsWith(u'.') && s != u".nojekyll");
    });
}

class FolderBackend final : public Backend {
public:
    void run(const std::shared_ptr<Job> &job) override
    {
        m_job = job.get();
        QPointer<FolderBackend> self(this);
        QTimer::singleShot(0, this, [self] {
            if (self)
                self->work();
        });
    }

private:
    void work()
    {
        Job &job = *m_job;
        const QString root = QDir::cleanPath(job.target.path.trimmed());
        const QFileInfo info(root);
        RemoteState remote;
        if (info.exists()) {
            if (!info.isDir()) {
                job.done(failed(job, u"io"_s, u"Not a folder: "_s + root));
                return;
            }
            job.progress(u"checking"_s, 0, 0);
            QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString file = it.next();
                const QString rel = QDir(root).relativeFilePath(file);
                // Only the files a publish could have made (hidden ones are the folder's own).
                if (!isSafeRelative(rel))
                    continue;
                remote.insert(rel, RemoteFile{sha256Of(file), QFileInfo(file).size()});
            }
        } else if (!job.preview() && !job.unpublish() && !QDir().mkpath(root)) {
            job.done(failed(job, u"io"_s, u"Cannot create "_s + root));
            return;
        }
        const PublishPlan plan = job.plan(remote, VersionKind::Sha256);
        if (job.preview()) {
            PublishReport r = reportFor(job, plan);
            r.url = siteAddress(job.target);
            job.done(r);
            return;
        }
        // Unpublishing removes the rest and keeps what changed there (listed as conflicts).
        if (!plan.conflicts().isEmpty() && !job.force && !job.unpublish()) {
            job.done(failed(job, u"conflicts"_s, {}, plan));
            return;
        }
        PublishReport report = reportFor(job, plan);
        QHash<QString, QString> written;
        QSet<QString> deleted;
        const int total =
                plan.count(FileAction::Upload) + plan.count(FileAction::Update) + plan.count(FileAction::Delete);
        int done = 0;
        QSet<QString> emptied;
        for (const PlannedFile &p : plan.files) {
            const QString file = root + u'/' + p.path;
            if (!isSafeRelative(p.path))
                continue;
            if (p.action == FileAction::Upload || p.action == FileAction::Update) {
                const SiteFile *f = job.site.file(p.path);
                if (!f)
                    continue;
                job.progress(u"uploading"_s, ++done, total);
                QDir().mkpath(QFileInfo(file).absolutePath());
                QSaveFile out(file);
                if (!out.open(QIODevice::WriteOnly) || out.write(f->data) != f->data.size() || !out.commit()) {
                    report.failures.append(FileFailure{p.path, u"io"_s, out.errorString()});
                    continue;
                }
                written.insert(p.path, f->sha256);
            } else if (p.action == FileAction::Delete) {
                job.progress(u"deleting"_s, ++done, total);
                if (QFileInfo::exists(file) && !QFile::remove(file)) {
                    report.failures.append(FileFailure{p.path, u"io"_s, u"Cannot remove the file"_s});
                    continue;
                }
                deleted.insert(p.path);
                for (QString dir = p.path.section(u'/', 0, -2); !dir.isEmpty(); dir = dir.section(u'/', 0, -2))
                    emptied.insert(dir);
            }
        }
        // The folders the removals emptied, deepest first (a folder with anything left stays).
        QStringList dirs(emptied.cbegin(), emptied.cend());
        std::ranges::sort(dirs, [](const QString &a, const QString &b) { return a.count(u'/') > b.count(u'/'); });
        for (const QString &dir : std::as_const(dirs))
            QDir(root).rmdir(dir);
        countDone(report, plan, written, deleted);
        report.manifest = manifestAfter(job, plan, written, deleted, VersionKind::Sha256);
        report.manifest.publishedAt = job.now;
        report.manifest.url = job.unpublish() ? QString() : siteAddress(job.target);
        report.manifestChanged = true;
        report.url = report.manifest.url;
        report.ok = report.failures.isEmpty();
        if (!report.ok)
            report.errorCode = u"files-failed"_s;
        job.done(report);
    }

    Job *m_job = nullptr;
};

} // namespace

std::unique_ptr<Backend> makeFolderBackend()
{
    return std::make_unique<FolderBackend>();
}

} // namespace brulion::publish::detail

#include "fileexchange.h"

#include "iobackground.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <utility>

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

/// A document's display name as one path segment: providers may answer anything ("a/b", "..").
QString safeSegment(QString name)
{
    name.replace(u'/', u'_');
    name.replace(u'\\', u'_');
    name = name.trimmed();
    if (name.isEmpty() || name == u"." || name == u"..")
        return {};
    return name;
}

/// `relative` itself, or "<name> 2.<ext>", "<name> 3.<ext>"… when a picked file of that name
/// (in any letter case) came already: two picked "Notatka.md" from different folders stay two.
QString uniqueRelative(QSet<QString> &used, const QString &relative)
{
    if (!used.contains(relative.toCaseFolded())) {
        used.insert(relative.toCaseFolded());
        return relative;
    }
    const qsizetype slash = relative.lastIndexOf(u'/');
    const qsizetype dot = relative.lastIndexOf(u'.');
    const bool hasExt = dot > slash + 1;
    const QString stem = hasExt ? relative.left(dot) : relative;
    const QString ext = hasExt ? relative.mid(dot) : QString();
    for (int n = 2;; ++n) {
        const QString candidate = u"%1 %2%3"_s.arg(stem).arg(n).arg(ext);
        if (!used.contains(candidate.toCaseFolded())) {
            used.insert(candidate.toCaseFolded());
            return candidate;
        }
    }
}

/// Up to `maxBytes` + 1 bytes of an open file whose size may be unknown (read in pieces, never one
/// allocation of `maxBytes`). `sizeHint` (from the listing, -1 = unknown) only reserves memory:
/// asking a document for its size would be one more query of its provider.
std::optional<QByteArray> readLimited(QFile &file, qint64 maxBytes, qint64 sizeHint, QString *error)
{
    QByteArray data;
    if (sizeHint > 0 && sizeHint <= maxBytes)
        data.reserve(sizeHint);
    constexpr qint64 Piece = 1 << 20;
    while (data.size() <= maxBytes) {
        const QByteArray piece = file.read(std::min(Piece, maxBytes + 1 - data.size()));
        if (piece.isEmpty())
            break;
        data.append(piece);
    }
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = file.errorString();
        return std::nullopt;
    }
    return data;
}

// ---- Qt's content file engine -------------------------------------------------------------------

class QtDocumentProvider final : public DocumentProvider {
public:
    std::optional<Entry> stat(const QUrl &url, QString *error) const override
    {
        const QFileInfo info(fileSystemPathOf(url));
        if (!info.exists()) {
            if (error)
                *error = QCoreApplication::translate("brulion::ioviews::FileGatherer", "not found");
            return std::nullopt;
        }
        return entryOf(info);
    }

    std::optional<QList<Entry>> children(const Entry &folder, QString *error) const override
    {
        const QDir dir(folder.uri);
        if (!dir.exists()) {
            if (error)
                *error = QCoreApplication::translate("brulion::ioviews::FileGatherer", "not found");
            return std::nullopt;
        }
        QList<Entry> out;
        const QFileInfoList entries =
                dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System);
        for (const QFileInfo &info : entries)
            out.append(entryOf(info));
        return out;
    }

    std::optional<QByteArray> read(const Entry &file, qint64 maxBytes, QString *error) const override
    {
        QFile f(file.uri);
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = f.errorString();
            return std::nullopt;
        }
        return readLimited(f, maxBytes, file.size, error);
    }

private:
    static Entry entryOf(const QFileInfo &info)
    {
        Entry e;
        // For a document the engine's path is its (document) URI; a tree URI as picked becomes
        // the URI of its root document.
        e.uri = info.filePath();
        e.name = info.fileName();
        e.isDir = info.isDir();
        e.size = e.isDir ? -1 : info.size();
        return e;
    }
};

#ifdef Q_OS_ANDROID
// ---- Android: DocumentsContract through the bridge ----------------------------------------------

/// One ContentResolver query per document / folder (Documents.kt); Qt's engine where the JNI
/// call itself fails (it then answers the same questions with many queries per entry).
class AndroidDocumentProvider final : public DocumentProvider {
public:
    std::optional<Entry> stat(const QUrl &url, QString *error) const override
    {
        const platform::DocumentsResult r = platform::AndroidBridge::documentInfo(fileSystemPathOf(url));
        if (r.bridgeFailed)
            return m_qt.stat(url, error);
        if (!r.ok || r.documents.isEmpty()) {
            if (error)
                *error = r.error;
            return std::nullopt;
        }
        return entryOf(r.documents.first());
    }

    std::optional<QList<Entry>> children(const Entry &folder, QString *error) const override
    {
        const platform::DocumentsResult r = platform::AndroidBridge::listDocuments(folder.uri);
        if (r.bridgeFailed)
            return m_qt.children(folder, error);
        if (!r.ok) {
            if (error)
                *error = r.error;
            return std::nullopt;
        }
        QList<Entry> out;
        out.reserve(r.documents.size());
        for (const platform::DocumentInfo &d : r.documents)
            out.append(entryOf(d));
        return out;
    }

    std::optional<QByteArray> read(const Entry &file, qint64 maxBytes, QString *error) const override
    {
        return m_qt.read(file, maxBytes, error); // QFile opens content URIs (a file descriptor)
    }

private:
    static Entry entryOf(const platform::DocumentInfo &d) { return Entry{d.uri, d.name, d.isDir, d.size, d.isVirtual}; }

    QtDocumentProvider m_qt;
};
#endif

} // namespace

// ---- Helpers -------------------------------------------------------------------------------------

DocumentProvider::~DocumentProvider() = default;

std::shared_ptr<const DocumentProvider> qtDocumentProvider()
{
    static const auto provider = std::make_shared<const QtDocumentProvider>();
    return provider;
}

std::shared_ptr<const DocumentProvider> platformDocumentProvider()
{
#ifdef Q_OS_ANDROID
    static const auto provider = std::make_shared<const AndroidDocumentProvider>();
    return provider;
#else
    return qtDocumentProvider();
#endif
}

bool isDocumentUrl(const QUrl &url)
{
    return url.scheme() == u"content";
}

QString fileSystemPathOf(const QUrl &url)
{
    if (url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());
    if (url.scheme().isEmpty())
        return QDir::cleanPath(url.toString());
    // content://… (Android documents): Qt's file engine opens the URL itself. Exactly as the
    // system encoded it ("primary%3ADownload%2FObsidian"): the document id is in there.
    return url.toString(QUrl::FullyEncoded);
}

QUrl urlFromUserInput(const QString &pathOrUrl)
{
    const QString text = pathOrUrl.trimmed();
    if (text.isEmpty())
        return {};
    if (text.startsWith(u"content:"_s, Qt::CaseInsensitive) || text.startsWith(u"file:"_s, Qt::CaseInsensitive))
        return QUrl(text);
    return QUrl::fromLocalFile(text);
}

namespace {

/// The name a content URI carries in itself: the end of its last decoded segment
/// ("primary:Download/Notatki" -> "Notatki"). Opaque ids give their number ("msf:1000012345" ->
/// "1000012345") — a placeholder until the provider names the document.
QString nameInUri(const QUrl &url)
{
    const QString last = QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).section(u'/', -1).toUtf8());
    return last.section(u':', -1).section(u'/', -1);
}

} // namespace

QString displayNameOf(const QUrl &url)
{
    const QString fsPath = fileSystemPathOf(url);
    QString name = QFileInfo(fsPath).fileName();
    if (name.isEmpty() || name.contains(u'%') || name.contains(u':'))
        name = nameInUri(url); // a content URI without a display name
    return name.isEmpty() ? url.toString(QUrl::RemoveUserInfo) : name;
}

bool isSkippedFolder(const QString &name)
{
    static const QSet<QString> skipped = {
        u".git"_s,         u".hg"_s,         u".svn"_s,        u"node_modules"_s, u".trash"_s, u".Trash"_s,
        u".stversions"_s,  u".stfolder"_s,   u"__MACOSX"_s,    u".brulion"_s,     u".cache"_s,
    };
    return skipped.contains(name) || name.startsWith(u".Trash-"_s);
}

bool writeToUrl(const QUrl &url, const QByteArray &data, QString *error)
{
    const QString path = fileSystemPathOf(url);
    if (path.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("brulion::ioviews::FileGatherer", "No file was chosen.");
        return false;
    }
    if (url.isLocalFile() || url.scheme().isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            if (error)
                *error = file.errorString();
            return false;
        }
        return true;
    }
    // Android documents cannot be replaced by rename: write in place.
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();
    return true;
}

// ---- FileGatherer ---------------------------------------------------------------------------------

FileGatherer::FileGatherer(QObject *parent) : QObject(parent), m_documents(platformDocumentProvider())
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &FileGatherer::step);
}

void FileGatherer::setDocumentProvider(std::shared_ptr<const DocumentProvider> provider)
{
    m_documents = provider ? std::move(provider) : platformDocumentProvider();
}

void FileGatherer::start(const QList<QUrl> &urls)
{
    cancel();
    const int generation = ++m_generation;
    m_running = true;
    m_reading = false;
    m_stopped = false;
    m_pending.clear();
    m_batch.clear();
    m_next = 0;
    m_files.clear();
    m_warnings.clear();
    m_bytes = 0;
    // A document's display name needs a query of its provider: the listing brings it.
    if (urls.size() != 1)
        m_sourceName = tr("%n file(s)", "", int(urls.size()));
    else
        m_sourceName = isDocumentUrl(urls.first()) ? nameInUri(urls.first()) : displayNameOf(urls.first());
    Q_EMIT progress(0, 0);
    if (!m_running)
        return; // cancelled from a progress handler
    // Listing (folders walked, documents asked for their names) off the GUI thread.
    const GatherLimits limits = m_limits;
    const std::shared_ptr<const DocumentProvider> documents = m_documents;
    runInBackground<Listing>(
            this, [urls, limits, documents] { return list(urls, limits, documents); },
            [this, generation](Listing listing) {
                if (generation == m_generation && m_running)
                    listed(listing);
            });
}

void FileGatherer::cancel()
{
    if (!m_running)
        return;
    ++m_generation;
    m_timer.stop();
    m_running = false;
    m_reading = false;
    m_pending.clear();
    m_batch.clear();
    m_files.clear();
    Q_EMIT cancelled();
}

FileGatherer::Listing FileGatherer::list(const QList<QUrl> &urls, const GatherLimits &limits,
                                         const std::shared_ptr<const DocumentProvider> &documents)
{
    Listing out;
    for (const QUrl &url : urls) {
        if (isDocumentUrl(url))
            collectDocument(url, limits, *documents, out);
        else
            collectLocal(url, limits, out);
    }
    // Two picked files of the same name (from different folders) stay two files.
    QSet<QString> used;
    for (Pending &p : out.pending)
        p.relative = uniqueRelative(used, p.relative);
    if (urls.size() == 1 && isDocumentUrl(urls.first()) && !out.pending.isEmpty()) {
        // The document's display name ("Eksport Notion.zip"), not the id in its URI.
        const QString &first = out.pending.first().relative;
        out.sourceName = out.pending.size() == 1 ? first : first.section(u'/', 0, 0);
    }
    return out;
}

void FileGatherer::collectLocal(const QUrl &url, const GatherLimits &limits, Listing &out)
{
    const QString root = fileSystemPathOf(url);
    const QFileInfo rootInfo(root);
    if (!rootInfo.exists()) {
        out.warnings.append(tr("“%1” could not be found.").arg(displayNameOf(url)));
        return;
    }
    if (!rootInfo.isDir()) {
        out.pending.append(Pending{root, displayNameOf(url), rootInfo.size(), false});
        return;
    }
    // A folder: walk it ourselves so skipped folders are never entered and symlinked folders
    // (loops, duplicates) are not followed.
    struct Dir {
        QString fsPath;
        QString relative;
    };
    // A symlinked file is read only when it stays inside the picked folder: a downloaded "vault"
    // with "Notatka.md -> ~/.ssh/id_rsa" must not copy the key into the notebook (and on to the
    // sync server).
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    const auto insideRoot = [&canonicalRoot](const QFileInfo &entry) {
        const QString target = entry.canonicalFilePath();
        return !canonicalRoot.isEmpty() && !target.isEmpty() && target.startsWith(canonicalRoot + u'/');
    };
    QList<Dir> stack{ Dir{ root, displayNameOf(url) } };
    bool tooMany = false;
    while (!stack.isEmpty() && !tooMany) {
        const Dir dir = stack.takeLast();
        const QFileInfoList entries = QDir(dir.fsPath).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString relative = dir.relative + u'/' + entry.fileName();
            if (entry.isDir()) {
                if (!entry.isSymLink() && !isSkippedFolder(entry.fileName()))
                    stack.append(Dir{ entry.filePath(), relative });
                continue;
            }
            // Pipes, sockets and devices hold no notes (reading a pipe would block forever).
            if (!entry.isFile())
                continue;
            if (entry.isSymLink() && !insideRoot(entry)) {
                out.warnings.append(
                        tr("“%1” is a link to a file outside the chosen folder and was skipped.").arg(relative));
                continue;
            }
            if (out.pending.size() >= limits.maxFiles) {
                out.warnings.append(
                        tr("The folder has more than %n file(s); only the first ones were read.", "", limits.maxFiles));
                tooMany = true;
                break;
            }
            out.pending.append(Pending{entry.filePath(), relative, entry.size(), false});
        }
    }
}

void FileGatherer::collectDocument(const QUrl &url, const GatherLimits &limits, const DocumentProvider &documents,
                                   Listing &out)
{
    QString error;
    const std::optional<DocumentProvider::Entry> root = documents.stat(url, &error);
    if (!root) {
        out.warnings.append(tr("“%1” could not be opened: %2").arg(displayNameOf(url), error));
        return;
    }
    QString rootName = safeSegment(root->name);
    if (rootName.isEmpty())
        rootName = safeSegment(displayNameOf(url));
    if (rootName.isEmpty())
        rootName = tr("Picked folder");
    if (!root->isDir) {
        if (root->isVirtual)
            out.warnings.append(tr("“%1” exists only online (like a Google Docs file) and was skipped: export it "
                                   "as a file first.")
                                        .arg(rootName));
        else
            out.pending.append(Pending{root->uri, rootName, root->size, true});
        return;
    }
    // A picked folder (a document tree): walked folder by folder, like a local folder. Document
    // trees have no links, but a misbehaving provider must not send the walk round in circles.
    struct Dir {
        DocumentProvider::Entry entry;
        QString relative;
        int depth = 0;
    };
    constexpr int MaxDepth = 64;
    QSet<QString> seen{root->uri};
    QList<Dir> stack{Dir{*root, rootName, 0}};
    bool tooMany = false;
    while (!stack.isEmpty() && !tooMany) {
        const Dir dir = stack.takeLast();
        std::optional<QList<DocumentProvider::Entry>> entries = documents.children(dir.entry, &error);
        if (!entries) {
            out.warnings.append(tr("The folder “%1” could not be read: %2").arg(dir.relative, error));
            continue;
        }
        std::sort(entries->begin(), entries->end(),
                  [](const DocumentProvider::Entry &a, const DocumentProvider::Entry &b) { return a.name < b.name; });
        for (const DocumentProvider::Entry &entry : std::as_const(*entries)) {
            const QString name = safeSegment(entry.name);
            if (name.isEmpty())
                continue;
            const QString relative = dir.relative + u'/' + name;
            if (entry.isDir) {
                if (!isSkippedFolder(name) && dir.depth < MaxDepth && !seen.contains(entry.uri)) {
                    seen.insert(entry.uri);
                    stack.append(Dir{entry, relative, dir.depth + 1});
                }
                continue;
            }
            if (entry.isVirtual) {
                out.warnings.append(tr("“%1” exists only online (like a Google Docs file) and was skipped: export "
                                       "it as a file first.")
                                            .arg(relative));
                continue;
            }
            if (out.pending.size() >= limits.maxFiles) {
                out.warnings.append(
                        tr("The folder has more than %n file(s); only the first ones were read.", "", limits.maxFiles));
                tooMany = true;
                break;
            }
            out.pending.append(Pending{entry.uri, relative, entry.size, true});
        }
    }
}

void FileGatherer::listed(const Listing &listing)
{
    m_pending = listing.pending;
    m_warnings = listing.warnings;
    if (!listing.sourceName.isEmpty())
        m_sourceName = listing.sourceName;
    Q_EMIT progress(0, int(m_pending.size()));
    if (!m_running)
        return; // cancelled from a progress handler
    m_timer.start();
}

QList<FileGatherer::Read> FileGatherer::readBatch(const QList<Pending> &batch, qint64 maxFileBytes,
                                                  const std::shared_ptr<const DocumentProvider> &documents)
{
    QList<Read> reads;
    reads.reserve(batch.size());
    for (const Pending &p : batch) {
        Read r;
        if (p.document) {
            DocumentProvider::Entry entry;
            entry.uri = p.fsPath;
            entry.name = p.relative.section(u'/', -1);
            entry.size = p.size;
            std::optional<QByteArray> data = documents->read(entry, maxFileBytes, &r.error);
            r.ok = data.has_value();
            if (data)
                r.data = std::move(*data);
        } else {
            QFile file(p.fsPath);
            if (file.open(QIODevice::ReadOnly)) {
                std::optional<QByteArray> data = readLimited(file, maxFileBytes, p.size, &r.error);
                r.ok = data.has_value();
                if (data)
                    r.data = std::move(*data);
            } else {
                r.error = file.errorString();
            }
        }
        reads.append(std::move(r));
    }
    return reads;
}

void FileGatherer::step()
{
    if (!m_running || m_reading)
        return;
    // A batch: at most m_chunk files and about 32 MB, the sizes the listing knows checked first.
    constexpr qint64 BytesPerBatch = 32LL * 1024 * 1024;
    qint64 batchBytes = 0;
    int planned = 0;
    m_batch.clear();
    while (m_next < m_pending.size() && planned < m_chunk && batchBytes < BytesPerBatch && !m_stopped) {
        const Pending &p = m_pending.at(m_next++);
        ++planned;
        if (p.size > m_limits.maxFileBytes) {
            m_warnings.append(tr("“%1” (%2) is too large and was skipped.").arg(p.relative, sizeText(p.size)));
            continue;
        }
        if (m_bytes + batchBytes + qMax<qint64>(p.size, 0) > m_limits.maxTotalBytes) {
            stopForTotal(); // the files planned before still come
            break;
        }
        m_batch.append(p);
        batchBytes += qMax<qint64>(p.size, 0);
    }
    if (m_batch.isEmpty()) {
        merge({});
        return;
    }
    m_reading = true;
    const int generation = m_generation;
    const QList<Pending> batch = m_batch;
    const qint64 maxFileBytes = m_limits.maxFileBytes;
    const std::shared_ptr<const DocumentProvider> documents = m_documents;
    runInBackground<QList<Read>>(
            this, [batch, maxFileBytes, documents] { return readBatch(batch, maxFileBytes, documents); },
            [this, generation](QList<Read> reads) {
                if (generation != m_generation || !m_running)
                    return;
                m_reading = false;
                merge(reads);
            });
}

void FileGatherer::merge(const QList<Read> &reads)
{
    for (qsizetype i = 0; i < reads.size() && i < m_batch.size(); ++i) {
        const Pending &p = m_batch.at(i);
        const Read &r = reads.at(i);
        if (!r.ok) {
            m_warnings.append(tr("“%1” could not be read: %2").arg(p.relative, r.error));
            continue;
        }
        // Sizes the listing did not know (some document providers) are checked now.
        if (r.data.size() > m_limits.maxFileBytes) {
            m_warnings.append(tr("“%1” (%2) is too large and was skipped.").arg(p.relative, sizeText(r.data.size())));
            continue;
        }
        if (m_bytes + r.data.size() > m_limits.maxTotalBytes) {
            stopForTotal();
            break;
        }
        m_bytes += r.data.size();
        m_files.append(io::InputFile{p.relative, r.data});
    }
    m_batch.clear();
    Q_EMIT progress(int(m_next), int(m_pending.size()));
    if (!m_running)
        return; // cancelled from a progress handler
    if (m_next < m_pending.size() && !m_stopped) {
        m_timer.start();
        return;
    }
    finish();
}

void FileGatherer::stopForTotal()
{
    if (!m_stopped)
        m_warnings.append(tr("Only the first %1 were read: the selection is too large to import at once.")
                                  .arg(sizeText(m_limits.maxTotalBytes)));
    m_stopped = true;
}

void FileGatherer::finish()
{
    m_running = false;
    const QList<io::InputFile> files = std::exchange(m_files, {});
    m_pending.clear();
    Q_EMIT finished(files, m_warnings, m_sourceName, m_bytes);
}

} // namespace brulion::ioviews

#include "sync/foldertransport.h"

#include "sync/davxml.h"
#include "sync/url.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QIODevice>
#include <QMetaObject>
#include <QMutex>
#include <QWaitCondition>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::sync {

QString deviceFolderUrl(const QString &id)
{
    return QString(DeviceFolderScheme) + u"://device/"_s + QString::fromLatin1(QUrl::toPercentEncoding(id));
}

QString deviceFolderId(const QString &address)
{
    return QString::fromLatin1(QCryptographicHash::hash(address.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}

bool isDeviceFolderUrl(const QString &url)
{
    return url.startsWith(QString(DeviceFolderScheme) + u"://"_s, Qt::CaseInsensitive);
}

namespace {

HttpResponse respond(int status, const QByteArray &body = {}, QHash<QByteArray, QByteArray> headers = {})
{
    HttpResponse r;
    r.status = status;
    r.body = body;
    r.headers = std::move(headers);
    return r;
}

/// A storage failure as the status a WebDAV server would answer; `missing` for NotFound.
HttpResponse failure(const FolderError &error, int missing = 404)
{
    int status = 500;
    switch (error.code) {
    case FolderErrorCode::None: status = 500; break;
    case FolderErrorCode::NotFound: status = missing; break;
    case FolderErrorCode::Exists: status = 405; break;
    case FolderErrorCode::NotAllowed: status = 403; break;
    case FolderErrorCode::Unavailable: status = 503; break;
    case FolderErrorCode::Unsupported: status = 405; break;
    case FolderErrorCode::Failed: status = 500; break;
    }
    return respond(status, error.message.toUtf8(), {{"content-type", "text/plain; charset=utf-8"}});
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash);
}

QString childOf(const QString &folder, const QString &name)
{
    return folder.isEmpty() ? name : folder + u'/' + name;
}

/// The path of `url` relative to the base folder (decoded), std::nullopt outside it.
std::optional<QString> relativeOf(const QUrl &url, const QString &basePathname)
{
    return relativeTo(urlPathname(url.toString(QUrl::FullyEncoded)), basePathname);
}

/// One resource of a multistatus answer.
DavResource davResource(const QString &basePathname, const QString &path, const FolderEntry &entry)
{
    DavResource res;
    res.href = basePathname + u'/' + encodePath(path);
    if (entry.isDir && !res.href.endsWith(u'/'))
        res.href += u'/';
    res.isCollection = entry.isDir;
    res.etag = FolderDavTransport::etagOf(entry);
    res.mtime = entry.mtime;
    res.size = entry.isDir ? 0 : entry.size;
    return res;
}

bool validName(const QString &name)
{
    return !name.isEmpty() && name != u"." && name != u".." && !name.contains(u'/') && !name.contains(QChar(0));
}

/// The children of the folder `path` appended to a listing (names no WebDAV path can carry left
/// out).
FolderError appendChildren(FolderStorage &storage, const QString &basePathname, const QString &path,
                           QList<DavResource> &resources)
{
    const FolderListResult children = storage.list(path);
    for (const FolderEntry &child : children.entries) {
        if (validName(child.name))
            resources.append(davResource(basePathname, childOf(path, child.name), child));
    }
    return children.error;
}

HttpResponse propfind(FolderStorage &storage, const QString &basePathname, const HttpRequest &request,
                      const QString &path)
{
    const QByteArray depth = request.hasHeader("depth") ? request.header("depth").trimmed() : QByteArray("infinity");
    if (depth != "0" && depth != "1")
        return respond(403, "Depth infinity not allowed");
    const FolderStatResult self = storage.stat(path);
    if (self.error.isError())
        return failure(self.error);
    if (!self.entry)
        return respond(404, "Not found");
    QList<DavResource> resources{davResource(basePathname, path, *self.entry)};
    if (depth == "1" && self.entry->isDir) {
        if (const FolderError error = appendChildren(storage, basePathname, path, resources); error.isError())
            return failure(error);
    }
    return respond(207, writeMultistatus(resources), {{"content-type", "application/xml; charset=utf-8"}});
}

/// The headers of a file's body read whole or in pieces: its length, and the ETag of the entry
/// stat() saw — unless the file changed meanwhile (then none, and the engine asks again before it
/// trusts this version).
QHash<QByteArray, QByteArray> bodyHeaders(const FolderEntry &entry, qint64 read)
{
    QHash<QByteArray, QByteArray> headers{{"content-length", QByteArray::number(read)}};
    if (read == entry.size)
        headers.insert("etag", quoteEtag(FolderDavTransport::etagOf(entry)).toUtf8());
    return headers;
}

/// GET into the request's bodySink: the response's body stays empty.
HttpResponse getInPieces(FolderStorage &storage, const HttpRequest &request, const QString &path,
                         const FolderEntry &entry)
{
    qint64 read = 0;
    bool refused = false;
    const FolderError error = storage.readInto(path, request.bodyLimit(), [&](QByteArrayView piece) {
        read += piece.size();
        refused = !request.bodySink(piece);
        return !refused;
    });
    if (refused) {
        HttpResponse stopped;
        stopped.networkError = u"The answer could not be kept"_s;
        return stopped;
    }
    if (error.isError())
        return failure(error);
    return respond(200, {}, bodyHeaders(entry, read));
}

/// The pieces of the request's bodyDevice, exactly its size() bytes: std::nullopt when it ends
/// sooner or cannot be read.
PieceSource bodyPieces(const HttpRequest &request)
{
    QIODevice *device = request.bodyDevice.get();
    auto left = std::make_shared<qint64>(device->size());
    return [device, left]() -> std::optional<QByteArray> {
        if (*left == 0)
            return QByteArray();
        QByteArray piece = device->read(std::min(*left, HttpRequest::StreamPieceBytes));
        if (piece.isEmpty())
            return std::nullopt;
        *left -= piece.size();
        return piece;
    };
}

HttpResponse get(FolderStorage &storage, const HttpRequest &request, const QString &path)
{
    const FolderStatResult self = storage.stat(path);
    if (self.error.isError())
        return failure(self.error);
    if (!self.entry)
        return respond(404, "Not found");
    if (self.entry->isDir)
        return respond(405, "A folder");
    if (self.entry->size > request.bodyLimit())
        return HttpResponse::oversized(request);
    if (request.bodySink)
        return getInPieces(storage, request, path, *self.entry);
    FolderReadResult read = storage.read(path, request.bodyLimit());
    if (read.error.isError())
        return failure(read.error);
    return respond(200, read.data, bodyHeaders(*self.entry, read.data.size()));
}

/// A PUT's folder is not there (409, as a WebDAV server answers), nullopt when it is.
std::optional<HttpResponse> parentMissing(FolderStorage &storage, const QString &path)
{
    const FolderStatResult parent = storage.stat(parentOf(path));
    if (parent.error.isError())
        return failure(parent.error, 409);
    if (!parent.entry || !parent.entry->isDir)
        return respond(409, "Parent missing");
    return std::nullopt;
}

/// The request's If-Match / If-None-Match does not hold for what is there (412).
std::optional<HttpResponse> conditionFails(const HttpRequest &request, const std::optional<FolderEntry> &current)
{
    if (request.hasHeader("if-match")) {
        const QString wanted = normalizeEtag(QString::fromUtf8(request.header("if-match")));
        if (!current || FolderDavTransport::etagOf(*current) != wanted)
            return respond(412, "Changed meanwhile");
    }
    if (request.header("if-none-match").trimmed() == "*" && current)
        return respond(412, "Exists");
    return std::nullopt;
}

/// Why a PUT of `path` cannot be written, nullopt when it can; `current` is what is there.
std::optional<HttpResponse> putRefusal(FolderStorage &storage, const HttpRequest &request, const QString &path,
                                       FolderStatResult &current)
{
    if (path.isEmpty())
        return respond(405, "The folder itself");
    if (std::optional<HttpResponse> missing = parentMissing(storage, path))
        return missing;
    current = storage.stat(path);
    if (current.error.isError())
        return failure(current.error);
    if (current.entry && current.entry->isDir)
        return respond(405, "A folder");
    return conditionFails(request, current.entry);
}

HttpResponse put(FolderStorage &storage, const HttpRequest &request, const QString &path)
{
    FolderStatResult current;
    if (std::optional<HttpResponse> refusal = putRefusal(storage, request, path, current))
        return *refusal;
    const FolderStatResult written =
            request.bodyDevice ? storage.writeFrom(path, bodyPieces(request)) : storage.write(path, request.body);
    if (written.error.isError())
        return failure(written.error, 409);
    QHash<QByteArray, QByteArray> headers;
    if (written.entry)
        headers.insert("etag", quoteEtag(FolderDavTransport::etagOf(*written.entry)).toUtf8());
    return respond(current.entry ? 204 : 201, {}, headers);
}

HttpResponse del(FolderStorage &storage, const HttpRequest &request, const QString &path)
{
    if (path.isEmpty())
        return respond(403, "The folder itself");
    const FolderStatResult current = storage.stat(path);
    if (current.error.isError())
        return failure(current.error);
    if (!current.entry)
        return respond(404, "Not found");
    if (request.hasHeader("if-match")
        && FolderDavTransport::etagOf(*current.entry) != normalizeEtag(QString::fromUtf8(request.header("if-match"))))
        return respond(412, "Changed meanwhile");
    if (const FolderError error = storage.remove(path); error.isError() && !error.is(FolderErrorCode::NotFound))
        return failure(error);
    return respond(204);
}

HttpResponse mkcol(FolderStorage &storage, const QString &path)
{
    const FolderStatResult current = storage.stat(path);
    if (current.error.isError())
        return failure(current.error);
    if (current.entry)
        return respond(405, "Exists");
    if (!path.isEmpty()) {
        if (std::optional<HttpResponse> missing = parentMissing(storage, path))
            return *missing;
    }
    const FolderError error = storage.makeFolder(path);
    if (error.is(FolderErrorCode::Exists))
        return respond(405, "Exists");
    if (error.isError())
        return failure(error, 409);
    return respond(201);
}

HttpResponse move(FolderStorage &storage, const QString &basePathname, const HttpRequest &request,
                  const QString &path)
{
    const QByteArray destinationHeader = request.header("destination");
    if (destinationHeader.isEmpty())
        return respond(400, "Destination missing");
    const std::optional<QString> target =
            relativeOf(QUrl(QString::fromUtf8(destinationHeader), QUrl::TolerantMode), basePathname);
    if (!target)
        return respond(502, "Destination outside the folder");
    if (path.isEmpty() || target->isEmpty())
        return respond(403, "The folder itself");
    if (*target == path || target->startsWith(path + u'/'))
        return respond(403, "Cannot move into itself");
    const FolderStatResult source = storage.stat(path);
    if (source.error.isError())
        return failure(source.error);
    if (!source.entry)
        return respond(404, "Not found");
    if (std::optional<HttpResponse> missing = parentMissing(storage, *target))
        return *missing;
    const bool overwrite = request.header("overwrite").trimmed().toUpper() != "F";
    const FolderStatResult existing = storage.stat(*target);
    if (existing.error.isError())
        return failure(existing.error);
    // (On storage that ignores letter case a rename "a.md" -> "A.md" finds its own source there.)
    const bool taken = existing.entry && target->compare(path, Qt::CaseInsensitive) != 0;
    if (taken && !overwrite)
        return respond(412, "Destination exists");
    const FolderError error = storage.move(path, *target, overwrite);
    if (error.is(FolderErrorCode::Exists))
        return respond(412, "Destination exists");
    if (error.isError())
        return failure(error, 409);
    return respond(taken ? 204 : 201);
}

/// A streamed request's sink and body, kept on the thread that owns the transport: the worker
/// hands each piece over to it and waits — one piece in flight —, so the client's callbacks run
/// where they belong and nothing of theirs is ever destroyed on the worker. A child of the
/// transport, deleted by its answer (or with the transport).
class OwnerSide : public QObject {
public:
    OwnerSide(QObject *transport, const HttpRequest &request, std::shared_ptr<std::atomic<bool>> stopping)
        : QObject(transport)
        , m_sink(request.bodySink)
        , m_device(request.bodyDevice)
        , m_size(m_device ? m_device->size() : 0)
        , m_stopping(std::move(stopping))
    {
    }

    /// `work` on the owner's thread, the worker waiting for it; false when the transport goes
    /// meanwhile (its destructor sets `stopping` before it waits for the worker).
    bool run(const std::function<bool()> &work)
    {
        struct Turn {
            QMutex mutex;
            QWaitCondition ended;
            bool done = false;
            bool result = false;
        };
        auto turn = std::make_shared<Turn>();
        QMetaObject::invokeMethod(this, [turn, &work] {
            const bool result = work();
            const QMutexLocker lock(&turn->mutex);
            turn->done = true;
            turn->result = result;
            turn->ended.wakeAll();
        }, Qt::QueuedConnection);
        QMutexLocker lock(&turn->mutex);
        while (!turn->done) {
            if (m_stopping->load())
                return false; // (the queued call is dropped with this object, `work` never runs)
            turn->ended.wait(&turn->mutex, QDeadlineTimer(20));
        }
        return turn->result;
    }

    /// The worker's copy of the request (on the worker: the device is made there and goes there):
    /// a sink and a body device that go through run().
    void fill(HttpRequest &copy)
    {
        if (m_sink)
            copy.bodySink = [this](QByteArrayView piece) { return run([this, piece] { return m_sink(piece); }); };
        if (m_device)
            copy.bodyDevice = std::make_shared<OwnerReadDevice>(this, m_size);
    }

private:
    /// The body device as the worker reads it: every read is the real device's, on the owner's thread.
    class OwnerReadDevice : public QIODevice {
    public:
        OwnerReadDevice(OwnerSide *side, qint64 size) : m_side(side), m_size(size)
        {
            open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        }
        qint64 size() const override { return m_size; }
        bool isSequential() const override { return true; }

    protected:
        qint64 readData(char *data, qint64 maxSize) override
        {
            qint64 n = -1;
            if (!m_side->run([this, data, maxSize, &n] {
                    n = m_side->m_device->read(data, maxSize);
                    return true;
                }))
                return -1;
            return n;
        }
        qint64 writeData(const char *, qint64) override { return -1; }

    private:
        OwnerSide *m_side;
        qint64 m_size;
    };

    std::function<bool(QByteArrayView)> m_sink;
    std::shared_ptr<QIODevice> m_device;
    qint64 m_size;
    std::shared_ptr<std::atomic<bool>> m_stopping;
};

} // namespace

QString FolderDavTransport::etagOf(const FolderEntry &entry)
{
    if (entry.isDir)
        return u"d"_s + QString::number(entry.mtime, 16);
    return QString::number(entry.mtime, 16) + u'-' + QString::number(entry.size, 16);
}

HttpResponse FolderDavTransport::answer(FolderStorage &storage, const QString &basePathname, const HttpRequest &request)
{
    const std::optional<QString> path = relativeOf(request.url, basePathname);
    if (!path)
        return respond(403, "Outside the folder");
    const QByteArray &method = request.method;
    if (method == "PROPFIND")
        return propfind(storage, basePathname, request, *path);
    if (method == "GET")
        return get(storage, request, *path);
    if (method == "PUT")
        return put(storage, request, *path);
    if (method == "DELETE")
        return del(storage, request, *path);
    if (method == "MKCOL")
        return mkcol(storage, *path);
    if (method == "MOVE")
        return move(storage, basePathname, request, *path);
    return respond(405, "Method not allowed");
}

FolderDavTransport::FolderDavTransport(std::shared_ptr<FolderStorage> storage, const QString &baseUrl, QObject *parent)
    : QObject(parent)
    , m_storage(std::move(storage))
    , m_baseUrl(trimTrailingSlashes(baseUrl.trimmed()))
    , m_basePathname(urlPathname(m_baseUrl))
{
    // One operation at a time: a document provider is asked from one thread only.
    m_pool.setMaxThreadCount(1);
    m_pool.setObjectName(u"brulion-folder-sync"_s);
}

FolderDavTransport::~FolderDavTransport()
{
    m_stopping->store(true); // (a worker waiting for a piece's turn here stops waiting)
    m_pool.clear();
    m_pool.waitForDone();
}

void FolderDavTransport::send(const HttpRequest &request, HttpCallback done)
{
    // What the worker needs of the request: never its callbacks (they belong to this thread).
    HttpRequest copy;
    copy.method = request.method;
    copy.url = request.url;
    copy.headers = request.headers;
    copy.body = request.body;
    copy.maxBodyBytes = request.maxBodyBytes;
    if (m_synchronous || !m_storage) {
        copy.bodySink = request.bodySink;
        copy.bodyDevice = request.bodyDevice;
        HttpResponse response = m_storage ? answer(*m_storage, m_basePathname, copy) : HttpResponse{};
        if (!m_storage)
            response.networkError = u"No folder"_s;
        QMetaObject::invokeMethod(this, [done = std::move(done), response = std::move(response)] { done(response); },
                                  Qt::QueuedConnection);
        return;
    }
    OwnerSide *side = request.bodySink || request.bodyDevice ? new OwnerSide(this, request, m_stopping) : nullptr;
    m_pool.start([this, storage = m_storage, base = m_basePathname, copy = std::move(copy), side,
                  done = std::move(done)]() mutable {
        if (side)
            side->fill(copy);
        HttpResponse response = answer(*storage, base, copy);
        copy = {}; // (its body device and sink go here, before their OwnerSide)
        // (The destructor waits for this job: `this` is alive while it posts.)
        QMetaObject::invokeMethod(this, [side, done = std::move(done), response = std::move(response)] {
            delete side;
            done(response);
        }, Qt::QueuedConnection);
    });
}

} // namespace brulion::sync

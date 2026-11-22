#include "sync/foldertransport.h"

#include "sync/url.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QLocale>
#include <QMetaObject>
#include <QTimeZone>
#include <QUrl>

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

QString escapeXml(QString s)
{
    return s.replace(u'&', u"&amp;"_s).replace(u'<', u"&lt;"_s).replace(u'>', u"&gt;"_s).replace(u'"', u"&quot;"_s);
}

QString httpDate(qint64 ms)
{
    return QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC), u"ddd, dd MMM yyyy HH:mm:ss 'GMT'"_s);
}

/// The path of `url` relative to the base folder (decoded), std::nullopt outside it.
std::optional<QString> relativeOf(const QUrl &url, const QString &basePathname)
{
    return relativeTo(urlPathname(url.toString(QUrl::FullyEncoded)), basePathname);
}

/// One <d:response> of a multistatus answer.
QString davResponse(const QString &basePathname, const QString &path, const FolderEntry &entry)
{
    QString href = basePathname + u'/' + encodePath(path);
    if (entry.isDir && !href.endsWith(u'/'))
        href += u'/';
    QString props = u"<d:getetag>&quot;%1&quot;</d:getetag>"_s.arg(FolderDavTransport::etagOf(entry));
    if (entry.mtime > 0)
        props += u"<d:getlastmodified>%1</d:getlastmodified>"_s.arg(httpDate(entry.mtime));
    if (entry.isDir)
        props += u"<d:resourcetype><d:collection/></d:resourcetype>"_s;
    else
        props += u"<d:resourcetype/><d:getcontentlength>%1</d:getcontentlength>"_s.arg(entry.size);
    return u"<d:response><d:href>%1</d:href><d:propstat><d:prop>%2</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>"_s
            .arg(escapeXml(href), props);
}

bool validName(const QString &name)
{
    return !name.isEmpty() && name != u"." && name != u".." && !name.contains(u'/') && !name.contains(QChar(0));
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
    QString body = davResponse(basePathname, path, *self.entry);
    if (depth == "1" && self.entry->isDir) {
        const FolderListResult children = storage.list(path);
        if (children.error.isError())
            return failure(children.error);
        for (const FolderEntry &child : children.entries) {
            if (validName(child.name))
                body += davResponse(basePathname, childOf(path, child.name), child);
        }
    }
    const QString xml = u"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<d:multistatus xmlns:d=\"DAV:\">%1</d:multistatus>"_s.arg(body);
    return respond(207, xml.toUtf8(), {{"content-type", "application/xml; charset=utf-8"}});
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
    FolderReadResult read = storage.read(path, request.bodyLimit());
    if (read.error.isError())
        return failure(read.error);
    // The ETag of what was read: the entry stat() saw, unless the file changed meanwhile (then
    // none, and the engine asks again before it trusts this version).
    QHash<QByteArray, QByteArray> headers{{"content-length", QByteArray::number(read.data.size())}};
    if (read.data.size() == self.entry->size)
        headers.insert("etag", quoteEtag(FolderDavTransport::etagOf(*self.entry)).toUtf8());
    return respond(200, read.data, headers);
}

HttpResponse put(FolderStorage &storage, const HttpRequest &request, const QString &path)
{
    if (path.isEmpty())
        return respond(405, "The folder itself");
    const FolderStatResult parent = storage.stat(parentOf(path));
    if (parent.error.isError())
        return failure(parent.error, 409);
    if (!parent.entry || !parent.entry->isDir)
        return respond(409, "Parent missing");
    const FolderStatResult current = storage.stat(path);
    if (current.error.isError())
        return failure(current.error);
    if (current.entry && current.entry->isDir)
        return respond(405, "A folder");
    if (request.hasHeader("if-match")) {
        const QString wanted = normalizeEtag(QString::fromUtf8(request.header("if-match")));
        if (!current.entry || FolderDavTransport::etagOf(*current.entry) != wanted)
            return respond(412, "Changed meanwhile");
    }
    if (request.header("if-none-match").trimmed() == "*" && current.entry)
        return respond(412, "Exists");
    const FolderStatResult written = storage.write(path, request.body);
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
        const FolderStatResult parent = storage.stat(parentOf(path));
        if (parent.error.isError())
            return failure(parent.error, 409);
        if (!parent.entry || !parent.entry->isDir)
            return respond(409, "Parent missing");
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
    const FolderStatResult parent = storage.stat(parentOf(*target));
    if (parent.error.isError())
        return failure(parent.error, 409);
    if (!parent.entry || !parent.entry->isDir)
        return respond(409, "Parent missing");
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
        HttpResponse response = m_storage ? answer(*m_storage, m_basePathname, copy) : HttpResponse{};
        if (!m_storage)
            response.networkError = u"No folder"_s;
        QMetaObject::invokeMethod(this, [done = std::move(done), response = std::move(response)] { done(response); },
                                  Qt::QueuedConnection);
        return;
    }
    m_pool.start([this, storage = m_storage, base = m_basePathname, copy = std::move(copy),
                  done = std::move(done)]() mutable {
        HttpResponse response = answer(*storage, base, copy);
        // (The destructor waits for this job: `this` is alive while it posts.)
        QMetaObject::invokeMethod(this, [done = std::move(done), response = std::move(response)] { done(response); },
                                  Qt::QueuedConnection);
    });
}

} // namespace brulion::sync

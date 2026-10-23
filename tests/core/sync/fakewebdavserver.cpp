#include "fakewebdavserver.h"

#include "sync/url.h"

#include <QDateTime>
#include <QLocale>
#include <QStringList>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace synctest {

namespace {

QString encodeHref(const QString &path)
{
    QStringList parts = path.split(u'/');
    for (QString &part : parts) {
        if (!part.isEmpty())
            part = QString::fromLatin1(QUrl::toPercentEncoding(part));
    }
    return parts.join(u'/');
}

QString decodePath(const QUrl &url)
{
    QStringList parts = url.path(QUrl::FullyEncoded).split(u'/');
    for (QString &part : parts)
        part = QUrl::fromPercentEncoding(part.toLatin1());
    QString path = parts.join(u'/');
    while (path.size() > 1 && path.endsWith(u'/'))
        path.chop(1);
    return path.isEmpty() ? u"/"_s : path;
}

QString parentOf(const QString &path)
{
    return path.left(qMax<qsizetype>(0, path.lastIndexOf(u'/')));
}

QString unquote(const QByteArray &etag)
{
    QString s = QString::fromUtf8(etag).trimmed();
    if (s.startsWith(u"W/"_s))
        s = s.mid(2);
    if (s.size() >= 2 && s.startsWith(u'"') && s.endsWith(u'"'))
        s = s.mid(1, s.size() - 2);
    return s;
}

QString escapeXml(QString s)
{
    return s.replace(u'&', u"&amp;"_s).replace(u'<', u"&lt;"_s).replace(u'>', u"&gt;"_s).replace(u'"', u"&quot;"_s);
}

QByteArray quoted(const QString &etag)
{
    return "\"" + etag.toUtf8() + "\"";
}

HttpResponse response(int status, const QByteArray &body = {}, QHash<QByteArray, QByteArray> headers = {})
{
    HttpResponse r;
    r.status = status;
    r.body = body;
    r.headers = std::move(headers);
    return r;
}

} // namespace

QByteArray FakeWebDavServer::LoggedRequest::header(QByteArrayView name) const
{
    for (const auto &[key, value] : headers) {
        if (key.compare(name, Qt::CaseInsensitive) == 0)
            return value;
    }
    return {};
}

FakeWebDavServer::FakeWebDavServer()
    : FakeWebDavServer(Options())
{
}

FakeWebDavServer::FakeWebDavServer(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    // The user's root and its ancestors exist.
    mkdirs(m_options.root);
}

FakeWebDavServer::~FakeWebDavServer() = default;

HttpResponse FakeWebDavServer::noResponse()
{
    return response(-1);
}

HttpResponse FakeWebDavServer::status(int code)
{
    return response(code);
}

qint64 FakeWebDavServer::now() const
{
    if (m_options.now)
        return m_options.now();
    return QDateTime(QDate(2026, 10, 14), QTime(8, 0), QTimeZone::UTC).toMSecsSinceEpoch();
}

QString FakeWebDavServer::url(const QString &folder) const
{
    QString out = m_options.origin + m_options.root;
    if (!folder.isEmpty())
        out += u'/' + encodeHref(folder);
    return out;
}

QString FakeWebDavServer::abs(const QString &rel) const
{
    return rel.isEmpty() ? m_options.root : m_options.root + u'/' + rel;
}

void FakeWebDavServer::send(const HttpRequest &request, HttpCallback done)
{
    const QString path = decodePath(request.url);
    m_log.append(LoggedRequest{request.method, path, request.headers});
    std::optional<HttpResponse> answer;
    const QList<Hook> current = hooks; // hooks may add hooks
    for (const Hook &hook : current) {
        if ((answer = hook(request, path)))
            break;
    }
    if (!answer)
        answer = handle(request, path);
    if (answer->status == -1)
        return; // stalled: never answers
    const QByteArray method = request.method;
    const int inFlight = ++m_inFlight[method];
    m_peak[method] = qMax(m_peak.value(method), inFlight);
    QTimer::singleShot(m_latency.value(method), this, [this, method, done = std::move(done), r = *answer] {
        --m_inFlight[method];
        done(r);
    });
}

HttpResponse FakeWebDavServer::handle(const HttpRequest &request, const QString &path)
{
    if (request.header("authorization") != brulion::sync::basicAuth(m_options.username, m_options.password))
        return response(401, "Unauthorized");
    if (!path.startsWith(m_options.root))
        return response(404);
    if (request.method == "PROPFIND")
        return propfind(request, path);
    if (request.method == "GET")
        return get(path);
    if (request.method == "PUT")
        return put(request, path);
    if (request.method == "DELETE")
        return del(request, path);
    if (request.method == "MKCOL")
        return mkcol(path);
    if (request.method == "MOVE")
        return move(request, path);
    return response(405);
}

HttpResponse FakeWebDavServer::propfind(const HttpRequest &request, const QString &path) const
{
    const QByteArray depth = request.hasHeader("depth") ? request.header("depth") : QByteArray("infinity");
    if (depth == "infinity")
        return response(403, "Depth infinity not allowed");
    const auto found = m_nodes.constFind(path);
    if (found == m_nodes.cend())
        return response(404);
    QList<std::pair<QString, Node>> items{{path, *found}};
    if (depth == "1" && found->isDir) {
        for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
            if (it.key() != path && parentOf(it.key()) == path)
                items.append({it.key(), it.value()});
        }
    }
    const QString &prefix = m_options.prefix;
    const QString p = prefix.isEmpty() ? QString() : prefix + u':';
    const QString ns = prefix.isEmpty() ? u"xmlns=\"DAV:\""_s : u"xmlns:%1=\"DAV:\""_s.arg(prefix);
    QString body;
    for (const auto &[key, node] : items) {
        const QString hrefPath = encodeHref(key) + (node.isDir ? u"/"_s : QString());
        const QString href = m_options.fullUrlHrefs ? m_options.origin + hrefPath : hrefPath;
        const QString modified = QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(node.mtime, QTimeZone::UTC),
                                                       u"ddd, dd MMM yyyy HH:mm:ss 'GMT'"_s);
        const QString etag = m_options.noEtags ? QString()
                                               : u"<%1getetag>&quot;%2&quot;</%1getetag>"_s.arg(p, node.etag);
        const QString lastModified = u"<%1getlastmodified>%2</%1getlastmodified>"_s.arg(p, modified);
        QString props;
        QString missing;
        if (node.isDir) {
            props = lastModified + u"<%1resourcetype><%1collection/></%1resourcetype>"_s.arg(p) + etag;
            missing = u"<%1propstat><%1prop><%1getcontentlength/></%1prop><%1status>HTTP/1.1 404 Not Found</%1status></%1propstat>"_s
                          .arg(p);
        } else {
            props = lastModified + u"<%1getcontentlength>%2</%1getcontentlength><%1resourcetype/>"_s.arg(p).arg(node.data.size())
                + etag;
        }
        body += u"<%1response><%1href>%2</%1href><%1propstat><%1prop>%3</%1prop><%1status>HTTP/1.1 200 OK</%1status></%1propstat>%4</%1response>"_s
                    .arg(p, escapeXml(href), props, missing);
    }
    const QString xml = u"<?xml version=\"1.0\"?>\n<%1multistatus %2 xmlns:s=\"http://sabredav.org/ns\" xmlns:oc=\"http://owncloud.org/ns\">%3</%1multistatus>"_s
                            .arg(p, ns, body);
    return response(207, xml.toUtf8(), {{"content-type", "application/xml; charset=utf-8"}});
}

HttpResponse FakeWebDavServer::get(const QString &path) const
{
    const auto node = m_nodes.constFind(path);
    if (node == m_nodes.cend())
        return response(404);
    if (node->isDir)
        return response(405);
    QHash<QByteArray, QByteArray> headers{{"content-length", QByteArray::number(node->data.size())}};
    if (!m_options.noEtags)
        headers.insert("etag", quoted(node->etag));
    return response(200, node->data, headers);
}

HttpResponse FakeWebDavServer::put(const HttpRequest &request, const QString &path)
{
    const auto node = m_nodes.constFind(path);
    const bool exists = node != m_nodes.cend();
    const auto parent = m_nodes.constFind(parentOf(path));
    if (parent == m_nodes.cend() || !parent->isDir)
        return response(409, "Parent missing");
    if (exists && node->isDir)
        return response(405);
    if (request.hasHeader("if-match") && (!exists || unquote(request.header("if-match")) != node->etag))
        return response(412);
    if (request.header("if-none-match") == "*" && exists)
        return response(412);
    const QString etag = nextEtag();
    const int status = exists ? 204 : 201;
    m_nodes.insert(path, Node{false, request.body, now(), etag});
    QHash<QByteArray, QByteArray> headers;
    if (m_options.putEtag && !m_options.noEtags) {
        headers.insert("etag", quoted(etag));
        headers.insert("oc-etag", quoted(etag));
    }
    return response(status, {}, headers);
}

HttpResponse FakeWebDavServer::del(const HttpRequest &request, const QString &path)
{
    const auto node = m_nodes.constFind(path);
    if (node == m_nodes.cend())
        return response(404);
    if (request.hasHeader("if-match") && unquote(request.header("if-match")) != node->etag)
        return response(412);
    const QString prefix = path + u'/';
    m_nodes.removeIf([&](QMap<QString, Node>::iterator it) { return it.key() == path || it.key().startsWith(prefix); });
    return response(204);
}

HttpResponse FakeWebDavServer::mkcol(const QString &path)
{
    if (m_nodes.contains(path))
        return response(405, "Already exists");
    const auto parent = m_nodes.constFind(parentOf(path));
    if (parent == m_nodes.cend() || !parent->isDir)
        return response(409, "Parent missing");
    m_nodes.insert(path, Node{true, {}, now(), nextEtag()});
    return response(201);
}

HttpResponse FakeWebDavServer::move(const HttpRequest &request, const QString &path)
{
    if (!m_options.supportsMove)
        return response(405, "MOVE not allowed");
    const QByteArray destinationHeader = request.header("destination");
    if (destinationHeader.isEmpty())
        return response(400, "Destination missing");
    const QString target = decodePath(QUrl(QString::fromUtf8(destinationHeader), QUrl::TolerantMode));
    if (!target.startsWith(m_options.root + u'/'))
        return response(502, "Destination on another server");
    const auto source = m_nodes.constFind(path);
    if (source == m_nodes.cend() || path == m_options.root)
        return response(404);
    if (target == path || target.startsWith(path + u'/'))
        return response(403, "Cannot move into itself");
    const auto parent = m_nodes.constFind(parentOf(target));
    if (parent == m_nodes.cend() || !parent->isDir)
        return response(409, "Parent missing");
    const bool overwrite = request.header("overwrite") != "F";
    const bool exists = m_nodes.contains(target);
    if (exists && !overwrite)
        return response(412, "Destination exists");
    if (exists)
        m_nodes.removeIf([&](QMap<QString, Node>::iterator it) {
            return it.key() == target || it.key().startsWith(target + u'/');
        });
    moveNodes(path, target);
    return response(exists ? 204 : 201);
}

void FakeWebDavServer::moveNodes(const QString &fromAbs, const QString &toAbs)
{
    const QString prefix = fromAbs + u'/';
    QList<std::pair<QString, Node>> moved;
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        if (it.key() == fromAbs || it.key().startsWith(prefix))
            moved.append({toAbs + it.key().mid(fromAbs.size()), it.value()});
    }
    m_nodes.removeIf(
            [&](QMap<QString, Node>::iterator it) { return it.key() == fromAbs || it.key().startsWith(prefix); });
    for (auto &[key, node] : moved) {
        if (!m_options.moveKeepsEtags)
            node.etag = nextEtag();
        m_nodes.insert(key, node);
    }
}

bool FakeWebDavServer::moveNode(const QString &fromRel, const QString &toRel)
{
    const QString from = abs(fromRel);
    const QString to = abs(toRel);
    if (!m_nodes.contains(from) || m_nodes.contains(to))
        return false;
    mkdirs(parentOf(to));
    moveNodes(from, to);
    return true;
}

QString FakeWebDavServer::nextEtag()
{
    return u"e%1"_s.arg(++m_etagCounter, 6, 16, QLatin1Char('0'));
}

void FakeWebDavServer::mkdirs(const QString &absPath)
{
    QString current;
    for (const QString &segment : absPath.split(u'/', Qt::SkipEmptyParts)) {
        current += u'/' + segment;
        if (!m_nodes.contains(current))
            m_nodes.insert(current, Node{true, {}, now(), nextEtag()});
    }
}

void FakeWebDavServer::putFile(const QString &rel, const QByteArray &content)
{
    const QString p = abs(rel);
    mkdirs(parentOf(p));
    m_nodes.insert(p, Node{false, content, now(), nextEtag()});
}

void FakeWebDavServer::mkdir(const QString &rel)
{
    mkdirs(abs(rel));
}

void FakeWebDavServer::remove(const QString &rel)
{
    const QString p = abs(rel);
    const QString prefix = p + u'/';
    m_nodes.removeIf([&](QMap<QString, Node>::iterator it) { return it.key() == p || it.key().startsWith(prefix); });
}

std::optional<QString> FakeWebDavServer::text(const QString &rel) const
{
    const std::optional<QByteArray> data = bytes(rel);
    return data ? std::optional<QString>(QString::fromUtf8(*data)) : std::nullopt;
}

std::optional<QByteArray> FakeWebDavServer::bytes(const QString &rel) const
{
    const auto node = m_nodes.constFind(abs(rel));
    if (node == m_nodes.cend() || node->isDir)
        return std::nullopt;
    return node->data;
}

QString FakeWebDavServer::etag(const QString &rel) const
{
    return m_nodes.value(abs(rel)).etag;
}

bool FakeWebDavServer::isDir(const QString &rel) const
{
    const auto node = m_nodes.constFind(abs(rel));
    return node != m_nodes.cend() && node->isDir;
}

void FakeWebDavServer::touchEtag(const QString &rel)
{
    const auto node = m_nodes.find(abs(rel));
    if (node != m_nodes.end())
        node->etag = nextEtag();
}

QMap<QString, QString> FakeWebDavServer::files(const QString &folder) const
{
    const QString base = abs(folder) + u'/';
    QMap<QString, QString> out;
    for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
        if (!it->isDir && it.key().startsWith(base))
            out.insert(it.key().mid(base.size()), QString::fromUtf8(it->data));
    }
    return out;
}

QList<FakeWebDavServer::LoggedRequest> FakeWebDavServer::requests(const QByteArray &method) const
{
    if (method.isEmpty())
        return m_log;
    QList<LoggedRequest> out;
    for (const LoggedRequest &r : m_log) {
        if (r.method == method)
            out.append(r);
    }
    return out;
}

} // namespace synctest

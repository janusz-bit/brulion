#include "publish/ocs.h"

#include "log/categories.h"
#include "sync/url.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

const QString kSharesPath = u"/ocs/v2.php/apps/files_sharing/api/v1/shares"_s;
/// OCS's share type of a public link.
constexpr int kPublicLink = 3;
/// Read-only (OCS permission "read").
constexpr int kReadOnly = 1;

/// The server's address from what the user typed (sync::nextcloudDavUrl's rules: a scheme when
/// none, the web UI's and DAV's parts dropped).
QString serverBase(const QString &server)
{
    const QString dav = sync::nextcloudDavUrl(server, u"x"_s, {});
    const qsizetype at = dav.indexOf(u"/remote.php/dav/files/"_s);
    return at < 0 ? sync::trimTrailingSlashes(dav) : dav.left(at);
}

QDate dateOf(const QJsonValue &value)
{
    // "2026-12-31 00:00:00" (Nextcloud) or "2026-12-31".
    return QDate::fromString(value.toString().left(10), Qt::ISODate);
}

Share shareOf(const QJsonObject &o)
{
    Share s;
    const QJsonValue id = o.value(u"id"_s);
    s.id = id.isDouble() ? QString::number(id.toInteger()) : id.toString();
    s.url = o.value(u"url"_s).toString();
    s.token = o.value(u"token"_s).toString();
    s.path = o.value(u"path"_s).toString();
    s.expires = dateOf(o.value(u"expiration"_s));
    // A link with a password names it (hashed) in `password` or `share_with`.
    const auto named = [&o](const QString &key) {
        const QJsonValue v = o.value(key);
        return v.isString() && !v.toString().isEmpty();
    };
    s.hasPassword = named(u"password"_s) || named(u"share_with"_s);
    return s;
}

int shareType(const QJsonObject &o)
{
    const QJsonValue v = o.value(u"share_type"_s);
    return v.isDouble() ? v.toInt() : v.toString().toInt();
}

} // namespace

QString OcsError::code() const
{
    if (status == 0)
        return timedOut ? u"timeout"_s : u"network"_s;
    if (status == 401)
        return u"auth"_s;
    if (status == 403 && ocsStatus != 403)
        return u"forbidden"_s;
    if (status == 404 || ocsStatus == 404)
        return u"not-found"_s;
    if (status >= 500)
        return u"server"_s;
    if (status == 200 && ocsStatus == 0)
        return u"parse"_s;
    return u"share"_s;
}

QString sharePath(const QString &folder)
{
    QString path = folder.trimmed();
    while (path.startsWith(u'/'))
        path.remove(0, 1);
    while (path.endsWith(u'/'))
        path.chop(1);
    return u'/' + path;
}

OcsShareClient::OcsShareClient(const QString &server, const QString &username, const QString &password,
                               sync::HttpTransport *transport, QObject *parent)
    : QObject(parent)
    , m_server(serverBase(server))
    , m_authorization(sync::basicAuth(username, password))
    , m_secretPassword(password)
    , m_secretToken(QString::fromLatin1(m_authorization.mid(m_authorization.indexOf(' ') + 1)))
    , m_transport(transport)
{ }

OcsShareClient::~OcsShareClient() = default;

void OcsShareClient::call(const QByteArray &method, const QString &path, const QList<std::pair<QString, QString>> &form,
                          std::function<void(const std::optional<OcsError> &, const QJsonValue &)> done)
{
    sync::HttpRequest request;
    request.method = method;
    QUrlQuery query;
    QUrlQuery body;
    for (const auto &[key, value] : form)
        (method == "GET" ? query : body).addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    query.addQueryItem(u"format"_s, u"json"_s);
    QUrl url(m_server + kSharesPath + path);
    url.setQuery(query);
    request.url = url;
    request.timeoutMs = m_timeoutMs;
    request.headers = {{"Authorization", m_authorization},
                       {"OCS-APIRequest", "true"},
                       {"Accept", "application/json"},
                       {"X-Requested-With", "XMLHttpRequest"}};
    if (method != "GET" && method != "DELETE") {
        request.headers.emplaceBack("Content-Type", "application/x-www-form-urlencoded");
        request.body = body.query(QUrl::FullyEncoded).toUtf8();
    }
    QPointer<OcsShareClient> self(this);
    m_transport->send(request, [self, method, done = std::move(done)](const sync::HttpResponse &response) {
        if (!self)
            return;
        const QJsonObject ocs = QJsonDocument::fromJson(response.body).object().value(u"ocs"_s).toObject();
        const QJsonObject meta = ocs.value(u"meta"_s).toObject();
        OcsError error;
        error.status = response.status;
        error.timedOut = response.timedOut;
        error.ocsStatus = meta.value(u"statuscode"_s).toInt();
        error.message =
                logging::redact(response.status == 0 ? response.networkError : meta.value(u"message"_s).toString());
        if (response.status == 0 || response.status >= 400 || error.ocsStatus >= 400 || ocs.isEmpty()) {
            if (error.message.isEmpty())
                error.message = u"HTTP %1"_s.arg(response.status);
            qCDebug(logging::lcNet).noquote() << u"Nextcloud share %1 -> %2 %3"_s.arg(QString::fromLatin1(method))
                                                         .arg(response.status)
                                                         .arg(error.message);
            done(error, {});
            return;
        }
        done(std::nullopt, ocs.value(u"data"_s));
    });
}

void OcsShareClient::find(const QString &path, Done<std::optional<Share>> done)
{
    call("GET", {}, {{u"path"_s, path}, {u"reshares"_s, u"false"_s}},
         [done = std::move(done)](const std::optional<OcsError> &error, const QJsonValue &data) {
             // A folder that is not shared at all: 404 from older servers.
             if (error && error->code() == u"not-found") {
                 done(std::nullopt, std::nullopt);
                 return;
             }
             if (error) {
                 done(error, std::nullopt);
                 return;
             }
             const QJsonArray shares = data.toArray();
             for (const QJsonValue v : shares) {
                 const QJsonObject o = v.toObject();
                 if (shareType(o) == kPublicLink) {
                     done(std::nullopt, shareOf(o));
                     return;
                 }
             }
             done(std::nullopt, std::nullopt);
         });
}

void OcsShareClient::get(const QString &id, Done<std::optional<Share>> done)
{
    call("GET", u'/' + QString::fromLatin1(QUrl::toPercentEncoding(id)), {},
         [done = std::move(done)](const std::optional<OcsError> &error, const QJsonValue &data) {
             if (error && error->code() == u"not-found") {
                 done(std::nullopt, std::nullopt);
                 return;
             }
             if (error) {
                 done(error, std::nullopt);
                 return;
             }
             const QJsonObject o = data.isArray() ? data.toArray().first().toObject() : data.toObject();
             if (o.isEmpty() || shareType(o) != kPublicLink) {
                 done(std::nullopt, std::nullopt);
                 return;
             }
             done(std::nullopt, shareOf(o));
         });
}

void OcsShareClient::create(const QString &path, const QString &password, const QDate &expires, Done<Share> done)
{
    QList<std::pair<QString, QString>> form{{u"path"_s, path},
                                            {u"shareType"_s, QString::number(kPublicLink)},
                                            {u"permissions"_s, QString::number(kReadOnly)}};
    if (!password.isEmpty())
        form.append({u"password"_s, password});
    if (expires.isValid())
        form.append({u"expireDate"_s, expires.toString(Qt::ISODate)});
    call("POST", {}, form, [done = std::move(done)](const std::optional<OcsError> &error, const QJsonValue &data) {
        if (error) {
            done(error, {});
            return;
        }
        const Share share = shareOf(data.toObject());
        if (share.id.isEmpty() || share.url.isEmpty()) {
            OcsError parse;
            parse.status = 200;
            parse.message = u"No share in the answer"_s;
            done(parse, share);
            return;
        }
        done(std::nullopt, share);
    });
}

void OcsShareClient::setPassword(const QString &id, const QString &password, Finished done)
{
    call("PUT", u'/' + QString::fromLatin1(QUrl::toPercentEncoding(id)), {{u"password"_s, password}},
         [done = std::move(done)](const std::optional<OcsError> &error, const QJsonValue &) { done(error); });
}

void OcsShareClient::setExpires(const QString &id, const QDate &expires, Finished done)
{
    call("PUT", u'/' + QString::fromLatin1(QUrl::toPercentEncoding(id)),
         {{u"expireDate"_s, expires.isValid() ? expires.toString(Qt::ISODate) : QString()}},
         [done = std::move(done)](const std::optional<OcsError> &error, const QJsonValue &) { done(error); });
}

void OcsShareClient::remove(const QString &id, Finished done)
{
    call("DELETE", u'/' + QString::fromLatin1(QUrl::toPercentEncoding(id)), {},
         [done = std::move(done)](const std::optional<OcsError> &error, const QJsonValue &) {
             done(error && error->code() == u"not-found" ? std::nullopt : error);
         });
}

} // namespace brulion::publish

#include "fakenextcloud.h"

#include "sync/url.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrlQuery>

using namespace Qt::StringLiterals;
using brulion::sync::HttpCallback;
using brulion::sync::HttpRequest;
using brulion::sync::HttpResponse;

namespace publishtest {

namespace {

const QString kShares = u"/ocs/v2.php/apps/files_sharing/api/v1/shares"_s;

HttpResponse ocsAnswer(int status, int ocsStatus, const QString &message, const QJsonValue &data = {})
{
    HttpResponse r;
    r.status = status;
    r.headers.insert("content-type", "application/json; charset=utf-8");
    const QJsonObject meta{{u"status"_s, ocsStatus < 300 ? u"ok"_s : u"failure"_s},
                           {u"statuscode"_s, ocsStatus},
                           {u"message"_s, message}};
    r.body = QJsonDocument(QJsonObject{{u"ocs"_s, QJsonObject{{u"meta"_s, meta}, {u"data"_s, data}}}})
                     .toJson(QJsonDocument::Compact);
    return r;
}

QMap<QString, QString> fieldsOf(const QString &encoded)
{
    QMap<QString, QString> out;
    // Form encoding: '+' is a space.
    QString text = encoded;
    text.replace(u'+', u' ');
    const QUrlQuery query(text);
    for (const auto &[key, value] : query.queryItems(QUrl::FullyDecoded))
        out.insert(key, value);
    return out;
}

} // namespace

FakeNextcloud::FakeNextcloud(QObject *parent) : QObject(parent) { }

FakeNextcloud::~FakeNextcloud() = default;

int FakeNextcloud::ocsCount(const QByteArray &method) const
{
    return int(std::ranges::count(ocsLog, method, &LoggedOcs::method));
}

void FakeNextcloud::send(const HttpRequest &request, HttpCallback done)
{
    if (!request.url.path().startsWith(u"/ocs/"_s)) {
        dav.send(request, std::move(done));
        return;
    }
    LoggedOcs logged;
    logged.method = request.method;
    const HttpResponse answer = ocs(request, logged);
    ocsLog.append(logged);
    QTimer::singleShot(0, this, [answer, done = std::move(done)] { done(answer); });
}

HttpResponse FakeNextcloud::ocs(const HttpRequest &request, LoggedOcs &logged)
{
    const auto json = [this](const QString &id, const Share &s) {
        return QJsonObject{{u"id"_s, id},
                           {u"share_type"_s, s.type},
                           {u"path"_s, s.path},
                           {u"permissions"_s, 1},
                           {u"token"_s, s.token},
                           {u"url"_s, QString(u"https://"_s + server() + u"/s/"_s + s.token)},
                           {u"expiration"_s,
                            s.expires.isValid() ? QJsonValue(QString(s.expires.toString(Qt::ISODate) + u" 00:00:00"_s))
                                                : QJsonValue()},
                           {u"password"_s, s.password.isEmpty() ? QJsonValue() : QJsonValue(u"$2y$10$hash"_s)}};
    };
    const synctest::FakeWebDavServer::Options &o = dav.options();
    if (request.header("authorization") != brulion::sync::basicAuth(o.username, o.password))
        return ocsAnswer(401, 997, u"Current user is not logged in"_s);
    if (request.header("ocs-apirequest") != "true")
        return ocsAnswer(400, 400, u"CSRF check failed"_s);
    const QString path = request.url.path();
    if (!path.startsWith(kShares))
        return ocsAnswer(404, 404, u"Not found"_s);
    logged.path = path.mid(kShares.size());
    logged.fields = fieldsOf(request.url.query(QUrl::FullyEncoded));
    const QMap<QString, QString> form = fieldsOf(QString::fromUtf8(request.body));
    for (auto it = form.cbegin(); it != form.cend(); ++it)
        logged.fields.insert(it.key(), it.value());
    const QString id = logged.path.mid(1);
    const QByteArray method = request.method;
    if (logged.path.isEmpty() && method == "GET") {
        const QString wanted = logged.fields.value(u"path"_s);
        if (!dav.isDir(wanted.mid(1)))
            return ocsAnswer(404, 404, u"Wrong path, file/folder does not exist"_s);
        QJsonArray list;
        for (auto it = shares.cbegin(); it != shares.cend(); ++it) {
            if (it->path == wanted)
                list.append(json(it.key(), *it));
        }
        return ocsAnswer(200, 200, u"OK"_s, list);
    }
    if (logged.path.isEmpty() && method == "POST") {
        const QString wanted = logged.fields.value(u"path"_s);
        if (!dav.isDir(wanted.mid(1)))
            return ocsAnswer(404, 404, u"Wrong path, file/folder does not exist"_s);
        const QString password = logged.fields.value(u"password"_s);
        if (!password.isEmpty() && password.size() < minPasswordLength)
            return ocsAnswer(400, 400, u"Password needs to be at least %1 characters long."_s.arg(minPasswordLength));
        Share s;
        s.path = wanted;
        s.type = logged.fields.value(u"shareType"_s).toInt();
        s.password = password;
        s.expires = QDate::fromString(logged.fields.value(u"expireDate"_s), Qt::ISODate);
        s.token = u"Tok"_s + QString::number(m_nextId) + u"xYz"_s;
        const QString newId = QString::number(m_nextId++);
        shares.insert(newId, s);
        return ocsAnswer(200, 200, u"OK"_s, json(newId, s));
    }
    if (!shares.contains(id))
        return ocsAnswer(404, 404, u"Wrong share ID, share does not exist"_s);
    Share &s = shares[id];
    if (method == "GET")
        return ocsAnswer(200, 200, u"OK"_s, QJsonArray{json(id, s)});
    if (method == "PUT") {
        if (logged.fields.contains(u"password"_s)) {
            const QString password = logged.fields.value(u"password"_s);
            if (!password.isEmpty() && password.size() < minPasswordLength)
                return ocsAnswer(400, 400,
                                 u"Password needs to be at least %1 characters long."_s.arg(minPasswordLength));
            s.password = password;
        }
        if (logged.fields.contains(u"expireDate"_s))
            s.expires = QDate::fromString(logged.fields.value(u"expireDate"_s), Qt::ISODate);
        return ocsAnswer(200, 200, u"OK"_s, json(id, s));
    }
    if (method == "DELETE") {
        shares.remove(id);
        return ocsAnswer(200, 200, u"OK"_s);
    }
    return ocsAnswer(405, 405, u"Method not allowed"_s);
}

} // namespace publishtest

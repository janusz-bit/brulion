#include "fakemicrosoft.h"

#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

using namespace Qt::StringLiterals;
using brulion::sync::HttpRequest;
using brulion::sync::HttpResponse;

namespace mstodotest {

namespace {

HttpResponse json(int status, const QJsonObject &o)
{
    HttpResponse r;
    r.status = status;
    r.headers.insert("content-type", "application/json; charset=utf-8");
    r.body = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return r;
}

HttpResponse graphError(int status, const QString &code, const QString &message)
{
    return json(status, QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
}

/// Query items of a URL, fully decoded ("$top" -> "100").
QHash<QString, QString> queryOf(const QUrl &url)
{
    QHash<QString, QString> out;
    for (const QString &item : url.query(QUrl::FullyDecoded).split(u'&', Qt::SkipEmptyParts))
        out.insert(item.section(u'=', 0, 0), item.section(u'=', 1));
    return out;
}

/// Fields of an application/x-www-form-urlencoded body.
QHash<QString, QString> formOf(const QByteArray &body)
{
    QHash<QString, QString> out;
    for (const QByteArray &item : body.split('&')) {
        const qsizetype eq = item.indexOf('=');
        if (eq < 0)
            continue;
        out.insert(QUrl::fromPercentEncoding(item.left(eq)), QUrl::fromPercentEncoding(item.mid(eq + 1)));
    }
    return out;
}

QJsonObject dateTime(const QString &value)
{
    return QJsonObject{{u"dateTime"_s, value}, {u"timeZone"_s, u"UTC"_s}};
}

} // namespace

FakeMicrosoft::FakeMicrosoft() = default;

int FakeMicrosoft::graphRequests() const
{
    int n = 0;
    for (const HttpRequest &r : requests)
        n += r.url.host() == u"graph.microsoft.com";
    return n;
}

QString FakeMicrosoft::transcript() const
{
    QStringList out;
    for (const HttpRequest &r : requests) {
        QString line = QString::fromLatin1(r.method) + u' ' + r.url.toString(QUrl::FullyEncoded);
        for (const auto &[name, value] : r.headers)
            line += u"\n  "_s + QString::fromUtf8(name) + u": "_s + QString::fromUtf8(value);
        line += u"\n  "_s + QString::fromUtf8(r.body);
        out.append(line);
    }
    out.append(m_answers);
    return out.join(u'\n');
}

void FakeMicrosoft::send(const HttpRequest &request, brulion::sync::HttpCallback done)
{
    requests.append(request);
    HttpResponse response = answer(request);
    m_answers.append(QString::fromUtf8(response.body));
    QTimer::singleShot(0, [response, done = std::move(done)] { done(response); });
}

HttpResponse FakeMicrosoft::answer(const HttpRequest &request)
{
    if (request.url.scheme() != u"https") {
        HttpResponse r;
        r.networkError = u"not https"_s;
        return r;
    }
    if (request.url.host() == u"login.microsoftonline.com")
        return login(request);
    if (request.url.host() == u"graph.microsoft.com")
        return graph(request);
    HttpResponse r;
    r.networkError = u"Host not found"_s;
    return r;
}

HttpResponse FakeMicrosoft::login(const HttpRequest &request)
{
    const QStringList path = request.url.path().split(u'/', Qt::SkipEmptyParts);
    const QHash<QString, QString> form = formOf(request.body);
    if (request.method != "POST" || path.size() != 4 || path[1] != u"oauth2" || path[2] != u"v2.0"
        || request.header("Content-Type") != "application/x-www-form-urlencoded")
        return json(404, {{u"error"_s, u"not_found"_s}});
    if (path[3] == u"devicecode") {
        if (!deviceCodeError.isEmpty())
            return json(400, deviceCodeError);
        if (form.value(u"client_id"_s) != clientId)
            return json(400, {{u"error"_s, u"unauthorized_client"_s},
                              {u"error_description"_s,
                               u"AADSTS700016: Application with identifier '%1' was not found in the directory "
                               u"'Microsoft Accounts'.\r\nTrace ID: 0000\r\nTimestamp: 2026-10-27 10:00:00Z"_s.arg(
                                       form.value(u"client_id"_s))},
                              {u"error_codes"_s, QJsonArray{700016}}});
        if (form.value(u"scope"_s) != u"Tasks.Read")
            return json(400, {{u"error"_s, u"invalid_scope"_s}});
        return json(200, {{u"user_code"_s, userCode},
                          {u"device_code"_s, u"fake-device-code"_s},
                          {u"verification_uri"_s, u"https://microsoft.com/devicelogin"_s},
                          {u"expires_in"_s, expiresIn},
                          {u"interval"_s, interval},
                          {u"message"_s, u"To sign in, use a web browser to open the page "
                                         u"https://microsoft.com/devicelogin and enter the code %1 to "
                                         u"authenticate."_s.arg(userCode)}});
    }
    if (path[3] == u"token") {
        if (form.value(u"grant_type"_s) != u"urn:ietf:params:oauth:grant-type:device_code"
            || form.value(u"client_id"_s) != clientId || form.value(u"device_code"_s) != u"fake-device-code")
            return json(400, {{u"error"_s, u"invalid_grant"_s}});
        ++polls;
        if (!pollScript.isEmpty()) {
            const QString step = pollScript.takeFirst();
            if (step == u"network") {
                HttpResponse r;
                r.networkError = u"Connection reset"_s;
                return r;
            }
            if (step.startsWith(u"AADSTS"_s)) {
                const int code = step.mid(6).toInt();
                return json(400, {{u"error"_s, code == 7000218 ? u"invalid_client"_s : u"invalid_request"_s},
                                  {u"error_description"_s, QString(step + u": something went wrong.\r\nTrace ID: 1"_s)},
                                  {u"error_codes"_s, QJsonArray{code}}});
            }
            return json(400, {{u"error"_s, step}, {u"error_description"_s, u"AADSTS70016: pending"_s}});
        }
        return json(200, {{u"token_type"_s, u"Bearer"_s},
                          {u"scope"_s, u"Tasks.Read"_s},
                          {u"expires_in"_s, 3599},
                          {u"access_token"_s, accessToken}});
    }
    return json(404, {{u"error"_s, u"not_found"_s}});
}

HttpResponse FakeMicrosoft::graph(const HttpRequest &request)
{
    if (request.header("Authorization") != "Bearer " + accessToken.toUtf8())
        return graphError(401, u"InvalidAuthenticationToken"_s, u"Access token is empty."_s);
    if (graphStatus < 0 || (dropNetworkAfter >= 0 && m_graphSeen >= dropNetworkAfter)) {
        HttpResponse r;
        r.networkError = u"Connection refused"_s;
        return r;
    }
    ++m_graphSeen;
    if (graphStatus)
        return graphError(graphStatus, u"ErrorCode"_s, graphErrorMessage);
    if (throttle > 0) {
        --throttle;
        HttpResponse r = graphError(429, u"TooManyRequests"_s, u"Please retry again later."_s);
        r.headers.insert("retry-after", "1");
        return r;
    }
    const QStringList path = request.url.path().split(u'/', Qt::SkipEmptyParts);
    const QHash<QString, QString> query = queryOf(request.url);
    if (request.method != "GET" || path.size() < 4 || path.mid(0, 4) != QStringList({u"v1.0"_s, u"me"_s, u"todo"_s, u"lists"_s}))
        return graphError(404, u"ResourceNotFound"_s, u"Not found."_s);
    const QStringList rest = path.mid(4);
    const QString base = u"https://graph.microsoft.com/v1.0/me/todo/lists"_s;
    if (rest.isEmpty()) {
        const int skip = query.value(u"$skiptoken"_s).toInt();
        QJsonArray page;
        for (int i = skip; i < lists.size() && i < skip + listPageSize; ++i)
            page.append(lists[i]);
        QJsonObject o{{u"@odata.context"_s, u"https://graph.microsoft.com/v1.0/$metadata#users('x')/todo/lists"_s},
                      {u"value"_s, page}};
        if (skip + listPageSize < lists.size())
            o.insert(u"@odata.nextLink"_s, QString(base + u"?$skiptoken="_s + QString::number(skip + listPageSize)));
        return json(200, o);
    }
    const QString listId = rest[0];
    if (!tasks.contains(listId) || missingLists.contains(listId) || rest.size() < 2 || rest[1] != u"tasks")
        return graphError(404, u"ResourceNotFound"_s, u"The list was not found."_s);
    const QList<QJsonObject> &listTasks = tasks[listId];
    if (rest.size() == 2) {
        const QString expand = query.value(u"$expand"_s);
        if (!expand.isEmpty() && refuseExpand)
            return graphError(400, u"BadRequest"_s, u"Invalid $expand."_s);
        const int top = query.value(u"$top"_s).toInt();
        const int size = top > 0 ? qMin(top, taskPageSize) : taskPageSize;
        const int skip = query.value(u"$skip"_s).toInt();
        QJsonArray page;
        for (int i = skip; i < listTasks.size() && i < skip + size; ++i) {
            QJsonObject task = listTasks[i];
            const QString id = task.value(u"id"_s).toString();
            if (expand.contains(u"checklistItems"_s) && !ignoreExpand)
                task.insert(u"checklistItems"_s, checklistItems.value(id));
            if (expand.contains(u"linkedResources"_s) && !ignoreExpand)
                task.insert(u"linkedResources"_s, linkedResources.value(id));
            page.append(task);
        }
        QJsonObject o{{u"value"_s, page}};
        if (skip + size < listTasks.size()) {
            const QString host = listId == foreignNextLinkList ? u"https://evil.example.com/v1.0/me/todo/lists"_s : base;
            QString next = host + u'/' + QString::fromLatin1(QUrl::toPercentEncoding(listId)) + u"/tasks?$top="_s
                           + QString::number(size) + u"&$skip="_s + QString::number(skip + size);
            if (!expand.isEmpty())
                next += u"&$expand="_s + expand;
            o.insert(u"@odata.nextLink"_s, next);
        }
        return json(200, o);
    }
    const QString taskId = rest[2];
    bool found = false;
    for (const QJsonObject &t : listTasks)
        found = found || t.value(u"id"_s).toString() == taskId;
    if (!found)
        return graphError(404, u"ResourceNotFound"_s, u"The task was not found."_s);
    if (rest.size() == 4 && rest[3] == u"checklistItems")
        return json(200, {{u"value"_s, checklistItems.value(taskId)}});
    if (rest.size() == 4 && rest[3] == u"linkedResources")
        return json(200, {{u"value"_s, linkedResources.value(taskId)}});
    if (rest.size() == 4 && rest[3] == u"attachments") {
        QJsonArray out;
        for (const QJsonValue &v : attachments.value(taskId)) {
            QJsonObject a = v.toObject();
            if (!attachmentListHasContent)
                a.remove(u"contentBytes"_s);
            out.append(a);
        }
        return json(200, {{u"value"_s, out}});
    }
    if (rest.size() == 5 && rest[3] == u"attachments") {
        for (const QJsonValue &v : attachments.value(taskId)) {
            if (v.toObject().value(u"id"_s).toString() == rest[4])
                return json(200, v.toObject());
        }
    }
    return graphError(404, u"ResourceNotFound"_s, u"Not found."_s);
}

void FakeMicrosoft::addSampleData()
{
    const QString zadania = u"AAMkADZhZG=="_s;
    const QString zakupy = u"AAMkADZak-_x"_s;
    lists = {QJsonObject{{u"id"_s, zadania},
                         {u"displayName"_s, u"Zadania"_s},
                         {u"isOwner"_s, true},
                         {u"isShared"_s, false},
                         {u"wellknownListName"_s, u"defaultList"_s}},
             QJsonObject{{u"id"_s, zakupy},
                         {u"displayName"_s, u"Zakupy ąę"_s},
                         {u"isOwner"_s, true},
                         {u"isShared"_s, true},
                         {u"wellknownListName"_s, u"none"_s}}};
    tasks[zadania] = {
            QJsonObject{{u"id"_s, u"t1=="_s},
                        {u"title"_s, u"Zadzwonić do pani Józefy"_s},
                        {u"status"_s, u"notStarted"_s},
                        {u"importance"_s, u"high"_s},
                        {u"isReminderOn"_s, true},
                        {u"reminderDateTime"_s, dateTime(u"2026-10-27T07:00:00.0000000"_s)},
                        {u"dueDateTime"_s, dateTime(u"2026-10-26T23:00:00.0000000"_s)},
                        {u"createdDateTime"_s, u"2026-10-20T08:12:33.1234567Z"_s},
                        {u"hasAttachments"_s, true},
                        {u"body"_s, QJsonObject{{u"content"_s, u"Numer w notesie — źródło: ąćęłńóśźż"_s},
                                                {u"contentType"_s, u"text"_s}}}},
            QJsonObject{{u"id"_s, u"t2"_s},
                        {u"title"_s, u"Podlać kwiaty"_s},
                        {u"status"_s, u"inProgress"_s},
                        {u"importance"_s, u"normal"_s},
                        {u"isReminderOn"_s, false},
                        {u"hasAttachments"_s, false},
                        {u"dueDateTime"_s, dateTime(u"2026-10-30T23:00:00.0000000"_s)},
                        {u"recurrence"_s,
                         QJsonObject{{u"pattern"_s, QJsonObject{{u"type"_s, u"weekly"_s},
                                                                {u"interval"_s, 1},
                                                                {u"daysOfWeek"_s, QJsonArray{u"saturday"_s}}}},
                                     {u"range"_s, QJsonObject{{u"type"_s, u"noEnd"_s},
                                                              {u"startDate"_s, u"2026-10-31"_s}}}}}},
            QJsonObject{{u"id"_s, u"t3"_s},
                        {u"title"_s, u"Oddać książkę"_s},
                        {u"status"_s, u"completed"_s},
                        {u"importance"_s, u"normal"_s},
                        {u"hasAttachments"_s, false},
                        {u"completedDateTime"_s, dateTime(u"2026-10-25T10:00:00.0000000"_s)}}};
    tasks[zakupy] = {QJsonObject{{u"id"_s, u"t4"_s},
                                 {u"title"_s, u"Chleb"_s},
                                 {u"status"_s, u"notStarted"_s},
                                 {u"importance"_s, u"low"_s},
                                 {u"hasAttachments"_s, false}}};
    checklistItems[u"t1=="_s] = QJsonArray{QJsonObject{{u"displayName"_s, u"Znaleźć numer"_s}, {u"isChecked"_s, true}},
                                           QJsonObject{{u"displayName"_s, u"Umówić się"_s}, {u"isChecked"_s, false}}};
    linkedResources[u"t1=="_s] = QJsonArray{QJsonObject{{u"webUrl"_s, u"https://example.com/kontakt"_s},
                                                        {u"displayName"_s, u"Kontakt"_s},
                                                        {u"applicationName"_s, u"Edge"_s}}};
    attachments[u"t1=="_s] =
            QJsonArray{QJsonObject{{u"@odata.type"_s, u"#microsoft.graph.taskFileAttachment"_s},
                                   {u"id"_s, u"att-1"_s},
                                   {u"name"_s, u"notatka.txt"_s},
                                   {u"contentType"_s, u"text/plain"_s},
                                   {u"size"_s, 12},
                                   {u"contentBytes"_s, QString::fromLatin1(QByteArray("źdźbło").toBase64())}}};
}

} // namespace mstodotest

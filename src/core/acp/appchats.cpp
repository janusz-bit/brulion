#include "acp/appchats.h"

#include "chat/chat.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QUrl>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace brulion::acp {

namespace {

/// A value of the address's query (percent-encoded: a path's spaces, "&", "#" …).
QString queryValue(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

/// `text` cut to its first `max` characters (a surrogate pair kept whole).
QString keepStart(const QString &text, qsizetype max)
{
    if (text.size() <= max)
        return text;
    qsizetype n = max;
    if (n > 0 && text.at(n - 1).isHighSurrogate())
        --n;
    return text.left(n);
}

QString line(const QJsonObject &o, QLatin1StringView key, qsizetype max = 200)
{
    return keepStart(o.value(key).toString().simplified(), max);
}

QString text(const QJsonObject &o, QLatin1StringView key, qsizetype max)
{
    return keepStart(o.value(key).toString(), max);
}

bool knownState(const QString &state)
{
    return state == u"running" || state == u"queued" || state == u"asking" || state == u"failed" || state == u"idle";
}

int count(const QJsonObject &o, QLatin1StringView key)
{
    const double v = o.value(key).toDouble();
    return v >= 0 && v < 1e9 ? int(v) : 0;
}

/// A chats/send-like answer: the error, else the object.
RemoteError errorOf(const sync::HttpResponse &response, QJsonObject *object = nullptr)
{
    return remoteErrorOf(response, object);
}

} // namespace

QString keepEnd(const QString &text, qsizetype max, bool *cut)
{
    if (cut)
        *cut = text.size() > max;
    if (text.size() <= max)
        return text;
    qsizetype from = text.size() - max;
    if (from < text.size() && text.at(from).isLowSurrogate())
        ++from;
    return text.mid(from);
}

bool isRemoteChatPath(const QString &path)
{
    if (path.isEmpty() || path.size() > MaxRemotePathChars || path.contains(QChar(u'\0'))
        || path.contains(u'\\') || path.startsWith(u'/') || !path.endsWith(u".md"_s, Qt::CaseInsensitive)
        || !chat::inFolder(path))
        return false;
    for (const QStringView part : QStringView(path).split(u'/')) {
        if (part.isEmpty() || part == u"." || part == u"..")
            return false;
    }
    return true;
}

QJsonObject toJson(const RemoteChat &c)
{
    QJsonObject o{{u"path"_s, c.path},       {u"name"_s, c.name},         {u"partner"_s, c.partner},
                  {u"partnerName"_s, c.partnerName}, {u"state"_s, c.state}, {u"messages"_s, c.messages}};
    const auto put = [&o](const QString &key, const QString &value) {
        if (!value.isEmpty())
            o.insert(key, value);
    };
    put(u"status"_s, c.status);
    put(u"question"_s, c.question);
    put(u"failure"_s, c.failure);
    put(u"project"_s, c.project);
    put(u"worktree"_s, c.worktree);
    put(u"branch"_s, c.branch);
    put(u"ended"_s, c.ended);
    if (c.waiting > 0)
        o.insert(u"waiting"_s, c.waiting);
    return o;
}

std::optional<RemoteChat> remoteChatFromJson(const QJsonObject &o)
{
    RemoteChat c;
    c.path = o.value(u"path"_s).toString();
    c.state = o.value(u"state"_s).toString();
    if (!isRemoteChatPath(c.path) || !knownState(c.state))
        return std::nullopt;
    c.name = line(o, "name"_L1);
    c.partner = line(o, "partner"_L1);
    c.partnerName = line(o, "partnerName"_L1);
    c.status = line(o, "status"_L1);
    c.question = line(o, "question"_L1, 500);
    c.failure = line(o, "failure"_L1, 2000);
    c.project = line(o, "project"_L1);
    c.worktree = text(o, "worktree"_L1, MaxRemotePathChars);
    c.branch = line(o, "branch"_L1);
    c.ended = line(o, "ended"_L1, 40);
    c.messages = count(o, "messages"_L1);
    c.waiting = count(o, "waiting"_L1);
    if (c.name.isEmpty())
        c.name = c.path.section(u'/', -1).chopped(3);
    return c;
}

QJsonObject toJson(const RemoteChatDetail &d)
{
    QJsonObject o{{u"path"_s, d.path}, {u"state"_s, d.state}, {u"messages"_s, d.messages}};
    if (!d.status.isEmpty())
        o.insert(u"status"_s, d.status);
    if (!d.partial.isEmpty())
        o.insert(u"partial"_s, d.partial);
    if (d.partialCut)
        o.insert(u"partialCut"_s, true);
    if (!d.thoughts.isEmpty())
        o.insert(u"thoughts"_s, d.thoughts);
    if (!d.failure.isEmpty())
        o.insert(u"failure"_s, d.failure);
    if (!d.tools.isEmpty()) {
        QJsonArray tools;
        for (const RemoteChatTool &t : d.tools) {
            QJsonObject tool{{u"title"_s, t.title}, {u"status"_s, t.status}};
            if (!t.kind.isEmpty())
                tool.insert(u"kind"_s, t.kind);
            tools.append(tool);
        }
        o.insert(u"tools"_s, tools);
    }
    if (!d.plan.isEmpty()) {
        QJsonArray plan;
        for (const RemoteChatStep &s : d.plan)
            plan.append(QJsonObject{{u"content"_s, s.content}, {u"status"_s, s.status}});
        o.insert(u"plan"_s, plan);
    }
    if (!d.question.isEmpty()) {
        QJsonObject q{{u"text"_s, d.question.text}, {u"options"_s, QJsonArray::fromStringList(d.question.options)}};
        if (!d.question.details.isEmpty())
            q.insert(u"details"_s, d.question.details);
        if (!d.question.plan.isEmpty())
            q.insert(u"plan"_s, d.question.plan);
        if (!d.question.file.isEmpty())
            q.insert(u"diff"_s, QJsonObject{{u"path"_s, d.question.file},
                                            {u"oldText"_s, d.question.oldText},
                                            {u"newText"_s, d.question.newText}});
        o.insert(u"question"_s, q);
    }
    if (!d.waiting.isEmpty())
        o.insert(u"waiting"_s, QJsonArray::fromStringList(d.waiting));
    return o;
}

std::optional<RemoteChatDetail> remoteChatDetailFromJson(const QJsonObject &o)
{
    RemoteChatDetail d;
    d.path = o.value(u"path"_s).toString();
    d.state = o.value(u"state"_s).toString();
    if (!isRemoteChatPath(d.path) || !knownState(d.state))
        return std::nullopt;
    d.status = line(o, "status"_L1);
    bool cut = false;
    d.partial = keepEnd(o.value(u"partial"_s).toString(), MaxLiveTextChars, &cut);
    d.partialCut = cut || o.value(u"partialCut"_s).toBool();
    d.thoughts = keepEnd(o.value(u"thoughts"_s).toString(), MaxLiveThoughtsChars);
    d.failure = line(o, "failure"_L1, 2000);
    d.messages = count(o, "messages"_L1);
    for (const QJsonValue &v : o.value(u"tools"_s).toArray()) {
        const QJsonObject t = v.toObject();
        RemoteChatTool tool{line(t, "title"_L1, 300), line(t, "status"_L1, 20), line(t, "kind"_L1, 20)};
        if (tool.title.isEmpty())
            continue;
        d.tools.append(tool);
        if (d.tools.size() >= MaxLiveTools)
            break;
    }
    for (const QJsonValue &v : o.value(u"plan"_s).toArray()) {
        const QJsonObject s = v.toObject();
        RemoteChatStep step{line(s, "content"_L1, 500), line(s, "status"_L1, 20)};
        if (step.content.isEmpty())
            continue;
        d.plan.append(step);
        if (d.plan.size() >= MaxLiveSteps)
            break;
    }
    const QJsonObject q = o.value(u"question"_s).toObject();
    for (const QJsonValue &v : q.value(u"options"_s).toArray()) {
        const QString option = v.toString().simplified().left(200);
        if (option.isEmpty() || d.question.options.contains(option))
            continue;
        d.question.options.append(option);
        if (d.question.options.size() >= MaxQuestionOptions)
            break;
    }
    if (!d.question.options.isEmpty()) {
        d.question.text = line(q, "text"_L1, 500);
        d.question.details = text(q, "details"_L1, MaxQuestionChars);
        d.question.plan = text(q, "plan"_L1, MaxLiveTextChars);
        const QJsonObject diff = q.value(u"diff"_s).toObject();
        d.question.file = line(diff, "path"_L1, MaxRemotePathChars);
        if (!d.question.file.isEmpty()) {
            d.question.oldText = text(diff, "oldText"_L1, MaxLiveTextChars);
            d.question.newText = text(diff, "newText"_L1, MaxLiveTextChars);
        }
    }
    for (const QJsonValue &v : o.value(u"waiting"_s).toArray()) {
        const QString message = keepStart(v.toString(), 4000);
        if (message.trimmed().isEmpty())
            continue;
        d.waiting.append(message);
        if (d.waiting.size() >= MaxLiveWaiting)
            break;
    }
    return d;
}

std::optional<RemoteChats> RemoteChats::fromJson(const QJsonObject &o)
{
    const QJsonValue revision = o.value(u"revision"_s);
    if (!revision.isDouble() || revision.toDouble() < 0)
        return std::nullopt;
    RemoteChats chats;
    chats.revision = qint64(revision.toDouble());
    QSet<QString> seen;
    for (const QJsonValue &v : o.value(u"chats"_s).toArray()) {
        std::optional<RemoteChat> c = remoteChatFromJson(v.toObject());
        if (!c || seen.contains(c->path.toLower()))
            continue;
        seen.insert(c->path.toLower());
        chats.chats.append(*c);
        if (chats.chats.size() >= MaxRemoteChats)
            break;
    }
    if (const QJsonValue detail = o.value(u"detail"_s); detail.isObject())
        chats.detail = remoteChatDetailFromJson(detail.toObject());
    return chats;
}

QJsonObject RemoteChats::toJson() const
{
    QJsonArray list;
    for (const RemoteChat &c : chats)
        list.append(acp::toJson(c));
    QJsonObject o{{u"revision"_s, double(revision)}, {u"chats"_s, list}};
    if (detail)
        o.insert(u"detail"_s, acp::toJson(*detail));
    return o;
}

void fetchRemoteChats(sync::HttpTransport *transport, const QUrl &base, const QString &token, qint64 since,
                      int waitSeconds, const QString &path,
                      std::function<void(const RemoteChats &, const RemoteError &)> done)
{
    const int wait = std::clamp(waitSeconds, 0, RemoteWaitSeconds);
    QString endpoint = u"chats?since=%1&wait=%2"_s.arg(QString::number(since), QString::number(wait));
    if (!path.isEmpty())
        endpoint += u"&path="_s + queryValue(path);
    sync::HttpRequest request = remoteRequest("GET", base, endpoint, token, nullptr);
    request.timeoutMs += wait * 1000; // (the long poll's own wait)
    transport->send(request, [done](const sync::HttpResponse &response) {
        QJsonObject o;
        RemoteError e = errorOf(response, &o);
        RemoteChats chats;
        if (!e.failed()) {
            if (std::optional<RemoteChats> read = RemoteChats::fromJson(o))
                chats = std::move(*read);
            else
                e = RemoteError{RemoteError::Kind::Malformed, response.status, QString(), u"no revision in the answer"_s};
        }
        done(chats, e);
    });
}

void fetchRemoteChatPage(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                         std::function<void(const QString &, const RemoteError &)> done)
{
    sync::HttpRequest request =
            remoteRequest("GET", base, u"chats/page?path="_s + queryValue(path), token, nullptr);
    request.maxBodyBytes = std::max<qint64>(request.maxBodyBytes, 2 * MaxRemotePageBytes);
    transport->send(request, [done](const sync::HttpResponse &response) {
        QJsonObject o;
        RemoteError e = errorOf(response, &o);
        QString text;
        if (!e.failed()) {
            if (!o.value(u"text"_s).isString())
                e = RemoteError{RemoteError::Kind::Malformed, response.status, QString(), u"no text in the answer"_s};
            else
                text = o.value(u"text"_s).toString();
        }
        done(text, e);
    });
}

void sendRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                    const QString &text, SendMode mode, std::function<void(const RemoteError &)> done)
{
    QJsonObject body{{u"path"_s, path}, {u"text"_s, text}};
    if (mode == SendMode::Queue)
        body.insert(u"mode"_s, u"queue"_s);
    else if (mode == SendMode::Steer)
        body.insert(u"mode"_s, u"steer"_s);
    transport->send(remoteRequest("POST", base, u"chats/send"_s, token, &body),
                    [done](const sync::HttpResponse &response) { done(errorOf(response)); });
}

void startRemoteThread(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &project,
                       const QString &text, const QString &partner, const QString &worktree, const QString &branch,
                       std::function<void(const QString &, const RemoteError &)> done)
{
    QJsonObject body{{u"project"_s, project}, {u"text"_s, text}};
    if (!partner.isEmpty())
        body.insert(u"partner"_s, partner);
    if (!worktree.isEmpty())
        body.insert(u"worktree"_s, worktree);
    if (!branch.isEmpty())
        body.insert(u"branch"_s, branch);
    transport->send(remoteRequest("POST", base, u"chats/start"_s, token, &body),
                    [done](const sync::HttpResponse &response) {
                        QJsonObject o;
                        RemoteError e = errorOf(response, &o);
                        const QString path = o.value(u"path"_s).toString();
                        if (!e.failed() && !isRemoteChatPath(path))
                            e = RemoteError{RemoteError::Kind::Malformed, response.status, QString(),
                                            u"no conversation in the answer"_s};
                        done(e.failed() ? QString() : path, e);
                    });
}

void stopRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                    std::function<void(const RemoteError &)> done)
{
    const QJsonObject body{{u"path"_s, path}};
    transport->send(remoteRequest("POST", base, u"chats/stop"_s, token, &body),
                    [done](const sync::HttpResponse &response) { done(errorOf(response)); });
}

void answerRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                      const QString &option, std::function<void(const RemoteError &)> done)
{
    const QJsonObject body{{u"path"_s, path}, {u"option"_s, option}};
    transport->send(remoteRequest("POST", base, u"chats/answer"_s, token, &body),
                    [done](const sync::HttpResponse &response) { done(errorOf(response)); });
}

} // namespace brulion::acp

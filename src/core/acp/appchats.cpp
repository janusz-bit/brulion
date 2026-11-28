#include "acp/appchats.h"

#include "chat/chat.h"
#include "util/paths.h"

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

/// POST `endpoint` with `body`; `done` gets the host's error (none: it was done).
void post(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &endpoint,
          const QJsonObject &body, const std::function<void(const RemoteError &)> &done)
{
    transport->send(remoteRequest("POST", base, endpoint, token, &body),
                    [done](const sync::HttpResponse &response) { done(errorOf(response)); });
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
    if (c.held)
        o.insert(u"held"_s, true);
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
    c.held = o.value(u"held"_s).toBool() && c.waiting > 0;
    if (c.name.isEmpty())
        c.name = c.path.section(u'/', -1).chopped(3);
    return c;
}

namespace {

/// `value` under `key` unless it is empty.
void put(QJsonObject &o, const QString &key, const QString &value)
{
    if (!value.isEmpty())
        o.insert(key, value);
}

QJsonArray toJson(const QList<RemoteChatTool> &tools)
{
    QJsonArray list;
    for (const RemoteChatTool &t : tools) {
        QJsonObject tool{{u"title"_s, t.title}, {u"status"_s, t.status}};
        put(tool, u"kind"_s, t.kind);
        list.append(tool);
    }
    return list;
}

QJsonObject toJson(const RemoteChatQuestion &question)
{
    QJsonObject q{{u"text"_s, question.text}, {u"options"_s, QJsonArray::fromStringList(question.options)}};
    put(q, u"details"_s, question.details);
    put(q, u"plan"_s, question.plan);
    if (!question.file.isEmpty())
        q.insert(u"diff"_s, QJsonObject{{u"path"_s, question.file},
                                        {u"oldText"_s, question.oldText},
                                        {u"newText"_s, question.newText}});
    return q;
}

/// The detail's lists (tools, plan, question, waiting, options), those it has.
void putLists(QJsonObject &o, const RemoteChatDetail &d)
{
    if (!d.tools.isEmpty())
        o.insert(u"tools"_s, toJson(d.tools));
    QJsonArray plan;
    for (const RemoteChatStep &s : d.plan)
        plan.append(QJsonObject{{u"content"_s, s.content}, {u"status"_s, s.status}});
    if (!plan.isEmpty())
        o.insert(u"plan"_s, plan);
    if (!d.question.isEmpty())
        o.insert(u"question"_s, toJson(d.question));
    if (!d.waiting.isEmpty())
        o.insert(u"waiting"_s, QJsonArray::fromStringList(d.waiting));
    QJsonArray options;
    for (const RemoteChatOption &option : d.options)
        options.append(acp::toJson(option));
    if (!options.isEmpty())
        o.insert(u"options"_s, options);
}

} // namespace

QJsonObject toJson(const RemoteChatDetail &d)
{
    QJsonObject o{{u"path"_s, d.path}, {u"state"_s, d.state}, {u"messages"_s, d.messages}};
    put(o, u"status"_s, d.status);
    put(o, u"partial"_s, d.partial);
    if (d.partialCut)
        o.insert(u"partialCut"_s, true);
    put(o, u"thoughts"_s, d.thoughts);
    put(o, u"failure"_s, d.failure);
    if (d.held)
        o.insert(u"held"_s, true);
    putLists(o, d);
    return o;
}

QJsonObject toJson(const RemoteChatOption &option)
{
    QJsonObject o{{u"id"_s, option.id}, {u"name"_s, option.name}, {u"current"_s, option.current}};
    if (!option.description.isEmpty())
        o.insert(u"description"_s, option.description);
    if (!option.currentName.isEmpty())
        o.insert(u"currentName"_s, option.currentName);
    if (option.boolean)
        o.insert(u"boolean"_s, true);
    if (!option.choices.isEmpty()) {
        QJsonArray choices;
        for (const RemoteChatChoice &c : option.choices) {
            QJsonObject choice{{u"value"_s, c.value}, {u"name"_s, c.name}};
            if (!c.group.isEmpty())
                choice.insert(u"group"_s, c.group);
            choices.append(choice);
        }
        o.insert(u"choices"_s, choices);
    }
    return o;
}

namespace {

/// An option's choices: bounded, each with a value, none twice (a nameless one named by its value).
QList<RemoteChatChoice> choicesFromJson(const QJsonArray &list)
{
    QList<RemoteChatChoice> choices;
    for (const QJsonValue &v : list) {
        const QJsonObject c = v.toObject();
        RemoteChatChoice choice{line(c, "value"_L1), line(c, "name"_L1), line(c, "group"_L1)};
        const bool twice =
                std::ranges::any_of(choices, [&choice](const RemoteChatChoice &k) { return k.value == choice.value; });
        if (choice.value.isEmpty() || twice)
            continue;
        if (choice.name.isEmpty())
            choice.name = choice.value;
        choices.append(choice);
        if (choices.size() >= MaxOptionChoices)
            break;
    }
    return choices;
}

} // namespace

std::optional<RemoteChatOption> remoteChatOptionFromJson(const QJsonObject &o)
{
    RemoteChatOption option;
    option.id = line(o, "id"_L1);
    option.boolean = o.value(u"boolean"_s).toBool();
    option.choices = choicesFromJson(o.value(u"choices"_s).toArray());
    if (option.id.isEmpty() || (!option.boolean && option.choices.isEmpty()))
        return std::nullopt;
    option.name = line(o, "name"_L1);
    if (option.name.isEmpty())
        option.name = option.id;
    option.description = line(o, "description"_L1, 500);
    option.current = line(o, "current"_L1);
    option.currentName = line(o, "currentName"_L1);
    return option;
}

QJsonArray toJson(const QList<RemotePicture> &pictures)
{
    QJsonArray list;
    for (const RemotePicture &p : pictures)
        list.append(QJsonObject{{u"name"_s, p.name}, {u"data"_s, QString::fromLatin1(p.data.toBase64())}});
    return list;
}

namespace {

/// One picture of chats/send's `pictures` ("" `*why`: taken).
std::optional<RemotePicture> pictureFromJson(const QJsonValue &value, QString *why)
{
    const QJsonObject o = value.toObject();
    const QJsonValue data = o.value(u"data"_s);
    // (Only its file name: whatever folders it names.)
    const QString name = o.value(u"name"_s).toString().section(u'/', -1).section(u'\\', -1).trimmed();
    if (!value.isObject() || name.isEmpty() || !paths::isPicturePath(name)) {
        *why = u"each picture is {name, data}: a picture's file name"_s;
        return std::nullopt;
    }
    // (Base64 of MaxRemotePictureBytes at most: refused before it is decoded.)
    if (!data.isString() || data.toString().size() > (MaxRemotePictureBytes + 2) / 3 * 4) {
        *why = u"a picture's data is base64 of %1 bytes at most"_s.arg(MaxRemotePictureBytes);
        return std::nullopt;
    }
    const auto decoded =
            QByteArray::fromBase64Encoding(data.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > MaxRemotePictureBytes) {
        *why = u"a picture's data is base64 of its bytes, %1 at most"_s.arg(MaxRemotePictureBytes);
        return std::nullopt;
    }
    return RemotePicture{name, decoded.decoded};
}

/// Each picture of the list ("" `*why`: all taken, MaxRemotePicturesBytes at most in all).
QList<RemotePicture> picturesOf(const QJsonArray &list, QString *why)
{
    QList<RemotePicture> pictures;
    qsizetype total = 0;
    for (const QJsonValue &v : list) {
        const std::optional<RemotePicture> picture = pictureFromJson(v, why);
        if (!picture)
            break;
        total += picture->data.size();
        if (total > MaxRemotePicturesBytes) {
            *why = u"the pictures are %1 bytes at most in all"_s.arg(MaxRemotePicturesBytes);
            break;
        }
        pictures.append(*picture);
    }
    return pictures;
}

} // namespace

std::optional<QList<RemotePicture>> remotePicturesFromJson(const QJsonValue &value, QString *why)
{
    QString reason;
    QList<RemotePicture> pictures;
    if (value.isUndefined() || value.isNull())
        return pictures;
    if (!value.isArray())
        reason = u"pictures is a list"_s;
    else if (value.toArray().size() > MaxRemotePictures)
        reason = u"%1 pictures at most"_s.arg(MaxRemotePictures);
    else
        pictures = picturesOf(value.toArray(), &reason);
    if (reason.isEmpty())
        return pictures;
    if (why)
        *why = reason;
    return std::nullopt;
}

namespace {

/// The detail's tool calls and plan steps (each with its words; bounded).
void readActivity(const QJsonObject &o, RemoteChatDetail &d)
{
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
}

/// Its question: none without options (at most MaxQuestionOptions, none twice).
RemoteChatQuestion questionFromJson(const QJsonObject &q)
{
    RemoteChatQuestion question;
    for (const QJsonValue &v : q.value(u"options"_s).toArray()) {
        const QString option = v.toString().simplified().left(200);
        if (option.isEmpty() || question.options.contains(option))
            continue;
        question.options.append(option);
        if (question.options.size() >= MaxQuestionOptions)
            break;
    }
    if (question.options.isEmpty())
        return question;
    question.text = line(q, "text"_L1, 500);
    question.details = text(q, "details"_L1, MaxQuestionChars);
    question.plan = text(q, "plan"_L1, MaxLiveTextChars);
    const QJsonObject diff = q.value(u"diff"_s).toObject();
    question.file = line(diff, "path"_L1, MaxRemotePathChars);
    if (!question.file.isEmpty()) {
        question.oldText = text(diff, "oldText"_L1, MaxLiveTextChars);
        question.newText = text(diff, "newText"_L1, MaxLiveTextChars);
    }
    return question;
}

/// The messages waiting (held or not) and the agent's options.
void readWaitingAndOptions(const QJsonObject &o, RemoteChatDetail &d)
{
    for (const QJsonValue &v : o.value(u"waiting"_s).toArray()) {
        const QString message = keepStart(v.toString(), 4000);
        if (message.trimmed().isEmpty())
            continue;
        d.waiting.append(message);
        if (d.waiting.size() >= MaxLiveWaiting)
            break;
    }
    d.held = o.value(u"held"_s).toBool() && !d.waiting.isEmpty();
    for (const QJsonValue &v : o.value(u"options"_s).toArray()) {
        std::optional<RemoteChatOption> option = remoteChatOptionFromJson(v.toObject());
        const bool twice = option && std::ranges::any_of(d.options, [&option](const RemoteChatOption &k) {
                               return k.id == option->id;
                           });
        if (!option || twice)
            continue;
        d.options.append(std::move(*option));
        if (d.options.size() >= MaxLiveOptions)
            break;
    }
}

} // namespace

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
    readActivity(o, d);
    d.question = questionFromJson(o.value(u"question"_s).toObject());
    readWaitingAndOptions(o, d);
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
                    const QString &text, SendMode mode, const QList<RemotePicture> &pictures,
                    std::function<void(const RemoteError &)> done)
{
    QJsonObject body{{u"path"_s, path}, {u"text"_s, text}};
    if (mode == SendMode::Queue)
        body.insert(u"mode"_s, u"queue"_s);
    else if (mode == SendMode::Steer)
        body.insert(u"mode"_s, u"steer"_s);
    if (!pictures.isEmpty())
        body.insert(u"pictures"_s, toJson(pictures));
    post(transport, base, token, u"chats/send"_s, body, done);
}

void startRemoteThread(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &project,
                       const QString &text, const QString &partner, const QString &worktree, const QString &branch,
                       const QList<RemotePicture> &pictures,
                       std::function<void(const QString &, const RemoteError &)> done)
{
    QJsonObject body{{u"project"_s, project}, {u"text"_s, text}};
    if (!partner.isEmpty())
        body.insert(u"partner"_s, partner);
    if (!worktree.isEmpty())
        body.insert(u"worktree"_s, worktree);
    if (!branch.isEmpty())
        body.insert(u"branch"_s, branch);
    if (!pictures.isEmpty())
        body.insert(u"pictures"_s, toJson(pictures));
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
    post(transport, base, token, u"chats/stop"_s, {{u"path"_s, path}}, done);
}

void answerRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                      const QString &option, std::function<void(const RemoteError &)> done)
{
    post(transport, base, token, u"chats/answer"_s, {{u"path"_s, path}, {u"option"_s, option}}, done);
}

void setRemoteChatOption(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                         const QString &option, const QString &value, std::function<void(const RemoteError &)> done)
{
    post(transport, base, token, u"chats/option"_s, {{u"path"_s, path}, {u"option"_s, option}, {u"value"_s, value}},
         done);
}

void postRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &endpoint,
                    const QJsonObject &body, std::function<void(const RemoteError &)> done)
{
    post(transport, base, token, endpoint, body, done);
}

void resumeRemoteChat(sync::HttpTransport *transport, const QUrl &base, const QString &token, const QString &path,
                      std::function<void(const RemoteError &)> done)
{
    post(transport, base, token, u"chats/resume"_s, {{u"path"_s, path}}, done);
}

} // namespace brulion::acp

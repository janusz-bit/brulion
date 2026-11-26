#include "chatcontrol.h"

#include "chatlineage.h"
#include "chatschedules.h"
#include "chatsecrets.h"
#include "codeprojects.h"
#include "notificationcenter.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "chat/chat.h"
#include "vault/vault.h"

#include <QDateTime>
#include <QJsonArray>
#include <QPointer>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

// The remote control's error codes (ipc/controlprotocol.h ControlError; this side is GUI-free).
const QString NotReady = u"not_ready"_s;
const QString NotFound = u"not_found"_s;
const QString Ambiguous = u"ambiguous_ref"_s;
const QString BadParams = u"bad_params"_s;
const QString Refused = u"refused"_s;
const QString Busy = u"busy"_s;
const QString Timeout = u"timeout"_s;
const QString Failed = u"failed"_s;

/// The longest partial answer chatRead and chatWait give of a turn that runs.
constexpr int MaxPartialChars = 4000;

QString minute(const QDateTime &time)
{
    return time.isValid() ? time.toString(u"yyyy-MM-dd HH:mm"_s) : QString();
}

/// `text` from `from`, at most `chars` characters, never half a character.
QString part(const QString &text, qsizetype from, qsizetype chars)
{
    QString out = text.mid(from, chars);
    if (!out.isEmpty() && out.back().isHighSurrogate() && from + out.size() < text.size())
        out.chop(1);
    return out;
}

} // namespace

/// A chatWait waiting for a turn to end.
struct ChatControl::Wait {
    QPointer<ChatTurn> turn;
    Reply reply;
    QTimer timer;
    bool done = false;
};

ChatControl::ChatControl(Workspace *workspace, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_chats(workspace->chats())
{
    connect(m_chats, &WorkspaceChats::stateChanged, this, &ChatControl::checkWaits);
    connect(m_chats, &WorkspaceChats::questionsChanged, this, &ChatControl::checkWaits);
    m_clientCheck.setInterval(1000);
    connect(&m_clientCheck, &QTimer::timeout, this, [this] {
        m_waits.removeIf([](const std::shared_ptr<Wait> &w) { return w->reply.connected && !w->reply.connected(); });
        if (m_waits.isEmpty())
            m_clientCheck.stop();
    });
}

ChatControl::~ChatControl() = default;

bool ChatControl::handles(const QString &method)
{
    static const QSet<QString> methods{u"chats"_s,
                                       u"chatRead"_s,
                                       u"chatStart"_s,
                                       u"chatSend"_s,
                                       u"chatWait"_s,
                                       u"chatStop"_s,
                                       u"chatPartners"_s,
                                       u"chatSchedules"_s,
                                       u"chatScheduleAdd"_s,
                                       u"chatScheduleSet"_s,
                                       u"chatScheduleRemove"_s,
                                       u"chatScheduleRun"_s,
                                       u"chatRequestSecret"_s,
                                       u"chatNotify"_s};
    return methods.contains(method);
}

void ChatControl::handle(const QString &method, const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    if (!ready(reply))
        return;
    if (method == u"chats")
        list(params, reply);
    else if (method == u"chatRead")
        read(params, reply);
    else if (method == u"chatStart")
        start(params, caller, reply);
    else if (method == u"chatSend")
        sendTo(params, caller, reply);
    else if (method == u"chatWait")
        wait(params, reply);
    else if (method == u"chatStop")
        stop(params, caller, reply);
    else if (method == u"chatPartners")
        partners(reply);
    else if (method == u"chatSchedules")
        schedules(reply);
    else if (method == u"chatScheduleAdd")
        scheduleAdd(params, caller, reply);
    else if (method == u"chatScheduleSet" || method == u"chatScheduleRemove" || method == u"chatScheduleRun")
        scheduleSet(method, params, caller, reply);
    else if (method == u"chatRequestSecret")
        requestSecret(params, caller, reply);
    else if (method == u"chatNotify")
        notify(params, caller, reply);
    else
        reply.fail(u"unknown_method"_s, u"Unknown method %1"_s.arg(method), {});
}

bool ChatControl::ready(const Reply &reply) const
{
    if (m_workspace->isOpen() && m_workspace->ready())
        return true;
    reply.fail(NotReady, u"No notebook is open in the app yet"_s, {});
    return false;
}

QString ChatControl::resolve(const QString &ref, QStringList *ambiguous) const
{
    QString wanted = ref.trimmed();
    if (wanted.isEmpty() || !m_workspace->vault())
        return {};
    if (!wanted.endsWith(u".md"_s, Qt::CaseInsensitive))
        wanted += u".md"_s;
    const QString inFolder = chat::inFolder(wanted) ? wanted : chat::folder() + u'/' + wanted;
    QStringList named;
    const QString name = ref.trimmed().section(u'/', -1);
    for (const FileStat &f : m_workspace->vault()->markdownFiles()) {
        if (!chat::inFolder(f.path))
            continue;
        if (f.path.compare(wanted, Qt::CaseInsensitive) == 0 || f.path.compare(inFolder, Qt::CaseInsensitive) == 0)
            return f.path;
        // (A side chat is the user's aside: named by its path only.)
        if (m_chats->nameOf(f.path).compare(name, Qt::CaseInsensitive) == 0 && m_chats->sideOf(f.path).isEmpty())
            named << f.path;
    }
    if (named.size() == 1)
        return named.first();
    if (ambiguous)
        *ambiguous = named;
    return {};
}

QString ChatControl::conversationOf(const QJsonObject &params, const Reply &reply, const QString &key)
{
    const QString ref = params.value(key).toString();
    QStringList several;
    const QString path = resolve(ref, &several);
    if (!path.isEmpty() && m_chats->isConversation(path))
        return path;
    if (several.size() > 1)
        reply.fail(Ambiguous, u"Several conversations are called %1"_s.arg(ref.left(200)),
                   {{u"candidates"_s, QJsonArray::fromStringList(several)}});
    else
        reply.fail(NotFound, u"No conversation %1 (chats lists them)"_s.arg(ref.left(200)), {{u"param"_s, key}});
    return {};
}

QJsonObject ChatControl::row(const QString &path, qint64 modified) const
{
    bool ok = false;
    const chat::Conversation c = chat::parse(m_workspace->read(path, &ok));
    QString failure;
    const QString state = m_chats->stateOf(path, &failure);
    const QString partnerName = m_chats->partnerLabel(c.partner);
    QJsonObject out{{u"path"_s, path},
                    {u"name"_s, m_chats->nameOf(path)},
                    {u"partner"_s, c.partner},
                    {u"partnerName"_s, partnerName.isEmpty() ? c.partner : partnerName},
                    {u"state"_s, state},
                    {u"messages"_s, int(c.messages.size())}};
    if (modified < 0) {
        if (const auto stat = m_workspace->vault()->stat(path))
            modified = stat->mtime;
    }
    if (modified >= 0)
        out.insert(u"changed"_s, minute(QDateTime::fromMSecsSinceEpoch(modified)));
    if (!failure.isEmpty())
        out.insert(u"failure"_s, failure);
    if (state == u"asking")
        out.insert(u"question"_s, m_chats->questionText(path).section(u'\n', 0, 0));
    if (!c.parent.isEmpty()) {
        const QString parent = ChatLineage::resolveParent(m_workspace, c.parent, path);
        out.insert(u"parent"_s, parent.isEmpty() ? c.parent : parent);
    }
    if (const QString side = m_chats->sideOf(path); !side.isEmpty())
        out.insert(u"side"_s, side);
    if (m_chats->wakesParent(path))
        out.insert(u"wakes"_s, true);
    if (!c.project.isEmpty())
        out.insert(u"project"_s, c.project);
    if (!c.page.isEmpty())
        out.insert(u"page"_s, c.page);
    return out;
}

void ChatControl::list(const QJsonObject &params, const Reply &reply)
{
    QString parent;
    if (params.contains(u"parent"_s)) {
        parent = conversationOf(params, reply, u"parent"_s);
        if (parent.isEmpty())
            return;
    }
    struct Entry {
        QString path;
        qint64 modified;
    };
    QList<Entry> entries;
    for (const FileStat &f : m_workspace->vault()->markdownFiles()) {
        if (!chat::inFolder(f.path))
            continue;
        if (!parent.isEmpty() && m_chats->lineage()->parentOf(f.path).compare(parent, Qt::CaseInsensitive) != 0)
            continue;
        if (!m_chats->sideOf(f.path).isEmpty())
            continue; // (a side chat: the user's aside, shown inside its conversation only)
        entries.append({f.path, f.mtime});
    }
    std::ranges::sort(entries, [](const Entry &a, const Entry &b) {
        return a.modified != b.modified ? a.modified > b.modified : a.path < b.path;
    });
    const int limit = params.value(u"limit"_s).toInt(50);
    QJsonArray rows;
    int total = 0;
    for (const Entry &e : std::as_const(entries)) {
        const QJsonObject r = row(e.path, e.modified);
        if (r.value(u"messages"_s).toInt() == 0)
            continue; // (a page in AI/ that holds no conversation)
        ++total;
        if (rows.size() < limit)
            rows.append(r);
    }
    reply.ok(QJsonObject{{u"conversations"_s, rows}, {u"total"_s, total}});
}

void ChatControl::read(const QJsonObject &params, const Reply &reply)
{
    const QString path = conversationOf(params, reply);
    if (path.isEmpty())
        return;
    bool ok = false;
    const chat::Conversation c = chat::parse(m_workspace->read(path, &ok));
    const int total = int(c.messages.size());
    const int from = std::min(params.value(u"from"_s).toInt(0), total);
    const int limit = params.value(u"limit"_s).toInt(20);
    const int end = std::min(total, from + limit);
    qsizetype budget = MaxReadChars;
    QJsonArray messages;
    QJsonObject next;
    for (int i = from; i < end; ++i) {
        const chat::Message &m = c.messages.at(i);
        const qsizetype start = i == from ? std::clamp<qsizetype>(params.value(u"offset"_s).toInteger(0), 0, m.text.size()) : 0;
        const QString text = part(m.text, start, budget);
        QJsonObject item{{u"position"_s, i},
                         {u"role"_s, m.role == chat::Role::User ? u"user"_s : u"assistant"_s},
                         {u"text"_s, text},
                         {u"length"_s, int(m.text.size())}};
        if (m.time.isValid())
            item.insert(u"time"_s, minute(m.time));
        if (start > 0)
            item.insert(u"offset"_s, int(start));
        const bool cut = start + text.size() < m.text.size();
        if (cut)
            item.insert(u"cut"_s, true);
        messages.append(item);
        budget -= text.size();
        if (cut) {
            next = QJsonObject{{u"from"_s, i}, {u"offset"_s, int(start + text.size())}};
            break;
        }
        if (budget <= 0 && i + 1 < end) {
            next = QJsonObject{{u"from"_s, i + 1}, {u"offset"_s, 0}};
            break;
        }
    }
    if (next.isEmpty() && end < total)
        next = QJsonObject{{u"from"_s, end}, {u"offset"_s, 0}};
    QJsonObject out = row(path);
    out.insert(u"total"_s, total);
    out.insert(u"messages"_s, messages);
    if (!next.isEmpty())
        out.insert(u"next"_s, next);
    // What the AI writes now.
    if (m_chats->isRunning(path)) {
        if (const QString partial = m_chats->turn(path)->partial(); !partial.isEmpty())
            out.insert(u"partial"_s, part(partial, 0, MaxPartialChars));
    }
    reply.ok(out);
}

void ChatControl::start(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    const QString text = params.value(u"text"_s).toString().trimmed();
    if (text.isEmpty())
        return reply.fail(BadParams, u"Write the message (text)"_s, {{u"param"_s, u"text"_s}});
    // (No partner named: the device's default — "" names the assistant.)
    const QString partner =
            params.contains(u"partner"_s) ? params.value(u"partner"_s).toString().trimmed() : m_chats->defaultPartner();
    QStringList ids;
    for (const QVariant &p : m_chats->partners())
        ids << p.toMap().value(u"id"_s).toString();
    if (!ids.contains(partner))
        return reply.fail(NotFound, u"No partner %1 answers here (chatPartners lists them)"_s.arg(partner.left(100)),
                          {{u"param"_s, u"partner"_s}, {u"partners"_s, QJsonArray::fromStringList(ids)}});
    // Who it comes from: an agent's own conversation (a sub-conversation of it), else as asked.
    QString parent;
    if (caller.granted) {
        if (caller.conversation.isEmpty() || !m_workspace->exists(caller.conversation))
            return reply.fail(Refused, u"This grant belongs to no conversation of the app: it starts none"_s, {});
        parent = caller.conversation;
        if (params.contains(u"parent"_s)
            && resolve(params.value(u"parent"_s).toString()).compare(parent, Qt::CaseInsensitive) != 0)
            return reply.fail(Refused,
                              u"An agent starts sub-conversations of its own conversation only (%1)"_s.arg(parent),
                              {{u"param"_s, u"parent"_s}});
    } else if (params.contains(u"parent"_s)) {
        parent = conversationOf(params, reply, u"parent"_s);
        if (parent.isEmpty())
            return;
    }
    ChatLineage *lineage = m_chats->lineage();
    if (!parent.isEmpty()) {
        if (lineage->depthOf(parent) + 1 > ChatLineage::MaxDepth)
            return reply.fail(Refused,
                              u"Sub-conversations go %1 levels deep at most: this one starts none"_s.arg(
                                      ChatLineage::MaxDepth),
                              {{u"reason"_s, u"depth"_s}, {u"max"_s, ChatLineage::MaxDepth}});
        if (lineage->runningChildren(parent) >= ChatLineage::MaxRunningChildren)
            return reply.fail(Refused,
                              u"%1 sub-conversations of this conversation run already: wait for one (chatWait)"_s.arg(
                                      ChatLineage::MaxRunningChildren),
                              {{u"reason"_s, u"children"_s}, {u"max"_s, ChatLineage::MaxRunningChildren}});
    }
    // Wake: the end of its turn wakes the conversation it came from (no wait needed).
    const bool wake = params.value(u"wake"_s).toBool();
    if (wake && parent.isEmpty())
        return reply.fail(BadParams, u"wake needs a conversation to wake: start it with parent"_s,
                          {{u"param"_s, u"wake"_s}});
    const QString page = params.value(u"page"_s).toString().trimmed();
    // A thread of a project (Code mode, docs/CODE.md): an AI agent's task in its folder.
    const QString project = params.value(u"project"_s).toString().trimmed();
    if (!project.isEmpty() && !m_workspace->projects()->contains(project)) {
        QStringList projects;
        for (const CodeProjects::Project &p : m_workspace->projects()->projects())
            projects << p.id;
        return reply.fail(NotFound,
                          u"No project %1 on this device (chatPartners names the projects)"_s.arg(project.left(100)),
                          {{u"param"_s, u"project"_s}, {u"projects"_s, QJsonArray::fromStringList(projects)}});
    }
    if (!project.isEmpty() && !WorkspaceAgents::isAgent(partner))
        return reply.fail(BadParams, u"A thread of a project is answered by an AI agent (acp:<id>)"_s,
                          {{u"param"_s, u"partner"_s}});
    const QString path = project.isEmpty()
                                 ? m_chats->send(QString(), text, page, {}, partner, parent, wake ? parent : QString())
                                 : m_chats->startThreadFrom(project, text, partner, parent, wake ? parent : QString());
    if (path.isEmpty()) {
        const QString failure = m_chats->turn(QString())->failure();
        return reply.fail(Failed, failure.isEmpty() ? u"The conversation could not be started"_s : failure, {});
    }
    reply.ok(row(path));
}

bool ChatControl::mayChange(const Caller &caller, const QString &path, const Reply &reply) const
{
    if (!caller.granted || m_chats->lineage()->isDescendant(path, caller.conversation))
        return true;
    reply.fail(Refused, u"An agent sends to and stops only the sub-conversations it started"_s,
               {{u"param"_s, u"path"_s}});
    return false;
}

void ChatControl::sendTo(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    const QString path = conversationOf(params, reply);
    if (path.isEmpty() || !mayChange(caller, path, reply))
        return;
    const QString text = params.value(u"text"_s).toString().trimmed();
    if (text.isEmpty())
        return reply.fail(BadParams, u"Write the message (text)"_s, {{u"param"_s, u"text"_s}});
    // Wake: the agent's own conversation (an ancestor of this one), a script's the parent.
    const bool wake = params.value(u"wake"_s).toBool();
    const QString woken = !wake ? QString() : caller.granted ? caller.conversation : m_chats->lineage()->parentOf(path);
    if (wake && (woken.isEmpty() || !m_workspace->exists(woken)))
        return reply.fail(BadParams, u"wake needs a conversation to wake: this one is no sub-conversation"_s,
                          {{u"param"_s, u"wake"_s}});
    if (wake && m_chats->isRunning(path))
        return reply.fail(Busy,
                          u"wake goes with a message sent now: wait for its turn (chatWait) or stop it (chatStop)"_s,
                          {{u"state"_s, m_chats->stateOf(path)}});
    if (m_chats->isRunning(path)) {
        // In line after the answer (as a message the user writes meanwhile), when asked to.
        if (params.value(u"queue"_s).toBool() && m_chats->queue(path, text)) {
            QJsonObject out = row(path);
            out.insert(u"queued"_s, true);
            return reply.ok(out);
        }
        return reply.fail(Busy, u"Its turn runs: wait for it (chatWait), stop it (chatStop) or put the message in line (queue)"_s,
                          {{u"state"_s, m_chats->stateOf(path)}});
    }
    const QString parent = m_chats->lineage()->parentOf(path);
    if (caller.granted && !parent.isEmpty()
        && m_chats->lineage()->runningChildren(parent) >= ChatLineage::MaxRunningChildren)
        return reply.fail(Refused,
                          u"%1 sub-conversations of its conversation run already: wait for one (chatWait)"_s.arg(
                                  ChatLineage::MaxRunningChildren),
                          {{u"reason"_s, u"children"_s}, {u"max"_s, ChatLineage::MaxRunningChildren}});
    if (m_chats->send(path, text, QString(), {}, QString(), QString(), woken).isEmpty()) {
        const QString failure = m_chats->turn(path)->failure();
        return reply.fail(Failed, failure.isEmpty() ? u"The message could not be sent"_s : failure, {});
    }
    reply.ok(row(path));
}

QJsonObject ChatControl::outcome(const QString &path) const
{
    QString failure;
    const QString state = m_chats->stateOf(path, &failure);
    QJsonObject out{{u"path"_s, path}, {u"state"_s, state}};
    if (state == u"asking") {
        const QString text = m_chats->questionText(path);
        out.insert(u"question"_s, QJsonObject{{u"text"_s, text.section(u'\n', 0, 0)},
                                             {u"details"_s, text.section(u'\n', 1).trimmed().left(4000)},
                                             {u"options"_s, QJsonArray::fromStringList(m_chats->questionOptions(path))}});
        return out;
    }
    if (state == u"running" || state == u"queued") {
        if (const QString partial = m_chats->turn(path)->partial(); !partial.isEmpty())
            out.insert(u"partial"_s, part(partial, 0, MaxPartialChars));
        return out;
    }
    if (!failure.isEmpty())
        out.insert(u"failure"_s, failure);
    bool ok = false;
    const chat::Conversation c = chat::parse(m_workspace->read(path, &ok));
    if (!c.messages.isEmpty() && c.messages.last().role == chat::Role::Assistant) {
        const chat::Message &m = c.messages.last();
        QJsonObject answer{{u"position"_s, int(c.messages.size()) - 1}, {u"text"_s, part(m.text, 0, MaxReadChars)}};
        if (m.time.isValid())
            answer.insert(u"time"_s, minute(m.time));
        if (m.text.size() > MaxReadChars)
            answer.insert(u"cut"_s, true);
        out.insert(u"answer"_s, answer);
    }
    return out;
}

void ChatControl::wait(const QJsonObject &params, const Reply &reply)
{
    const QString path = conversationOf(params, reply);
    if (path.isEmpty())
        return;
    const QString state = m_chats->stateOf(path);
    if (state != u"running" && state != u"queued")
        return reply.ok(outcome(path));
    auto w = std::make_shared<Wait>();
    w->turn = m_chats->turn(path);
    w->reply = reply;
    w->timer.setSingleShot(true);
    w->timer.setInterval(std::clamp(params.value(u"ms"_s).toInt(DefaultWaitMs), 0, MaxWaitMs));
    const std::weak_ptr<Wait> weak = w;
    connect(&w->timer, &QTimer::timeout, this, [this, weak] {
        if (const std::shared_ptr<Wait> live = weak.lock())
            finishWait(live, true);
    });
    w->timer.start();
    m_waits.append(w);
    if (!m_clientCheck.isActive())
        m_clientCheck.start();
}

void ChatControl::checkWaits()
{
    const QList<std::shared_ptr<Wait>> waits = m_waits;
    for (const std::shared_ptr<Wait> &w : waits) {
        if (!w->turn) {
            finishWait(w, false);
            continue;
        }
        const QString state = m_chats->stateOf(w->turn->path());
        if (state != u"running" && state != u"queued")
            finishWait(w, false);
    }
}

void ChatControl::finishWait(const std::shared_ptr<Wait> &w, bool timedOut)
{
    if (w->done)
        return;
    w->done = true;
    w->timer.stop();
    m_waits.removeAll(w);
    if (!w->turn) {
        w->reply.fail(NotFound, u"The conversation is gone"_s, {});
        return;
    }
    const QString path = w->turn->path();
    if (timedOut)
        w->reply.fail(Timeout, u"Its turn still runs: wait again (chatWait), or stop it (chatStop)"_s, outcome(path));
    else
        w->reply.ok(outcome(path));
}

void ChatControl::stop(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    const QString path = conversationOf(params, reply);
    if (path.isEmpty() || !mayChange(caller, path, reply))
        return;
    const bool was = m_chats->isRunning(path);
    // (Who stops it knows: no wake for it.)
    m_chats->setWake(path, QString());
    m_chats->stop(path);
    reply.ok(QJsonObject{{u"path"_s, path}, {u"stopped"_s, was}});
}

void ChatControl::partners(const Reply &reply)
{
    QJsonArray out;
    const QString byDefault = m_chats->defaultPartner();
    for (const QVariant &p : m_chats->partners()) {
        const QVariantMap m = p.toMap();
        const QString id = m.value(u"id"_s).toString();
        QJsonObject one{{u"id"_s, id},
                        {u"name"_s, m.value(u"label"_s).toString()},
                        {u"kind"_s,
                         id.isEmpty() ? u"assistant"_s : (WorkspaceAgents::isAgent(id) ? u"agent"_s : u"extension"_s)},
                        {u"by"_s, m.value(u"extension"_s).toString()}};
        if (id == byDefault)
            one.insert(u"default"_s, true);
        out.append(one);
    }
    // (The projects of Code mode a thread may be started in: chatStart's `project`.)
    QJsonArray projects;
    for (const CodeProjects::Project &p : m_workspace->projects()->projects())
        projects.append(QJsonObject{{u"id"_s, p.id}, {u"name"_s, p.name}});
    reply.ok(QJsonObject{{u"partners"_s, out}, {u"default"_s, byDefault}, {u"projects"_s, projects}});
}

void ChatControl::schedules(const Reply &reply)
{
    ChatSchedules *s = m_chats->schedules();
    QJsonArray out;
    for (const schedule::Schedule &one : s->schedules())
        out.append(s->toJson(one));
    reply.ok(QJsonObject{{u"schedules"_s, out}, {u"available"_s, s->available()}});
}

namespace {

/// The fields of a schedule given in a request.
ChatSchedules::Fields fieldsOf(const QJsonObject &params)
{
    ChatSchedules::Fields f;
    const auto text = [&params](const QString &key) -> std::optional<QString> {
        if (!params.contains(key))
            return std::nullopt;
        return params.value(key).toString();
    };
    f.title = text(u"title"_s);
    f.prompt = text(u"prompt"_s);
    f.partner = text(u"partner"_s);
    f.when = text(u"when"_s);
    if (params.contains(u"destination"_s))
        f.continues = params.value(u"destination"_s).toString().toLower() == u"continue";
    if (params.contains(u"enabled"_s))
        f.enabled = params.value(u"enabled"_s).toBool();
    return f;
}

} // namespace

void ChatControl::scheduleAdd(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    ChatSchedules *s = m_chats->schedules();
    if (!s->available())
        return reply.fail(NotReady, u"Scheduled conversations are kept for a notebook of the user's: none is open"_s, {});
    const QString partner =
            params.contains(u"partner"_s) ? params.value(u"partner"_s).toString().trimmed() : m_chats->defaultPartner();
    QStringList ids;
    for (const QVariant &p : m_chats->partners())
        ids << p.toMap().value(u"id"_s).toString();
    if (!ids.contains(partner))
        return reply.fail(NotFound, u"No partner %1 answers here (chatPartners lists them)"_s.arg(partner.left(100)),
                          {{u"param"_s, u"partner"_s}, {u"partners"_s, QJsonArray::fromStringList(ids)}});
    QString parent;
    if (caller.granted) {
        if (caller.conversation.isEmpty() || !m_workspace->exists(caller.conversation))
            return reply.fail(Refused, u"This grant belongs to no conversation of the app: it schedules none"_s, {});
        if (m_chats->lineage()->depthOf(caller.conversation) + 1 > ChatLineage::MaxDepth)
            return reply.fail(Refused,
                              u"Sub-conversations go %1 levels deep at most: this one schedules none"_s.arg(
                                      ChatLineage::MaxDepth),
                              {{u"reason"_s, u"depth"_s}, {u"max"_s, ChatLineage::MaxDepth}});
        parent = caller.conversation;
    }
    QString error;
    QJsonObject fields = params;
    fields.insert(u"partner"_s, partner); // (none named: the default's, kept as chosen now)
    const QString id = s->add(fieldsOf(fields), caller.client, parent, &error);
    if (id.isEmpty())
        return reply.fail(BadParams, error, {});
    reply.ok(s->toJson(*s->find(id)));
}

void ChatControl::scheduleSet(const QString &method, const QJsonObject &params, const Caller &caller,
                              const Reply &reply)
{
    ChatSchedules *s = m_chats->schedules();
    const QString id = params.value(u"id"_s).toString().trimmed();
    const schedule::Schedule *found = s->find(id);
    if (!found)
        return reply.fail(NotFound, u"No scheduled conversation %1 (chatSchedules lists them)"_s.arg(id.left(80)),
                          {{u"param"_s, u"id"_s}});
    // An agent changes only what it set up from its conversation (or a sub-conversation of it).
    if (caller.granted
        && !(found->parent.compare(caller.conversation, Qt::CaseInsensitive) == 0 && !found->parent.isEmpty())
        && !m_chats->lineage()->isDescendant(found->parent, caller.conversation))
        return reply.fail(Refused, u"An agent changes only the scheduled conversations it set up"_s,
                          {{u"param"_s, u"id"_s}});
    if (method == u"chatScheduleRemove") {
        s->remove(id);
        return reply.ok(QJsonObject{{u"id"_s, id}, {u"removed"_s, true}});
    }
    if (method == u"chatScheduleRun") {
        const QString path = s->runNow(id);
        const schedule::Schedule *after = s->find(id);
        if (path.isEmpty())
            return reply.fail(Failed, after ? after->lastError : QString(), after ? s->toJson(*after) : QJsonObject());
        QJsonObject out = s->toJson(*after);
        out.insert(u"path"_s, path);
        return reply.ok(out);
    }
    if (params.contains(u"partner"_s)) {
        QStringList ids;
        for (const QVariant &p : m_chats->partners())
            ids << p.toMap().value(u"id"_s).toString();
        if (!ids.contains(params.value(u"partner"_s).toString().trimmed()))
            return reply.fail(NotFound, u"No such partner answers here (chatPartners lists them)"_s,
                              {{u"param"_s, u"partner"_s}, {u"partners"_s, QJsonArray::fromStringList(ids)}});
    }
    QString error;
    if (!s->update(id, fieldsOf(params), &error))
        return reply.fail(BadParams, error, {});
    reply.ok(s->toJson(*s->find(id)));
}

void ChatControl::requestSecret(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    // An agent asks in its own conversation (its grant's), while its turn runs; a script of the
    // user's names the conversation.
    QString path;
    if (caller.granted) {
        if (caller.conversation.isEmpty() || !m_workspace->exists(caller.conversation))
            return reply.fail(Refused, u"This grant belongs to no conversation of the app: it asks for no secret"_s,
                              {});
        path = caller.conversation;
        if (params.contains(u"path"_s)
            && resolve(params.value(u"path"_s).toString()).compare(path, Qt::CaseInsensitive) != 0)
            return reply.fail(Refused, u"An agent asks for a secret in its own conversation only (%1)"_s.arg(path),
                              {{u"param"_s, u"path"_s}});
        if (!m_chats->isRunning(path))
            return reply.fail(Refused, u"Its conversation's turn does not run: a secret is asked for while answering"_s,
                              {{u"state"_s, m_chats->stateOf(path)}});
    } else {
        path = conversationOf(params, reply);
        if (path.isEmpty())
            return;
    }
    ChatSecrets::Request r;
    r.path = path;
    r.agent = caller.client;
    r.label = params.value(u"label"_s).toString();
    r.reason = params.value(u"reason"_s).toString();
    r.placeholder = params.value(u"placeholder"_s).toString();
    r.url = params.value(u"url"_s).toString();
    r.inlineValue = params.value(u"delivery"_s).toString().toLower() == u"value";
    if (params.contains(u"ms"_s))
        r.waitMs = params.value(u"ms"_s).toInt(ChatSecrets::DefaultWaitMs);
    ChatSecrets::Reply answer;
    answer.ok = reply.ok;
    answer.fail = reply.fail;
    answer.connected = reply.connected;
    m_chats->secrets()->ask(r, answer);
}

void ChatControl::notify(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    // An agent notifies from its own conversation (its grant's): a click opens it; a script of the
    // user's may name one, or none (a click then shows the app).
    QString path;
    if (caller.granted) {
        if (caller.conversation.isEmpty() || !m_workspace->exists(caller.conversation))
            return reply.fail(Refused, u"This grant belongs to no conversation of the app: it sends no notification"_s,
                              {});
        path = caller.conversation;
        if (params.contains(u"path"_s)
            && resolve(params.value(u"path"_s).toString()).compare(path, Qt::CaseInsensitive) != 0)
            return reply.fail(Refused, u"An agent notifies from its own conversation only (%1)"_s.arg(path),
                              {{u"param"_s, u"path"_s}});
    } else if (params.contains(u"path"_s)) {
        path = conversationOf(params, reply);
        if (path.isEmpty())
            return;
    }
    const QString text = params.value(u"text"_s).toString();
    if (text.trimmed().isEmpty())
        return reply.fail(BadParams, u"Write what the notification says (text)"_s, {{u"param"_s, u"text"_s}});
    // What a click opens: the page named, else the conversation.
    QString open = path;
    if (const QString page = params.value(u"page"_s).toString().trimmed(); !page.isEmpty()) {
        open = m_workspace->resolveLink(page, QString());
        if (open.isEmpty() || !m_workspace->exists(open))
            return reply.fail(NotFound, u"No page %1 in the notebook"_s.arg(page.left(100)), {{u"param"_s, u"page"_s}});
    }
    // Who sends it: the conversation's partner, else the script's name; an agent's count is its
    // conversation's, the scripts' one of their own (NotificationCenter::MaxPerHour an hour).
    NotificationCenter::Post post;
    post.kind = caller.granted ? NotificationCenter::Kind::Agent : NotificationCenter::Kind::Script;
    post.key = path;
    post.who = path.isEmpty() ? caller.client : m_chats->partnerLabel(m_chats->partnerOf(path));
    if (post.who.isEmpty())
        post.who = caller.client;
    post.title = params.value(u"title"_s).toString();
    post.text = text;
    post.open = open;
    qint64 retryMs = 0;
    const QString why = m_workspace->notifications()->post(post, &retryMs);
    if (why == u"off")
        return reply.fail(Refused,
                          u"The user turned notifications from AI agents off (Settings → Extensions → AI): tell them "
                          u"in your answer instead"_s,
                          {{u"reason"_s, u"off"_s}});
    if (why == u"limit")
        return reply.fail(
                Refused,
                u"%1 notifications in the last hour already: wait, or tell the user in your answer"_s.arg(
                        NotificationCenter::MaxPerHour),
                {{u"reason"_s, u"limit"_s}, {u"max"_s, NotificationCenter::MaxPerHour}, {u"retryAfterMs"_s, retryMs}});
    if (!why.isEmpty())
        return reply.fail(BadParams, u"Write what the notification says (text)"_s, {{u"param"_s, u"text"_s}});
    QJsonObject out{{u"sent"_s, true}};
    if (!path.isEmpty())
        out.insert(u"conversation"_s, path);
    if (!open.isEmpty())
        out.insert(u"opens"_s, open);
    reply.ok(out);
}

} // namespace brulion::shell

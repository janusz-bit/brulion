#include "chatcontrol.h"

#include "chatlineage.h"
#include "chatschedules.h"
#include "chatsecrets.h"
#include "codeprojects.h"
#include "notificationcenter.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "acp/appchats.h"
#include "chat/chat.h"
#include "vault/vault.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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
const QString NoQuestion = u"no_question"_s;
const QString BadOption = u"bad_option"_s;
const QString BadPictures = u"bad_pictures"_s;
const QString NotHeld = u"not_held"_s;

/// The longest partial answer chatRead and chatWait give of a turn that runs.
constexpr int MaxPartialChars = 4000;

QString minute(const QDateTime &time)
{
    return time.isValid() ? time.toString(u"yyyy-MM-dd HH:mm"_s) : QString();
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

/// When a turn ended, as chatLive says it: ISO 8601, the local time with its offset.
QString isoTime(const QDateTime &time)
{
    return time.toOffsetFromUtc(time.offsetFromUtc()).toString(Qt::ISODate);
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

/// A chatLive waiting for a revision past `since`.
struct ChatControl::LiveWait {
    Reply reply;
    qint64 since = 0;
    QString path;
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
        m_liveWaits.removeIf(
                [](const std::shared_ptr<LiveWait> &w) { return w->reply.connected && !w->reply.connected(); });
        if (m_waits.isEmpty() && m_liveWaits.isEmpty())
            m_clientCheck.stop();
    });
    // chatLive: the revision follows every change of the conversations' turns (at most every
    // LiveIntervalMs), and never goes back — not even over a restart of the app.
    m_revision = QDateTime::currentMSecsSinceEpoch();
    m_sinceRevision.start();
    m_liveTimer.setSingleShot(true);
    connect(&m_liveTimer, &QTimer::timeout, this, &ChatControl::bumpRevision);
    m_pruneTimer.setInterval(30000);
    connect(&m_pruneTimer, &QTimer::timeout, this, [this] {
        // (One that ended longer ago leaves the list.)
        const int recent = int(recentlyEnded().size());
        if (recent != m_recentEnded)
            liveChanged();
        m_recentEnded = recent;
        if (recent == 0)
            m_pruneTimer.stop();
    });
    const auto turns = [this] {
        noteTurns();
        liveChanged();
    };
    connect(m_chats, &WorkspaceChats::stateChanged, this, turns);
    connect(m_chats, &WorkspaceChats::questionsChanged, this, turns);
    connect(m_chats, &WorkspaceChats::queueChanged, this, turns);
    connect(m_chats, &WorkspaceChats::agentsChanged, this, turns);
    connect(m_chats, &WorkspaceChats::written, this, [this](const QString &path) {
        m_activity.insert(path.toLower(), ++m_activitySerial);
        liveChanged();
    });
    connect(m_workspace, &Workspace::openChanged, this, [this] {
        // (Another notebook: nothing of the one before.)
        m_activity.clear();
        m_facts.clear();
        noteTurns();
        liveChanged();
    });
    noteTurns();
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
                                       u"chatNotify"_s,
                                       u"chatLive"_s,
                                       u"chatAnswer"_s,
                                       u"chatOption"_s,
                                       u"chatResume"_s};
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
    else if (method == u"chatLive")
        live(params, reply);
    else if (method == u"chatAnswer")
        answerQuestion(params, caller, reply);
    else if (method == u"chatOption")
        setOption(params, caller, reply);
    else if (method == u"chatResume")
        resume(params, caller, reply);
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
    // (No partner named: the device's default — "" names the assistant —, a thread's the agent a
    // new thread gets.)
    const bool thread = !params.value(u"project"_s).toString().trimmed().isEmpty();
    const QString partner = params.contains(u"partner"_s) ? params.value(u"partner"_s).toString().trimmed()
                            : thread                      ? m_chats->threadPartner()
                                                          : m_chats->defaultPartner();
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
    QString why;
    if (!acp::remotePicturesFromJson(params.value(u"pictures"_s), &why))
        return reply.fail(BadPictures, why, {{u"param"_s, u"pictures"_s}});
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
    // A thread in a worktree of its project (the phone's new thread there, a script's).
    const QString worktree = params.value(u"worktree"_s).toString().trimmed();
    const QString branch = params.value(u"branch"_s).toString().trimmed();
    if ((!worktree.isEmpty() || !branch.isEmpty()) && project.isEmpty())
        return reply.fail(BadParams, u"worktree and branch go with a thread of a project (project)"_s,
                          {{u"param"_s, worktree.isEmpty() ? u"branch"_s : u"worktree"_s}});
    if ((!worktree.isEmpty() || !branch.isEmpty()) && (!parent.isEmpty() || wake))
        return reply.fail(BadParams,
                          u"A thread in a worktree is started by the user, not as a sub-conversation (parent, wake)"_s,
                          {{u"param"_s, u"worktree"_s}});
    if (!worktree.isEmpty() || !branch.isEmpty()) {
        CodeProjects *projects = m_workspace->projects();
        const QString folder = QDir::cleanPath(worktree);
        const auto notOurs = [reply, worktree] {
            reply.fail(BadParams, u"%1 is no worktree of the project"_s.arg(worktree.left(300)),
                       {{u"param"_s, u"worktree"_s}});
        };
        // (Another computer's project: that computer checks its own worktrees.)
        if (worktree.isEmpty() || projects->isRemote(project))
            return startThreadNow(project, text, params, partner, worktree, branch, reply);
        if (!QDir::isAbsolutePath(folder) || !QFileInfo(folder).isDir())
            return notOurs();
        const QString canonical = QFileInfo(folder).canonicalFilePath();
        // The project's own folder: no worktree of its own.
        if (canonical == QFileInfo(projects->folderOf(project)).canonicalFilePath())
            return startThreadNow(project, text, params, partner, QString(), branch, reply);
        if (projects->projectOfFolder(folder) == project)
            return startThreadNow(project, text, params, partner, folder, branch, reply);
        // Elsewhere: only one `git worktree list` of the project lists.
        if (!projects->listWorktrees(project))
            return notOurs();
        auto connection = std::make_shared<QMetaObject::Connection>();
        auto timer = std::make_shared<QTimer>();
        const auto finish = [connection, timer] {
            QObject::disconnect(*connection);
            timer->stop();
        };
        *connection = connect(projects, &CodeProjects::worktreesListed, this,
                              [this, finish, notOurs, project, text, params, partner, folder, canonical, branch,
                               reply](const QString &id, const QVariantList &list, const QString &) {
                                  if (id != project)
                                      return;
                                  finish();
                                  for (const QVariant &w : list) {
                                      const QString listed = w.toMap().value(u"folder"_s).toString();
                                      if (QFileInfo(listed).canonicalFilePath() == canonical)
                                          return startThreadNow(project, text, params, partner, folder, branch, reply);
                                  }
                                  notOurs();
                              });
        timer->setSingleShot(true);
        connect(timer.get(), &QTimer::timeout, this, [finish, notOurs] {
            finish();
            notOurs();
        });
        timer->start(15000);
        return;
    }
    const std::optional<QStringList> pictures = storePictures(params, reply);
    if (!pictures)
        return;
    const QString path =
            project.isEmpty()
                    ? m_chats->send(QString(), text, page, *pictures, partner, parent, wake ? parent : QString())
                    : m_chats->startThreadFrom(project, text, partner, parent, wake ? parent : QString(), *pictures);
    if (path.isEmpty()) {
        dropPictures(*pictures);
        const QString failure = m_chats->turn(QString())->failure();
        return reply.fail(Failed, failure.isEmpty() ? u"The conversation could not be started"_s : failure, {});
    }
    reply.ok(row(path));
}

void ChatControl::startThreadNow(const QString &project, const QString &text, const QJsonObject &params,
                                 const QString &partner, const QString &worktree, const QString &branch,
                                 const Reply &reply)
{
    if (!ready(reply))
        return;
    const std::optional<QStringList> pictures = storePictures(params, reply);
    if (!pictures)
        return;
    const QString path = m_chats->startThread(project, text, *pictures, partner, worktree, branch);
    if (path.isEmpty()) {
        dropPictures(*pictures);
        const QString failure = m_chats->turn(QString())->failure();
        return reply.fail(Failed, failure.isEmpty() ? u"The thread could not be started"_s : failure, {});
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
    if (text.isEmpty() && params.value(u"pictures"_s).toArray().isEmpty())
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
    const QString mode = params.value(u"mode"_s).toString().toLower();
    const bool queue = params.value(u"queue"_s).toBool() || mode == u"queue";
    if (m_chats->isRunning(path) && mode != u"steer" && !queue)
        return reply.fail(Busy, u"Its turn runs: wait for it (chatWait), stop it (chatStop) or put the message in line (queue)"_s,
                          {{u"state"_s, m_chats->stateOf(path)}});
    const QString parent = m_chats->lineage()->parentOf(path);
    if (!m_chats->isRunning(path) && caller.granted && !parent.isEmpty()
        && m_chats->lineage()->runningChildren(parent) >= ChatLineage::MaxRunningChildren)
        return reply.fail(Refused,
                          u"%1 sub-conversations of its conversation run already: wait for one (chatWait)"_s.arg(
                                  ChatLineage::MaxRunningChildren),
                          {{u"reason"_s, u"children"_s}, {u"max"_s, ChatLineage::MaxRunningChildren}});
    const std::optional<QStringList> pictures = storePictures(params, reply);
    if (!pictures)
        return;
    if (m_chats->isRunning(path))
        return sendWhileRunning(path, text, *pictures, mode == u"steer", reply);
    if (m_chats->send(path, text, QString(), *pictures, QString(), QString(), woken).isEmpty()) {
        dropPictures(*pictures);
        const QString failure = m_chats->turn(path)->failure();
        return reply.fail(Failed, failure.isEmpty() ? u"The message could not be sent"_s : failure, {});
    }
    reply.ok(row(path));
}

void ChatControl::sendWhileRunning(const QString &path, const QString &text, const QStringList &pictures, bool steer,
                                   const Reply &reply)
{
    // Now, ahead of the waiting messages: the running turn stopped first (as T3 Code's steer); else
    // in line after the answer (as a message the user writes meanwhile).
    const bool sent = steer ? m_chats->steer(path, text, pictures) : m_chats->queue(path, text, pictures);
    if (!sent) {
        dropPictures(pictures);
        return reply.fail(Failed, u"The message could not be sent"_s, {});
    }
    QJsonObject out = row(path);
    out.insert(steer ? u"steered"_s : u"queued"_s, true);
    reply.ok(out);
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

// ---- chatLive: the conversations the app runs, as they go (core acp/appchats.h) ------------------

void ChatControl::noteTurns()
{
    // Every turn of the conversations is a child of WorkspaceChats (made when first asked for).
    const QList<ChatTurn *> turns = m_chats->findChildren<ChatTurn *>(Qt::FindDirectChildrenOnly);
    QSet<ChatTurn *> active;
    for (ChatTurn *t : turns) {
        if (!m_watched.contains(t)) {
            m_watched.insert(t);
            connect(t, &QObject::destroyed, this, [this, t] {
                m_watched.remove(t);
                m_active.remove(t);
            });
            const auto touched = [this, t] {
                m_activity.insert(t->path().toLower(), ++m_activitySerial);
                liveChanged();
            };
            connect(t, &ChatTurn::stateChanged, this, [this, touched] {
                noteTurns();
                touched();
            });
            connect(t, &ChatTurn::partialChanged, this, touched);
            connect(t, &ChatTurn::activityChanged, this, touched);
        }
        if (t->running() && !t->path().isEmpty())
            active.insert(t);
    }
    for (ChatTurn *t : std::as_const(active)) {
        if (!m_activity.contains(t->path().toLower()))
            m_activity.insert(t->path().toLower(), ++m_activitySerial);
    }
    m_active = active;
    // (The turns that ended lately are listed a while yet: the list changes when one leaves it.)
    m_recentEnded = int(recentlyEnded().size());
    if (m_recentEnded > 0 && !m_pruneTimer.isActive())
        m_pruneTimer.start();
}

QList<std::pair<QString, QDateTime>> ChatControl::recentlyEnded() const
{
    return m_chats->endedSince(m_workspace->now().addSecs(-60LL * acp::RecentChatMinutes));
}

void ChatControl::liveChanged()
{
    if (m_liveTimer.isActive())
        return;
    m_liveTimer.start(int(std::clamp<qint64>(LiveIntervalMs - m_sinceRevision.elapsed(), 0, LiveIntervalMs)));
}

void ChatControl::bumpRevision()
{
    m_revision = std::max(m_revision + 1, QDateTime::currentMSecsSinceEpoch());
    m_sinceRevision.restart();
    // The waits for an older revision answered (one answer for the waits of the same conversation).
    QHash<QString, QJsonObject> answers;
    const QList<std::shared_ptr<LiveWait>> waits = m_liveWaits;
    for (const std::shared_ptr<LiveWait> &w : waits) {
        if (w->done || w->since >= m_revision)
            continue;
        if (w->reply.connected && !w->reply.connected()) {
            w->done = true;
            m_liveWaits.removeAll(w);
            continue;
        }
        auto it = answers.find(w->path);
        if (it == answers.end())
            it = answers.insert(w->path, liveAnswer(w->path));
        w->done = true;
        w->timer.stop();
        m_liveWaits.removeAll(w);
        w->reply.ok(*it);
    }
}

void ChatControl::live(const QJsonObject &params, const Reply &reply)
{
    const qint64 since = params.contains(u"since"_s) ? params.value(u"since"_s).toInteger(-1) : -1;
    const int timeout = std::clamp(params.value(u"timeout"_s).toInt(DefaultLiveWaitMs), 0, MaxLiveWaitMs);
    const QString path = params.value(u"path"_s).toString().trimmed();
    if (since < 0 || m_revision > since || timeout == 0)
        return reply.ok(liveAnswer(path));
    auto w = std::make_shared<LiveWait>();
    w->reply = reply;
    w->since = since;
    w->path = path;
    w->timer.setSingleShot(true);
    w->timer.setInterval(timeout);
    const std::weak_ptr<LiveWait> weak = w;
    connect(&w->timer, &QTimer::timeout, this, [this, weak] {
        if (const std::shared_ptr<LiveWait> live = weak.lock())
            finishLive(live);
    });
    w->timer.start();
    m_liveWaits.append(w);
    if (!m_clientCheck.isActive())
        m_clientCheck.start();
}

void ChatControl::finishLive(const std::shared_ptr<LiveWait> &w)
{
    if (w->done)
        return;
    w->done = true;
    w->timer.stop();
    m_liveWaits.removeAll(w);
    w->reply.ok(liveAnswer(w->path));
}

const ChatControl::Facts &ChatControl::factsOf(const QString &path)
{
    const QString key = path.toLower();
    Facts &f = m_facts[key];
    const std::optional<FileStat> stat = m_workspace->vault() ? m_workspace->vault()->stat(path) : std::nullopt;
    const qint64 mtime = stat ? stat->mtime : -2;
    const qint64 size = stat ? stat->size : -2;
    if (f.mtime == mtime && f.size == size)
        return f;
    f = Facts{};
    f.mtime = mtime;
    f.size = size;
    if (!stat || !chat::inFolder(path))
        return f;
    bool ok = false;
    const chat::Conversation c = chat::parse(m_workspace->read(path, &ok));
    f.conversation = ok && !c.messages.isEmpty();
    f.messages = int(c.messages.size());
    f.partner = m_chats->partnerOf(path);
    f.project = c.project;
    f.worktree = c.worktree;
    f.branch = c.branch;
    return f;
}

QStringList ChatControl::livePaths()
{
    // The conversations whose turn runs, waits or asks, and those that ended lately: newest
    // activity first (side chats — the user's asides — are no conversation of the list). One that
    // ended before this list followed it (before the app shared them, or started again): by when.
    struct Entry {
        QString path;
        qint64 order = 0;
        qint64 ended = 0;
    };
    QList<Entry> entries;
    QSet<QString> seen;
    const auto consider = [&](const QString &path, qint64 ended) {
        const QString key = path.toLower();
        if (path.isEmpty() || seen.contains(key) || !m_chats->sideOf(path).isEmpty() || !factsOf(path).conversation)
            return;
        seen.insert(key);
        entries.append({path, m_activity.value(key, 0), ended});
    };
    for (ChatTurn *t : std::as_const(m_active))
        consider(t->path(), 0);
    for (const auto &[path, at] : recentlyEnded())
        consider(path, at.toMSecsSinceEpoch());
    std::ranges::sort(entries, [](const Entry &a, const Entry &b) {
        if (a.order != b.order)
            return a.order > b.order;
        return a.ended != b.ended ? a.ended > b.ended : a.path < b.path;
    });
    QStringList out;
    for (const Entry &e : std::as_const(entries)) {
        if (out.size() >= acp::MaxRemoteChats)
            break;
        out.append(e.path);
    }
    return out;
}

acp::RemoteChat ChatControl::liveRow(const QString &path)
{
    const Facts &f = factsOf(path);
    acp::RemoteChat c;
    c.path = path;
    c.name = m_chats->nameOf(path);
    c.partner = f.partner;
    const QString label = m_chats->partnerLabel(f.partner);
    c.partnerName = label.isEmpty() ? f.partner : label;
    QString failure;
    c.state = m_chats->stateOf(path, &failure);
    c.failure = failure.left(2000);
    if (m_chats->isRunning(path))
        c.status = m_chats->turn(path)->status();
    if (c.state == u"asking")
        c.question = m_chats->questionText(path).section(u'\n', 0, 0).simplified().left(500);
    c.project = f.project;
    c.worktree = f.worktree;
    c.branch = f.branch;
    c.messages = f.messages;
    c.waiting = int(m_chats->queued(path).size());
    c.held = c.waiting > 0 && m_chats->queueHeld(path);
    if (!c.active()) {
        for (const auto &[ended, at] : recentlyEnded()) {
            if (ended.compare(path, Qt::CaseInsensitive) == 0) {
                c.ended = isoTime(at);
                break;
            }
        }
    }
    return c;
}

void ChatControl::addRunning(acp::RemoteChatDetail &d, const QString &path) const
{
    const ChatTurn *t = m_chats->turn(path);
    d.status = t->status();
    bool cut = false;
    d.partial = acp::keepEnd(t->partial(), acp::MaxLiveTextChars, &cut);
    d.partialCut = cut;
    d.thoughts = acp::keepEnd(t->thoughts(), acp::MaxLiveThoughtsChars);
    // (The latest tool calls when there are more; the plan's first steps.)
    const QVariantList tools = t->tools();
    for (qsizetype i = std::max<qsizetype>(0, tools.size() - acp::MaxLiveTools); i < tools.size(); ++i) {
        const QVariantMap m = tools.at(i).toMap();
        const acp::RemoteChatTool tool{m.value(u"title"_s).toString().simplified().left(300),
                                       m.value(u"status"_s).toString().left(20), m.value(u"kind"_s).toString().left(20)};
        if (!tool.title.isEmpty())
            d.tools.append(tool);
    }
    for (const QVariant &v : t->plan()) {
        const QVariantMap m = v.toMap();
        const acp::RemoteChatStep step{m.value(u"content"_s).toString().simplified().left(500),
                                       m.value(u"status"_s).toString().left(20)};
        if (step.content.isEmpty())
            continue;
        d.plan.append(step);
        if (d.plan.size() >= acp::MaxLiveSteps)
            break;
    }
}

void ChatControl::addQuestion(acp::RemoteChatDetail &d, const QString &path) const
{
    const QString text = m_chats->questionText(path);
    for (const QString &option : m_chats->questionOptions(path)) {
        if (option.trimmed().isEmpty() || d.question.options.contains(option))
            continue;
        d.question.options.append(option);
        if (d.question.options.size() >= acp::MaxQuestionOptions)
            break;
    }
    d.question.text = text.section(u'\n', 0, 0).simplified().left(500);
    d.question.details = keepStart(text.section(u'\n', 1).trimmed(), acp::MaxQuestionChars);
    d.question.plan = keepStart(m_chats->questionPlan(path), acp::MaxLiveTextChars);
    const QVariantMap diff = m_chats->questionDiff(path);
    d.question.file = diff.value(u"path"_s).toString().left(acp::MaxRemotePathChars);
    if (!d.question.file.isEmpty()) {
        d.question.oldText = keepStart(diff.value(u"oldText"_s).toString(), acp::MaxLiveTextChars);
        d.question.newText = keepStart(diff.value(u"newText"_s).toString(), acp::MaxLiveTextChars);
    }
}

namespace {

/// An option of WorkspaceAgents::options() ({id, name, description, boolean, current, currentName,
/// choices: [{value, name, group}]}) as chatLive says it.
acp::RemoteChatOption liveOption(const QVariantMap &m)
{
    acp::RemoteChatOption o;
    o.id = m.value(u"id"_s).toString();
    o.name = m.value(u"name"_s).toString().simplified().left(200);
    o.description = m.value(u"description"_s).toString().simplified().left(500);
    o.boolean = m.value(u"boolean"_s).toBool();
    o.current = m.value(u"current"_s).toString().left(200);
    o.currentName = m.value(u"currentName"_s).toString().simplified().left(200);
    for (const QVariant &c : m.value(u"choices"_s).toList()) {
        const QVariantMap choice = c.toMap();
        o.choices.append({choice.value(u"value"_s).toString().left(200),
                          choice.value(u"name"_s).toString().simplified().left(200),
                          choice.value(u"group"_s).toString().simplified().left(200)});
        if (o.choices.size() >= acp::MaxOptionChoices)
            break;
    }
    return o;
}

} // namespace

void ChatControl::addOptions(acp::RemoteChatDetail &d, const QString &path) const
{
    const QString partner = m_chats->partnerOf(path);
    for (const QVariant &row : m_chats->agentOptions(path, partner)) {
        const acp::RemoteChatOption option = liveOption(row.toMap());
        if (option.id.isEmpty() || (!option.boolean && option.choices.isEmpty()))
            continue;
        d.options.append(option);
        if (d.options.size() >= acp::MaxLiveOptions)
            break;
    }
}

acp::RemoteChatDetail ChatControl::liveDetail(const QString &path)
{
    acp::RemoteChatDetail d;
    d.path = path;
    QString failure;
    d.state = m_chats->stateOf(path, &failure);
    d.failure = failure.left(2000);
    d.messages = factsOf(path).messages;
    if (m_chats->isRunning(path))
        addRunning(d, path);
    if (m_chats->asking(path))
        addQuestion(d, path);
    for (const QVariant &q : m_chats->queued(path)) {
        const QString text = keepStart(q.toMap().value(u"text"_s).toString(), 4000);
        if (text.trimmed().isEmpty())
            continue;
        d.waiting.append(text);
        if (d.waiting.size() >= acp::MaxLiveWaiting)
            break;
    }
    d.held = !d.waiting.isEmpty() && m_chats->queueHeld(path);
    addOptions(d, path);
    return d;
}

QJsonObject ChatControl::liveAnswer(const QString &wanted)
{
    // (One that ended longer ago leaves the list now: its going is a change too.)
    if (const int recent = int(recentlyEnded().size()); recent != m_recentEnded) {
        m_recentEnded = recent;
        liveChanged();
    }
    acp::RemoteChats out;
    out.revision = m_revision;
    for (const QString &path : livePaths())
        out.chats.append(liveRow(path));
    // One of them in full: the conversation the device shows (any, its turn running or not).
    QString path;
    if (!wanted.isEmpty()) {
        path = m_workspace->exists(wanted) ? wanted : resolve(wanted);
        if (!path.isEmpty() && !factsOf(path).conversation)
            path.clear();
    }
    if (!path.isEmpty())
        out.detail = liveDetail(path);
    return out.toJson();
}

void ChatControl::setOption(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    const QString path = conversationOf(params, reply);
    if (path.isEmpty() || !mayChange(caller, path, reply))
        return;
    const QString id = params.value(u"option"_s).toString().trimmed();
    const QString value = params.value(u"value"_s).toString();
    const QString partner = m_chats->partnerOf(path);
    QStringList ids;
    for (const QVariant &row : m_chats->agentOptions(path, partner)) {
        const acp::RemoteChatOption option = liveOption(row.toMap());
        ids << option.id;
        if (option.id != id)
            continue;
        const bool fits = option.boolean ? value == u"true" || value == u"false"
                                         : std::ranges::any_of(option.choices, [&value](const acp::RemoteChatChoice &c) {
                                               return c.value == value;
                                           });
        if (!fits) {
            QStringList values{u"true"_s, u"false"_s};
            if (!option.boolean) {
                values.clear();
                for (const acp::RemoteChatChoice &c : option.choices)
                    values << c.value;
            }
            return reply.fail(BadOption, u"%1 is no value of the option %2"_s.arg(value.left(200), id),
                              {{u"param"_s, u"value"_s}, {u"values"_s, QJsonArray::fromStringList(values)}});
        }
        m_chats->setAgentOption(path, partner, id, value);
        liveChanged();
        return reply.ok(QJsonObject{{u"path"_s, path}, {u"option"_s, id}, {u"value"_s, value}});
    }
    reply.fail(BadOption, u"%1 is no option of the conversation's AI agent"_s.arg(id.left(200)),
               {{u"param"_s, u"option"_s}, {u"options"_s, QJsonArray::fromStringList(ids)}});
}

void ChatControl::resume(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    const QString path = conversationOf(params, reply);
    if (path.isEmpty() || !mayChange(caller, path, reply))
        return;
    if (!m_chats->queueHeld(path))
        return reply.fail(NotHeld, u"No messages of that conversation are held"_s,
                          {{u"state"_s, m_chats->stateOf(path)}});
    reply.ok(QJsonObject{{u"path"_s, path}, {u"resumed"_s, m_chats->resumeQueue(path)}});
}

std::optional<QStringList> ChatControl::storePictures(const QJsonObject &params, const Reply &reply)
{
    QString why;
    const std::optional<QList<acp::RemotePicture>> pictures =
            acp::remotePicturesFromJson(params.value(u"pictures"_s), &why);
    if (!pictures) {
        reply.fail(BadPictures, why, {{u"param"_s, u"pictures"_s}});
        return std::nullopt;
    }
    QStringList stored;
    for (const acp::RemotePicture &picture : *pictures) {
        const QString path = m_workspace->importAttachment(picture.name, picture.data);
        if (path.isEmpty()) {
            dropPictures(stored);
            reply.fail(Failed, u"The picture %1 could not be stored in the notebook"_s.arg(picture.name), {});
            return std::nullopt;
        }
        if (!stored.contains(path))
            stored.append(path);
    }
    return stored;
}

void ChatControl::dropPictures(const QStringList &pictures)
{
    for (const QString &picture : pictures)
        m_workspace->discardImport(picture);
}

void ChatControl::answerQuestion(const QJsonObject &params, const Caller &caller, const Reply &reply)
{
    // The user's answer (a script of theirs, the phone through the agent sharing), never an agent's:
    // it would allow what it asked for itself.
    if (caller.granted)
        return reply.fail(Refused, u"An AI agent does not answer the questions meant for the user"_s, {});
    const QString path = conversationOf(params, reply);
    if (path.isEmpty())
        return;
    if (!m_chats->asking(path))
        return reply.fail(NoQuestion, u"Nothing is asked in that conversation now"_s,
                          {{u"state"_s, m_chats->stateOf(path)}});
    const QStringList options = m_chats->questionOptions(path);
    const QString option = params.value(u"option"_s).toString();
    // The option as given, else as a device shows it (its spaces made one, at most 200 characters).
    QString chosen = options.contains(option) ? option : QString();
    if (chosen.isEmpty()) {
        const QString shown = option.simplified().left(200);
        QStringList matching;
        for (const QString &o : options) {
            if (!shown.isEmpty() && o.simplified().left(200) == shown)
                matching << o;
        }
        if (matching.size() == 1)
            chosen = matching.first();
    }
    if (chosen.isEmpty())
        return reply.fail(BadOption, u"%1 is not one of the question's options"_s.arg(option.left(200)),
                          {{u"param"_s, u"option"_s}, {u"options"_s, QJsonArray::fromStringList(options)}});
    m_chats->answer(path, chosen);
    reply.ok(QJsonObject{{u"path"_s, path}, {u"answered"_s, true}, {u"option"_s, chosen}});
}

} // namespace brulion::shell

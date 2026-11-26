#include "workspacechats.h"

#include "agentshare.h"
#include "chatlineage.h"
#include "chatschedules.h"
#include "chattools.h"
#include "codeprojects.h"

#include "workspace.h"
#include "workspaceagents.h"
#include "workspaceextensions.h"

#include "capture.h"
#include "pickedfiles.h"
#ifdef Q_OS_WASM
#include "wasm/wasmfiles.h"
#endif

#include "ai/local.h"
#include "aireport/aireport.h"
#include "chat/chat.h"
#include "gitcli/client.h"
#include "log/categories.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QMimeData>
#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// The notebook's device state of the conversations with an unread answer: {path, lower case: ms}.
const QString UnreadKey = u"chat/unread"_s;

/// The first line of `text` with words as the list shows them (chat::preview), for a
/// notification.
QString firstLineOf(const QString &text)
{
    for (const QString &line : text.split(u'\n')) {
        const QString words = chat::preview(line, 200);
        if (!words.isEmpty())
            return words;
    }
    return chat::preview(text, 200);
}

} // namespace

// ---- ChatTurn -------------------------------------------------------------------------------------

ChatTurn::ChatTurn(const QString &path, QObject *parent) : QObject(parent), m_path(path)
{
    // The answer as it is written: shown ten times a second at most (a provider streams a word at
    // a time, every change renders the message anew, and a model on the device shares the
    // processor with that).
    m_partialTimer.setSingleShot(true);
    m_partialTimer.setInterval(100);
    connect(&m_partialTimer, &QTimer::timeout, this, [this] {
        const double rate = WorkspaceChats::tokensPerSecond(m_samples);
        setPartial(m_nextPartial, m_nextThoughts,
                   rate > 0 ? WorkspaceChats::tr("%1 tok/s").arg(QLocale().toString(rate, 'f', 1)) : QString());
    });
}

void ChatTurn::setPartial(const QString &text, const QString &thoughts, const QString &speed)
{
    const QString tail = WorkspaceChats::thoughtsTail(thoughts);
    const QString all = thoughts.trimmed();
    if (text == m_partial && tail == m_thoughts && all == m_allThoughts && speed == m_speed)
        return;
    const bool newText = text != m_partial;
    m_partial = text;
    m_thoughts = tail;
    m_allThoughts = all;
    m_speed = speed;
    if (newText) {
        auto [settled, rest] = WorkspaceChats::splitWritten(text);
        m_tail = rest;
        if (settled != m_settled) {
            m_settled = settled;
            Q_EMIT settledChanged();
        }
    }
    Q_EMIT partialChanged();
}

void ChatTurn::setActivity(const QVariantList &tools, const QVariantList &plan)
{
    if (tools == m_tools && plan == m_plan)
        return;
    m_tools = tools;
    m_plan = plan;
    Q_EMIT activityChanged();
}

void ChatTurn::clearPartial()
{
    m_partialTimer.stop();
    m_samples.clear();
    m_nextPartial.clear();
    m_nextThoughts.clear();
    setPartial(QString(), QString(), QString());
}

// ---- WorkspaceChats -------------------------------------------------------------------------------

WorkspaceChats::WorkspaceChats(Workspace *workspace, WorkspaceExtensions *extensions, QObject *parent)
    : QObject(parent)
    , m_workspace(workspace)
    , m_extensions(extensions)
    , m_agents(new WorkspaceAgents(workspace, this))
    , m_share(new AgentShare(workspace, m_agents, this))
    , m_limits(new AgentLimits(m_agents, this))
    , m_usage(new AgentUsage(workspace, m_agents, this))
    , m_lineage(new ChatLineage(workspace, this, this))
    , m_schedules(new ChatSchedules(workspace, this, this))
    , m_secrets(new ChatSecrets(workspace, this, this))
    , m_hooks(new HookServer(this))
{
    // A conversation's sub-conversations (an agent's tasks for other partners) are told which one
    // they come from: WorkspaceAgents gives their agents no tools that change the app, and none of
    // the chat's from MaxDepth levels down.
    m_agents->setLineage([workspace](const QString &path) { return ChatLineage::depth(workspace, path); },
                         [workspace](const QString &path) { return !ChatLineage::writtenParent(workspace, path).isEmpty(); });
    connect(m_extensions, &WorkspaceExtensions::chatAnswered, this, &WorkspaceChats::answered);
    connect(m_extensions, &WorkspaceExtensions::chatFailed, this, &WorkspaceChats::failed);
    connect(m_extensions, &WorkspaceExtensions::chatStatus, this, &WorkspaceChats::setStatus);
    connect(m_extensions, &WorkspaceExtensions::chatPartial, this, &WorkspaceChats::setWriting);
    connect(m_extensions, &WorkspaceExtensions::commandsChanged, this, &WorkspaceChats::partnersChanged);
    connect(this, &WorkspaceChats::partnersChanged, this, &WorkspaceChats::defaultPartnerChanged);
    connect(m_extensions, &WorkspaceExtensions::questionChanged, this, &WorkspaceChats::questionsMoved);
    connect(m_extensions, &WorkspaceExtensions::chatQuestionsChanged, this, &WorkspaceChats::questionsMoved);
    // An AI agent's turns (WorkspaceAgents) end, say and ask the same way.
    connect(m_agents, &WorkspaceAgents::answered, this, &WorkspaceChats::answered);
    connect(m_agents, &WorkspaceAgents::failed, this, &WorkspaceChats::failed);
    connect(m_agents, &WorkspaceAgents::status, this, &WorkspaceChats::setStatus);
    connect(m_agents, &WorkspaceAgents::partial, this, [this](const QString &key, const QString &text, const QString &thoughts) {
        setWriting(key, text, thoughts, 0, 0);
    });
    connect(m_agents, &WorkspaceAgents::questionChanged, this, [this](const QString &key) {
        questionsMoved();
        // A question nobody sees: said where the user is (a toast that opens the conversation).
        if (ChatTurn *t = m_byKey.value(key); t && !isShown(t->m_path)) {
            const QString text = m_agents->questionText(key).section(u'\n', 0, 0);
            if (!text.isEmpty())
                Q_EMIT questionWaiting(t->m_path, text);
        }
    });
    connect(m_agents, &WorkspaceAgents::activityChanged, this, [this](const QString &key) {
        if (ChatTurn *t = m_byKey.value(key))
            t->setActivity(m_agents->tools(key), m_agents->plan(key));
    });
    connect(m_agents, &WorkspaceAgents::sessionChanged, this, [this] {
        ++m_agentSerial;
        Q_EMIT agentsChanged();
    });
    connect(m_agents, &WorkspaceAgents::checked, this, &WorkspaceChats::agentChecked);
    connect(m_agents, &WorkspaceAgents::changesChanged, this, [this] {
        ++m_changesSerial;
        Q_EMIT changesChanged();
    });
    connect(m_agents, &WorkspaceAgents::approvalsChanged, this, [this] {
        ++m_agentSerial;
        Q_EMIT agentsChanged();
    });
    // A question the approvals answered: what was allowed, as what the agent does now.
    connect(m_agents, &WorkspaceAgents::autoAnswered, this, &WorkspaceChats::setStatus);
    connect(m_agents, &WorkspaceAgents::paired, this,
            [this](bool ok, const QString &text, const QList<AgentConfig> &agents) {
                m_agentPairing = false;
                if (ok)
                    Q_EMIT agentsPaired(agents);
                Q_EMIT agentPairingChanged();
                Q_EMIT agentHostPaired(ok, text);
            });
    connect(m_agents, &WorkspaceAgents::remembered, this, &WorkspaceChats::agentRemembered);
    // A pairing without a code the computer did not take: its code is asked for.
    connect(m_agents, &WorkspaceAgents::codeNeeded, this, &WorkspaceChats::agentHostNeedsCode);
    // A turn an agent on another computer went on with while this device was gone (Android ended
    // the app): running here again until that computer says how it ended.
    connect(m_agents, &WorkspaceAgents::recovering, this, [this](const QString &path) {
        ChatTurn *t = turn(path);
        if (t->m_running)
            return;
        t->m_key = path;
        m_byKey.insert(path, t);
        t->m_running = true;
        t->m_queued = false;
        t->m_place = 0;
        t->m_omitted = 0;
        t->m_status.clear();
        t->m_failure.clear();
        t->clearPartial();
        Q_EMIT t->stateChanged();
        Q_EMIT stateChanged();
    });
    // A secret an agent asks for (chatsecrets.h): the conversation asks meanwhile; nobody sees it:
    // said where the user is, as a question is (never with the value: there is none yet).
    connect(m_secrets, &ChatSecrets::changed, this, &WorkspaceChats::questionsMoved);
    // (A hook's address follows its server's: the schedules' rows show it.)
    connect(m_hooks, &HookServer::changed, m_schedules, &ChatSchedules::changed);
    connect(m_secrets, &ChatSecrets::asked, this, [this](const QString &path, const QString &text) {
        if (!isShown(path))
            Q_EMIT questionWaiting(path, text);
    });
    connect(m_workspace, &Workspace::readyChanged, this, &WorkspaceChats::recoverAgentTurns);
    connect(m_extensions, &WorkspaceExtensions::aiSettingsChanged, this, [this] {
        Q_EMIT settingsChanged();
        startWaiting(); // (a provider on the network answers every conversation at once)
    });
    // The page of a turn renamed or moved: the turn follows it (its answer goes where it is now);
    // deleted: the turn ends, nothing is written (a page made anew would hold the answer alone).
    connect(m_workspace, &Workspace::pathRenamed, this, [this](const QString &from, const QString &to) {
        ChatTurn *t = m_turns.take(from.toLower());
        if (!t)
            return;
        if (ChatTurn *other = m_turns.take(to.toLower()); other && other != t)
            other->deleteLater();
        t->m_path = to;
        m_turns.insert(to.toLower(), t);
        Q_EMIT t->stateChanged();
        Q_EMIT stateChanged();
    });
    // (The waiting messages and the draft follow their conversation, whether a turn ran or not.)
    connect(m_workspace, &Workspace::pathRenamed, this, &WorkspaceChats::moveQueue);
    connect(m_workspace, &Workspace::pathRenamed, this, [this](const QString &from, const QString &to) {
        if (const QString text = draft(from); !text.isEmpty()) {
            setDraft(from, QString());
            setDraft(to, text);
        }
    });
    // An unread answer follows its conversation (a rename, a move) and goes with it.
    connect(m_workspace, &Workspace::pathRenamed, this, [this](const QString &from, const QString &to) {
        if (!isUnread(from))
            return;
        setUnread(from, false);
        if (!isShown(to))
            setUnread(to, true);
    });
    // A sub-conversation that wakes its parent: both may be renamed meanwhile; a deleted one wakes
    // nobody (a deleted parent is not woken: wake()).
    connect(m_workspace, &Workspace::pathRenamed, this, [this](const QString &from, const QString &to) {
        loadWakes();
        bool moved = false;
        if (m_wakes.contains(from.toLower())) {
            m_wakes.insert(to.toLower(), m_wakes.take(from.toLower()));
            moved = true;
        }
        for (QString &parent : m_wakes) {
            if (parent.compare(from, Qt::CaseInsensitive) == 0) {
                parent = to;
                moved = true;
            }
        }
        if (m_woken.contains(from.toLower()))
            m_woken.insert(to.toLower(), m_woken.take(from.toLower()));
        if (moved) {
            saveWakes();
            Q_EMIT wakesChanged();
        }
    });
    connect(m_workspace, &Workspace::pathRemoved, this, [this](const QString &path) {
        if (wakesParent(path))
            setWake(path, QString());
        if (ChatTurn *t = find(path); t && t->m_running)
            stop(path);
        moveQueue(path, QString());
        m_rewound.remove(path.toLower());
        setDraft(path, QString());
        setUnread(path, false);
    });
    connect(this, &WorkspaceChats::stateChanged, this, &WorkspaceChats::updateRunningShown);
    connect(this, &WorkspaceChats::shownChanged, this, &WorkspaceChats::updateRunningShown);
    // Conversations untouched for a while: archived when the notebook is ready, then every hour.
    m_settleTimer.setInterval(3600 * 1000);
    connect(&m_settleTimer, &QTimer::timeout, this, &WorkspaceChats::archiveUntouched);
    connect(m_workspace, &Workspace::readyChanged, this, [this] {
        if (m_workspace->ready() && m_autoArchiveDays > 0)
            archiveUntouched();
    });
}

void WorkspaceChats::setStatus(const QString &key, const QString &text)
{
    ChatTurn *t = m_byKey.value(key);
    if (!t || text == t->m_status)
        return;
    t->m_status = text;
    Q_EMIT t->stateChanged();
}

void WorkspaceChats::setWriting(const QString &key, const QString &text, const QString &thoughts, int tokens,
                                qint64 writingMs)
{
    ChatTurn *t = m_byKey.value(key);
    if (!t)
        return;
    t->m_nextPartial = text;
    t->m_nextThoughts = thoughts;
    if (text.isEmpty() && thoughts.isEmpty() && tokens == 0) {
        t->clearPartial(); // (a new request: at once)
        return;
    }
    if (tokens > 0) {
        t->m_samples.append({writingMs, tokens});
        while (t->m_samples.size() > 200)
            t->m_samples.removeFirst();
    }
    if (!t->m_partialTimer.isActive())
        t->m_partialTimer.start();
}

void WorkspaceChats::questionsMoved()
{
    ++m_questionSerial;
    // A turn that asks now came back to the user (the list puts it first; a notification says
    // what it asks); one whose question went, no longer.
    bool moved = false;
    for (ChatTurn *t : std::as_const(m_turns)) {
        const bool asks = t->m_running && (asking(t->m_path) || m_secrets->pending(t->m_path));
        if (asks == t->m_asking)
            continue;
        t->m_asking = asks;
        moved = true;
        if (asks) {
            t->m_cameBack = stamp();
            notifyUser(t->m_path, questionText(t->m_path).section(u'\n', 0, 0).trimmed());
            wake(t->m_path, int(chat::Wake::Asking), questionText(t->m_path));
        } else if (t->m_failure.isEmpty()) {
            t->m_cameBack = 0;
        }
    }
    Q_EMIT questionsChanged();
    if (moved)
        Q_EMIT attentionChanged();
}

QVariantList WorkspaceChats::partners() const
{
    return m_extensions->chatPartners() + m_agents->partners();
}

QString WorkspaceChats::partnerLabel(const QString &partner) const
{
    return WorkspaceAgents::isAgent(partner) ? m_agents->label(partner) : m_extensions->partnerLabel(partner);
}

void WorkspaceChats::setAgents(const QList<AgentConfig> &agents)
{
    if (agents == m_agents->agents())
        return;
    m_agents->setAgents(agents);
    m_share->agentsChanged();
    m_limits->agentsChanged();
    m_usage->agentsChanged();
    Q_EMIT partnersChanged();
    recoverAgentTurns();
}

void WorkspaceChats::recoverAgentTurns()
{
    if (!m_workspace->ready())
        return;
    for (const QString &path : m_agents->recoverable())
        m_agents->recover(path);
}

bool WorkspaceChats::agentsAvailable() const
{
    return WorkspaceAgents::available() || WorkspaceAgents::remoteAvailable();
}

bool WorkspaceChats::isAgent(const QString &partner) const
{
    return WorkspaceAgents::isAgent(partner);
}

QString WorkspaceChats::toolSummary(const QVariantList &tools) const
{
    return shell::toolSummary(tools);
}

QVariantList WorkspaceChats::agentOptions(const QString &path, const QString &partner, int) const
{
    return isAgent(partner) ? m_agents->options(path, partner) : QVariantList();
}

QVariantList WorkspaceChats::agentChoices(const QString &path, const QString &partner, const QString &option, int) const
{
    for (const QVariant &row : agentOptions(path, partner)) {
        const QVariantMap o = row.toMap();
        if (o.value(u"id"_s).toString() == option)
            return o.value(u"choices"_s).toList();
    }
    return {};
}

void WorkspaceChats::setAgentOption(const QString &path, const QString &partner, const QString &option,
                                    const QString &value)
{
    if (isAgent(partner))
        m_agents->setOption(path, partner, option, value);
}

QVariantList WorkspaceChats::agentCommands(const QString &path, const QString &partner, const QString &typed, int) const
{
    // A command being written: "/" and its name, nothing after it yet.
    if (!typed.startsWith(u'/') || typed.contains(u' ') || typed.contains(u'\n'))
        return {};
    const QString prefix = typed.mid(1);
    QVariantList out;
    // The app's own: a side question, whoever answers (never sent to an agent).
    const QString aside = u"btw"_s;
    if (!path.isEmpty() && aside.startsWith(prefix, Qt::CaseInsensitive) && !isSideChat(path))
        out.append(
                QVariantMap{{u"name"_s, aside},
                            {u"description"_s, tr("Ask a side question: answered beside this conversation, not in it")},
                            {u"hint"_s, tr("question")},
                            {u"app"_s, true}});
    if (!isAgent(partner))
        return out;
    for (const QVariant &row : m_agents->commands(path, partner)) {
        const QString name = row.toMap().value(u"name"_s).toString();
        if (name.compare(aside, Qt::CaseInsensitive) != 0 && name.startsWith(prefix, Qt::CaseInsensitive))
            out.append(row);
        if (out.size() == 12)
            break;
    }
    return out;
}

QString WorkspaceChats::agentUsage(const QString &path, int) const
{
    return m_agents->usage(path);
}

void WorkspaceChats::checkAgent(const QString &id)
{
    m_agents->check(id);
}

void WorkspaceChats::pairAgentHost(const QString &address, const QString &code)
{
    if (m_agentPairing)
        return;
    m_agentPairing = true;
    Q_EMIT agentPairingChanged();
    m_agents->pair(address, code);
}

void WorkspaceChats::addOfferedAgent(const QString &address, const QString &agent)
{
    if (m_agentPairing)
        return;
    m_agentPairing = true;
    Q_EMIT agentPairingChanged();
    m_agents->pair(address, QString(), agent);
}

bool WorkspaceChats::asking(const QString &path, int) const
{
    return !path.isEmpty()
           && (!m_agents->questionOptions(path).isEmpty() || m_extensions->chatQuestionKind(path) == u"choose");
}

QString WorkspaceChats::questionText(const QString &path, int) const
{
    if (!m_agents->questionOptions(path).isEmpty())
        return m_agents->questionText(path);
    if (m_extensions->chatQuestionKind(path) == u"choose")
        return m_extensions->chatQuestionTitle(path);
    // (A secret asked for: its card's line — the list's row, a parent's wait, the notification.)
    return m_secrets->questionText(path);
}

QStringList WorkspaceChats::questionOptions(const QString &path, int) const
{
    if (const QStringList agent = m_agents->questionOptions(path); !agent.isEmpty())
        return agent;
    return m_extensions->chatQuestionKind(path) == u"choose" ? m_extensions->chatQuestionOptions(path) : QStringList();
}

QVariantMap WorkspaceChats::questionDiff(const QString &path, int) const
{
    return m_agents->questionOptions(path).isEmpty() ? QVariantMap() : m_agents->questionDiff(path);
}

void WorkspaceChats::answer(const QString &path, const QString &option)
{
    if (!m_agents->questionOptions(path).isEmpty())
        m_agents->answer(path, option);
    else
        m_extensions->answerChat(path, option);
}

QString WorkspaceChats::questionPlan(const QString &path, int) const
{
    return m_agents->questionOptions(path).isEmpty() ? QString() : m_agents->questionPlan(path);
}

QString WorkspaceChats::savePlan(const QString &path)
{
    const QString plan = questionPlan(path);
    if (plan.isEmpty() || !m_workspace->ready())
        return {};
    const QString title = tr("Plan – %1").arg(nameOf(path));
    const QString target = paths::uniquePath(paths::sanitizeFileName(title) + u".md"_s,
                                             [this](const QString &p) { return m_workspace->exists(p); });
    const QString from = u"[["_s + (path.endsWith(u".md"_s, Qt::CaseInsensitive) ? path.chopped(3) : path) + u"]]"_s;
    return m_workspace->write(target, plan + u"\n\n"_s + tr("From the conversation %1").arg(from) + u'\n') ? target
                                                                                                         : QString();
}

QString WorkspaceChats::approvals(const QString &path, int) const
{
    return m_agents->approvals(path);
}

void WorkspaceChats::setApprovals(const QString &path, const QString &mode)
{
    m_agents->setApprovals(path, mode);
}

QVariantList WorkspaceChats::changes(const QString &path, int) const
{
    return m_agents->changes(path);
}

QString WorkspaceChats::undoChanges(const QString &path)
{
    const QVariantMap result = m_agents->undoChanges(path);
    const int undone = result.value(u"undone"_s).toInt();
    const int skipped = int(result.value(u"skipped"_s).toStringList().size());
    if (skipped == 0)
        return tr("%n file(s) put back as they were", "", undone);
    return tr("%n file(s) put back", "", undone) + u"; "_s
           + tr("%n changed since or not known before: left as they are (their history has them)", "", skipped);
}

void WorkspaceChats::forgetChanges(const QString &path)
{
    m_agents->forgetChanges(path);
}

double WorkspaceChats::tokensPerSecond(const QList<std::pair<qint64, int>> &samples, qint64 windowMs)
{
    if (samples.isEmpty())
        return 0;
    const auto &[lastMs, lastTokens] = samples.last();
    // The first sample within the window.
    qsizetype from = samples.size() - 1;
    while (from > 0 && lastMs - samples.at(from - 1).first <= windowMs)
        --from;
    const auto &[firstMs, firstTokens] = samples.at(from);
    if (lastMs - firstMs >= windowMs / 2 && lastTokens > firstTokens)
        return 1000.0 * (lastTokens - firstTokens) / double(lastMs - firstMs);
    // Too short a window yet: since the writing began.
    return lastMs > 0 ? 1000.0 * lastTokens / double(lastMs) : 0;
}

QString WorkspaceChats::thoughtsTail(const QString &thoughts, int chars)
{
    const QString text = thoughts.trimmed();
    if (text.size() <= chars)
        return text;
    qsizetype from = text.size() - chars;
    // From the next word on (a long word: where it is).
    const qsizetype space = text.indexOf(u' ', from);
    if (space >= 0 && space < text.size() - chars / 2)
        from = space + 1;
    if (text.at(from).isLowSurrogate())
        ++from;
    return u"…"_s + text.mid(from).trimmed();
}

std::pair<QString, QString> WorkspaceChats::splitWritten(const QString &markdown)
{
    qsizetype cut = -1;
    bool blank = false; // the line before was blank
    QString fence;      // the open code block's fence ("```", "~~~~"), "" outside one
    for (qsizetype at = 0; at < markdown.size();) {
        qsizetype end = markdown.indexOf(u'\n', at);
        if (end < 0)
            end = markdown.size();
        const QStringView line = QStringView(markdown).sliced(at, end - at);
        const QStringView trimmed = line.trimmed();
        // A fence: up to three spaces, then ``` or ~~~ (one closes only its own kind, as long).
        qsizetype indent = 0;
        while (indent < line.size() && indent < 4 && line.at(indent) == u' ')
            ++indent;
        const QStringView rest = line.sliced(indent);
        qsizetype run = 0;
        while (run < rest.size() && (rest.at(run) == u'`' || rest.at(run) == u'~') && rest.at(run) == rest.at(0))
            ++run;
        const bool isFence = indent < 4 && run >= 3;
        if (fence.isEmpty()) {
            if (blank && at > 0 && !line.isEmpty() && !line.at(0).isSpace())
                cut = at; // a block at the margin after a blank line
            if (isFence)
                fence = rest.left(run).toString();
        } else if (isFence && rest.at(0) == fence.at(0) && run >= fence.size() && rest.sliced(run).trimmed().isEmpty()) {
            fence.clear();
        }
        blank = trimmed.isEmpty();
        at = end + 1;
    }
    if (cut <= 0)
        return {QString(), markdown};
    QString settled = markdown.left(cut);
    while (!settled.isEmpty() && settled.back().isSpace())
        settled.chop(1);
    return {settled, markdown.mid(cut)};
}

int WorkspaceChats::runningCount() const
{
    int n = 0;
    for (const ChatTurn *t : m_turns)
        n += t->m_running ? 1 : 0;
    return n;
}

bool WorkspaceChats::onDevice() const
{
    return m_extensions->aiSettings().provider == u"local";
}

int WorkspaceChats::historyBudget() const
{
    return onDevice() ? DeviceBudget : NetworkBudget;
}

bool WorkspaceChats::seesPictures() const
{
    const ai::Settings &settings = m_extensions->aiSettings();
    if (settings.provider != u"local")
        return true; // (Claude sees; a server's model that does not says so in its error)
    const ai::LocalModel *model = ai::localModel(settings.model.trimmed());
    return model && model->sees();
}

void WorkspaceChats::updateRunningShown()
{
    bool any = false, all = true;
    for (const ChatTurn *t : std::as_const(m_turns)) {
        if (!t->m_running)
            continue;
        any = true;
        all = all && isShown(t->m_path);
    }
    const bool shown = any && all;
    if (shown == m_runningShown)
        return;
    m_runningShown = shown;
    Q_EMIT runningShownChanged();
}

QString WorkspaceChats::folder() const { return chat::folder(); }

void WorkspaceChats::showing(const QString &path, bool shown)
{
    if (path.isEmpty())
        return;
    const QString key = path.toLower();
    const int n = std::max(0, m_shown.value(key) + (shown ? 1 : -1));
    if (n == 0)
        m_shown.remove(key);
    else
        m_shown.insert(key, n);
    Q_EMIT shownChanged();
    if (shown && isUnread(path))
        setUnread(path, false); // (read)
    if (shown)
        m_workspace->notifications()->markOpened(path); // (its notifications seen too)
}

bool WorkspaceChats::isShown(const QString &path) const { return !path.isEmpty() && m_shown.contains(path.toLower()); }

bool WorkspaceChats::isConversation(const QString &path) const
{
    if (!chat::inFolder(path) || !m_workspace->ready())
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    return ok && !chat::parse(content).messages.isEmpty();
}

ChatTurn *WorkspaceChats::find(const QString &path) const
{
    return m_turns.value(path.toLower());
}

bool WorkspaceChats::isRunning(const QString &path) const
{
    const ChatTurn *t = find(path);
    return t && t->m_running;
}

bool WorkspaceChats::isQueued(const QString &path) const
{
    const ChatTurn *t = find(path);
    return t && t->m_queued;
}

QString WorkspaceChats::stateOf(const QString &path, QString *failure) const
{
    const ChatTurn *t = find(path);
    if (failure)
        *failure = t && !t->m_running ? t->m_failure : QString();
    if (t && t->m_running)
        return (asking(path) || m_secrets->pending(path)) ? u"asking"_s : (t->m_queued ? u"queued"_s : u"running"_s);
    return t && !t->m_failure.isEmpty() ? u"failed"_s : u"idle"_s;
}

ChatTurn *WorkspaceChats::turn(const QString &path)
{
    if (ChatTurn *t = find(path))
        return t;
    auto *t = new ChatTurn(path, this);
    m_turns.insert(path.toLower(), t);
    return t;
}

QString WorkspaceChats::send(const QString &path, const QString &text, const QString &page, const QStringList &pictures,
                             const QString &partner, const QString &parent, const QString &wakes)
{
    return sendMessage(path, text, page, pictures, partner, parent, wakes, nullptr);
}

QString WorkspaceChats::startThreadFrom(const QString &project, const QString &text, const QString &partner,
                                        const QString &parent, const QString &wakes)
{
    if (!m_workspace->projects()->contains(project)) {
        setFailure(turn(QString()), tr("No such project on this device: add its folder in Code mode."));
        return {};
    }
    const QString who = partner.trimmed().isEmpty() ? threadPartner() : partner.trimmed();
    if (!WorkspaceAgents::isAgent(who)) {
        setFailure(turn(QString()), tr("A thread is answered by an AI agent such as Claude Code or Codex: add one in "
                                       "Settings → Extensions → AI agents."));
        return {};
    }
    const ThreadPlace place{project, QString(), QString()};
    return sendMessage(QString(), text, QString(), {}, who, parent, wakes, &place);
}

QString WorkspaceChats::startThread(const QString &project, const QString &text, const QStringList &pictures,
                                    const QString &partner, const QString &worktree, const QString &branch)
{
    if (!m_workspace->projects()->contains(project)) {
        setFailure(turn(QString()), tr("No such project on this device: add its folder in Code mode."));
        return {};
    }
    // An AI agent answers a thread: the one chosen, else the default one, else the first set up.
    QString who = partner.trimmed();
    if (who.isEmpty())
        who = threadPartner();
    if (!WorkspaceAgents::isAgent(who)) {
        setFailure(turn(QString()), tr("A thread is answered by an AI agent such as Claude Code or Codex: add one in "
                                       "Settings → Extensions → AI agents."));
        return {};
    }
    const ThreadPlace place{project, worktree.trimmed(), branch.trimmed()};
    return sendMessage(QString(), text, QString(), pictures, who, QString(), QString(), &place);
}

namespace {

const QString CheckpointsKey = u"code/checkpoints"_s;

} // namespace

void WorkspaceChats::checkpointThenRun(ChatTurn *t, const QString &folder, int message, const QString &partner,
                                       const QVariantList &history, const QString &page)
{
    if (!m_git)
        m_git = new gitcli::Client(this);
    const QString key = t->m_key;
    const int token = ++m_checkpointToken;
    m_checkpointing.insert(key, token);
    t->m_status = tr("Saving a checkpoint…");
    Q_EMIT t->stateChanged();
    const QString id = gitcli::Client::newCheckpointId();
    const QPointer<ChatTurn> guard(t);
    m_git->checkpoint(
            folder, id,
            [this, guard, key, token, folder, id, message, partner, history, page](const gitcli::Outcome &outcome) {
                // (Stopped meanwhile, or another turn of it started: nothing more for this one.)
                if (m_checkpointing.value(key) != token)
                    return;
                m_checkpointing.remove(key);
                if (!guard || !guard->m_running || guard->m_key != key)
                    return;
                // (No git there, or a folder git refuses: the turn goes on without one.)
                if (outcome.ok)
                    keepCheckpoint(guard->m_path, folder, id, message);
                else
                    qCInfo(logging::lcApp) << "No checkpoint of" << folder << ':' << outcome.message;
                guard->m_status.clear();
                Q_EMIT guard->stateChanged();
                m_agents->run(key, partner, history, page);
            });
}

void WorkspaceChats::keepCheckpoint(const QString &path, const QString &folder, const QString &id, int message)
{
    LocalStore *store = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!store)
        return;
    QJsonObject all = store->get(CheckpointsKey).toObject();
    QJsonArray list = all.value(path.toLower()).toArray();
    list.append(QJsonObject{{u"id"_s, id},
                            {u"at"_s, m_workspace->now().toString(u"yyyy-MM-dd HH:mm"_s)},
                            {u"message"_s, message},
                            {u"folder"_s, folder}});
    QStringList dropped;
    while (list.size() > MaxCheckpoints) {
        dropped << list.first().toObject().value(u"id"_s).toString();
        list.removeFirst();
    }
    all.insert(path.toLower(), list);
    store->set(CheckpointsKey, all);
    // (The oldest ones' refs go too: git may collect their objects.)
    if (!dropped.isEmpty() && m_git)
        m_git->dropCheckpoints(folder, dropped, [](const gitcli::Outcome &) { });
    ++m_checkpointSerial;
    Q_EMIT checkpointsChanged();
}

QVariantList WorkspaceChats::checkpoints(const QString &path, int serial) const
{
    Q_UNUSED(serial);
    const LocalStore *store = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    QVariantList out;
    if (!store || path.isEmpty())
        return out;
    for (const QJsonValue &v : store->get(CheckpointsKey).toObject().value(path.toLower()).toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(u"id"_s).toString().isEmpty())
            out << QVariantMap{{u"id"_s, o.value(u"id"_s).toString()},
                               {u"at"_s, o.value(u"at"_s).toString()},
                               {u"message"_s, o.value(u"message"_s).toInt()}};
    }
    return out;
}

QString WorkspaceChats::lastCheckpoint(const QString &path, int serial) const
{
    const QVariantList all = checkpoints(path, serial);
    return all.isEmpty() ? QString() : all.last().toMap().value(u"id"_s).toString();
}

QVariantList WorkspaceChats::agentPartners() const
{
    QVariantList out;
    for (const QVariant &p : partners())
        if (WorkspaceAgents::isAgent(p.toMap().value(u"id"_s).toString()))
            out << p;
    return out;
}

QString WorkspaceChats::threadPartner() const
{
    if (WorkspaceAgents::isAgent(defaultPartner()))
        return defaultPartner();
    const QVariantList agents = agentPartners();
    return agents.isEmpty() ? QString() : agents.first().toMap().value(u"id"_s).toString();
}

QVariantMap WorkspaceChats::threadPlace(const QString &path) const
{
    if (path.isEmpty() || !m_workspace->ready())
        return {};
    bool ok = false;
    const chat::Conversation c = chat::parse(m_workspace->read(path, &ok));
    if (!ok || c.project.isEmpty())
        return {};
    const CodeProjects *projects = m_workspace->projects();
    const bool known = projects->contains(c.project);
    const QString folder = !c.worktree.isEmpty() ? c.worktree : projects->folderOf(c.project);
    return {{u"project"_s, c.project},
            {u"projectName"_s, known ? projects->nameOf(c.project) : c.project},
            {u"worktree"_s, c.worktree},
            {u"branch"_s, c.branch},
            {u"folder"_s, known || !c.worktree.isEmpty() ? folder : QString()},
            {u"known"_s, known},
            {u"exists"_s, !folder.isEmpty() && QFileInfo(folder).isDir()}};
}

QString WorkspaceChats::threadFolder(const QString &path) const
{
    const QVariantMap place = threadPlace(path);
    return place.value(u"exists"_s).toBool() ? place.value(u"folder"_s).toString() : QString();
}

bool WorkspaceChats::setThreadWorktree(const QString &path, const QString &worktree, const QString &branch)
{
    bool ok = false;
    const QString content = path.isEmpty() ? QString() : m_workspace->read(path, &ok);
    if (!ok || chat::parse(content).project.isEmpty())
        return false;
    const QString folder = worktree.trimmed();
    QString next = chat::setField(content, u"worktree", folder.isEmpty() ? QString() : QString(u'"' + folder + u'"'));
    next = chat::setField(next, u"branch", folder.isEmpty() ? QString() : branch.trimmed());
    return next == content || m_workspace->write(path, next);
}

QString WorkspaceChats::sendMessage(const QString &path, const QString &text, const QString &page,
                                    const QStringList &pictures, const QString &partner, const QString &parent,
                                    const QString &wakes, const ThreadPlace *thread)
{
    const QString message = text.trimmed();
    QStringList sent;
    for (const QString &picture : pictures) {
        if (!picture.trimmed().isEmpty() && !sent.contains(picture.trimmed()))
            sent.append(picture.trimmed());
    }
    if ((message.isEmpty() && sent.isEmpty()) || !m_workspace->ready())
        return {};
    if (ChatTurn *t = path.isEmpty() ? nullptr : find(path); t && t->m_running)
        return {};
    const QString language = m_extensions->language();
    const QString who = path.isEmpty() ? partner.trimmed() : partnerOf(path);
    if (who.isEmpty() && !m_extensions->enabled().contains(u"asystent"_s)) {
        setFailure(turn(path), tr("The AI assistant is off: switch it on in Settings → Extensions → AI."));
        return {};
    }
    const QString markdown = chat::withPictures(message, sent);
    const QDateTime now = m_workspace->now();
    QString target = path;
    if (target.isEmpty()) {
        // A new conversation: its page, named after the question, and a line in today's journal.
        const QString title = chat::pageTitle(message, now.date(), language);
        target = paths::uniquePath(title + u".md"_s, [this](const QString &p) { return m_workspace->exists(p); });
        QString content = chat::newConversation(markdown, page, language, now, who, parent);
        if (thread) {
            // A thread of Code mode: where it works (the worktree quoted: a path may hold ": ").
            content = chat::setField(content, u"project", thread->project);
            if (!thread->worktree.isEmpty()) {
                content = chat::setField(content, u"worktree", QString(u'"' + thread->worktree + u'"'));
                content = chat::setField(content, u"branch", thread->branch);
            }
        }
        if (!m_workspace->write(target, content)) {
            setFailure(turn(QString()), tr("The conversation could not be saved."));
            return {};
        }
        // (A thread of a project is no event of the day: not in the journal.)
        if (!thread)
            m_workspace->appendJournalBlocks(chat::journalEntry(target.chopped(3)));
        setFailure(turn(QString()), QString());
    } else {
        bool ok = false;
        const QString content = m_workspace->read(target, &ok);
        if (!ok
            || !m_workspace->write(target, chat::appendMessage(content, chat::Role::User, markdown, now, language))) {
            setFailure(turn(target), tr("The conversation could not be saved."));
            return {};
        }
    }
    for (const QString &picture : std::as_const(sent))
        m_added.removeAll(picture); // (part of the notebook now)
    // (Before the turn runs: one that fails at once wakes it too.)
    if (!wakes.isEmpty())
        setWake(target, wakes);
    Q_EMIT written(target);
    runTurn(turn(target));
    return target;
}

bool WorkspaceChats::retry(const QString &path)
{
    if (path.isEmpty() || !m_workspace->ready())
        return false;
    ChatTurn *t = turn(path);
    if (t->m_running)
        return false;
    return runTurn(t);
}

bool WorkspaceChats::runTurn(ChatTurn *t)
{
    bool ok = false;
    const QString content = m_workspace->read(t->m_path, &ok);
    const chat::Conversation conversation = chat::parse(content);
    if (!ok || conversation.messages.isEmpty() || conversation.messages.last().role != chat::Role::User) {
        setFailure(t, tr("There is no question to answer: write one."));
        return false;
    }
    // (An AI agent of the user's own is no model on the device: it never waits in line.)
    if (onDevice() && m_onDevice && m_onDevice != t && !WorkspaceAgents::isAgent(conversation.partner)) {
        // The model on the device answers another conversation: this one waits in line.
        t->m_running = true;
        t->m_queued = true;
        t->m_status.clear();
        t->m_failure.clear();
        t->clearPartial();
        m_waiting.append(t);
        updatePlaces();
        Q_EMIT t->stateChanged();
        Q_EMIT stateChanged();
        return true;
    }
    return startTurn(t);
}

bool WorkspaceChats::startTurn(ChatTurn *t)
{
    bool ok = false;
    const QString content = m_workspace->read(t->m_path, &ok);
    const chat::Conversation conversation = chat::parse(content);
    // Whole turns from the oldest left out when the conversation does not fit the model. (An AI
    // agent gets them only when its session is new: as much as a model on the network, pictures
    // as files it reads.)
    const bool agent = WorkspaceAgents::isAgent(conversation.partner);
    const int from = chat::firstSent(conversation, agent ? NetworkBudget : historyBudget());
    const QVariantList history = chat::history(conversation, from, agent || seesPictures());
    if (!ok || history.isEmpty() || history.last().toMap().value(u"role"_s) != u"user"_s) {
        t->m_running = false;
        t->m_queued = false;
        Q_EMIT t->stateChanged();
        setFailure(t, tr("There is no question to answer: write one."));
        return false;
    }
    t->m_key = t->m_path;
    m_byKey.insert(t->m_key, t);
    t->m_running = true;
    t->m_queued = false;
    t->m_place = 0;
    t->m_omitted = from;
    t->m_status.clear();
    t->m_failure.clear();
    t->clearPartial();
    if (onDevice() && !agent)
        m_onDevice = t;
    Q_EMIT t->stateChanged();
    Q_EMIT stateChanged();
    // (A missing partner fails at once, through failed().) A thread of Code mode: a git snapshot
    // of its folder first, so its changes can be seen and reverted (docs/CODE.md "Checkpoints").
    const QString folder = agent && !conversation.project.isEmpty() ? threadFolder(t->m_path) : QString();
    if (!folder.isEmpty() && gitcli::Client::processesAvailable())
        checkpointThenRun(t, folder,
                          int(std::ranges::count_if(conversation.messages,
                                                    [](const chat::Message &m) { return m.role == chat::Role::User; })),
                          conversation.partner, history, conversation.page);
    else if (agent)
        m_agents->run(t->m_key, conversation.partner, history, conversation.page);
    else
        m_extensions->runChat(t->m_key, history, conversation.page, conversation.partner, from);
    return true;
}

void WorkspaceChats::startWaiting()
{
    while (!m_waiting.isEmpty() && (!onDevice() || !m_onDevice)) {
        QPointer<ChatTurn> next = m_waiting.takeFirst();
        if (!next || !next->m_queued)
            continue;
        next->m_queued = false;
        startTurn(next);
    }
    updatePlaces();
}

void WorkspaceChats::updatePlaces()
{
    int place = 0;
    for (const QPointer<ChatTurn> &t : std::as_const(m_waiting)) {
        if (!t || !t->m_queued)
            continue;
        if (t->m_place != ++place) {
            t->m_place = place;
            Q_EMIT t->stateChanged();
        }
    }
}

void WorkspaceChats::answered(const QString &key, const QString &text)
{
    ChatTurn *t = m_byKey.value(key);
    if (!t)
        return; // (a stopped turn's)
    const QString path = t->m_path;
    bool ok = false;
    const QString content = m_workspace->exists(path) ? m_workspace->read(path, &ok) : QString();
    if (!ok
        || !m_workspace->write(path, chat::appendMessage(content, chat::Role::Assistant, text, m_workspace->now(),
                                                         m_extensions->language()))) {
        qCWarning(logging::lcApp) << "AI chat: the answer could not be written to" << path;
        endTurn(t);
        setFailure(t, tr("The answer could not be saved."));
        wake(path, int(chat::Wake::Failed), t->m_failure);
        return;
    }
    // Written before the turn ends: a view shows the message in place of the answer being
    // written, never neither of them.
    Q_EMIT written(path);
    if (!isShown(path) && !isSideChat(path))
        setUnread(path, true);
    endTurn(t);
    notifyUser(path, firstLineOf(text));
    wake(path, int(chat::Wake::Answered), text);
    sendNextQueued(path);
}

void WorkspaceChats::failed(const QString &key, const QString &error)
{
    ChatTurn *t = m_byKey.value(key);
    if (!t)
        return;
    endTurn(t);
    setFailure(t, error);
    notifyUser(t->m_path, error.section(u'\n', 0, 0).trimmed());
    wake(t->m_path, int(chat::Wake::Failed), error);
    // (The messages waiting for it wait for the user: Resume, or Send now.)
    loadQueue();
    if (!m_queued.value(t->m_path.toLower()).isEmpty() && !m_held.contains(t->m_path.toLower())) {
        m_held.insert(t->m_path.toLower());
        queueMoved();
    }
}

void WorkspaceChats::stop(const QString &path)
{
    ChatTurn *t = find(path);
    if (!t || !t->m_running)
        return;
    // (A sub-conversation stopped by the user: its parent hears of it. Its agent's own chatStop
    // took the wake away first.)
    wake(t->m_path, int(chat::Wake::Stopped), QString());
    // Stopped: what waits for it is held (Resume sends it).
    loadQueue();
    if (!m_queued.value(t->m_path.toLower()).isEmpty() && !m_held.contains(t->m_path.toLower())) {
        m_held.insert(t->m_path.toLower());
        queueMoved();
    }
    if (t->m_queued) {
        m_waiting.removeAll(QPointer<ChatTurn>(t));
        t->m_queued = false;
        m_stopping = t;
        endTurn(t);
        m_stopping = nullptr;
        return;
    }
    const QString key = t->m_key;
    m_checkpointing.remove(key); // (a snapshot on its way: the agent's turn never starts)
    m_stopping = t;
    endTurn(t);
    m_stopping = nullptr;
    m_extensions->stopChat(key);
    m_agents->stop(key);
}

void WorkspaceChats::stopAll()
{
    for (ChatTurn *t : m_turns.values()) {
        if (t->m_running)
            stop(t->m_path);
    }
}

void WorkspaceChats::stopRemoteAgents()
{
    for (const QString &path : m_agents->remoteTurns())
        stop(path);
}

void WorkspaceChats::stopOnDevice()
{
    QStringList paths;
    if (m_onDevice)
        paths << m_onDevice->path();
    for (const QPointer<ChatTurn> &t : std::as_const(m_waiting)) {
        if (t)
            paths << t->path();
    }
    for (const QString &path : std::as_const(paths))
        stop(path);
}

void WorkspaceChats::dismissFailure(const QString &path)
{
    setFailure(find(path), QString());
}

void WorkspaceChats::reset()
{
    m_secrets->reset();
    m_agents->reset();
    m_waiting.clear();
    m_byKey.clear();
    m_onDevice = nullptr;
    for (ChatTurn *t : std::as_const(m_turns)) {
        t->m_key.clear();
        t->m_running = false;
        t->m_queued = false;
        t->m_place = 0;
        t->m_status.clear();
        t->m_failure.clear();
        t->m_omitted = 0;
        t->m_asking = false;
        t->m_cameBack = 0;
        t->clearPartial();
        Q_EMIT t->stateChanged();
    }
    for (const QString &picture : std::exchange(m_added, {}))
        m_workspace->discardImport(picture);
    // (The notebook's waiting messages stay in its device state, for when it opens again.)
    m_queued.clear();
    m_held.clear();
    m_rewound.clear();
    m_queueLoaded = false;
    m_wakes.clear();
    m_wakesLoaded = false;
    m_woken.clear();
    m_removed.clear();
    queueMoved();
    Q_EMIT wakesChanged();
    Q_EMIT stateChanged();
}

void WorkspaceChats::endTurn(ChatTurn *t)
{
    if (!t)
        return;
    // A secret asked for goes with the turn; one handed over is deleted.
    m_secrets->turnEnded(t->m_path, m_stopping == t);
    if (!t->m_key.isEmpty() && m_byKey.value(t->m_key) == t)
        m_byKey.remove(t->m_key);
    t->m_key.clear();
    t->m_running = false;
    t->m_queued = false;
    t->m_place = 0;
    t->m_status.clear();
    t->m_asking = false; // (its question went with it)
    if (t->m_failure.isEmpty())
        t->m_cameBack = 0;
    t->clearPartial();
    t->setActivity({}, {});
    Q_EMIT t->stateChanged();
    const bool freed = m_onDevice == t;
    if (freed)
        m_onDevice = nullptr;
    Q_EMIT stateChanged();
    if (freed || !onDevice())
        startWaiting();
    else
        updatePlaces();
}

void WorkspaceChats::setFailure(ChatTurn *t, const QString &failure)
{
    if (!t || t->m_failure == failure)
        return;
    t->m_failure = failure;
    if (!failure.isEmpty())
        t->m_cameBack = stamp();
    else if (!t->m_asking)
        t->m_cameBack = 0;
    Q_EMIT t->stateChanged();
    Q_EMIT stateChanged();
    Q_EMIT attentionChanged();
}

// ---- what needs the user --------------------------------------------------------------------------

bool WorkspaceChats::isUnread(const QString &path) const
{
    const LocalStore *store = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    return store && !path.isEmpty() && store->get(UnreadKey).toObject().contains(path.toLower());
}

void WorkspaceChats::setUnread(const QString &path, bool unread)
{
    LocalStore *store = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!store || path.isEmpty() || (unread && isSideChat(path)))
        return;
    QJsonObject all = store->get(UnreadKey).toObject();
    const QString key = path.toLower();
    if (all.contains(key) == unread && !unread)
        return;
    if (unread)
        all.insert(key, double(stamp()));
    else
        all.remove(key);
    store->set(UnreadKey, all.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(all));
    Q_EMIT attentionChanged();
}

qint64 WorkspaceChats::stamp()
{
    // (Two in one millisecond keep their order.)
    m_lastStamp = std::max(QDateTime::currentMSecsSinceEpoch(), m_lastStamp + 1);
    return m_lastStamp;
}

qint64 WorkspaceChats::cameBack(const QString &path) const
{
    qint64 at = 0;
    if (const LocalStore *store = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
        store && !path.isEmpty())
        at = qint64(store->get(UnreadKey).toObject().value(path.toLower()).toDouble());
    if (const ChatTurn *t = find(path); t && (!t->m_failure.isEmpty() || t->m_asking))
        at = std::max(at, t->m_cameBack);
    return at;
}

QString WorkspaceChats::failureOf(const QString &path) const
{
    const ChatTurn *t = path.isEmpty() ? nullptr : find(path);
    return t ? t->m_failure : QString();
}

void WorkspaceChats::setNotifyMode(const QString &mode)
{
    m_notifyMode = mode == u"always" || mode == u"never" ? mode : u"background"_s;
}

void WorkspaceChats::notifyUser(const QString &path, const QString &text)
{
    // (A side chat is the user's aside: shown inside its conversation, never a notification.)
    if (path.isEmpty() || m_notifyMode == u"never" || isSideChat(path))
        return;
    const bool active = appActive();
    // On screen in an active window: seen there.
    if (active && (isShown(path) || m_notifyMode != u"always"))
        return;
    m_workspace->notifications()->record(NotificationCenter::Kind::Chat, partnerLabel(partnerOf(path)), nameOf(path),
                                         text, path);
    Q_EMIT notificationWanted(path, nameOf(path), text);
}

bool WorkspaceChats::appActive() const
{
    return m_appActive ? m_appActive()
                       : qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr
                                 && QGuiApplication::applicationState() == Qt::ApplicationActive;
}

void WorkspaceChats::setDefaultPartner(const QString &partner)
{
    const QString id = partner.trimmed();
    if (id == m_defaultPartner)
        return;
    m_defaultPartner = id;
    Q_EMIT defaultPartnerChanged();
}

QString WorkspaceChats::defaultPartner() const
{
    if (m_defaultPartner.isEmpty())
        return {};
    for (const QVariant &p : partners())
        if (p.toMap().value(u"id"_s).toString() == m_defaultPartner)
            return m_defaultPartner;
    return {};
}

// ---- the list's menu ------------------------------------------------------------------------------

QString WorkspaceChats::nameOf(const QString &path) const
{
    return chat::splitTitle(paths::pageTitle(path)).second;
}

QString WorkspaceChats::rename(const QString &path, const QString &name)
{
    const QString words = paths::sanitizeFileName(name.simplified()).trimmed();
    if (words.isEmpty() || words == u"Untitled" || !m_workspace->exists(path))
        return {};
    const QString date = chat::splitTitle(paths::pageTitle(path)).first;
    const QString title = date.isEmpty() ? words : date + u' ' + words;
    if (title == paths::pageTitle(path))
        return path;
    return m_workspace->renamePage(path, title);
}

QString WorkspaceChats::remove(const QString &path)
{
    m_removed.clear();
    if (path.isEmpty() || !m_workspace->exists(path))
        return {};
    // (Deleted by the user: it wakes nobody.)
    setWake(path, QString());
    stop(path);
    const QStringList sides = m_lineage->sidesOf(path);
    const QString trash = m_workspace->deletePage(path);
    if (trash.isEmpty())
        return {};
    m_removed << trash;
    // Its side chats go with it (one Undo puts them all back: lastRemoved()).
    for (const QString &side : sides) {
        if (!m_workspace->exists(side))
            continue;
        stop(side);
        if (const QString t = m_workspace->deletePage(side); !t.isEmpty())
            m_removed << t;
    }
    return trash;
}

bool WorkspaceChats::setPinned(const QString &path, bool on)
{
    return setMark(path, int(chat::Mark::Pinned), on);
}

bool WorkspaceChats::setArchived(const QString &path, bool on)
{
    const bool done = setMark(path, int(chat::Mark::Archived), on);
    if (done)
        Q_EMIT archivedChanged(path, on);
    return done;
}

bool WorkspaceChats::setMark(const QString &path, int mark, bool on)
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok)
        return false;
    const QString changed = chat::setMark(content, chat::Mark(mark), on, m_extensions->language());
    return changed == content || m_workspace->write(path, changed);
}

// ---- who answers ----------------------------------------------------------------------------------

QString WorkspaceChats::partnerOf(const QString &path) const
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return {};
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    return ok ? chat::parse(content).partner : QString();
}

bool WorkspaceChats::setPartner(const QString &path, const QString &partner)
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok)
        return false;
    const QString changed = chat::setPartner(content, partner);
    return changed == content || m_workspace->write(path, changed);
}

// ---- reported and hidden answers -----------------------------------------------------------------

bool WorkspaceChats::setFlag(const QString &path, int number, const QString &flag)
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return false;
    chat::Flag mark = chat::Flag::None;
    if (flag == u"reported")
        mark = chat::Flag::Reported;
    else if (flag == u"hidden")
        mark = chat::Flag::Hidden;
    else if (!flag.isEmpty())
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok)
        return false;
    const chat::Conversation conversation = chat::parse(content);
    if (number < 1 || number > conversation.messages.size()
        || conversation.messages.at(number - 1).role != chat::Role::Assistant)
        return false;
    const QString changed = chat::setFlag(content, number, mark, m_extensions->language());
    return changed == content || m_workspace->write(path, changed);
}

QString WorkspaceChats::modelName(const QString &path) const
{
    const QString partner = partnerOf(path);
    if (WorkspaceAgents::isAgent(partner)) {
        const QString label = m_agents->label(partner);
        QString out = u"AI agent "_s + (label.isEmpty() ? partner : label);
        for (const QVariant &row : m_agents->options(path, partner)) {
            const QVariantMap o = row.toMap();
            if (o.value(u"category"_s).toString() == u"model" && !o.value(u"currentName"_s).toString().isEmpty())
                out += u" · model "_s + o.value(u"currentName"_s).toString();
        }
        return out;
    }
    const QString who = partner.isEmpty() ? u"Brulion's AI assistant"_s
                                          : u"Chat partner "_s + partner + u" (an extension)"_s;
    return who + u" · "_s + aireport::modelDescription(m_extensions->aiSettings());
}

// ---- pictures -------------------------------------------------------------------------------------

QString WorkspaceChats::addPicture(const QUrl &file)
{
    if (!m_workspace->ready() || !file.isValid())
        return {};
    const QString name = file.isLocalFile() ? file.toLocalFile() : file.toString();
    // (A content:// document of Android names no extension: its type is the picker's.)
    if (file.isLocalFile() && !paths::pictureExtensions().contains(paths::extension(name).toLower()))
        return {};
    const QString path = m_workspace->importAttachment(file);
    if (!path.isEmpty() && !m_added.contains(path))
        m_added.append(path);
    return path;
}

QStringList WorkspaceChats::addPictures(const QList<QUrl> &files)
{
    QStringList stored;
    for (const QUrl &file : files) {
        const QString path = addPicture(file);
        if (!path.isEmpty())
            stored.append(path);
    }
    if (stored.size() < files.size())
        Q_EMIT picturesRefused();
    return stored;
}

QStringList WorkspaceChats::addPicturesTo(const QStringList &attached, const QList<QUrl> &files)
{
    QStringList out = attached;
    for (const QString &path : addPictures(files)) {
        if (!out.contains(path))
            out.append(path);
    }
    return out;
}

QString WorkspaceChats::addClipboardPicture()
{
    const QClipboard *clipboard =
            qobject_cast<QGuiApplication *>(QCoreApplication::instance()) ? QGuiApplication::clipboard() : nullptr;
    const QMimeData *data = clipboard ? clipboard->mimeData() : nullptr;
    if (!data || !m_workspace->ready())
        return {};
    // Files copied in a file manager: the first picture among them.
    if (data->hasUrls()) {
        for (const QUrl &url : data->urls()) {
            if (url.isLocalFile() && paths::pictureExtensions().contains(paths::extension(url.toLocalFile()).toLower()))
                return addPicture(url);
        }
    }
    // (Text with a picture — a browser's copy of a page — is pasted as text.)
    if (!data->hasImage() || (data->hasText() && !data->text().trimmed().isEmpty()))
        return {};
    const QImage image = qvariant_cast<QImage>(data->imageData());
    QByteArray png;
    QBuffer buffer(&png);
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    const QString name = tr("Pasted image %1").arg(m_workspace->now().toString(u"yyyyMMddHHmmss"_s)) + u".png"_s;
    const QString path = m_workspace->importAttachment(name, png);
    if (!path.isEmpty() && !m_added.contains(path))
        m_added.append(path);
    return path;
}

bool WorkspaceChats::browserPicker() const
{
#ifdef Q_OS_WASM
    return true;
#else
    return false;
#endif
}

int WorkspaceChats::pickPicturesInBrowser()
{
#ifdef Q_OS_WASM
    if (!m_workspace->ready())
        return 0;
    const int pick = ++m_lastPick;
    QStringList filters;
    for (const QString &extension : paths::pictureExtensions())
        filters << u"*."_s + extension;
    const QPointer<WorkspaceChats> self(this);
    //: %1: the file name patterns of pictures, e.g. "*.png *.jpg *.jpeg"
    platform::wasm::openFiles({tr("Pictures (%1)").arg(filters.join(u' '))}, true, MaxPickedPictureBytes,
                              MaxPickedPicturesBytes, [self, pick](const QList<platform::PickedFile> &files) {
                                  if (!self || files.isEmpty())
                                      return; // (cancelled, or the notebook went meanwhile)
                                  const QStringList stored = self->addPickedPictures(files);
                                  if (!stored.isEmpty())
                                      Q_EMIT self->picturesPicked(pick, stored);
                              });
    return pick;
#else
    return 0;
#endif
}

QStringList WorkspaceChats::addPickedPictures(const QList<platform::PickedFile> &files)
{
    QStringList stored;
    if (!m_workspace->ready())
        return stored;
    for (const platform::PickedFile &file : files) {
        // (The browser names a file as the user's system does: its extension says what it is.)
        if (!file.isRead() || file.data.isEmpty()
            || !paths::pictureExtensions().contains(paths::extension(file.name).toLower()))
            continue;
        const QString path = m_workspace->importAttachment(file.name, file.data);
        if (path.isEmpty())
            continue;
        if (!m_added.contains(path))
            m_added.append(path);
        if (!stored.contains(path))
            stored.append(path);
    }
    if (stored.size() < files.size())
        Q_EMIT picturesRefused();
    return stored;
}

void WorkspaceChats::dropPicture(const QString &path)
{
    if (m_added.removeAll(path) > 0)
        m_workspace->discardImport(path);
}

QString WorkspaceChats::picturePath(const QString &name, const QString &conversation) const
{
    if (name.isEmpty() || !m_workspace->ready())
        return {};
    return m_workspace->resolveLink(name, conversation);
}

// ---- the camera and dictation ---------------------------------------------------------------------

bool WorkspaceChats::cameraAvailable() const
{
    return platform::cameraAvailable();
}

bool WorkspaceChats::dictationAvailable() const
{
    return platform::speechRecognizer() != nullptr;
}

bool WorkspaceChats::listening() const
{
    const platform::SpeechRecognizer *speech = platform::speechRecognizer();
    return speech && speech->listening();
}

bool WorkspaceChats::dictationReady() const
{
    const platform::SpeechRecognizer *speech = platform::speechRecognizer();
    return speech && speech->ready();
}

bool WorkspaceChats::recognizing() const
{
    const platform::SpeechRecognizer *speech = platform::speechRecognizer();
    return speech && speech->recognizing();
}

int WorkspaceChats::dictationElapsed() const
{
    const platform::SpeechRecognizer *speech = platform::speechRecognizer();
    return speech ? int(speech->elapsedMs()) : -1;
}

int WorkspaceChats::dictationLimit() const
{
    const platform::SpeechRecognizer *speech = platform::speechRecognizer();
    return speech ? int(speech->maxMs()) : 0;
}

void WorkspaceChats::useSpeechRecognizer()
{
    connectSpeech();
    Q_EMIT dictationChanged();
}

bool WorkspaceChats::takePhoto()
{
    const QPointer<WorkspaceChats> self(this);
    return platform::takePhoto([self](const QString &file, const QString &error) {
        if (!self)
            return;
        if (file.isEmpty()) {
            if (!error.isEmpty())
                Q_EMIT self->captureFailed(error);
            return;
        }
        const QString path = self->addPicture(QUrl::fromLocalFile(file));
        if (path.isEmpty())
            Q_EMIT self->captureFailed(tr("The photo could not be stored in the notebook."));
        else
            Q_EMIT self->photoTaken(path);
    });
}

void WorkspaceChats::connectSpeech()
{
    platform::SpeechRecognizer *speech = platform::speechRecognizer();
    if (!speech || m_speech == speech)
        return;
    if (m_speech)
        disconnect(m_speech, nullptr, this, nullptr);
    m_speech = speech;
    connect(speech, &platform::SpeechRecognizer::partial, this, &WorkspaceChats::dictationPartial);
    connect(speech, &platform::SpeechRecognizer::recognized, this, &WorkspaceChats::dictated);
    connect(speech, &platform::SpeechRecognizer::failed, this, &WorkspaceChats::dictationFailed);
    connect(speech, &platform::SpeechRecognizer::listeningChanged, this, &WorkspaceChats::dictationChanged);
    connect(speech, &platform::SpeechRecognizer::readyChanged, this, &WorkspaceChats::dictationChanged);
    connect(speech, &platform::SpeechRecognizer::elapsedChanged, this, &WorkspaceChats::dictationElapsedChanged);
}

bool WorkspaceChats::startDictation()
{
    connectSpeech();
    platform::SpeechRecognizer *speech = platform::speechRecognizer();
    if (!speech)
        return false;
    const bool started = speech->start(m_extensions->language() == u"en" ? u"en-GB"_s : u"pl-PL"_s);
    Q_EMIT dictationChanged();
    return started;
}

void WorkspaceChats::stopDictation()
{
    if (platform::SpeechRecognizer *speech = platform::speechRecognizer())
        speech->stop();
}

void WorkspaceChats::cancelDictation()
{
    if (platform::SpeechRecognizer *speech = platform::speechRecognizer())
        speech->cancel();
    Q_EMIT dictationChanged();
}

QUrl WorkspaceChats::pictureUrl(const QString &name, const QString &conversation) const
{
    if (name.isEmpty() || !m_workspace->ready())
        return {};
    const QString resolved = m_workspace->resolveLink(name, conversation);
    return m_workspace->assetUrl(resolved.isEmpty() ? name : resolved);
}

// ---- messages written while a turn runs -----------------------------------------------------------

namespace {

const QString QueueKey = u"chat/queued"_s;

} // namespace

void WorkspaceChats::loadQueue()
{
    if (m_queueLoaded)
        return;
    LocalStore *local = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!local)
        return;
    m_queueLoaded = true;
    const QJsonObject all = local->get(QueueKey).toObject();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        QList<Waiting> list;
        for (const QJsonValue &v : it.value().toArray()) {
            const QJsonObject o = v.toObject();
            Waiting w{m_nextQueued++, o.value(u"text"_s).toString(), {}};
            for (const QJsonValue &picture : o.value(u"pictures"_s).toArray())
                w.pictures << picture.toString();
            if (!w.text.trimmed().isEmpty() || !w.pictures.isEmpty())
                list.append(w);
        }
        if (!list.isEmpty()) {
            m_queued.insert(it.key().toLower(), list);
            m_held.insert(it.key().toLower()); // (after a restart they wait for the user)
        }
    }
}

void WorkspaceChats::saveQueue()
{
    LocalStore *local = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!local || !m_queueLoaded)
        return;
    QJsonObject all;
    for (auto it = m_queued.constBegin(); it != m_queued.constEnd(); ++it) {
        QJsonArray list;
        for (const Waiting &w : it.value())
            list.append(QJsonObject{{u"text"_s, w.text}, {u"pictures"_s, QJsonArray::fromStringList(w.pictures)}});
        if (!list.isEmpty())
            all.insert(it.key(), list);
    }
    local->set(QueueKey, all);
}

void WorkspaceChats::queueMoved()
{
    ++m_queueSerial;
    Q_EMIT queueChanged();
}

void WorkspaceChats::moveQueue(const QString &from, const QString &to)
{
    loadQueue();
    const QString key = from.toLower();
    if (!m_queued.contains(key))
        return;
    QList<Waiting> list = m_queued.take(key);
    const bool held = m_held.remove(key);
    if (to.isEmpty()) {
        for (const Waiting &w : std::as_const(list)) {
            for (const QString &picture : w.pictures)
                m_workspace->discardImport(picture);
        }
    } else {
        m_queued.insert(to.toLower(), list);
        if (held)
            m_held.insert(to.toLower());
    }
    saveQueue();
    queueMoved();
}

bool WorkspaceChats::queue(const QString &path, const QString &text, const QStringList &pictures)
{
    if (!m_workspace->ready())
        return false;
    QStringList sent;
    for (const QString &picture : pictures) {
        if (!picture.trimmed().isEmpty() && !sent.contains(picture.trimmed()))
            sent.append(picture.trimmed());
    }
    if (text.trimmed().isEmpty() && sent.isEmpty())
        return false;
    loadQueue();
    const QString key = path.toLower();
    if (path.isEmpty() || (!isRunning(path) && m_queued.value(key).isEmpty()))
        return !send(path, text, QString(), sent).isEmpty();
    m_queued[key].append(Waiting{m_nextQueued++, text.trimmed(), sent});
    for (const QString &picture : std::as_const(sent))
        m_added.removeAll(picture); // (the queue keeps them now)
    // (Nothing runs — after a restart, a stop, a failure — and the user writes on: what was held
    // goes on, this one after it, as T3 Code's queue resumes with a new message.)
    if (!isRunning(path)) {
        m_held.remove(key);
        if (ChatTurn *t = find(path))
            setFailure(t, QString());
        saveQueue();
        queueMoved();
        sendNextQueued(path);
        return true;
    }
    saveQueue();
    queueMoved();
    return true;
}

QVariantList WorkspaceChats::queued(const QString &path, int) const
{
    const_cast<WorkspaceChats *>(this)->loadQueue();
    QVariantList out;
    for (const Waiting &w : m_queued.value(path.toLower())) {
        const QString from = chat::wakeSource(w.text);
        out.append(QVariantMap{{u"id"_s, w.id},
                               {u"text"_s, w.text},
                               {u"pictures"_s, w.pictures},
                               {u"from"_s, from.isEmpty() ? QString() : nameOf(from)}});
    }
    return out;
}

bool WorkspaceChats::heldForWake(const QString &path, int) const
{
    if (!queueHeld(path))
        return false;
    const QList<Waiting> list = m_queued.value(path.toLower());
    return std::ranges::all_of(list, [](const Waiting &w) { return !chat::wakeSource(w.text).isEmpty(); });
}

bool WorkspaceChats::queueHeld(const QString &path, int) const
{
    const_cast<WorkspaceChats *>(this)->loadQueue();
    const QString key = path.toLower();
    return m_held.contains(key) && !m_queued.value(key).isEmpty();
}

QString WorkspaceChats::unqueue(const QString &path, int id, bool keepPictures)
{
    loadQueue();
    m_taken.clear();
    const QString key = path.toLower();
    auto it = m_queued.find(key);
    if (it == m_queued.end())
        return {};
    for (qsizetype i = 0; i < it->size(); ++i) {
        if (it->at(i).id != id)
            continue;
        const Waiting w = it->takeAt(i);
        if (it->isEmpty()) {
            m_queued.erase(it);
            m_held.remove(key);
        }
        for (const QString &picture : w.pictures) {
            if (keepPictures)
                m_added.append(picture); // (the field's again: dropped when taken off there)
            else
                m_workspace->discardImport(picture);
        }
        saveQueue();
        queueMoved();
        if (keepPictures)
            m_taken = w.pictures;
        return w.text;
    }
    return {};
}

void WorkspaceChats::sendNextQueued(const QString &path)
{
    loadQueue();
    const QString key = path.toLower();
    auto it = m_queued.find(key);
    if (it == m_queued.end() || it->isEmpty() || m_held.contains(key) || isRunning(path))
        return;
    const Waiting w = it->takeFirst();
    if (it->isEmpty())
        m_queued.erase(it);
    saveQueue();
    queueMoved();
    if (send(path, w.text, QString(), w.pictures).isEmpty()) {
        // Not sent (the assistant off, a page not written): back in front, held.
        m_queued[key].prepend(w);
        m_held.insert(key);
        saveQueue();
        queueMoved();
    }
}

bool WorkspaceChats::sendNow(const QString &path, int id)
{
    loadQueue();
    const QString key = path.toLower();
    auto it = m_queued.find(key);
    if (it == m_queued.end() || it->isEmpty())
        return false;
    qsizetype at = 0;
    if (id >= 0) {
        at = -1;
        for (qsizetype i = 0; i < it->size(); ++i) {
            if (it->at(i).id == id)
                at = i;
        }
        if (at < 0)
            return false;
    }
    // That one goes first; the turn running is stopped (the others wait for this one's answer).
    it->move(at, 0);
    if (ChatTurn *t = find(path); t && t->m_running) {
        const QString turnKey = t->m_key;
        const bool wasQueued = t->m_queued;
        if (wasQueued)
            m_waiting.removeAll(QPointer<ChatTurn>(t));
        endTurn(t);
        if (!wasQueued) {
            m_extensions->stopChat(turnKey);
            m_agents->stop(turnKey);
        }
    }
    m_held.remove(key);
    sendNextQueued(path);
    return true;
}

bool WorkspaceChats::steer(const QString &path, const QString &text, const QStringList &pictures)
{
    if (path.isEmpty() || !isRunning(path))
        return queue(path, text, pictures);
    loadQueue();
    const int before = m_nextQueued;
    if (!queue(path, text, pictures) || m_nextQueued == before)
        return false;
    return sendNow(path, before);
}

bool WorkspaceChats::resumeQueue(const QString &path)
{
    loadQueue();
    const QString key = path.toLower();
    if (m_queued.value(key).isEmpty())
        return false;
    m_held.remove(key);
    queueMoved();
    if (ChatTurn *t = find(path))
        setFailure(t, QString());
    sendNextQueued(path);
    return true;
}

// ---- going back, forking, quoting -----------------------------------------------------------------

QStringList WorkspaceChats::prompts(const QString &path) const
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return {};
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    return ok ? chat::prompts(chat::parse(content)) : QStringList();
}

QString WorkspaceChats::rewind(const QString &path, int line)
{
    m_taken.clear();
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return {};
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok)
        return {};
    const chat::Conversation conversation = chat::parse(content);
    int index = -1;
    for (int i = 0; i < conversation.messages.size(); ++i) {
        if (conversation.messages.at(i).line == line)
            index = i;
    }
    if (index < 0 || conversation.messages.at(index).role != chat::Role::User)
        return {};
    const chat::Message message = conversation.messages.at(index);
    // Nothing more of the turn, nor of what waited for it: the conversation goes on from here.
    if (ChatTurn *t = find(path); t && t->m_running) {
        const QString turnKey = t->m_key;
        const bool wasQueued = t->m_queued;
        if (wasQueued)
            m_waiting.removeAll(QPointer<ChatTurn>(t));
        endTurn(t);
        if (!wasQueued) {
            m_extensions->stopChat(turnKey);
            m_agents->stop(turnKey);
        }
    }
    moveQueue(path, QString());
    const QString shorter = chat::truncated(content, index);
    if (shorter == content || !m_workspace->write(path, shorter))
        return {};
    m_rewound.insert(path.toLower(), {content, shorter});
    if (ChatTurn *t = find(path))
        setFailure(t, QString());
    Q_EMIT written(path);
    const int gone = int(conversation.messages.size()) - index;
    Q_EMIT rewound(path, tr("%n message(s) taken out of the conversation", "", gone));
    // The text as it was asked (the first question keeps its link to the page it is about: the
    // page goes along again), its pictures for the field.
    m_taken = message.pictures;
    const QString text = chat::withoutPictures(message.text);
    return text.isEmpty() ? u" "_s : text; // (a message of pictures only: not "")
}

bool WorkspaceChats::undoRewind(const QString &path)
{
    const auto it = m_rewound.constFind(path.toLower());
    if (it == m_rewound.cend() || !m_workspace->ready() || !m_workspace->exists(path))
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok || content != it->second || isRunning(path))
        return false;
    const QString before = it->first;
    m_rewound.remove(path.toLower());
    if (!m_workspace->write(path, before))
        return false;
    Q_EMIT written(path);
    return true;
}

QString WorkspaceChats::fork(const QString &path, int line)
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return {};
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    if (!ok)
        return {};
    const chat::Conversation conversation = chat::parse(content);
    int index = -1;
    for (int i = 0; i < conversation.messages.size(); ++i) {
        if (conversation.messages.at(i).line == line)
            index = i;
    }
    const QString copy = chat::forked(content, index, path);
    if (copy.isEmpty())
        return {};
    // Today's, with the source's name (" 2" and on when it is in use).
    const QString name = chat::splitTitle(paths::pageTitle(path)).second;
    const QString title = chat::folder() + u'/' + m_workspace->now().date().toString(u"yyyy-MM-dd"_s) + u' ' + name;
    const QString target = paths::uniquePath(title + u".md"_s, [this](const QString &p) { return m_workspace->exists(p); });
    if (!m_workspace->write(target, copy))
        return {};
    m_workspace->appendJournalBlocks(chat::journalEntry(target.chopped(3)));
    return target;
}

QString WorkspaceChats::forkedFrom(const QString &path) const
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return {};
    bool ok = false;
    const QString source = chat::parse(m_workspace->read(path, &ok)).fork;
    if (!ok || source.isEmpty())
        return {};
    return m_workspace->resolveLink(source, path);
}

QString WorkspaceChats::quote(const QString &markdown) const
{
    return chat::quoted(markdown);
}

namespace {

const QString DraftsKey = u"chat/drafts"_s;

} // namespace

QString WorkspaceChats::draft(const QString &path) const
{
    LocalStore *local = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    return local ? local->get(DraftsKey).toObject().value(path.toLower()).toString() : QString();
}

void WorkspaceChats::setDraft(const QString &path, const QString &text)
{
    LocalStore *local = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!local)
        return;
    QJsonObject all = local->get(DraftsKey).toObject();
    const QString key = path.toLower();
    if (text.trimmed().isEmpty()) {
        if (!all.contains(key))
            return;
        all.remove(key);
    } else {
        if (all.value(key).toString() == text)
            return;
        all.insert(key, text.left(120000));
    }
    while (all.size() > 200)
        all.remove(all.keys().first());
    local->set(DraftsKey, all);
}

std::pair<int, QString> WorkspaceChats::mentionAt(const QString &text, int caret)
{
    if (caret < 0 || caret > text.size())
        return {-1, {}};
    // "[[" and the name so far (no "]", no line break).
    for (qsizetype i = caret - 1; i >= 1; --i) {
        const QChar c = text.at(i);
        if (c == u']' || c == u'\n' || (c == u'[' && text.at(i - 1) != u'['))
            break;
        if (c == u'[' && text.at(i - 1) == u'[') {
            if (i >= 2 && text.at(i - 2) == u'!')
                break; // (an embed)
            return {int(i - 1), text.mid(i + 1, caret - i - 1)};
        }
    }
    // "@" and a word: letters, digits, "-", "_", "/" and ".", at the start or after a space.
    qsizetype at = caret;
    while (at > 0) {
        const QChar c = text.at(at - 1);
        if (!(c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'/' || c == u'.' || c.isMark()))
            break;
        --at;
    }
    if (at > 0 && text.at(at - 1) == u'@' && (at == 1 || text.at(at - 2).isSpace() || text.at(at - 2) == u'('))
        return {int(at - 1), text.mid(at, caret - at)};
    return {-1, {}};
}

QString WorkspaceChats::withMention(const QString &text, int caret, const QString &title) const
{
    const int start = mentionAt(text, caret).first;
    if (start < 0 || title.trimmed().isEmpty())
        return {};
    return text.left(start) + u"[["_s + title.trimmed() + u"]] "_s + text.mid(caret);
}

int WorkspaceChats::mentionEnd(const QString &text, int caret, const QString &title) const
{
    const int start = mentionAt(text, caret).first;
    return start < 0 ? caret : start + int(title.trimmed().size()) + 5;
}

QVariantList WorkspaceChats::mentions(const QString &path, const QString &text, int caret, int limit) const
{
    const auto [start, query] = mentionAt(text, caret);
    if (start < 0 || !m_workspace->ready())
        return {};
    QVariantList out;
    for (const QVariant &v : m_workspace->suggestPages(query, limit + 1)) {
        QVariantMap m = v.toMap();
        if (m.value(u"path"_s).toString().compare(path, Qt::CaseInsensitive) == 0 || out.size() >= limit)
            continue;
        m.insert(u"start"_s, start);
        m.insert(u"end"_s, caret);
        out.append(m);
    }
    return out;
}

// ---- side chats (/btw) -----------------------------------------------------------------------------

bool WorkspaceChats::isAside(const QString &text) const
{
    const QString t = text.trimmed();
    const QString command = u"/btw"_s;
    return t.startsWith(command, Qt::CaseInsensitive) && (t.size() == command.size() || t.at(command.size()).isSpace());
}

QString WorkspaceChats::asideQuestion(const QString &text) const
{
    return isAside(text) ? text.trimmed().mid(4).trimmed() : QString();
}

QString WorkspaceChats::sideOf(const QString &path) const
{
    return path.isEmpty() ? QString() : m_lineage->sideOf(path);
}

bool WorkspaceChats::isSideChat(const QString &path) const
{
    return !sideOf(path).isEmpty();
}

QString WorkspaceChats::askAside(const QString &path, const QString &question, const QStringList &pictures)
{
    const QString text = question.trimmed();
    QStringList sent;
    for (const QString &picture : pictures) {
        if (!picture.trimmed().isEmpty() && !sent.contains(picture.trimmed()))
            sent.append(picture.trimmed());
    }
    if ((text.isEmpty() && sent.isEmpty()) || path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path)
        || isSideChat(path))
        return {};
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    const chat::Conversation conversation = chat::parse(content);
    if (!ok || conversation.messages.isEmpty())
        return {};
    if (conversation.partner.isEmpty() && !m_extensions->enabled().contains(u"asystent"_s)) {
        setFailure(turn(QString()), tr("The AI assistant is off: switch it on in Settings → Extensions → AI."));
        return {};
    }
    const QString language = m_extensions->language();
    const QDateTime now = m_workspace->now();
    const QString page = chat::sideChat(content, path, chat::withPictures(text, sent), now, language);
    if (page.isEmpty())
        return {};
    // Named after the side question (no journal line: it belongs to its conversation).
    const QString title = chat::pageTitle(text.isEmpty() ? nameOf(path) : text, now.date(), language);
    const QString target =
            paths::uniquePath(title + u".md"_s, [this](const QString &p) { return m_workspace->exists(p); });
    if (!m_workspace->write(target, page)) {
        setFailure(turn(QString()), tr("The conversation could not be saved."));
        return {};
    }
    for (const QString &picture : std::as_const(sent))
        m_added.removeAll(picture); // (part of the notebook now)
    Q_EMIT written(target);
    runTurn(turn(target));
    return target;
}

QString WorkspaceChats::lastAnswer(const QString &path) const
{
    if (path.isEmpty() || !m_workspace->ready() || !m_workspace->exists(path))
        return {};
    bool ok = false;
    const chat::Conversation conversation = chat::parse(m_workspace->read(path, &ok));
    for (qsizetype i = conversation.messages.size() - 1; i >= 0; --i) {
        if (conversation.messages.at(i).role == chat::Role::Assistant)
            return conversation.messages.at(i).text;
    }
    return {};
}

// ---- a sub-conversation that wakes its parent -----------------------------------------------------

namespace {

/// The notebook's device state of the sub-conversations that wake their parent: {child, lower case: parent}.
const QString WakeKey = u"chat/wake"_s;

} // namespace

void WorkspaceChats::loadWakes()
{
    if (m_wakesLoaded)
        return;
    const LocalStore *store = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!store)
        return;
    m_wakesLoaded = true;
    const QJsonObject all = store->get(WakeKey).toObject();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
        if (const QString parent = it.value().toString(); !parent.isEmpty())
            m_wakes.insert(it.key().toLower(), parent);
    }
}

void WorkspaceChats::saveWakes()
{
    LocalStore *store = m_workspace->vault() ? m_workspace->vault()->store() : nullptr;
    if (!store || !m_wakesLoaded)
        return;
    QJsonObject all;
    for (auto it = m_wakes.constBegin(); it != m_wakes.constEnd(); ++it)
        all.insert(it.key(), it.value());
    store->set(WakeKey, all.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(all));
}

void WorkspaceChats::setWake(const QString &child, const QString &parent)
{
    loadWakes();
    if (child.isEmpty() || !m_wakesLoaded)
        return;
    const QString key = child.toLower();
    if (parent.isEmpty() ? !m_wakes.contains(key) : m_wakes.value(key) == parent)
        return;
    if (parent.isEmpty())
        m_wakes.remove(key);
    else
        m_wakes.insert(key, parent);
    saveWakes();
    Q_EMIT wakesChanged();
}

bool WorkspaceChats::wakesParent(const QString &child) const
{
    const_cast<WorkspaceChats *>(this)->loadWakes();
    return !child.isEmpty() && m_wakes.contains(child.toLower());
}

void WorkspaceChats::wake(const QString &child, int how, const QString &text)
{
    loadWakes();
    const QString key = child.toLower();
    const auto it = m_wakes.constFind(key);
    if (it == m_wakes.cend())
        return;
    const QString parent = it.value();
    const auto outcome = chat::Wake(how);
    // Once (a question wakes it and the end of the turn again).
    if (outcome != chat::Wake::Asking) {
        m_wakes.remove(key);
        saveWakes();
        Q_EMIT wakesChanged();
    }
    // A parent deleted meanwhile is not woken.
    if (!m_workspace->ready() || !m_workspace->exists(parent))
        return;
    bool ok = false;
    const chat::Conversation conversation = chat::parse(m_workspace->read(parent, &ok));
    if (!ok || conversation.messages.isEmpty())
        return;
    // Archived, or woken too often in the last hour: the message waits for the user.
    const qint64 now = m_workspace->now().toMSecsSinceEpoch();
    QList<qint64> &times = m_woken[parent.toLower()];
    times.removeIf([now](qint64 at) { return now - at >= 3600 * 1000 || at > now; });
    chat::WakeHeld held = chat::WakeHeld::No;
    if (conversation.archived)
        held = chat::WakeHeld::Archived;
    else if (times.size() >= MaxWakesPerHour)
        held = chat::WakeHeld::Limit;
    else
        times.append(now);
    enqueue(parent, chat::wakeMessage(outcome, child, text, m_extensions->language(), held, MaxWakesPerHour),
            held != chat::WakeHeld::No);
}

void WorkspaceChats::enqueue(const QString &path, const QString &text, bool hold)
{
    loadQueue();
    if (!m_queueLoaded)
        return;
    const QString key = path.toLower();
    const bool waiting = !m_queued.value(key).isEmpty();
    if (!hold && !waiting && !isRunning(path)) {
        if (!send(path, text).isEmpty())
            return;
        hold = true; // (not sent — the assistant off, a page not written: it waits for the user)
    }
    // After a running turn (or what waits for it already); held: until the user resumes it.
    m_queued[key].append(Waiting{m_nextQueued++, text, {}});
    if (hold)
        m_held.insert(key);
    saveQueue();
    queueMoved();
}

// ---- conversations untouched for a while ------------------------------------------------------------

void WorkspaceChats::setAutoArchiveDays(int days)
{
    days = std::max(0, days);
    if (days == m_autoArchiveDays)
        return;
    m_autoArchiveDays = days;
    if (days > 0)
        m_settleTimer.start();
    else
        m_settleTimer.stop();
    if (days > 0 && m_workspace->ready())
        archiveUntouched();
}

QStringList WorkspaceChats::archiveUntouched()
{
    QStringList out;
    if (m_autoArchiveDays <= 0 || !m_workspace->ready() || !m_workspace->vault())
        return out;
    loadQueue();
    loadWakes();
    const QDateTime now = m_workspace->now();
    const QDateTime before = now.addDays(-m_autoArchiveDays);
    // (The parents a sub-conversation will wake: not untouched.)
    QSet<QString> awaited;
    for (const QString &parent : std::as_const(m_wakes))
        awaited.insert(parent.toLower());
    for (const FileStat &f : m_workspace->vault()->markdownFiles()) {
        if (!chat::inFolder(f.path))
            continue;
        const QString key = f.path.toLower();
        if (isRunning(f.path) || asking(f.path) || !failureOf(f.path).isEmpty() || isUnread(f.path) || isShown(f.path)
            || !m_queued.value(key).isEmpty() || awaited.contains(key) || !draft(f.path).trimmed().isEmpty()
            || isSideChat(f.path))
            continue;
        bool ok = false;
        const QString content = m_workspace->read(f.path, &ok);
        const chat::Conversation conversation = ok ? chat::parse(content) : chat::Conversation();
        if (conversation.messages.isEmpty() || conversation.pinned || conversation.archived)
            continue;
        // Its last message's time; a page written without times: its last change.
        const QDateTime last = conversation.messages.last().time.isValid() ? conversation.messages.last().time
                                                                           : QDateTime::fromMSecsSinceEpoch(f.mtime);
        if (!last.isValid() || last >= before)
            continue;
        if (setArchived(f.path, true))
            out << f.path;
    }
    if (!out.isEmpty())
        Q_EMIT autoArchived(out, m_autoArchiveDays);
    return out;
}

} // namespace brulion::shell

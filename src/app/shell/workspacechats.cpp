#include "workspacechats.h"

#include "agentshare.h"

#include "workspace.h"
#include "workspaceagents.h"
#include "workspaceextensions.h"

#include "capture.h"

#include "ai/local.h"
#include "chat/chat.h"
#include "log/categories.h"
#include "util/paths.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMimeData>

using namespace Qt::StringLiterals;

namespace brulion::shell {

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
    : QObject(parent), m_workspace(workspace), m_extensions(extensions), m_agents(new WorkspaceAgents(workspace, this)),
      m_share(new AgentShare(workspace, m_agents, this))
{
    connect(m_extensions, &WorkspaceExtensions::chatAnswered, this, &WorkspaceChats::answered);
    connect(m_extensions, &WorkspaceExtensions::chatFailed, this, &WorkspaceChats::failed);
    connect(m_extensions, &WorkspaceExtensions::chatStatus, this, &WorkspaceChats::setStatus);
    connect(m_extensions, &WorkspaceExtensions::chatPartial, this, &WorkspaceChats::setWriting);
    connect(m_extensions, &WorkspaceExtensions::commandsChanged, this, &WorkspaceChats::partnersChanged);
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
    connect(m_workspace, &Workspace::pathRemoved, this, [this](const QString &path) {
        if (ChatTurn *t = find(path); t && t->m_running)
            stop(path);
        moveQueue(path, QString());
        m_rewound.remove(path.toLower());
        setDraft(path, QString());
    });
    connect(this, &WorkspaceChats::stateChanged, this, &WorkspaceChats::updateRunningShown);
    connect(this, &WorkspaceChats::shownChanged, this, &WorkspaceChats::updateRunningShown);
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
    Q_EMIT questionsChanged();
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
    if (!isAgent(partner) || !typed.startsWith(u'/') || typed.contains(u' ') || typed.contains(u'\n'))
        return {};
    const QString prefix = typed.mid(1);
    QVariantList out;
    for (const QVariant &row : m_agents->commands(path, partner)) {
        if (row.toMap().value(u"name"_s).toString().startsWith(prefix, Qt::CaseInsensitive))
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
    return m_extensions->chatQuestionKind(path) == u"choose" ? m_extensions->chatQuestionTitle(path) : QString();
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

ChatTurn *WorkspaceChats::turn(const QString &path)
{
    if (ChatTurn *t = find(path))
        return t;
    auto *t = new ChatTurn(path, this);
    m_turns.insert(path.toLower(), t);
    return t;
}

QString WorkspaceChats::send(const QString &path, const QString &text, const QString &page, const QStringList &pictures,
                             const QString &partner)
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
        if (!m_workspace->write(target, chat::newConversation(markdown, page, language, now, who))) {
            setFailure(turn(QString()), tr("The conversation could not be saved."));
            return {};
        }
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
    // (A missing partner fails at once, through failed().)
    if (agent)
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
        return;
    }
    // Written before the turn ends: a view shows the message in place of the answer being
    // written, never neither of them.
    Q_EMIT written(path);
    endTurn(t);
    sendNextQueued(path);
}

void WorkspaceChats::failed(const QString &key, const QString &error)
{
    ChatTurn *t = m_byKey.value(key);
    if (!t)
        return;
    endTurn(t);
    setFailure(t, error);
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
    // Stopped: what waits for it is held (Resume sends it).
    loadQueue();
    if (!m_queued.value(t->m_path.toLower()).isEmpty() && !m_held.contains(t->m_path.toLower())) {
        m_held.insert(t->m_path.toLower());
        queueMoved();
    }
    if (t->m_queued) {
        m_waiting.removeAll(QPointer<ChatTurn>(t));
        t->m_queued = false;
        endTurn(t);
        return;
    }
    const QString key = t->m_key;
    endTurn(t);
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

void WorkspaceChats::dismissFailure(const QString &path)
{
    setFailure(find(path), QString());
}

void WorkspaceChats::reset()
{
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
    queueMoved();
    Q_EMIT stateChanged();
}

void WorkspaceChats::endTurn(ChatTurn *t)
{
    if (!t)
        return;
    if (!t->m_key.isEmpty() && m_byKey.value(t->m_key) == t)
        m_byKey.remove(t->m_key);
    t->m_key.clear();
    t->m_running = false;
    t->m_queued = false;
    t->m_place = 0;
    t->m_status.clear();
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
    Q_EMIT t->stateChanged();
    Q_EMIT stateChanged();
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
    if (path.isEmpty() || !m_workspace->exists(path))
        return {};
    stop(path);
    return m_workspace->deletePage(path);
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
    const QClipboard *clipboard = qGuiApp ? QGuiApplication::clipboard() : nullptr;
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
    // (Nothing runs — after a restart, a failure — and the user writes on: they go now.)
    if (!isRunning(path) && !m_held.contains(key)) {
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
    for (const Waiting &w : m_queued.value(path.toLower()))
        out.append(QVariantMap{{u"id"_s, w.id}, {u"text"_s, w.text}, {u"pictures"_s, w.pictures}});
    return out;
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

QVariantList WorkspaceChats::mentions(const QString &text, int caret, int limit) const
{
    const auto [start, query] = mentionAt(text, caret);
    if (start < 0 || !m_workspace->ready())
        return {};
    QVariantList out;
    for (const QVariant &v : m_workspace->suggestPages(query, limit)) {
        QVariantMap m = v.toMap();
        m.insert(u"start"_s, start);
        m.insert(u"end"_s, caret);
        out.append(m);
    }
    return out;
}

} // namespace brulion::shell

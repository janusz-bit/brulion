#include "workspacechats.h"

#include "workspace.h"
#include "workspaceextensions.h"

#include "chat/chat.h"
#include "log/categories.h"
#include "util/paths.h"

using namespace Qt::StringLiterals;

namespace brulion::shell {

WorkspaceChats::WorkspaceChats(Workspace *workspace, WorkspaceExtensions *extensions, QObject *parent)
    : QObject(parent)
    , m_workspace(workspace)
    , m_extensions(extensions)
{
    connect(m_extensions, &WorkspaceExtensions::chatAnswered, this, &WorkspaceChats::answered);
    connect(m_extensions, &WorkspaceExtensions::chatFailed, this, &WorkspaceChats::failed);
    connect(m_extensions, &WorkspaceExtensions::chatStatus, this, [this](const QString &turn, const QString &text) {
        if (turn != m_turn || m_turn.isEmpty() || text == m_status)
            return;
        m_status = text;
        Q_EMIT stateChanged();
    });
    // The page of the running turn renamed or moved: the answer goes where it is now; deleted:
    // the turn ends, nothing is written (a page made anew would hold the answer alone).
    connect(m_workspace, &Workspace::pathRenamed, this, [this](const QString &from, const QString &to) {
        bool changed = false;
        if (!m_running.isEmpty() && m_running.compare(from, Qt::CaseInsensitive) == 0) {
            m_running = to;
            changed = true;
        }
        if (!m_failedPath.isEmpty() && m_failedPath.compare(from, Qt::CaseInsensitive) == 0) {
            m_failedPath = to;
            changed = true;
        }
        if (changed)
            Q_EMIT stateChanged();
    });
    connect(m_workspace, &Workspace::pathRemoved, this, [this](const QString &path) {
        if (!m_running.isEmpty() && m_running.compare(path, Qt::CaseInsensitive) == 0)
            stop();
    });
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

QString WorkspaceChats::send(const QString &path, const QString &text, const QString &page)
{
    const QString message = text.trimmed();
    if (message.isEmpty() || !m_running.isEmpty() || !m_workspace->ready())
        return {};
    if (!m_extensions->enabled().contains(u"asystent"_s)) {
        setFailure(path, tr("The AI assistant is off: switch it on in Settings → Extensions → AI."));
        return {};
    }
    const QString language = m_extensions->language();
    QString target = path;
    if (target.isEmpty()) {
        // A new conversation: its page, named after the question, and a line in today's journal.
        const QString title = chat::pageTitle(message, m_workspace->now().date(), language);
        target = paths::uniquePath(title + u".md"_s, [this](const QString &p) { return m_workspace->exists(p); });
        if (!m_workspace->write(target, chat::newConversation(message, page, language))) {
            setFailure(QString(), tr("The conversation could not be saved."));
            return {};
        }
        m_workspace->appendJournalBlocks(chat::journalEntry(target.chopped(3)));
    } else {
        bool ok = false;
        const QString content = m_workspace->read(target, &ok);
        if (!ok || !m_workspace->write(target, chat::appendMessage(content, chat::Role::User, message))) {
            setFailure(target, tr("The conversation could not be saved."));
            return {};
        }
    }
    Q_EMIT written(target);
    runTurn(target);
    return target;
}

bool WorkspaceChats::retry(const QString &path)
{
    if (path.isEmpty() || !m_running.isEmpty() || !m_workspace->ready())
        return false;
    return runTurn(path);
}

bool WorkspaceChats::runTurn(const QString &path)
{
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    const chat::Conversation conversation = chat::parse(content);
    const QVariantList history = chat::history(conversation);
    if (!ok || history.isEmpty() || history.last().toMap().value(u"role"_s) != u"user"_s) {
        setFailure(path, tr("There is no question to answer: write one."));
        return false;
    }
    m_turn = path;
    m_running = path;
    m_status.clear();
    if (m_failedPath.compare(path, Qt::CaseInsensitive) == 0) {
        m_failedPath.clear();
        m_failure.clear();
    }
    Q_EMIT stateChanged();
    // (A missing assistant fails at once, through failed().)
    m_extensions->runChat(path, history, conversation.page);
    return true;
}

void WorkspaceChats::answered(const QString &turn, const QString &text)
{
    if (turn.isEmpty() || turn != m_turn)
        return; // (a stopped turn's)
    const QString path = m_running;
    endTurn();
    bool ok = false;
    const QString content = m_workspace->exists(path) ? m_workspace->read(path, &ok) : QString();
    if (!ok || !m_workspace->write(path, chat::appendMessage(content, chat::Role::Assistant, text))) {
        qCWarning(logging::lcApp) << "AI chat: the answer could not be written to" << path;
        setFailure(path, tr("The answer could not be saved."));
        return;
    }
    Q_EMIT written(path);
}

void WorkspaceChats::failed(const QString &turn, const QString &error)
{
    if (turn.isEmpty() || turn != m_turn)
        return;
    const QString path = m_running;
    endTurn();
    setFailure(path, error);
}

void WorkspaceChats::stop()
{
    if (m_turn.isEmpty())
        return;
    const QString turn = m_turn;
    endTurn();
    m_extensions->stopChat(turn);
}

void WorkspaceChats::dismissFailure() { setFailure(QString(), QString()); }

void WorkspaceChats::reset()
{
    m_turn.clear();
    m_running.clear();
    m_status.clear();
    m_failedPath.clear();
    m_failure.clear();
    Q_EMIT stateChanged();
}

void WorkspaceChats::endTurn()
{
    m_turn.clear();
    m_running.clear();
    m_status.clear();
    Q_EMIT stateChanged();
}

void WorkspaceChats::setFailure(const QString &path, const QString &failure)
{
    if (m_failedPath == path && m_failure == failure)
        return;
    m_failedPath = failure.isEmpty() ? QString() : path;
    m_failure = failure;
    Q_EMIT stateChanged();
}

} // namespace brulion::shell

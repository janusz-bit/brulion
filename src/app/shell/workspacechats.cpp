#include "workspacechats.h"

#include "workspace.h"
#include "workspaceextensions.h"

#include "chat/chat.h"
#include "log/categories.h"
#include "util/paths.h"

#include <QLocale>

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
    connect(this, &WorkspaceChats::stateChanged, this, &WorkspaceChats::updateRunningShown);
    connect(this, &WorkspaceChats::shownChanged, this, &WorkspaceChats::updateRunningShown);
    // The answer as it is written: shown ten times a second at most (a provider streams a word at
    // a time, every change renders the message anew, and a model on the device shares the
    // processor with that).
    m_partialTimer.setSingleShot(true);
    m_partialTimer.setInterval(100);
    connect(&m_partialTimer, &QTimer::timeout, this, [this] {
        const double rate = tokensPerSecond(m_samples);
        setPartial(m_nextPartial, m_nextThoughts,
                   rate > 0 ? tr("%1 tok/s").arg(QLocale().toString(rate, 'f', 1)) : QString());
    });
    connect(m_extensions, &WorkspaceExtensions::chatPartial, this,
            [this](const QString &turn, const QString &text, const QString &thoughts, int tokens, qint64 writingMs) {
                if (turn != m_turn || m_turn.isEmpty())
                    return;
                m_nextPartial = text;
                m_nextThoughts = thoughtsTail(thoughts);
                if (text.isEmpty() && thoughts.isEmpty() && tokens == 0) {
                    m_partialTimer.stop(); // (a new request: at once)
                    m_samples.clear();
                    setPartial(QString(), QString(), QString());
                    return;
                }
                if (tokens > 0) {
                    m_samples.append({writingMs, tokens});
                    while (m_samples.size() > 200)
                        m_samples.removeFirst();
                }
                if (!m_partialTimer.isActive())
                    m_partialTimer.start();
            });
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

void WorkspaceChats::setPartial(const QString &text, const QString &thoughts, const QString &speed)
{
    if (text == m_partial && thoughts == m_thoughts && speed == m_speed)
        return;
    const bool newText = text != m_partial;
    m_partial = text;
    m_thoughts = thoughts;
    m_speed = speed;
    if (newText) {
        auto [settled, tail] = splitWritten(text);
        m_tail = tail;
        if (settled != m_settled) {
            m_settled = settled;
            Q_EMIT settledChanged();
        }
    }
    Q_EMIT partialChanged();
}

void WorkspaceChats::updateRunningShown()
{
    const bool shown = isShown(m_running);
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
    m_partialTimer.stop();
    m_samples.clear();
    setPartial(QString(), QString(), QString());
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
    bool ok = false;
    const QString content = m_workspace->exists(path) ? m_workspace->read(path, &ok) : QString();
    if (!ok || !m_workspace->write(path, chat::appendMessage(content, chat::Role::Assistant, text))) {
        qCWarning(logging::lcApp) << "AI chat: the answer could not be written to" << path;
        endTurn();
        setFailure(path, tr("The answer could not be saved."));
        return;
    }
    // Written before the turn ends: a view shows the message in place of the answer being
    // written, never neither of them.
    Q_EMIT written(path);
    endTurn();
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
    m_partialTimer.stop();
    m_samples.clear();
    setPartial(QString(), QString(), QString());
    Q_EMIT stateChanged();
}

void WorkspaceChats::endTurn()
{
    m_turn.clear();
    m_running.clear();
    m_status.clear();
    m_partialTimer.stop();
    m_samples.clear();
    setPartial(QString(), QString(), QString());
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

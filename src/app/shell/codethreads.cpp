// CodeThreads (codethreads.h): Code mode's threads settled, snoozed, settled by themselves.
#include "codethreads.h"

#include "chat/chat.h"
#include "codeprojects.h"
#include "codethreads/codethreads.h"
#include "vault/vault.h"
#include "workspace.h"
#include "workspacechats.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::shell {

CodeThreads::CodeThreads(Workspace *workspace, WorkspaceChats *chats, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_chats(chats)
{
    m_timer.setInterval(3600 * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        settleUntouched();
        tidy();
    });
    connect(m_workspace, &Workspace::readyChanged, this, [this] {
        if (!m_workspace->ready())
            return;
        settleUntouched();
        tidy();
    });
    m_timer.start();
}

QString CodeThreads::threadContent(const QString &path, bool *ok) const
{
    *ok = false;
    if (path.isEmpty() || !m_workspace->ready() || !chat::inFolder(path) || !m_workspace->exists(path))
        return {};
    bool read = false;
    const QString content = m_workspace->read(path, &read);
    *ok = read && !chat::parse(content).project.isEmpty();
    return *ok ? content : QString();
}

template<typename Edit>
bool CodeThreads::edit(const QString &path, Edit edit)
{
    bool ok = false;
    const QString content = threadContent(path, &ok);
    if (!ok)
        return false;
    const QString changed = edit(content);
    return changed == content || m_workspace->write(path, changed);
}

bool CodeThreads::settle(const QString &path) { return markSettled(path, true); }

bool CodeThreads::unsettle(const QString &path) { return markSettled(path, false); }

bool CodeThreads::markSettled(const QString &path, bool on)
{
    const QDateTime now = m_workspace->now();
    const bool done = edit(path, [on, &now](const QString &c) {
        return on ? codethreads::settle(c, now) : codethreads::unsettle(c);
    });
    if (done)
        Q_EMIT settledChanged(path, on);
    return done;
}

QString CodeThreads::snooze(const QString &path, const QString &preset)
{
    const std::optional<QDateTime> until = codethreads::snoozeEnd(preset, m_workspace->now());
    if (!until)
        return tr("“%1” names no time ahead.").arg(preset.trimmed());
    if (!edit(path, [&until](const QString &c) { return codethreads::snooze(c, *until); }))
        return tr("This thread could not be snoozed.");
    Q_EMIT snoozedChanged(path);
    return {};
}

bool CodeThreads::wake(const QString &path)
{
    if (!edit(path, [](const QString &c) { return codethreads::wake(c); }))
        return false;
    Q_EMIT snoozedChanged(path);
    return true;
}

bool CodeThreads::setNeverAutoSettle(const QString &path, bool never)
{
    return edit(path, [never](const QString &c) { return codethreads::setNeverAutoSettle(c, never); });
}

QVariantMap CodeThreads::state(const QString &path) const
{
    bool ok = false;
    const QString content = threadContent(path, &ok);
    const codethreads::State s = codethreads::readState(content);
    const auto text = [](const QDateTime &t) { return t.isValid() ? codethreads::formatTime(t) : QString(); };
    return {{u"settled"_s, text(s.settled)},
            {u"snoozedUntil"_s, text(s.snoozedUntil)},
            {u"snoozed"_s, s.snoozedUntil.isValid() && s.snoozedUntil > m_workspace->now()},
            {u"never"_s, s.neverAutoSettle}};
}

QString CodeThreads::snoozeLabel(const QString &preset) const
{
    const QDateTime now = m_workspace->now();
    const std::optional<QDateTime> end = codethreads::snoozeEnd(preset, now);
    if (!end)
        return {};
    const QString time = QLocale().toString(end->time(), QLocale::ShortFormat);
    if (end->date() == now.date())
        return time;
    if (end->date() == now.date().addDays(1))
        return tr("tomorrow %1").arg(time);
    return QLocale().toString(end->date(), u"ddd d MMM"_s) + u' ' + time;
}

void CodeThreads::setAutoSettleDays(int days)
{
    days = std::max(0, days);
    if (days == m_autoSettleDays)
        return;
    m_autoSettleDays = days;
    Q_EMIT autoSettleDaysChanged();
}

QDateTime CodeThreads::lastActivity(const chat::Conversation &conversation, qint64 mtime)
{
    // Its last message's time; a page written without times: its last change.
    const QDateTime time = conversation.messages.last().time;
    return time.isValid() ? time : QDateTime::fromMSecsSinceEpoch(mtime);
}

bool CodeThreads::untouched(const FileStat &file) const
{
    bool ok = false;
    const QString content = threadContent(file.path, &ok);
    const chat::Conversation conversation = ok ? chat::parse(content) : chat::Conversation();
    if (conversation.messages.isEmpty())
        return false;
    const codethreads::Facts facts{conversation.pinned, conversation.archived, false};
    return codethreads::settlesFromInactivity(codethreads::readState(content), facts,
                                              lastActivity(conversation, file.mtime), m_workspace->now(),
                                              m_autoSettleDays, m_chats->heldBack(file.path));
}

QStringList CodeThreads::settleUntouched()
{
    QStringList out;
    if (m_autoSettleDays <= 0 || !m_workspace->ready() || !m_workspace->vault())
        return out;
    for (const FileStat &f : m_workspace->vault()->markdownFiles())
        if (chat::inFolder(f.path) && untouched(f) && settle(f.path))
            out << f.path;
    if (!out.isEmpty())
        Q_EMIT autoSettled(out, m_autoSettleDays);
    return out;
}

void CodeThreads::setWorktreeCleanup(const QString &cleanup)
{
    const codethreads::Cleanup c = codethreads::cleanupOf(cleanup);
    const QString value = c.when == codethreads::Cleanup::When::Never ? u"never"_s
                          : c.when == codethreads::Cleanup::When::Merged ? u"merged"_s
                                                                          : QString::number(c.days);
    if (value == m_cleanup)
        return;
    m_cleanup = value;
    Q_EMIT worktreeCleanupChanged();
}

QList<std::pair<QString, chat::Conversation>> CodeThreads::worktreeThreads() const
{
    QList<std::pair<QString, chat::Conversation>> threads;
    for (const FileStat &f : m_workspace->vault()->markdownFiles()) {
        bool ok = false;
        const QString content = chat::inFolder(f.path) ? threadContent(f.path, &ok) : QString();
        const chat::Conversation c = ok ? chat::parse(content) : chat::Conversation();
        if (ok && !c.worktree.isEmpty() && !c.branch.isEmpty() && m_workspace->projects()->contains(c.project))
            threads.append({f.path, c});
    }
    return threads;
}

int CodeThreads::tidy()
{
    if (m_tidying > 0 || !m_workspace->ready() || !m_workspace->vault())
        return 0;
    if (m_autoSettleDays <= 0 && m_cleanup == u"never")
        return 0;
    const QList<std::pair<QString, chat::Conversation>> threads = worktreeThreads();
    m_tidying = int(threads.size());
    for (const auto &[path, c] : std::as_const(threads)) {
        const QString at = path;
        m_workspace->projects()->pullRequestState(c.worktree, c.branch,
                                                  [this, at](const QString &state) { pullRequestKnown(at, state); });
    }
    if (m_tidying == 0)
        Q_EMIT tidied();
    return int(threads.size());
}

void CodeThreads::pullRequestKnown(const QString &path, const QString &state)
{
    bool ok = false;
    const QString content = threadContent(path, &ok);
    if (!ok) {
        tidyDone();
        return;
    }
    const chat::Conversation c = chat::parse(content);
    const codethreads::Facts facts{c.pinned, c.archived, false};
    if (m_autoSettleDays > 0
        && codethreads::settlesFromPullRequest(codethreads::readState(content), facts,
                                               codethreads::pullRequestOf(state), m_chats->heldBack(path))
        && settle(path))
        Q_EMIT autoSettled({path}, 0);
    if (m_cleanup == u"never") {
        tidyDone();
        return;
    }
    m_workspace->projects()->worktreeChanges(c.worktree, [this, path, state](int changes, const QString &, const QString &) {
        changesKnown(path, state, changes);
    });
}

void CodeThreads::changesKnown(const QString &path, const QString &state, int changes)
{
    bool ok = false;
    const QString content = threadContent(path, &ok);
    const chat::Conversation c = ok ? chat::parse(content) : chat::Conversation();
    if (ok && QFileInfo(c.worktree).isDir() && !m_removing.contains(QDir::cleanPath(c.worktree))
        && codethreads::removesWorktree(codethreads::readState(content), codethreads::pullRequestOf(state),
                                        codethreads::cleanupOf(m_cleanup), m_workspace->now(), changes,
                                        othersActiveIn(path, c.worktree))) {
        const QString folder = c.worktree;
        auto *projects = m_workspace->projects();
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(projects, &CodeProjects::worktreeRemoved, this,
                              [this, path, folder, connection](const QString &removed, bool done, const QString &) {
                                  if (QDir::cleanPath(removed) != QDir::cleanPath(folder))
                                      return;
                                  disconnect(*connection);
                                  m_removing.remove(QDir::cleanPath(folder));
                                  if (done)
                                      Q_EMIT worktreeCleaned(path, folder);
                                  tidyDone();
                              });
        m_removing.insert(QDir::cleanPath(folder));
        if (projects->removeWorktree(c.project, folder, false))
            return;
        m_removing.remove(QDir::cleanPath(folder));
        disconnect(*connection);
    }
    tidyDone();
}

void CodeThreads::tidyDone()
{
    if (m_tidying > 0 && --m_tidying == 0)
        Q_EMIT tidied();
}

bool CodeThreads::othersActiveIn(const QString &path, const QString &folder) const
{
    for (const QString &other : m_chats->threadsIn(folder)) {
        if (other == path)
            continue;
        bool ok = false;
        const QString content = threadContent(other, &ok);
        if (!ok || !codethreads::readState(content).settled.isValid())
            return true;
    }
    return false;
}

} // namespace brulion::shell

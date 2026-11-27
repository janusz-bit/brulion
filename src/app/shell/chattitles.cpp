// WorkspaceChats: T3 Code's "Regenerate title" (docs/CODE.md "Threads") — the AI of Settings →
// Extensions → AI names a conversation anew from its messages and its name now (codeai: the
// question, the answer read), and the page is renamed when the name it gives is another. One
// question of a conversation at a time; the notebook closed stops them (reset()).
#include "workspacechats.h"

#include "chat/chat.h"
#include "codeai/codeai.h"
#include "workspace.h"
#include "workspaceextensions.h"

using namespace Qt::StringLiterals;

namespace brulion::shell {

bool WorkspaceChats::canRegenerateTitles() const
{
    return m_extensions && m_extensions->aiSettings().usable();
}

bool WorkspaceChats::regeneratingTitle(const QString &path, int) const
{
    return m_titling.contains(path.toLower());
}

bool WorkspaceChats::regenerateTitle(const QString &path)
{
    const QString key = path.toLower();
    if (path.isEmpty() || !canRegenerateTitles() || m_titling.contains(key) || !m_workspace->ready())
        return false;
    bool ok = false;
    const QString content = m_workspace->read(path, &ok);
    const QString context = ok ? codeai::titleContext(chat::parse(content)) : QString();
    if (context.isEmpty())
        return false;
    const QString project = threadPlace(path).value(u"projectName"_s).toString();
    const QPointer<WorkspaceChats> self(this);
    const quint64 id = ++m_titleId;
    const ai::Pending pending = m_extensions->sendAi(codeai::threadTitle(nameOf(path), project, context),
                                                     [self, path, id](const ai::Reply &reply) {
                                                         if (self)
                                                             self->titleAnswered(path, id, reply);
                                                     });
    m_titling.insert(key, {id, pending});
    ++m_titleSerial;
    Q_EMIT titlesChanged();
    return true;
}

void WorkspaceChats::titleAnswered(const QString &path, quint64 id, const ai::Reply &reply)
{
    // (Stopped — the notebook closed —, or asked again since: nothing more to say.)
    const auto it = m_titling.constFind(path.toLower());
    if (it == m_titling.cend() || it->id != id)
        return;
    m_titling.erase(it);
    ++m_titleSerial;
    Q_EMIT titlesChanged();
    const auto failed = [this, &path](const QString &why) { Q_EMIT titleRegenerated(path, path, u"failed"_s, why); };
    if (!reply.ok) {
        failed(reply.error);
        return;
    }
    const QString title = codeai::readTitle(reply.text);
    if (title.isEmpty()) {
        failed(tr("The AI gave no title: try again."));
        return;
    }
    // (Renamed, moved or deleted meanwhile: the name it has now is the user's.)
    if (!m_workspace->exists(path)) {
        failed(tr("The thread was renamed or moved meanwhile."));
        return;
    }
    const QString renamed = rename(path, title);
    if (renamed.isEmpty())
        failed(tr("Another conversation is named “%1” already.").arg(title));
    else
        Q_EMIT titleRegenerated(path, renamed, renamed == path ? u"unchanged"_s : u"renamed"_s, QString());
}

void WorkspaceChats::stopTitles()
{
    const QHash<QString, Titling> titling = std::exchange(m_titling, {});
    for (Titling t : titling)
        t.pending.stop();
    if (!titling.isEmpty()) {
        ++m_titleSerial;
        Q_EMIT titlesChanged();
    }
}

} // namespace brulion::shell

#include "aiask.h"

#include "workspace.h"
#include "workspaceextensions.h"

using namespace Qt::StringLiterals;

namespace brulion::shell {

AiAsk::AiAsk(QObject *parent) : QObject(parent) {}

AiAsk::~AiAsk()
{
    m_pending.stop();
}

void AiAsk::setWorkspace(Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace && m_workspace->extensions())
        m_workspace->extensions()->disconnect(this);
    stop();
    m_workspace = workspace;
    if (m_workspace && m_workspace->extensions())
        connect(m_workspace->extensions(), &WorkspaceExtensions::aiSettingsChanged, this, &AiAsk::availableChanged);
    Q_EMIT workspaceChanged();
    Q_EMIT availableChanged();
}

bool AiAsk::available() const
{
    return m_workspace && m_workspace->extensions() && m_workspace->extensions()->aiSettings().usable();
}

bool AiAsk::ask(const QString &system, const QString &prompt)
{
    if (!available() || prompt.trimmed().isEmpty())
        return false;
    stop();
    ai::Request request;
    request.system = system;
    request.messages = {ai::Message{u"user"_s, prompt, {}, {}, {}}};
    const quint64 id = ++m_request;
    setBusy(true);
    const QPointer<AiAsk> self(this);
    m_pending = m_workspace->extensions()->sendAi(request, [self, id](const ai::Reply &reply) {
        if (!self || id != self->m_request)
            return; // (an older question's, stopped or asked again)
        self->setBusy(false);
        if (!reply.ok) {
            if (reply.error != u"stopped")
                Q_EMIT self->answered(false, reply.error);
            return;
        }
        const QString text = cleaned(reply.text);
        if (text.isEmpty())
            Q_EMIT self->answered(false, tr("The AI gave no text: try again."));
        else
            Q_EMIT self->answered(true, text);
    });
    return true;
}

void AiAsk::stop()
{
    ++m_request;
    m_pending.stop();
    setBusy(false);
}

QString AiAsk::cleaned(const QString &text)
{
    QString t = text.trimmed();
    if (t.startsWith(u"```"_s) && t.endsWith(u"```"_s) && t.size() >= 6) {
        const qsizetype firstEnd = t.indexOf(u'\n');
        const qsizetype lastStart = t.lastIndexOf(u'\n');
        if (firstEnd > 0 && lastStart > firstEnd && t.mid(lastStart + 1).trimmed() == u"```"_s)
            t = t.mid(firstEnd + 1, lastStart - firstEnd - 1).trimmed();
    }
    return t;
}

void AiAsk::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

} // namespace brulion::shell

#include "chatlineage.h"

#include "workspace.h"
#include "workspacechats.h"

#include "chat/chat.h"
#include "vault/vault.h"

#include <QSet>
#include <QVariantMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

ChatLineage::ChatLineage(Workspace *workspace, WorkspaceChats *chats, QObject *parent)
    : QObject(parent), m_workspace(workspace), m_chats(chats)
{
    // A conversation's page changed (a parent named or gone, one more conversation): read again —
    // that page alone; a new or a gone one, all of them.
    connect(m_workspace, &Workspace::filesChanged, this, [this](const QStringList &paths) {
        bool any = false;
        for (const QString &p : paths) {
            if (chat::inFolder(p)) {
                update(p);
                any = true;
            }
        }
        if (any)
            bump();
    });
    connect(m_workspace, &Workspace::fileListChanged, this, &ChatLineage::invalidate);
    connect(m_workspace, &Workspace::pathRenamed, this, &ChatLineage::invalidate);
    connect(m_workspace, &Workspace::pathRemoved, this, &ChatLineage::invalidate);
    connect(m_workspace, &Workspace::openChanged, this, &ChatLineage::invalidate);
    connect(m_workspace, &Workspace::readyChanged, this, &ChatLineage::invalidate);
    // A turn's state, its failure or its question: the rows say it.
    connect(m_chats, &WorkspaceChats::stateChanged, this, &ChatLineage::bump);
    connect(m_chats, &WorkspaceChats::questionsChanged, this, &ChatLineage::bump);
    connect(m_chats, &WorkspaceChats::written, this, [this](const QString &path) {
        update(path);
        bump();
    });
}

void ChatLineage::update(const QString &path) const
{
    if (m_dirty)
        return; // (all of them are read when asked next)
    const QString parent = writtenParent(m_workspace, path);
    m_parents.remove(path);
    m_changed.remove(path);
    if (parent.isEmpty())
        return;
    m_parents.insert(path, resolveParent(m_workspace, parent, path));
    const std::optional<FileStat> stat = m_workspace->vault() ? m_workspace->vault()->stat(path) : std::nullopt;
    m_changed.insert(path, stat ? stat->mtime : 0);
}

void ChatLineage::invalidate()
{
    m_dirty = true;
    bump();
}

void ChatLineage::bump()
{
    ++m_serial;
    Q_EMIT changed();
}

QString ChatLineage::writtenParent(Workspace *workspace, const QString &path)
{
    if (path.isEmpty() || !workspace || !workspace->isOpen() || !workspace->exists(path))
        return {};
    bool ok = false;
    const QString content = workspace->read(path, &ok);
    return ok ? chat::parse(content).parent : QString();
}

QString ChatLineage::resolveParent(Workspace *workspace, const QString &parent, const QString &from)
{
    if (parent.isEmpty())
        return {};
    // (The chat writes the parent's vault path: no need of the index, which may not have it yet.)
    const QString direct = parent.endsWith(u".md"_s, Qt::CaseInsensitive) ? parent : parent + u".md"_s;
    if (workspace->exists(direct))
        return direct;
    return workspace->resolveLink(parent, from);
}

int ChatLineage::depth(Workspace *workspace, const QString &path)
{
    int levels = 0;
    QString at = path;
    QSet<QString> seen{path.toLower()};
    // (A loop of parents written by hand ends where it closes; MaxDepth + 1 is as deep as it matters.)
    while (levels <= MaxDepth) {
        const QString parent = writtenParent(workspace, at);
        if (parent.isEmpty())
            break;
        ++levels;
        const QString next = resolveParent(workspace, parent, at);
        if (next.isEmpty() || seen.contains(next.toLower()))
            break;
        seen.insert(next.toLower());
        at = next;
    }
    return levels;
}

void ChatLineage::ensure() const
{
    if (!m_dirty)
        return;
    m_dirty = false;
    m_parents.clear();
    m_changed.clear();
    if (!m_workspace->isOpen() || !m_workspace->vault())
        return;
    for (const FileStat &f : m_workspace->vault()->markdownFiles()) {
        if (!chat::inFolder(f.path))
            continue;
        const QString parent = writtenParent(m_workspace, f.path);
        if (parent.isEmpty())
            continue;
        m_parents.insert(f.path, resolveParent(m_workspace, parent, f.path));
        m_changed.insert(f.path, f.mtime);
    }
}

QString ChatLineage::parentOf(const QString &path) const
{
    ensure();
    for (auto it = m_parents.cbegin(); it != m_parents.cend(); ++it) {
        if (it.key().compare(path, Qt::CaseInsensitive) == 0)
            return it.value();
    }
    return {};
}

int ChatLineage::depthOf(const QString &path) const
{
    return depth(m_workspace, path);
}

QStringList ChatLineage::childrenOf(const QString &path) const
{
    ensure();
    QStringList out;
    if (path.isEmpty())
        return out;
    for (auto it = m_parents.cbegin(); it != m_parents.cend(); ++it) {
        if (it.value().compare(path, Qt::CaseInsensitive) == 0)
            out << it.key();
    }
    std::ranges::sort(out, [this](const QString &a, const QString &b) {
        const qint64 ta = m_changed.value(a), tb = m_changed.value(b);
        return ta != tb ? ta > tb : a < b;
    });
    return out;
}

bool ChatLineage::isDescendant(const QString &path, const QString &ancestor) const
{
    if (path.isEmpty() || ancestor.isEmpty())
        return false;
    QString at = path;
    for (int i = 0; i <= MaxDepth + 1; ++i) {
        at = parentOf(at);
        if (at.isEmpty())
            return false;
        if (at.compare(ancestor, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

int ChatLineage::runningChildren(const QString &path) const
{
    const QStringList children = childrenOf(path);
    return int(std::ranges::count_if(children, [this](const QString &c) { return m_chats->isRunning(c); }));
}

QVariantList ChatLineage::children(const QString &path, int) const
{
    QVariantList out;
    for (const QString &child : childrenOf(path)) {
        QString failure;
        const QString state = m_chats->stateOf(child, &failure);
        const QString partner = m_chats->partnerOf(child);
        const QString label = m_chats->partnerLabel(partner);
        out.append(QVariantMap{{u"path"_s, child},
                               {u"name"_s, m_chats->nameOf(child)},
                               {u"partner"_s, label.isEmpty() ? partner : label},
                               {u"state"_s, state},
                               {u"failure"_s, failure}});
    }
    return out;
}

} // namespace brulion::shell

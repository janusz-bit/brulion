#pragma once
// ChatLineage — which conversations of the AI chat an AI agent started from which (docs/UI.md
// "Agents delegate"; QML: Workspace.chats.lineage). An agent the app starts with Brulion's tools
// may hand a task to another partner of the chat — Codex for a second opinion, the assistant for a
// note — through its chat tools (ChatControl): that conversation is a sub-conversation, its page
// naming the one it came from (`parent: "[[AI/…]]"`, core chat/chat.h). The parent conversation's
// view lists its sub-conversations with their partner and state (children()); a sub-conversation
// shows where it came from.
//
// Limits against runaway delegation: a conversation MaxDepth levels down starts none of its own
// (its agent gets no chat tools), and at most MaxRunningChildren sub-conversations of one
// conversation run at once.
//
//   Workspace.chats.lineage.children(path, Workspace.chats.lineage.serial)   // [{path, name, partner, …}]
//   Workspace.chats.lineage.parentOf(path)                                  // "AI/2026-11-22 Plan.md"
//
// The parents come from the pages (they may come from another device: a sync), read once — and a
// conversation's page again when it changes, all of them when one comes or goes. Single-threaded (the
// GUI thread).

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace brulion::shell {

class Workspace;
class WorkspaceChats;

class ChatLineage : public QObject {
    Q_OBJECT
    /// Changes whenever a conversation's parent, state or question may have changed (children()
    /// asks again).
    Q_PROPERTY(int serial READ serial NOTIFY changed FINAL)

public:
    /// How deep sub-conversations go: one this many levels below a conversation of the user's
    /// starts no more.
    static constexpr int MaxDepth = 3;
    /// The sub-conversations of one conversation that may run at once.
    static constexpr int MaxRunningChildren = 4;

    ChatLineage(Workspace *workspace, WorkspaceChats *chats, QObject *parent = nullptr);

    int serial() const { return m_serial; }

    /// The conversation `path` was started from, as a vault path ("" none; a parent that is no
    /// page of the notebook any more: "" too).
    Q_INVOKABLE QString parentOf(const QString &path) const;
    /// How many levels below a conversation of the user's `path` is (0: none above it; a parent
    /// that is gone counts as one more).
    int depthOf(const QString &path) const;
    /// The sub-conversations of `path` (vault paths), newest first.
    QStringList childrenOf(const QString &path) const;
    /// `path` is a sub-conversation of `ancestor`, at any depth.
    bool isDescendant(const QString &path, const QString &ancestor) const;
    /// How many of `path`'s sub-conversations run now.
    int runningChildren(const QString &path) const;
    /// The sub-conversations of `path` as the view shows them: {path, name, partner (its name),
    /// state: idle | running | queued | asking | failed, failure}.
    Q_INVOKABLE QVariantList children(const QString &path, int serial = 0) const;

    /// The parent of `path` as written in its page ("AI/2026-11-22 Plan"; "" none) — the
    /// Workspace alone, for WorkspaceAgents.
    static QString writtenParent(Workspace *workspace, const QString &path);
    /// depthOf() with the Workspace alone.
    static int depth(Workspace *workspace, const QString &path);
    /// The page a parent link names, from the conversation `from` ("" none there): the vault path
    /// it writes (".md" added), else the link resolved as any.
    static QString resolveParent(Workspace *workspace, const QString &parent, const QString &from);

Q_SIGNALS:
    void changed();

private:
    /// The pages' parents read again when asked next.
    void invalidate();
    void bump();
    void ensure() const;
    /// The parent of `path` read again (its page changed).
    void update(const QString &path) const;

    Workspace *m_workspace = nullptr;
    WorkspaceChats *m_chats = nullptr;
    int m_serial = 0;
    mutable bool m_dirty = true;
    /// The conversations that name a parent: child path → its parent's path ("" one that is gone),
    /// and each one's last change (newest first in children()).
    mutable QHash<QString, QString> m_parents;
    mutable QHash<QString, qint64> m_changed;
};

} // namespace brulion::shell

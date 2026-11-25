// ViewKeeper — which hidden views a pane holds on to (ShellRouteView with `keepViews`; docs/UI.md
// "Navigation", docs/PERFORMANCE.md "Memory: only what is in use").
//
// The bottom navigation's views — Journal, All pages, Search, Tasks, Calendar — stay, hidden, once
// they were shown, so that going back to one costs a frame instead of building it anew. Every one
// of them holds its items, its model and its pictures for as long as it exists (Tasks and
// Calendar tens of megabytes in a large notebook). The keeper bounds that by use:
//
//   * at most `limit` hidden views, the most recently shown (-1: all of them);
//   * none of them once the view shown was not switched for `idleMs` (0: never);
//   * none of them when the app is away (ui::MemoryPolicy::letGo()).
//
// A view is "let go" by setting its Loader's property `kept` to false (KeptLoader: `active:
// shown || kept`): the Loader deletes it, and makes it again when it is shown next.
//
//   ViewKeeper { id: keeper; limit: MemoryPolicy.keptViews; idleMs: MemoryPolicy.hiddenViewMs }
//   Component.onCompleted: keeper.add("tasks", tasksLoader)
//   keeper.show("tasks")   // "": none of the kept views is shown (a page, Settings, …)
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

namespace brulion::shell {

class ViewKeeper : public QObject {
    Q_OBJECT
    /// Hidden views held (the most recently shown); -1: all of them, 0: none.
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged FINAL)
    /// The hidden views go when show() was not called for this long (0: never).
    Q_PROPERTY(int idleMs READ idleMs WRITE setIdleMs NOTIFY idleMsChanged FINAL)
    /// The hidden views held now, the one shown last first.
    Q_PROPERTY(QStringList hidden READ hidden NOTIFY hiddenChanged FINAL)

public:
    explicit ViewKeeper(QObject *parent = nullptr);

    int limit() const { return m_limit; }
    void setLimit(int limit);
    int idleMs() const { return m_idleMs; }
    void setIdleMs(int ms);
    QStringList hidden() const { return m_hidden; }

    /// `loader` holds the view `key` while its property `kept` is true.
    Q_INVOKABLE void add(const QString &key, QObject *loader);
    /// The view `key` is shown now ("" or a key never added: none of the kept views): the one
    /// shown before joins the hidden ones, those beyond the limit go, the idle time starts again.
    Q_INVOKABLE void show(const QString &key);
    /// The view `key` was built ahead, hidden: held like one shown just before the current one.
    Q_INVOKABLE void built(const QString &key);
    /// Every hidden view goes now.
    Q_INVOKABLE void letGoHidden();
    /// Nothing is held any more (the pane shows no route: its Loaders were emptied).
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void limitChanged();
    void idleMsChanged();
    void hiddenChanged();
    /// The view `key` was let go (its Loader's `kept` is false).
    void letGo(const QString &key);

private:
    void trim();
    void drop(const QString &key);
    void startIdle();

    QHash<QString, QPointer<QObject>> m_loaders;
    QStringList m_hidden; ///< the one shown last first
    QString m_shown;
    QTimer m_idle;
    int m_limit = -1;
    int m_idleMs = 0;
};

} // namespace brulion::shell

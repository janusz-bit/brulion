// WorkspaceListModel — base of the content models: a list model over the open vault of a
// shell::Workspace (`workspace` property, a QObject* so QML passes the `Workspace` singleton),
// refreshed (coalesced, `refreshDelay` ms) whenever the workspace's files or index change, and
// cleared while no vault is open.
//
// Subclasses implement refresh() and publish their rows with applyRows(): the new list is
// compared with the current one by a key and only the differences are signalled (removals,
// moves, insertions, dataChanged), so delegates — e.g. a journal day being edited — survive
// background updates (sync, Syncthing, another pane saving). Large reorderings fall back to a
// model reset.
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace brulion {
class VaultIndex;
}

namespace brulion::shell {
class Workspace;
}

namespace brulion::content {

class WorkspaceListModel : public QAbstractListModel {
    Q_OBJECT
    /// The shell's Workspace (QML: the `Workspace` singleton); null = no rows.
    Q_PROPERTY(QObject *workspace READ workspaceObject WRITE setWorkspaceObject NOTIFY workspaceChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    explicit WorkspaceListModel(QObject *parent = nullptr);
    ~WorkspaceListModel() override;

    QObject *workspaceObject() const;
    void setWorkspaceObject(QObject *workspace);
    shell::Workspace *workspace() const;
    void setWorkspace(shell::Workspace *workspace);
    /// The open vault's index (null while closed).
    VaultIndex *vaultIndex() const;

    int count() const { return rowCount(); }
    int rowCount(const QModelIndex &parent = {}) const override;

    /// Recompute the rows now (tests; normally coalesced after changes).
    Q_INVOKABLE void refreshNow();

Q_SIGNALS:
    void workspaceChanged();
    void countChanged();
    /// The rows were recomputed (after every refresh, also when nothing changed).
    void refreshed();

protected:
    /// Recompute the rows (call applyRows() / reset the model) — only from refreshNow().
    virtual void refresh() = 0;
    /// Number of rows (the subclass owns them).
    virtual int rows() const = 0;
    /// Schedule a refresh after `ms` (coalesced; an earlier pending refresh wins).
    void scheduleRefresh(int ms = 0);
    /// Delay of refreshes caused by vault changes (default 120 ms).
    void setRefreshDelay(int ms) { m_refreshDelay = ms; }
    /// Called when the workspace changes or a vault opens/closes (before the refresh).
    virtual void workspaceReset() { }

    /// Replace `rows` by `next` with fine-grained model signals. `key(row)` identifies rows
    /// (unique in each list); `same(a, b)` decides whether a kept row changed its data.
    template<typename Row, typename KeyFn, typename SameFn>
    void applyRows(QList<Row> &rows, QList<Row> next, KeyFn key, SameFn same);

private:
    void connectWorkspace();

    QPointer<shell::Workspace> m_workspace;
    QTimer m_timer;
    int m_refreshDelay = 120;
    int m_lastCount = 0;
};

template<typename Row, typename KeyFn, typename SameFn>
void WorkspaceListModel::applyRows(QList<Row> &rows, QList<Row> next, KeyFn key, SameFn same)
{
    using Key = std::decay_t<decltype(key(next.first()))>;
    const auto reset = [&] {
        beginResetModel();
        rows = std::move(next);
        endResetModel();
    };
    if (rows.isEmpty() || next.isEmpty()) {
        if (rows.isEmpty() && next.isEmpty())
            return;
        reset();
        return;
    }
    QHash<Key, int> wanted;
    wanted.reserve(next.size());
    for (int i = 0; i < next.size(); ++i)
        wanted.insert(key(next.at(i)), i);

    // Removals, in contiguous runs from the end.
    for (int i = int(rows.size()) - 1; i >= 0; --i) {
        if (wanted.contains(key(rows.at(i))))
            continue;
        int first = i;
        while (first > 0 && !wanted.contains(key(rows.at(first - 1))))
            --first;
        beginRemoveRows({}, first, i);
        rows.remove(first, i - first + 1);
        endRemoveRows();
        i = first;
    }

    // Walk the wanted order: keep, move up, or insert.
    QHash<Key, bool> present;
    present.reserve(rows.size());
    for (const Row &r : std::as_const(rows))
        present.insert(key(r), true);
    constexpr int MaxMoves = 64;
    int moves = 0;
    for (int i = 0; i < next.size(); ++i) {
        const Key k = key(next.at(i));
        if (i < rows.size() && key(rows.at(i)) == k) {
            if (!same(rows.at(i), next.at(i))) {
                rows[i] = next.at(i);
                Q_EMIT dataChanged(index(i), index(i));
            }
            continue;
        }
        int found = -1;
        if (present.contains(k)) {
            for (int j = i + 1; j < rows.size(); ++j) {
                if (key(rows.at(j)) == k) {
                    found = j;
                    break;
                }
            }
        }
        if (found < 0) {
            int last = i;
            // Insert the run of new keys at once.
            while (last + 1 < next.size() && !present.contains(key(next.at(last + 1))))
                ++last;
            beginInsertRows({}, i, last);
            for (int n = i; n <= last; ++n)
                rows.insert(n, next.at(n));
            endInsertRows();
            i = last;
            continue;
        }
        if (++moves > MaxMoves) {
            reset();
            return;
        }
        beginMoveRows({}, found, found, {}, i);
        rows.move(found, i);
        endMoveRows();
        if (!same(rows.at(i), next.at(i))) {
            rows[i] = next.at(i);
            Q_EMIT dataChanged(index(i), index(i));
        }
    }
    // Anything left over (should not happen with unique keys) goes.
    if (rows.size() > next.size()) {
        beginRemoveRows({}, int(next.size()), int(rows.size()) - 1);
        rows.resize(next.size());
        endRemoveRows();
    }
}

} // namespace brulion::content

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
//
// `active` (default true): a list nobody sees computes nothing. While it is false — the view
// kept hidden by ShellRouteView (All pages, Search, the Journal while a page is shown) —, a change
// of the files or the index only marks the rows stale; they are computed once when `active`
// becomes true again (docs/PERFORMANCE.md "Only what is on screen, only what changed"). The first fill, a new
// workspace and changes of the model's own properties (a filter, a folder) are computed as
// before, also while inactive.
//
// A subclass that knows which notes its rows come from overrides affectedBy(): a change of the
// index (VaultIndex::changesSince() since the last refresh) that touches none of them is skipped
// — the references of a page do not change while that page itself is being typed in.
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace brulion {
class VaultIndex;
struct IndexChanges;
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
    /// The rows follow the changes of the files and the index (false: they are only marked
    /// stale, and computed again when this is true again — a hidden view).
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged FINAL)

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
    bool isActive() const { return m_active; }
    void setActive(bool active);
    /// A change came while inactive: the rows are computed again when the model is active.
    bool isStale() const { return m_stale; }
    int rowCount(const QModelIndex &parent = {}) const override;

    /// Recompute the rows now (tests; normally coalesced after changes).
    Q_INVOKABLE void refreshNow();

Q_SIGNALS:
    void workspaceChanged();
    void countChanged();
    void activeChanged();
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
    /// Whether the rows can differ after `changes` of the index since the last refresh (only
    /// asked for a complete log without changed names: anything can differ otherwise). Default:
    /// always — the rows are computed again after every change.
    virtual bool affectedBy(const IndexChanges &changes) const;
    /// Every role of row `row` by name ({} for a bad row): the subclasses' get().
    QVariantMap rowMap(int row) const;
    /// Something else the rows come from changed (the attachments' text): as a change of the files
    /// — a refresh after the delay, or the rows marked stale while inactive.
    void markContentChanged() { contentChanged(); }

    /// Replace `rows` by `next` with fine-grained model signals. `key(row)` identifies rows
    /// (unique in each list); `same(a, b)` decides whether a kept row changed its data.
    template<typename Row, typename KeyFn, typename SameFn>
    void applyRows(QList<Row> &rows, QList<Row> next, KeyFn key, SameFn same);

private:
    void connectWorkspace();
    /// The files or the index changed: refresh after the delay, or only mark stale while inactive.
    void contentChanged();
    /// The index changed since the last refresh only in notes the rows do not come from
    /// (affectedBy()); then the rows count as computed at the index's current version.
    bool unaffectedByIndexChanges();
    /// countChanged() when the row count differs from the last one told.
    void publishCount();

    QPointer<shell::Workspace> m_workspace;
    QTimer m_timer;
    int m_refreshDelay = 120;
    int m_lastCount = 0;
    bool m_active = true;
    bool m_stale = false;
    /// The index (VaultIndex::serial(), 0 = none) and its version the rows were computed at.
    quint64 m_indexSerial = 0;
    int m_indexVersion = -1;
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
    QSet<Key> present;
    present.reserve(rows.size());
    for (const Row &r : std::as_const(rows))
        present.insert(key(r));
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

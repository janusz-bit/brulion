// QueryCache — runQuery() for a query asked again and again (a ```query block, the Tasks view, the
// Android widgets' task list): after an index change only the changed notes are evaluated again.
// Part of the index module — see index/index.h.
#pragma once

#include "index/notecache.h"
#include "index/query.h"

#include <QSet>

#include <memory>

namespace brulion {

/// Keeps the matches of one query over the whole vault in result order. update() follows the
/// index (VaultIndex::changesSince()): only the notes a change names are evaluated again and
/// merged into the order. Everything is evaluated again when the query, `today`, `weekStart` or
/// QueryOptions::currentPath differ from the previous update(), for another index, when the
/// change log does not reach back, when names changed and the query has `page:` / `ref:`
/// filters (they resolve names), and on every update() of a query with a `remind:` filter (it
/// depends on the time of day). result() equals runQuery() with the same arguments — one code
/// path: runQuery() is a cache without history.
class QueryCache {
public:
    QueryCache();
    ~QueryCache();
    QueryCache(QueryCache &&) noexcept;
    QueryCache &operator=(QueryCache &&) noexcept;
    QueryCache(const QueryCache &) = delete;
    QueryCache &operator=(const QueryCache &) = delete;

    /// Bring the matches up to date with `index` (no result built). Returns what was evaluated
    /// again (also lastSync()).
    const NoteCacheSync &update(const Query &query, const VaultIndex &index, QDate today,
                                Qt::DayOfWeek weekStart = Qt::Monday, const QueryOptions &options = {});
    /// The result of the last update(), exactly as runQuery() gives it (`index` unchanged since:
    /// items are read from its notes). Empty before the first update().
    QueryResult result(const VaultIndex &index) const;
    /// update() + result().
    QueryResult run(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart = Qt::Monday,
                    const QueryOptions &options = {});
    /// The matches of note `path` in result order (all of them: no `limit`).
    QList<QueryItem> itemsOf(const VaultPath &path, const VaultIndex &index) const;
    /// The matches of the notes `paths` in result order (all of them: no `limit`) — one pass over
    /// the matches, however many notes (a change naming thousands of them).
    QList<QueryItem> itemsOf(const QSet<VaultPath> &paths, const VaultIndex &index) const;
    /// Matches in all (QueryResult::total).
    int total() const;
    /// What the last update() evaluated again.
    const NoteCacheSync &lastSync() const;
    /// Forget everything (the next update() evaluates every note).
    void clear();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

/// The query has filters that resolve names (`page:`, `ref:` — links, aliases, tags naming pages):
/// a name change anywhere (VaultIndex::changesSince()'s `names`) may change their matches in any
/// note. QueryCache follows the index fully then; so must anything following the index beside
/// a QueryCache for the same query (IndexFollower::follow()'s `dependsOnNames`).
bool queryResolvesNames(const Query &query);

} // namespace brulion

// Routes and the back/forward history of the main view (docs/UI.md "Navigation").
//
// A route is a QVariantMap {view, path?, line?, blockId?, date?, tag?, query?, ...}:
//
//   view           shows                         parameters
//   "journals"     JournalsView (home)           date? (scroll to that day)
//   "journal"      JournalsView, one day         date ("YYYY-MM-DD"), path?
//   "page"         PageView                      path, line?, blockId?
//   "calendar"     CalendarView                  date?
//   "tasks"        TasksView                     query?
//   "graph"        GraphView                     path? (focus)
//   "search"       SearchView                    query?
//   "allPages"     AllPagesView                  path? (folder)
//   "tags" / "tag" TagsView                      tag?
//   "sync"         SyncView
//   "importExport" ImportExportView
//   "history"      HistoryView                   path
//   "trash"        TrashView
//   "settings"     SettingsView                  section?
//
// normalizeRoute() keeps non-empty values (`line` as an int >= 0), maps the alias "home" to
// "journals", turns {view: "tag"} without a tag into "tags", and returns an empty map for an
// unknown view or a missing required parameter (page/history: path, journal: valid date).
// Views receive the whole route, so extra keys (e.g. "section" for settings) pass through.
#pragma once

#include <QList>
#include <QStringList>
#include <QVariantMap>

namespace brulion::shell {

/// The known route views.
QStringList routeViews();

/// Clean a route: known view (else {}), only non-empty parameters, `line` as int >= 0.
QVariantMap normalizeRoute(const QVariantMap &route);

/// Routes that show the same thing (same view and parameters; `line`/`blockId` included).
bool sameRoute(const QVariantMap &a, const QVariantMap &b);

/// Back/forward history: the current route plus the routes before and after it.
class RouteHistory {
public:
    static constexpr int MaxDepth = 100;

    explicit RouteHistory(const QVariantMap &home = {});

    QVariantMap current() const { return m_current; }
    QVariantMap home() const { return m_home; }
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    int backDepth() const { return int(m_back.size()); }
    int forwardDepth() const { return int(m_forward.size()); }

    /// Go to `route` (pushes the current one, clears forward). Returns false (and changes
    /// nothing) when it is invalid or the same as the current route.
    bool navigate(const QVariantMap &route);
    /// Replace the current route without touching back/forward (e.g. a view updates its
    /// parameters while the user scrolls). False when invalid.
    bool replace(const QVariantMap &route);
    bool back();
    bool forward();
    /// Clear everything and show the home route.
    void reset();
    /// Apply `f(QVariantMap &route)` to every route (back, current, forward), e.g. to follow a
    /// renamed page. The caller keeps the routes valid.
    template<typename F>
    void transform(F f)
    {
        for (QVariantMap &route : m_back)
            f(route);
        f(m_current);
        for (QVariantMap &route : m_forward)
            f(route);
    }
    /// Drop history entries for which `pred` is true, e.g. pages that were deleted. When the
    /// current route goes, the previous remaining one (else home) becomes current.
    template<typename Pred>
    bool removeIf(Pred pred)
    {
        bool changed = false;
        for (auto *list : { &m_back, &m_forward }) {
            const auto before = list->size();
            list->removeIf(pred);
            changed |= list->size() != before;
        }
        if (pred(m_current)) {
            m_current = m_back.isEmpty() ? m_home : m_back.takeLast();
            changed = true;
        }
        // Collapse neighbouring duplicates left behind (also next to the current route).
        for (auto *list : { &m_back, &m_forward }) {
            for (qsizetype i = list->size() - 1; i > 0; --i) {
                if (sameRoute(list->at(i), list->at(i - 1)))
                    list->removeAt(i);
            }
            while (!list->isEmpty() && sameRoute(list->last(), m_current))
                list->removeLast();
        }
        return changed;
    }

private:
    QVariantMap m_home;
    QVariantMap m_current;
    QList<QVariantMap> m_back;
    QList<QVariantMap> m_forward;
};

} // namespace brulion::shell

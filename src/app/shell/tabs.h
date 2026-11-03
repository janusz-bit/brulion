// Tabs of the main window, its split view and pages in windows of their own (docs/UI.md "Tabs,
// split view and windows"). A pure model: AppController keeps one for the open notebook and
// saves it per notebook as a device setting (save() / restore()).
//
// Every tab has its own back/forward history (RouteHistory) and the view state of its current
// route (where its page was scrolled; dropped when the route changes). The main window shows the
// tabs of its bar: one in the main pane (`mainTab`), optionally a second one beside it in the
// split pane (`splitTab`, 0 = no split); one of the two panes is focused (`splitFocused`). A tab
// can also live in a window of its own (desktop: `windowed`), outside the bar. The tab navigation
// goes to (`active`) is the focused pane's — or, while such a window has the focus, the window's.
// Invariants: the bar is never empty (closing the last tab opens a fresh one at home); `mainTab`
// is a bar tab; `splitTab` is 0 or another bar tab; `active` is `mainTab`, `splitTab` or a windowed
// tab.
#pragma once

#include "navigation.h"

#include <QByteArray>
#include <QList>
#include <QVariantMap>

namespace brulion::shell {

struct Tab {
    int id = 0;
    RouteHistory history;
    /// Where its view was scrolled for the current route ({} = nothing kept): the view's own keys
    /// (PageView: row, offset). Dropped whenever the current route changes.
    QVariantMap viewState;
    /// Shown in a window of its own (desktop) instead of the main window's bar.
    bool windowed = false;
};

class TabSet {
public:
    /// Tabs at most (the bar and windows together): open() refuses more.
    static constexpr int MaxTabs = 100;
    /// Back / forward entries a tab keeps across restarts (each side; the nearest ones).
    static constexpr int SavedDepth = 20;

    explicit TabSet(const QVariantMap &home = {});

    QVariantMap home() const { return m_home; }

    /// The ids of the main window's bar, in order.
    QList<int> bar() const;
    /// The ids of the tabs in windows of their own, in the order they were opened.
    QList<int> windows() const;
    /// Bar and windows.
    int size() const { return int(m_bar.size() + m_windows.size()); }
    bool contains(int id) const { return tab(id) != nullptr; }
    const Tab *tab(int id) const;
    /// Position in the bar, -1 for none (also a windowed tab).
    int indexOf(int id) const;

    int active() const { return m_active; }
    int mainTab() const { return m_main; }
    int splitTab() const { return m_split; }
    bool splitFocused() const { return m_split != 0 && m_splitFocused; }
    /// The main window's focused pane's tab: the split one when focused, else the main one.
    int focusedBarTab() const { return splitFocused() ? m_split : m_main; }
    /// The tab's current route ({} for none).
    QVariantMap route(int id) const;

    // ---- One tab's navigation (id 0: the active tab) ----------------------------------------
    /// RouteHistory::navigate() / replace() / back() / forward() of tab `id`; the view state
    /// goes when the route changes. False when nothing changed (or no such tab).
    bool navigate(int id, const QVariantMap &route);
    bool replace(int id, const QVariantMap &route);
    bool back(int id);
    bool forward(int id);
    /// Back to the home route (pushes the current one, as RouteHistory::navigate()).
    bool goHome(int id);
    bool setViewState(int id, const QVariantMap &state);
    /// RouteHistory of tab `id` (read only; nullptr for none).
    const RouteHistory *history(int id) const;

    // ---- Tabs -------------------------------------------------------------------------------
    /// A new bar tab showing `route` (home when {} or invalid), right after the focused one;
    /// `select`: shown in the focused pane and active. Returns its id, 0 when MaxTabs are open.
    int open(const QVariantMap &route, bool select = true);
    /// A new tab in a window of its own (active). Its id, 0 when MaxTabs are open.
    int openWindow(const QVariantMap &route);
    /// Make `id` active: a bar tab shown in a pane focuses that pane, another bar tab replaces
    /// the focused pane's one; a windowed tab (its window got the focus). False when unknown.
    bool select(int id);
    /// select() the bar tab at `index`; -1 (or past the end): the last one (Ctrl+9).
    bool selectIndex(int index);
    /// The bar tab `step` places from the focused one (wrapping around; 0: that one).
    int neighbour(int step) const;
    /// Drop tab `id`. A shown bar tab's pane gets its neighbour (the split pane closes instead,
    /// and closing the main pane's tab when the split one is the only other lets that one take
    /// the whole width); the last bar tab is replaced by a fresh one at home. False when unknown.
    bool close(int id);
    /// Drop every bar tab but `id` (the split closes); windows stay.
    bool closeOthers(int id);
    /// Move the bar tab at `from` to `to` (drag and drop in the bar).
    bool move(int from, int to);
    /// Show bar tab `id` in the split pane, focused (it may not be the main pane's); id 0: a new
    /// tab with the focused tab's route (Podziel widok). False when nothing changed.
    bool split(int id = 0);
    /// The split pane goes (its tab stays in the bar); the main pane is focused.
    bool closeSplit();
    /// Focus the main window's split pane (or its main pane); it becomes active.
    bool focusPane(bool split);
    /// The main window got the focus: its focused pane's tab is active again.
    bool focusMainWindow();
    /// Bar tab `id` into a window of its own (active); its pane gets another tab as close() does.
    bool detach(int id);
    /// Windowed tab `id` back into the bar, after the focused tab, shown in the focused pane.
    bool attach(int id);
    /// One tab at home, nothing else.
    void reset();

    // ---- Saving -----------------------------------------------------------------------------
    /// JSON: { tabs: [{ back, current, forward, view }], main, split, splitFocused } — windowed
    /// tabs come back as bar tabs after the others; histories trimmed to SavedDepth per side.
    QByteArray save() const;
    /// Replace everything with what save() wrote. Invalid routes are dropped (a tab without a
    /// valid current route goes); false — and one tab at home — for malformed JSON or no tab
    /// left. Ids are new.
    bool restore(const QByteArray &json);

    /// Apply `f(QVariantMap &route)` to every route of every tab (a renamed page).
    template<typename F>
    void transform(F f)
    {
        for (auto *list : {&m_bar, &m_windows}) {
            for (Tab &t : *list)
                t.history.transform(f);
        }
    }
    /// RouteHistory::removeIf() on every tab; the ids whose current route changed (their view
    /// state goes) — and `historyOnly`, those whose back/forward lost entries.
    template<typename Pred>
    QList<int> removeIf(Pred pred, QList<int> *historyOnly = nullptr)
    {
        QList<int> changed;
        for (auto *list : {&m_bar, &m_windows}) {
            for (Tab &t : *list) {
                const QVariantMap before = t.history.current();
                if (!t.history.removeIf(pred))
                    continue;
                if (!sameRoute(before, t.history.current())) {
                    t.viewState.clear();
                    changed.append(t.id);
                } else if (historyOnly) {
                    historyOnly->append(t.id);
                }
            }
        }
        return changed;
    }

private:
    Tab *find(int id);
    Tab makeTab(const QVariantMap &route, bool windowed);
    /// The bar position a new tab goes to: after the focused one.
    qsizetype insertPosition() const;
    /// After bar tab `id` left the bar (closed or detached; `index` was its place): the panes and
    /// the active tab show valid tabs again.
    void barTabLeft(int id, qsizetype index);

    QVariantMap m_home;
    QList<Tab> m_bar;
    QList<Tab> m_windows;
    int m_nextId = 1;
    int m_main = 0;
    int m_split = 0;
    bool m_splitFocused = false;
    int m_active = 0;
};

} // namespace brulion::shell

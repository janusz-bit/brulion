#include "tabs.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// The last `count` routes (the ones next to the current route), as JSON.
QJsonArray routesJson(const QList<QVariantMap> &routes, int count)
{
    QJsonArray out;
    for (qsizetype i = std::max<qsizetype>(0, routes.size() - count); i < routes.size(); ++i)
        out.append(QJsonObject::fromVariantMap(routes.at(i)));
    return out;
}

/// The routes of a saved list (objects only), the last `count` of them.
QList<QVariantMap> routesFrom(const QJsonValue &value, int count)
{
    QList<QVariantMap> out;
    const QJsonArray array = value.toArray();
    for (const auto &v : array) {
        if (v.isObject())
            out.append(v.toObject().toVariantMap());
    }
    return out.size() > count ? out.last(count) : out;
}

/// The position of tab `id` in `tabs`, -1 for none.
qsizetype indexIn(const QList<Tab> &tabs, int id)
{
    const auto it = std::ranges::find(tabs, id, &Tab::id);
    return it == tabs.cend() ? -1 : it - tabs.cbegin();
}

/// Whether the tab's route `changed` — then its view state goes.
bool routeChanged(Tab &t, bool changed)
{
    if (changed)
        t.viewState.clear();
    return changed;
}

} // namespace

TabSet::TabSet(const QVariantMap &home)
    : m_home(home.isEmpty() || normalizeRoute(home).isEmpty() ? QVariantMap{{u"view"_s, u"journals"_s}}
                                                              : normalizeRoute(home))
{
    reset();
}

QList<int> TabSet::bar() const
{
    QList<int> out(m_bar.size());
    std::ranges::transform(m_bar, out.begin(), &Tab::id);
    return out;
}

QList<int> TabSet::windows() const
{
    QList<int> out(m_windows.size());
    std::ranges::transform(m_windows, out.begin(), &Tab::id);
    return out;
}

const Tab *TabSet::tab(int id) const
{
    for (const QList<Tab> *list : {&m_bar, &m_windows}) {
        if (const qsizetype at = indexIn(*list, id); at >= 0)
            return &list->at(at);
    }
    return nullptr;
}

Tab *TabSet::find(int id)
{
    return const_cast<Tab *>(std::as_const(*this).tab(id == 0 ? m_active : id));
}

int TabSet::indexOf(int id) const
{
    return int(indexIn(m_bar, id));
}

QVariantMap TabSet::route(int id) const
{
    const Tab *t = tab(id);
    return t ? t->history.current() : QVariantMap();
}

const RouteHistory *TabSet::history(int id) const
{
    const Tab *t = tab(id == 0 ? m_active : id);
    return t ? &t->history : nullptr;
}

// ---- One tab's navigation ----------------------------------------------------------------------

bool TabSet::navigate(int id, const QVariantMap &route)
{
    Tab *t = find(id);
    return t && routeChanged(*t, t->history.navigate(route));
}

bool TabSet::replace(int id, const QVariantMap &route)
{
    Tab *t = find(id);
    if (!t)
        return false;
    const QVariantMap before = t->history.current();
    // (A view updates its parameters as the user scrolls or types: its view state stays.)
    return t->history.replace(route) && !sameRoute(before, t->history.current());
}

bool TabSet::back(int id)
{
    Tab *t = find(id);
    return t && routeChanged(*t, t->history.back());
}

bool TabSet::forward(int id)
{
    Tab *t = find(id);
    return t && routeChanged(*t, t->history.forward());
}

bool TabSet::goHome(int id)
{
    return navigate(id, m_home);
}

bool TabSet::setViewState(int id, const QVariantMap &state)
{
    Tab *t = find(id);
    if (!t || t->viewState == state)
        return false;
    t->viewState = state;
    return true;
}

// ---- Tabs ----------------------------------------------------------------------------------------

Tab TabSet::makeTab(const QVariantMap &route, bool windowed)
{
    Tab t;
    t.id = m_nextId++;
    t.history = RouteHistory(m_home);
    // The route as the tab's first one: nothing to go back to.
    t.history.replace(route);
    t.windowed = windowed;
    return t;
}

qsizetype TabSet::insertPosition() const
{
    const int at = indexOf(focusedBarTab());
    return at < 0 ? m_bar.size() : at + 1;
}

int TabSet::open(const QVariantMap &route, bool select)
{
    if (size() >= MaxTabs)
        return 0;
    const Tab t = makeTab(route, false);
    m_bar.insert(insertPosition(), t);
    if (select)
        this->select(t.id);
    return t.id;
}

int TabSet::openWindow(const QVariantMap &route)
{
    if (size() >= MaxTabs)
        return 0;
    const Tab t = makeTab(route, true);
    m_windows.append(t);
    m_active = t.id;
    return t.id;
}

bool TabSet::select(int id)
{
    const Tab *t = tab(id);
    if (!t)
        return false;
    bool changed = m_active != id;
    if (t->windowed) {
        m_active = id;
        return changed;
    }
    if (id == m_split) {
        changed |= !m_splitFocused;
        m_splitFocused = true;
    } else if (id == m_main) {
        changed |= m_splitFocused;
        m_splitFocused = false;
    } else {
        // Shown in the focused pane, instead of that pane's tab (which stays in the bar).
        if (splitFocused())
            m_split = id;
        else
            m_main = id;
        changed = true;
    }
    m_active = id;
    return changed;
}

bool TabSet::selectIndex(int index)
{
    if (m_bar.isEmpty())
        return false;
    if (index < 0 || index >= m_bar.size())
        index = int(m_bar.size() - 1);
    return select(m_bar.at(index).id);
}

int TabSet::neighbour(int step) const
{
    const qsizetype n = m_bar.size();
    if (n == 0)
        return 0;
    const qsizetype at = std::max(0, indexOf(focusedBarTab()));
    const qsizetype to = ((at + step) % n + n) % n;
    return m_bar.at(to).id;
}

void TabSet::barTabLeft(int id, qsizetype index)
{
    if (m_bar.isEmpty()) {
        m_bar.append(makeTab(m_home, false));
        m_main = m_bar.constFirst().id;
        m_split = 0;
        m_splitFocused = false;
    } else if (id == m_split) {
        m_split = 0;
        m_splitFocused = false;
    } else if (id == m_main) {
        // Its neighbour, the next one first, never the split pane's tab.
        int next = 0;
        for (qsizetype d = 0; next == 0 && d < m_bar.size() + 1; ++d) {
            for (const qsizetype i : {index + d, index - 1 - d}) {
                if (next == 0 && i >= 0 && i < m_bar.size() && m_bar.at(i).id != m_split)
                    next = m_bar.at(i).id;
            }
        }
        if (next != 0) {
            m_main = next;
        } else {
            // Only the split pane's tab is left: it takes the whole width.
            m_main = m_split;
            m_split = 0;
            m_splitFocused = false;
        }
    }
    if (m_active == id || !contains(m_active))
        m_active = focusedBarTab();
}

bool TabSet::close(int id)
{
    if (const qsizetype window = indexIn(m_windows, id); window >= 0) {
        m_windows.removeAt(window);
        if (m_active == id)
            m_active = focusedBarTab();
        return true;
    }
    const int index = indexOf(id);
    if (index < 0)
        return false;
    m_bar.removeAt(index);
    barTabLeft(id, index);
    return true;
}

bool TabSet::closeOthers(int id)
{
    const int index = indexOf(id);
    if (index < 0 || (m_bar.size() == 1 && m_split == 0))
        return false;
    m_bar = m_bar.sliced(index, 1);
    m_main = id;
    m_split = 0;
    m_splitFocused = false;
    if (const Tab *activeTab = tab(m_active); !activeTab || !activeTab->windowed)
        m_active = id;
    return true;
}

bool TabSet::move(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_bar.size() || to >= m_bar.size() || from == to)
        return false;
    m_bar.move(from, to);
    return true;
}

bool TabSet::split(int id)
{
    if (id == 0) {
        if (size() >= MaxTabs)
            return false;
        const Tab t = makeTab(route(focusedBarTab()), false);
        m_bar.insert(insertPosition(), t);
        // (From the focused split pane its copy replaces it there; the main pane stays.)
        id = t.id;
    } else {
        const Tab *t = tab(id);
        if (!t || t->windowed || id == m_main || (id == m_split && m_splitFocused && m_active == id))
            return false;
    }
    m_split = id;
    m_splitFocused = true;
    m_active = id;
    return true;
}

bool TabSet::closeSplit()
{
    if (m_split == 0)
        return false;
    const bool wasActive = m_active == m_split;
    m_split = 0;
    m_splitFocused = false;
    if (wasActive)
        m_active = m_main;
    return true;
}

bool TabSet::focusPane(bool split)
{
    if (split && m_split == 0)
        return false;
    const int focused = split ? m_split : m_main;
    const bool changed = m_splitFocused != split || m_active != focused;
    m_splitFocused = split;
    m_active = focused;
    return changed;
}

bool TabSet::focusMainWindow()
{
    const int focused = focusedBarTab();
    if (m_active == focused)
        return false;
    m_active = focused;
    return true;
}

bool TabSet::detach(int id)
{
    const int index = indexOf(id);
    if (index < 0)
        return false;
    Tab t = m_bar.takeAt(index);
    t.windowed = true;
    m_windows.append(std::move(t));
    barTabLeft(id, index);
    m_active = id;
    return true;
}

bool TabSet::attach(int id)
{
    const qsizetype window = indexIn(m_windows, id);
    if (window < 0)
        return false;
    Tab t = m_windows.takeAt(window);
    t.windowed = false;
    m_bar.insert(insertPosition(), std::move(t));
    select(id);
    return true;
}

void TabSet::reset()
{
    m_bar = {makeTab(m_home, false)};
    m_windows.clear();
    m_main = m_bar.constFirst().id;
    m_split = 0;
    m_splitFocused = false;
    m_active = m_main;
}

// ---- Saving --------------------------------------------------------------------------------------

QByteArray TabSet::save() const
{
    QJsonArray tabs;
    for (auto *list : {&m_bar, &m_windows}) {
        for (const Tab &t : *list) {
            QJsonObject o{{u"back"_s, routesJson(t.history.backRoutes(), SavedDepth)},
                          {u"current"_s, QJsonObject::fromVariantMap(t.history.current())},
                          {u"forward"_s, routesJson(t.history.forwardRoutes(), SavedDepth)}};
            if (!t.viewState.isEmpty())
                o.insert(u"view"_s, QJsonObject::fromVariantMap(t.viewState));
            tabs.append(o);
        }
    }
    const QJsonObject root{{u"tabs"_s, tabs},
                           {u"main"_s, indexOf(m_main)},
                           {u"split"_s, indexOf(m_split)}, // -1 for none: no tab has id 0
                           {u"splitFocused"_s, splitFocused()}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool TabSet::restore(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonObject root = QJsonDocument::fromJson(json, &error).object(); // {} unless an object
    const QJsonArray saved = root.value(u"tabs"_s).toArray();
    QList<Tab> bar;
    QList<int> idOfSaved; // the new id of each saved tab, 0 when it was dropped
    for (const auto &value : saved) {
        idOfSaved.append(0);
        const QJsonObject o = value.toObject();
        if (bar.size() >= MaxTabs || !o.value(u"current"_s).isObject())
            continue;
        Tab t;
        t.history = RouteHistory(m_home);
        if (!t.history.restore(routesFrom(o.value(u"back"_s), SavedDepth),
                               o.value(u"current"_s).toObject().toVariantMap(),
                               routesFrom(o.value(u"forward"_s), SavedDepth)))
            continue;
        t.id = m_nextId++;
        t.viewState = o.value(u"view"_s).toObject().toVariantMap(); // {} unless an object
        idOfSaved.last() = t.id;
        bar.append(std::move(t));
    }
    // Malformed JSON, no saved tab or none left.
    if (error.error != QJsonParseError::NoError || bar.isEmpty()) {
        reset();
        return false;
    }
    // The new id of a saved tab's index (main, split); 0 for none.
    const auto savedId = [&idOfSaved](const QJsonValue &v) { return idOfSaved.value(v.toInt(-1)); };
    m_bar = std::move(bar);
    m_windows.clear();
    m_main = savedId(root.value(u"main"_s));
    if (m_main == 0)
        m_main = m_bar.constFirst().id;
    m_split = savedId(root.value(u"split"_s));
    if (m_split == m_main)
        m_split = 0;
    m_splitFocused = m_split != 0 && root.value(u"splitFocused"_s).toBool();
    m_active = focusedBarTab();
    return true;
}

} // namespace brulion::shell

#include "navigation.h"

#include <QDate>

using namespace Qt::StringLiterals;

namespace brulion::shell {

QStringList routeViews()
{
    static const QStringList views{u"journals"_s, u"journal"_s, u"page"_s,         u"calendar"_s, u"tasks"_s,
                                   u"habits"_s,   u"graph"_s,   u"search"_s,       u"allPages"_s, u"tags"_s,
                                   u"tag"_s,      u"sync"_s,    u"importExport"_s, u"history"_s,  u"trash"_s,
                                   u"cleanup"_s,  u"replace"_s, u"settings"_s,     u"logs"_s,     u"drawing"_s,
                                   u"gym"_s};
    return views;
}

QVariantMap normalizeRoute(const QVariantMap &route)
{
    QString view = route.value(u"view"_s).toString();
    if (view == u"home")
        view = u"journals"_s;
    if (!routeViews().contains(view))
        return {};
    QVariantMap out;
    out.insert(u"view"_s, view);
    for (auto [key, v] : route.asKeyValueRange()) {
        if (key == u"view")
            continue;
        if (key == u"line") {
            bool ok = false;
            if (const int line = v.toInt(&ok); ok && line >= 0)
                out.insert(u"line"_s, line);
            continue;
        }
        if (!v.isValid() || v.isNull() || (v.typeId() == QMetaType::QString && v.toString().isEmpty()))
            continue;
        out.insert(key, v);
    }
    // Views that need a parameter.
    if ((view == u"page" || view == u"history" || view == u"drawing") && out.value(u"path"_s).toString().isEmpty())
        return {};
    if (view == u"journal") {
        const QDate d = QDate::fromString(out.value(u"date"_s).toString(), Qt::ISODate);
        if (!d.isValid())
            return {};
        out.insert(u"date"_s, d.toString(Qt::ISODate));
    }
    if (view == u"tag" && out.value(u"tag"_s).toString().isEmpty())
        out.insert(u"view"_s, u"tags"_s);
    return out;
}

bool sameRoute(const QVariantMap &a, const QVariantMap &b)
{
    if (a.size() != b.size())
        return false;
    for (auto [key, value] : a.asKeyValueRange()) {
        const auto other = b.constFind(key);
        // Compare as strings: QML hands numbers over as double, C++ as int.
        if (other == b.constEnd() || value.toString() != other->toString())
            return false;
    }
    return true;
}

RouteHistory::RouteHistory(const QVariantMap &home)
    : m_home(home.isEmpty() ? QVariantMap{ { u"view"_s, u"journals"_s } } : normalizeRoute(home)),
      m_current(m_home)
{
}

bool RouteHistory::navigate(const QVariantMap &route)
{
    const QVariantMap r = normalizeRoute(route);
    if (r.isEmpty() || sameRoute(r, m_current))
        return false;
    m_back.append(m_current);
    while (m_back.size() > MaxDepth)
        m_back.removeFirst();
    m_forward.clear();
    m_current = r;
    return true;
}

bool RouteHistory::replace(const QVariantMap &route)
{
    const QVariantMap r = normalizeRoute(route);
    if (r.isEmpty())
        return false;
    m_current = r;
    return true;
}

bool RouteHistory::back()
{
    if (m_back.isEmpty())
        return false;
    m_forward.append(m_current);
    m_current = m_back.takeLast();
    return true;
}

bool RouteHistory::forward()
{
    if (m_forward.isEmpty())
        return false;
    m_back.append(m_current);
    m_current = m_forward.takeLast();
    return true;
}

bool RouteHistory::restore(const QList<QVariantMap> &back, const QVariantMap &current,
                           const QList<QVariantMap> &forward)
{
    const QVariantMap now = normalizeRoute(current);
    if (now.isEmpty())
        return false;
    // `toward` is the end next to the current route (back: the last, forward: the last too).
    const auto clean = [&now](const QList<QVariantMap> &routes) {
        QList<QVariantMap> out;
        for (const QVariantMap &route : routes) {
            const QVariantMap r = normalizeRoute(route);
            if (!r.isEmpty() && (out.isEmpty() || !sameRoute(out.last(), r)))
                out.append(r);
        }
        while (!out.isEmpty() && sameRoute(out.last(), now))
            out.removeLast();
        while (out.size() > MaxDepth)
            out.removeFirst();
        return out;
    };
    m_back = clean(back);
    m_forward = clean(forward);
    m_current = now;
    return true;
}

void RouteHistory::reset()
{
    m_back.clear();
    m_forward.clear();
    m_current = m_home;
}

} // namespace brulion::shell

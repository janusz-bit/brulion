#include "navigation.h"

#include <QDate>

using namespace Qt::StringLiterals;

namespace brulion::shell {

QStringList routeViews()
{
    return {u"journals"_s, u"journal"_s,  u"page"_s,     u"calendar"_s, u"tasks"_s, u"graph"_s,
            u"search"_s,   u"allPages"_s, u"tags"_s,     u"tag"_s,      u"sync"_s,  u"importExport"_s,
            u"history"_s,  u"trash"_s,    u"settings"_s, u"logs"_s, u"drawing"_s};
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
    for (auto it = route.constBegin(); it != route.constEnd(); ++it) {
        if (it.key() == u"view")
            continue;
        if (it.key() == u"line") {
            bool ok = false;
            const int line = it.value().toInt(&ok);
            if (ok && line >= 0)
                out.insert(u"line"_s, line);
            continue;
        }
        const QVariant &v = it.value();
        if (!v.isValid() || v.isNull())
            continue;
        if (v.typeId() == QMetaType::QString && v.toString().isEmpty())
            continue;
        out.insert(it.key(), v);
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
    for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
        const auto other = b.constFind(it.key());
        if (other == b.constEnd())
            return false;
        // Compare as strings: QML hands numbers over as double, C++ as int.
        if (it.value().toString() != other.value().toString())
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

void RouteHistory::reset()
{
    m_back.clear();
    m_forward.clear();
    m_current = m_home;
}

} // namespace brulion::shell

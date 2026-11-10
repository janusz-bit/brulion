#include "viewtraits.h"

#include <QJSValue>
#include <QMetaProperty>
#include <QQmlComponent>

#include <utility>

namespace brulion::shell {

namespace {

/// The properties followed, with the flag each sets (0 for those read otherwise).
struct Trait {
    const char *name;
    int flag;
};
constexpr Trait kTraits[] = {
        {"title", 0},
        {"sidePanel", 0},
        {"noteMode", 0},
        {"statusText", 0},
        {"statusPosition", 0},
        {"editing", 1 << 0},
        {"toolbarDocked", 1 << 1},
        {"replacesBottomNav", 1 << 2},
        {"pictureRoute", 1 << 3},
        {"pictureAsText", 1 << 4},
        {"pdfRoute", 1 << 5},
        {"fileRoute", 1 << 6},
        {"sourceMode", 1 << 7},
        {"edgeSwipe", 1 << 8},
        {"canStepBack", 1 << 9},
};

/// A `var` property's value as the plain QVariant (a QJSValue unwrapped).
QVariant plain(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return value.value<QJSValue>().toVariant();
    return value;
}

} // namespace

ViewTraits::ViewTraits(QObject *parent) : QObject(parent) { }

void ViewTraits::setView(QObject *view)
{
    if (m_view == view)
        return;
    for (const QMetaObject::Connection &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_view = view;
    if (view) {
        const QMetaObject *meta = view->metaObject();
        const QMetaMethod slot = metaObject()->method(metaObject()->indexOfSlot("refresh()"));
        for (const Trait &trait : kTraits) {
            // (A property the view lacks is an invalid one, without a notify signal.)
            const QMetaProperty property = meta->property(meta->indexOfProperty(trait.name));
            if (property.hasNotifySignal())
                m_connections.append(connect(view, property.notifySignal(), this, slot));
        }
        m_connections.append(connect(view, &QObject::destroyed, this, &ViewTraits::viewDestroyed));
    }
    refresh();
    Q_EMIT viewChanged();
}

QVariant ViewTraits::read(const char *name) const
{
    if (!m_view)
        return {};
    const QMetaObject *meta = m_view->metaObject(); // (a property it lacks reads as an invalid QVariant)
    return plain(meta->property(meta->indexOfProperty(name)).read(m_view));
}

void ViewTraits::refresh()
{
    const auto text = [this](const char *name) {
        const QVariant value = read(name);
        return value.metaType() == QMetaType::fromType<QString>() ? value.toString() : QString();
    };
    int flags = 0;
    for (const Trait &trait : kTraits) {
        if (trait.flag == 0)
            continue;
        const QVariant value = read(trait.name);
        if (value.metaType() == QMetaType::fromType<bool>() && value.toBool())
            flags |= trait.flag;
    }
    Values values{.title = text("title"),
                  .sidePanel = qobject_cast<QQmlComponent *>(read("sidePanel").value<QObject *>()),
                  .noteMode = text("noteMode"),
                  .statusText = text("statusText"),
                  .statusPosition = text("statusPosition"),
                  .flags = flags};
    if (values == m_values)
        return;
    m_values = std::move(values);
    Q_EMIT traitsChanged();
}

void ViewTraits::viewDestroyed()
{
    m_connections.clear();
    m_view = nullptr;
    refresh();
    Q_EMIT viewChanged();
}

QMetaMethod ViewTraits::method(const char *name, int parameters) const
{
    if (!m_view)
        return {};
    const QMetaObject *meta = m_view->metaObject();
    for (int i = meta->methodCount() - 1; i >= 0; --i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() != QMetaMethod::Signal && m.parameterCount() == parameters && m.name() == name)
            return m;
    }
    return {};
}

QVariantMap ViewTraits::viewState() const
{
    const QMetaMethod m = method("viewState", 0);
    if (!m.isValid())
        return {};
    if (m.returnMetaType() == QMetaType::fromType<void>()) {
        m.invoke(m_view, Qt::DirectConnection);
        return {};
    }
    QVariant result(m.returnMetaType());
    m.invoke(m_view, Qt::DirectConnection,
             QTemplatedMetaMethodReturnArgument<void>{{result.metaType().iface(), m.typeName(), result.data()}});
    if (result.metaType() == QMetaType::fromType<QVariant>()) // (a `var` result: the QVariant in it)
        result = result.value<QVariant>();
    return plain(result).toMap();
}

void ViewTraits::restoreViewState(const QVariantMap &state)
{
    m_pending = state;
    applyPendingState();
}

void ViewTraits::applyPendingState()
{
    if (m_pending.isEmpty() || !m_view)
        return;
    QVariant argument(std::exchange(m_pending, {})); // (taken first: a state set during the call is kept)
    const QMetaMethod m = method("restoreViewState", 1);
    if (!m.isValid())
        return; // a view without restoreViewState() drops it
    // The parameter as the function declares it: QVariant for `var` (or no type), else the map
    // converted to its type.
    const QMetaType type = m.parameterMetaType(0);
    if (type == QMetaType::fromType<QVariant>())
        m.invoke(m_view, Qt::DirectConnection, QMetaMethodArgument{type.iface(), "QVariant", &argument});
    else if (argument.convert(type))
        m.invoke(m_view, Qt::DirectConnection, QMetaMethodArgument{type.iface(), type.name(), argument.constData()});
}

void ViewTraits::endEditing() const
{
    if (const QMetaMethod m = method("endEditing", 0); m.isValid())
        m.invoke(m_view, Qt::DirectConnection);
}

void ViewTraits::stepBack() const
{
    if (const QMetaMethod m = method("stepBack", 0); m.isValid())
        m.invoke(m_view, Qt::DirectConnection);
}

} // namespace brulion::shell

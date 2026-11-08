#include "viewtraits.h"

#include <QJSValue>
#include <QMetaProperty>
#include <QQmlComponent>

namespace brulion::shell {

namespace {

/// The properties followed, with the flag each sets ("" for those read otherwise).
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
            const int index = meta->indexOfProperty(trait.name);
            if (index < 0)
                continue;
            const QMetaProperty property = meta->property(index);
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
    if (!m_view || m_view->metaObject()->indexOfProperty(name) < 0)
        return {};
    return plain(m_view->property(name));
}

void ViewTraits::refresh()
{
    const QVariant title = read("title");
    const QString newTitle = title.metaType() == QMetaType::fromType<QString>() ? title.toString() : QString();
    const auto text = [this](const char *name) {
        const QVariant value = read(name);
        return value.metaType() == QMetaType::fromType<QString>() ? value.toString() : QString();
    };
    const QString newNoteMode = text("noteMode");
    const QString newStatusText = text("statusText");
    const QString newStatusPosition = text("statusPosition");
    QQmlComponent *newSidePanel = qobject_cast<QQmlComponent *>(read("sidePanel").value<QObject *>());
    int newFlags = 0;
    for (const Trait &trait : kTraits) {
        if (trait.flag == 0)
            continue;
        const QVariant value = read(trait.name);
        if (value.metaType() == QMetaType::fromType<bool>() && value.toBool())
            newFlags |= trait.flag;
    }
    if (newTitle == m_title && newNoteMode == m_noteMode && newSidePanel == m_sidePanel && newFlags == m_flags
        && newStatusText == m_statusText && newStatusPosition == m_statusPosition)
        return;
    m_title = newTitle;
    m_noteMode = newNoteMode;
    m_statusText = newStatusText;
    m_statusPosition = newStatusPosition;
    m_sidePanel = newSidePanel;
    m_flags = newFlags;
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
    QTemplatedMetaMethodReturnArgument<void> returned;
    returned.metaType = result.metaType().iface();
    returned.name = m.typeName();
    returned.data = result.data();
    m.invoke(m_view, Qt::DirectConnection, returned);
    if (result.metaType() == QMetaType::fromType<QVariant>()) // (a `var` result: the QVariant in it)
        result = result.value<QVariant>();
    return plain(result).toMap();
}

void ViewTraits::restoreViewState(const QVariantMap &state)
{
    m_pending = state;
    m_hasPending = !state.isEmpty();
    applyPendingState();
}

void ViewTraits::applyPendingState()
{
    if (!m_hasPending || !m_view)
        return;
    const QMetaMethod m = method("restoreViewState", 1);
    if (m.isValid()) {
        // The parameter as the function declares it: QVariant for `var` (or no type), else the
        // map converted to its type.
        const QMetaType type = m.parameterMetaType(0);
        QVariant argument(m_pending);
        if (type == QMetaType::fromType<QVariant>()) {
            m.invoke(m_view, Qt::DirectConnection, QMetaMethodArgument{type.iface(), "QVariant", &argument});
        } else if (argument.convert(type)) {
            m.invoke(m_view, Qt::DirectConnection,
                     QMetaMethodArgument{type.iface(), type.name(), argument.constData()});
        }
    }
    m_pending.clear();
    m_hasPending = false;
}

void ViewTraits::endEditing() const
{
    const QMetaMethod m = method("endEditing", 0);
    if (m.isValid())
        m.invoke(m_view, Qt::DirectConnection);
}

void ViewTraits::stepBack() const
{
    const QMetaMethod m = method("stepBack", 0);
    if (m.isValid())
        m.invoke(m_view, Qt::DirectConnection);
}

} // namespace brulion::shell

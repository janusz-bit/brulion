#include "focusring.h"

#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

namespace brulion::ui {

namespace {

/// The ring's component (BFocusRingShape.qml), one per engine.
QQmlComponent *shapeComponent(QQmlEngine *engine)
{
    static QHash<QQmlEngine *, QQmlComponent *> components;
    if (QQmlComponent *c = components.value(engine))
        return c;
    auto *c = new QQmlComponent(engine, "Brulion", "BFocusRingShape", engine);
    if (c->isError())
        qWarning("BFocusRing: %s", qPrintable(c->errorString()));
    components.insert(engine, c);
    QObject::connect(engine, &QObject::destroyed, [engine] { components.remove(engine); });
    return c;
}

} // namespace

FocusRing::FocusRing(QQuickItem *parent) : QQuickItem(parent)
{
    setZ(10);
}

void FocusRing::setTarget(QQuickItem *target)
{
    if (m_target == target)
        return;
    m_target = target;
    Q_EMIT targetChanged();
}

void FocusRing::resetTarget()
{
    setTarget(nullptr);
}

void FocusRing::setInset(bool inset)
{
    if (inset == m_inset)
        return;
    m_inset = inset;
    Q_EMIT insetChanged();
}

void FocusRing::setTargetRadius(qreal radius)
{
    if (m_hasTargetRadius && radius == m_targetRadius)
        return;
    m_targetRadius = radius;
    m_hasTargetRadius = true;
    Q_EMIT targetRadiusChanged();
}

void FocusRing::componentComplete()
{
    QQuickItem::componentComplete();
    makeRingIfShown();
}

void FocusRing::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemParentHasChanged && !m_target)
        Q_EMIT targetChanged(); // (the parent is the target)
    else if (change == ItemVisibleHasChanged && value.boolValue)
        makeRingIfShown();
}

void FocusRing::makeRingIfShown()
{
    if (m_ring || !isComponentComplete() || !isVisible())
        return;
    QQmlEngine *engine = qmlEngine(this);
    if (!engine)
        return;
    QQmlComponent *component = shapeComponent(engine);
    if (!component->isReady())
        return;
    QQmlContext *context = qmlContext(this);
    QObject *object = component->beginCreate(context ? context : engine->rootContext());
    auto *ring = qobject_cast<QQuickItem *>(object);
    if (!ring) {
        delete object;
        return;
    }
    component->setInitialProperties(ring, {{QStringLiteral("ring"), QVariant::fromValue(this)}});
    ring->setParentItem(this);
    component->completeCreate();
    m_ring = ring;
    Q_EMIT ringChanged();
}

} // namespace brulion::ui

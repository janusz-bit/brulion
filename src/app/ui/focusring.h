// BFocusRing — the visible keyboard focus indicator shared by all components: a 2 px ring in
// Theme.focusRing drawn just outside the target (or inside with `inset: true`).
//
//   BFocusRing { target: control; visible: control.visualFocus; targetRadius: Theme.radius }
//
// Place it as a child of the control's background (or any item filling the control): the ring is
// drawn in its parent's coordinates, around `target`'s size (default: the parent), above its
// siblings (z 10), with the radius `targetRadius` (default Theme.radius) grown by the gap.
//
// A C++ item since 2026-11 (it was BFocusRing.qml, a Rectangle with eight bindings): hundreds of
// controls hold one and almost none shows it. The ring itself — BFocusRingShape.qml, the Rectangle
// BFocusRing.qml was, its bindings reading this item — is made from one component per engine the
// first time the item is visible, and kept. Until then the item is an empty QQuickItem of size 0
// at (0, 0) with no bindings of its own (docs/PERFORMANCE.md).
#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace brulion::ui {

class FocusRing : public QQuickItem {
    Q_OBJECT
    QML_NAMED_ELEMENT(BFocusRing)
    /// The item the ring goes around (its width and height); the parent unless set.
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget RESET resetTarget NOTIFY targetChanged FINAL)
    /// Inside the target's edge instead of outside it.
    Q_PROPERTY(bool inset READ inset WRITE setInset NOTIFY insetChanged FINAL)
    /// The target's corner radius (the ring's grows by the gap); Theme.radius unless set.
    Q_PROPERTY(qreal targetRadius READ targetRadius WRITE setTargetRadius NOTIFY targetRadiusChanged FINAL)
    /// targetRadius was set (the shape reads Theme.radius otherwise).
    Q_PROPERTY(bool hasTargetRadius READ hasTargetRadius NOTIFY targetRadiusChanged FINAL)
    /// The ring has been made (it is made the first time the item is visible).
    Q_PROPERTY(QQuickItem *ring READ ring NOTIFY ringChanged FINAL)

public:
    explicit FocusRing(QQuickItem *parent = nullptr);
    /// Deletes the ring first: its `ring` (this item) would become null while QObject's destructor
    /// tells QML this item is going, and its bindings would read the null.
    ~FocusRing() override;

    QQuickItem *target() const { return m_target ? m_target.data() : parentItem(); }
    void setTarget(QQuickItem *target);
    void resetTarget();
    bool inset() const { return m_inset; }
    void setInset(bool inset);
    qreal targetRadius() const { return m_targetRadius; }
    void setTargetRadius(qreal radius);
    bool hasTargetRadius() const { return m_hasTargetRadius; }
    QQuickItem *ring() const { return m_ring; }

Q_SIGNALS:
    void targetChanged();
    void insetChanged();
    void targetRadiusChanged();
    void ringChanged();

protected:
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void makeRingIfShown();

    QPointer<QQuickItem> m_target;
    bool m_inset = false;
    qreal m_targetRadius = 0;
    bool m_hasTargetRadius = false;
    QQuickItem *m_ring = nullptr;
};

} // namespace brulion::ui

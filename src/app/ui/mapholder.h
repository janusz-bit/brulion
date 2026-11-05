// MapHolder — a QVariantMap kept in a typed FINAL property, so that QML compiled ahead of time
// (docs/UI.md "Compiled QML") can read its keys: a member of a Q_INVOKABLE's QVariantMap result
// (`controller.copyText(t).ok`) or of a `var` does not compile, `holder.map.ok` does (one level:
// a nested map goes into a holder of its own, `MapHolder { map: outer.map.cover }`; a list is
// read through a typed function, `function items(): list<var> { return holder.map.items }`).
// A key the map lacks reads as `undefined`, as on the JavaScript object.
//
//   MapHolder { id: result }
//   result.map = controller.copyText(text)       // imperative
//   if (result.map.ok) notice(qsTr("Copied %1").arg(result.map.text))
//   MapHolder { id: cover; map: page.look.cover } // or bound
//
// QML type `MapHolder` of module Brulion (qmlforeign.h); the class itself is QML-free.
#pragma once

#include <QObject>
#include <QVariantMap>

namespace brulion::ui {

class MapHolder : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap map READ map WRITE setMap NOTIFY mapChanged FINAL)

public:
    explicit MapHolder(QObject *parent = nullptr) : QObject(parent) { }

    QVariantMap map() const { return m_map; }
    /// Announces every write (also of an equal map), like a JavaScript `var` property.
    void setMap(const QVariantMap &map)
    {
        m_map = map;
        Q_EMIT mapChanged();
    }

Q_SIGNALS:
    void mapChanged();

private:
    QVariantMap m_map;
};

} // namespace brulion::ui

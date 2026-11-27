#include "qmlcall.h"

#include <QJSValue>
#include <QMetaMethod>
#include <QMetaObject>

#include <array>

namespace brulion::ui {

QmlCall::QmlCall(QObject *parent) : QObject(parent) { }

bool QmlCall::invoke(QObject *object, const QByteArray &name, const QVariantList &args, QVariant *result)
{
    if (!object || args.size() > 6)
        return false;
    const QMetaObject *meta = object->metaObject();
    QMetaMethod method;
    // (From the most derived: a QML file's own function before a base type's of the same name.)
    for (int i = meta->methodCount() - 1; i >= 0; --i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() != QMetaMethod::Signal && m.parameterCount() == args.size() && m.name() == name) {
            method = m;
            break;
        }
    }
    if (!method.isValid())
        return false;
    // Each argument as its parameter's type; a QVariant (`var`) parameter gets the QVariant itself
    // (QVariant::fromValue() does not wrap a QVariant in another one).
    std::array<QVariant, 6> values;
    std::array<QMetaMethodArgument, 6> arguments{};
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QMetaType type = method.parameterMetaType(int(i));
        const void *data = &args.at(i);
        if (type != QMetaType::fromType<QVariant>()) {
            values[i] = args.at(i);
            if (values[i].metaType() != type && !values[i].convert(type))
                return false;
            data = values[i].constData();
        }
        arguments[i] = {type.iface(), type.name(), data};
    }
    const QMetaType returnType = method.returnMetaType();
    const bool returns = returnType.isValid() && returnType != QMetaType::fromType<void>();
    QVariant returned = returns ? QVariant(returnType) : QVariant();
    QTemplatedMetaMethodReturnArgument<void> r;
    r.metaType = returns ? returnType.iface() : nullptr;
    r.name = returns ? method.typeName() : nullptr;
    r.data = returns ? returned.data() : nullptr;
    bool ok = false;
    switch (args.size()) {
    case 0: ok = method.invoke(object, Qt::DirectConnection, r); break;
    case 1: ok = method.invoke(object, Qt::DirectConnection, r, arguments[0]); break;
    case 2: ok = method.invoke(object, Qt::DirectConnection, r, arguments[0], arguments[1]); break;
    case 3: ok = method.invoke(object, Qt::DirectConnection, r, arguments[0], arguments[1], arguments[2]); break;
    case 4:
        ok = method.invoke(object, Qt::DirectConnection, r, arguments[0], arguments[1], arguments[2], arguments[3]);
        break;
    case 5:
        ok = method.invoke(object, Qt::DirectConnection, r, arguments[0], arguments[1], arguments[2], arguments[3],
                           arguments[4]);
        break;
    default:
        ok = method.invoke(object, Qt::DirectConnection, r, arguments[0], arguments[1], arguments[2], arguments[3],
                           arguments[4], arguments[5]);
        break;
    }
    if (ok && result) {
        if (returned.metaType() == QMetaType::fromType<QVariant>())
            returned = returned.value<QVariant>();
        if (returned.metaType() == QMetaType::fromType<QJSValue>())
            returned = returned.value<QJSValue>().toVariant();
        *result = returned;
    }
    return ok;
}

QVariant QmlCall::call(QObject *object, const QString &name) const
{
    QVariant result;
    invoke(object, name.toUtf8(), {}, &result);
    return result;
}

QVariant QmlCall::call1(QObject *object, const QString &name, const QVariant &a) const
{
    QVariant result;
    invoke(object, name.toUtf8(), {a}, &result);
    return result;
}

QVariant QmlCall::call2(QObject *object, const QString &name, const QVariant &a, const QVariant &b) const
{
    QVariant result;
    invoke(object, name.toUtf8(), {a, b}, &result);
    return result;
}

QVariant QmlCall::call3(QObject *object, const QString &name, const QVariant &a, const QVariant &b,
                        const QVariant &c) const
{
    QVariant result;
    invoke(object, name.toUtf8(), {a, b, c}, &result);
    return result;
}

QVariant QmlCall::call4(QObject *object, const QString &name, const QVariant &a, const QVariant &b, const QVariant &c,
                        const QVariant &d) const
{
    QVariant result;
    invoke(object, name.toUtf8(), {a, b, c, d}, &result);
    return result;
}

QVariant QmlCall::call5(QObject *object, const QString &name, const QVariant &a, const QVariant &b, const QVariant &c,
                        const QVariant &d, const QVariant &e) const
{
    QVariant result;
    invoke(object, name.toUtf8(), {a, b, c, d, e}, &result);
    return result;
}

QVariant QmlCall::call6(QObject *object, const QString &name, const QVariant &a, const QVariant &b, const QVariant &c,
                        const QVariant &d, const QVariant &e, const QVariant &f) const
{
    QVariant result;
    invoke(object, name.toUtf8(), {a, b, c, d, e, f}, &result);
    return result;
}

namespace {
bool trueValue(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<bool>() && value.toBool();
}

double numberValue(const QVariant &value)
{
    bool ok = false;
    const double n = value.toDouble(&ok);
    return ok ? n : 0.0;
}
} // namespace

bool QmlCall::callFlag(QObject *object, const QString &name) const
{
    return trueValue(call(object, name));
}

bool QmlCall::callFlag1(QObject *object, const QString &name, const QVariant &a) const
{
    return trueValue(call1(object, name, a));
}

double QmlCall::callNumber(QObject *object, const QString &name) const
{
    return numberValue(call(object, name));
}

bool QmlCall::isTrue(const QVariant &value) const
{
    return trueValue(value);
}

double QmlCall::number(const QVariant &value) const
{
    return numberValue(value);
}

int QmlCall::integer(const QVariant &value) const
{
    return int(numberValue(value));
}

QString QmlCall::text(const QVariant &value) const
{
    return value.isValid() ? value.toString() : QString();
}

bool QmlCall::connect(QObject *sender, const QString &signal, QObject *receiver, const QString &method) const
{
    if (!sender || !receiver)
        return false;
    const QByteArray signalName = signal.toUtf8();
    const QByteArray methodName = method.toUtf8();
    const QMetaObject *from = sender->metaObject();
    const QMetaObject *to = receiver->metaObject();
    for (int i = from->methodCount() - 1; i >= 0; --i) {
        const QMetaMethod s = from->method(i);
        if (s.methodType() != QMetaMethod::Signal || s.name() != signalName)
            continue;
        for (int j = to->methodCount() - 1; j >= 0; --j) {
            const QMetaMethod m = to->method(j);
            if (m.name() != methodName || !QMetaObject::checkConnectArgs(s, m))
                continue;
            return QObject::connect(sender, s, receiver, m);
        }
        return false;
    }
    return false;
}

bool QmlCall::hasProperty(QObject *object, const QString &name) const
{
    return object && object->metaObject()->indexOfProperty(name.toUtf8().constData()) >= 0;
}

bool QmlCall::hasFunction(QObject *object, const QString &name) const
{
    if (!object)
        return false;
    const QByteArray wanted = name.toUtf8();
    const QMetaObject *meta = object->metaObject();
    for (int i = meta->methodCount() - 1; i >= 0; --i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() != QMetaMethod::Signal && m.name() == wanted)
            return true;
    }
    return false;
}

QVariant QmlCall::get(QObject *object, const QString &name) const
{
    if (!object)
        return {};
    const int index = object->metaObject()->indexOfProperty(name.toUtf8().constData());
    return index >= 0 ? object->metaObject()->property(index).read(object) : QVariant();
}

QObject *QmlCall::object(QObject *object, const QString &name) const
{
    return objectOf(get(object, name));
}

QObject *QmlCall::objectOf(const QVariant &value) const
{
    return value.metaType().flags().testFlag(QMetaType::PointerToQObject) ? value.value<QObject *>() : nullptr;
}

bool QmlCall::set(QObject *object, const QString &name, const QVariant &value) const
{
    if (!object)
        return false;
    const int index = object->metaObject()->indexOfProperty(name.toUtf8().constData());
    if (index < 0)
        return false;
    return object->metaObject()->property(index).write(object, value);
}

QmlValue::QmlValue(QObject *parent) : QObject(parent) { }

void QmlValue::setObject(QObject *object)
{
    if (m_object == object)
        return;
    m_object = object;
    Q_EMIT objectChanged();
    follow();
}

void QmlValue::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    Q_EMIT nameChanged();
    follow();
}

double QmlValue::number() const
{
    return numberValue(m_value);
}

bool QmlValue::flag() const
{
    return trueValue(m_value);
}

QString QmlValue::text() const
{
    return m_value.isValid() ? m_value.toString() : QString();
}

QObject *QmlValue::objectValue() const
{
    return m_value.metaType().flags().testFlag(QMetaType::PointerToQObject) ? m_value.value<QObject *>() : nullptr;
}

void QmlValue::follow()
{
    disconnect(m_notify);
    disconnect(m_destroyed);
    m_property = QMetaProperty();
    if (m_object && !m_name.isEmpty()) {
        const QMetaObject *meta = m_object->metaObject();
        const int index = meta->indexOfProperty(m_name.toUtf8().constData());
        if (index >= 0) {
            m_property = meta->property(index);
            if (m_property.hasNotifySignal()) {
                const int slot = metaObject()->indexOfSlot("refresh()");
                m_notify = QMetaObject::connect(m_object, m_property.notifySignalIndex(), this, slot);
            }
        }
        m_destroyed = connect(m_object, &QObject::destroyed, this, &QmlValue::objectDestroyed);
    }
    refresh();
}

void QmlValue::refresh()
{
    const QVariant value = m_object && m_property.isValid() ? m_property.read(m_object) : QVariant();
    if (value == m_value && value.metaType() == m_value.metaType())
        return;
    m_value = value;
    Q_EMIT valueChanged();
}

void QmlValue::objectDestroyed()
{
    m_object = nullptr;
    m_property = QMetaProperty();
    disconnect(m_notify);
    disconnect(m_destroyed);
    Q_EMIT objectChanged();
    refresh();
}

} // namespace brulion::ui

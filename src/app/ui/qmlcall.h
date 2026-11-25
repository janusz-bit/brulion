// QmlCall and QmlValue — reaching a QML object whose type its user does not name (docs/UI.md
// "QML files loaded when first used"). A QML file named as a type in another one is loaded together
// with it, so a dialog, menu or block view used rarely is made from its file when it is first used
// (`Loader.setSource(url, values)`) and reached through a base type (Item, T.Popup) that lacks its
// own functions and properties; compiled QML cannot call or read those dynamically. QmlCall
// calls a function by name, each argument converted to the type its parameter declares (a typed
// QML function takes nothing else), and reads or writes a property by name; QmlValue follows one
// property by name, for bindings:
//
//   QmlCall.call2(moveLoader.item, "openFor", path, isDir)
//   QmlValue { id: lineCenter; object: content.item; name: "firstLineCenter" }   // lineCenter.number
//
// A missing object, function or property does nothing and reads as nothing (an invalid value, 0,
// false, ""). Library brulion_ui_qmlcall (QtCore + QtQml), the singleton `QmlCall` and the type
// `QmlValue` of module Brulion (registered by qmlforeign.h); tests/app/tst_qmlcall.cpp.
#pragma once

#include <QMetaProperty>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVariantList>

namespace brulion::ui {

class QmlCall : public QObject {
    Q_OBJECT

public:
    explicit QmlCall(QObject *parent = nullptr);

    /// `object`'s function `name` with as many parameters as arguments are given, called with
    /// them (each converted to its parameter's type); its result (invalid for none, or when
    /// there is no such function or an argument does not convert).
    Q_INVOKABLE QVariant call(QObject *object, const QString &name) const;
    Q_INVOKABLE QVariant call1(QObject *object, const QString &name, const QVariant &a) const;
    Q_INVOKABLE QVariant call2(QObject *object, const QString &name, const QVariant &a, const QVariant &b) const;
    Q_INVOKABLE QVariant call3(QObject *object, const QString &name, const QVariant &a, const QVariant &b,
                               const QVariant &c) const;
    Q_INVOKABLE QVariant call4(QObject *object, const QString &name, const QVariant &a, const QVariant &b,
                               const QVariant &c, const QVariant &d) const;
    Q_INVOKABLE QVariant call5(QObject *object, const QString &name, const QVariant &a, const QVariant &b,
                               const QVariant &c, const QVariant &d, const QVariant &e) const;
    Q_INVOKABLE QVariant call6(QObject *object, const QString &name, const QVariant &a, const QVariant &b,
                               const QVariant &c, const QVariant &d, const QVariant &e, const QVariant &f) const;
    /// The same as call(), the result as a boolean (true only for the boolean true) or a number
    /// (0 for anything else).
    Q_INVOKABLE bool callFlag(QObject *object, const QString &name) const;
    Q_INVOKABLE bool callFlag1(QObject *object, const QString &name, const QVariant &a) const;
    Q_INVOKABLE double callNumber(QObject *object, const QString &name) const;
    /// A result of the calls above typed: true only for the boolean true; a number, an integer
    /// (truncated; 0 for anything else); a text ("" for nothing).
    Q_INVOKABLE bool isTrue(const QVariant &value) const;
    Q_INVOKABLE double number(const QVariant &value) const;
    Q_INVOKABLE int integer(const QVariant &value) const;
    Q_INVOKABLE QString text(const QVariant &value) const;
    /// Connects `sender`'s signal `signal` to `receiver`'s function or signal `method` (by name;
    /// it takes the signal's first arguments, of the same types): what a handler in the file that
    /// named the sender's type did, without an object of its own. False when either is missing or
    /// they do not fit.
    Q_INVOKABLE bool connect(QObject *sender, const QString &signal, QObject *receiver, const QString &method) const;
    /// `object` has a property, or a function (not a signal), of that name.
    Q_INVOKABLE bool hasProperty(QObject *object, const QString &name) const;
    Q_INVOKABLE bool hasFunction(QObject *object, const QString &name) const;
    /// `object`'s property `name` (invalid when there is none).
    Q_INVOKABLE QVariant get(QObject *object, const QString &name) const;
    /// `object`'s property `name` as an object (null for anything else).
    Q_INVOKABLE QObject *object(QObject *object, const QString &name) const;
    /// Writes `object`'s property `name` (converted to its type); false when it has none or the
    /// value does not convert.
    Q_INVOKABLE bool set(QObject *object, const QString &name, const QVariant &value) const;

    /// The function behind the calls above, for C++ (at most six arguments).
    static bool invoke(QObject *object, const QByteArray &name, const QVariantList &args, QVariant *result);
};

class QmlValue : public QObject {
    Q_OBJECT
    /// The object whose property is followed (null: none).
    Q_PROPERTY(QObject *object READ object WRITE setObject NOTIFY objectChanged FINAL)
    /// The property's name.
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged FINAL)
    /// The property's value (invalid without an object or such a property).
    Q_PROPERTY(QVariant value READ value NOTIFY valueChanged FINAL)
    /// The value as a number (0 for none or anything else), true only for the boolean true, as a
    /// text ("" for none), as an object (null for anything else).
    Q_PROPERTY(double number READ number NOTIFY valueChanged FINAL)
    Q_PROPERTY(bool flag READ flag NOTIFY valueChanged FINAL)
    Q_PROPERTY(QString text READ text NOTIFY valueChanged FINAL)
    Q_PROPERTY(QObject *objectValue READ objectValue NOTIFY valueChanged FINAL)

public:
    explicit QmlValue(QObject *parent = nullptr);

    QObject *object() const { return m_object; }
    void setObject(QObject *object);
    QString name() const { return m_name; }
    void setName(const QString &name);
    QVariant value() const { return m_value; }
    double number() const;
    bool flag() const;
    QString text() const;
    QObject *objectValue() const;

Q_SIGNALS:
    void objectChanged();
    void nameChanged();
    void valueChanged();

private Q_SLOTS:
    void refresh();
    void objectDestroyed();

private:
    void follow();

    QPointer<QObject> m_object;
    QString m_name;
    QMetaProperty m_property;
    QMetaObject::Connection m_notify;
    QMetaObject::Connection m_destroyed;
    QVariant m_value;
};

} // namespace brulion::ui

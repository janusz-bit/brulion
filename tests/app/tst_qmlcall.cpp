// Tests for QmlCall and QmlValue (src/app/ui/qmlcall.h): a QML object's typed functions called by
// name with arguments converted to their parameters' types (strings, numbers, booleans, lists,
// objects, `var`), results returned, missing functions and arguments that do not convert refused;
// properties read, written and followed by name, through their objects' deletion.
#include "qmlcall.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::QmlCall;
using brulion::ui::QmlValue;

namespace {
const QByteArray kQml = R"(
import QtQml
QtObject {
    id: root
    component Box: QtObject { property int size: 3 }
    property Box box: null
    property Box made: Box { size: 5 }
    property string seen
    property int received: -1
    property real lineCenter: 4.5
    property bool ready
    property QtObject other: null
    property list<string> names
    signal said(text: string)
    function openFor(path: string, isDir: bool): void { seen = path + (isDir ? " dir" : " file") }
    function twice(n: int): int { return 2 * n }
    function backStep(): bool { return seen !== "" }
    function names2(a: list<string>, b: var): string { return a.join("+") + "|" + b }
    function holder(o: QtObject): bool { return o !== null }
    function four(a: string, b: string, c: string, d: string): string { return a + b + c + d }
    function six(a: int, b: int, c: int, d: int, e: int, f: int): int { return a + b + c + d + e + f }
    function five(a: string, b: string, c: string, d: string, e: bool): string { return a + b + c + d + (e ? "!" : "?") }
    function receive(index: int): void { received = index }
    function receiveText(text: string): void { seen = text }
    function boxSize(b: Box): int { return b === null ? -1 : b.size }
}
)";
} // namespace

class TstQmlCall : public QObject {
    Q_OBJECT

    std::unique_ptr<QObject> make(QQmlEngine &engine)
    {
        QQmlComponent component(&engine);
        component.setData(kQml, QUrl(u"qrc:/tst_qmlcall.qml"_s));
        std::unique_ptr<QObject> object(component.create());
        if (!object)
            qWarning() << component.errors();
        return object;
    }

private Q_SLOTS:
    void callsByName()
    {
        QQmlEngine engine;
        auto object = make(engine);
        QVERIFY(object);
        QmlCall call;
        QVERIFY(!call.callFlag(object.get(), u"backStep"_s));
        // (A function returning nothing gives an invalid value.)
        QVERIFY(!call.call2(object.get(), u"openFor"_s, u"Notatki/żółw.md"_s, true).isValid());
        QCOMPARE(object->property("seen").toString(), u"Notatki/żółw.md dir"_s);
        QVERIFY(call.callFlag(object.get(), u"backStep"_s));
        // Numbers from QML arrive as doubles: converted to the parameter's int.
        QCOMPARE(call.call1(object.get(), u"twice"_s, 21.0).toInt(), 42);
        QCOMPARE(call.call2(object.get(), u"names2"_s, QStringList{u"ą"_s, u"ę"_s}, 7).toString(), u"ą+ę|7"_s);
        QCOMPARE(call.call4(object.get(), u"four"_s, u"a"_s, u"b"_s, u"c"_s, u"d"_s).toString(), u"abcd"_s);
        QVERIFY(call.callFlag1(object.get(), u"holder"_s, QVariant::fromValue<QObject *>(object.get())));
        QVERIFY(QmlCall::invoke(object.get(), "openFor", {u"x"_s, false}, nullptr));
        QCOMPARE(object->property("seen").toString(), u"x file"_s);
        QCOMPARE(call.call6(object.get(), u"six"_s, 1, 2, 3, 4, 5, 6).toInt(), 21);
        QCOMPARE(call.call5(object.get(), u"five"_s, u"ą"_s, u"b"_s, u"c"_s, u"d"_s, true).toString(), u"ąbcd!"_s);
        QCOMPARE(call.callNumber(object.get(), u"backStep"_s), 1.0); // (true as a number, as in JavaScript)
        QCOMPARE(call.integer(call.call1(object.get(), u"twice"_s, 2.6)), 4); // (the parameter's int: 2)
        QCOMPARE(call.integer(u"x"_s), 0);
        QVERIFY(call.isTrue(call.call(object.get(), u"backStep"_s)));
        QVERIFY(!call.isTrue(1));
        QCOMPARE(call.number(call.call1(object.get(), u"twice"_s, 4)), 8.0);
        QCOMPARE(call.text(call.call4(object.get(), u"four"_s, u"ż"_s, 1, 2, 3)), u"ż123"_s);
        QCOMPARE(call.text(QVariant()), QString());
        // An object of a QML type (an inline component) to a parameter of that type.
        QObject *made = call.object(object.get(), u"made"_s);
        QVERIFY(made);
        QCOMPARE(call.call1(object.get(), u"boxSize"_s, QVariant::fromValue(made)).toInt(), 5);
        // …and to a property of that type; another object refused.
        QVERIFY(call.set(object.get(), u"box"_s, QVariant::fromValue(made)));
        QCOMPARE(call.object(object.get(), u"box"_s), made);
        QVERIFY(!call.set(object.get(), u"box"_s, QVariant::fromValue<QObject *>(object.get())));
        QCOMPARE(call.object(object.get(), u"box"_s), made);
    }

    void connectsSignalsToFunctions()
    {
        QQmlEngine engine;
        auto sender = make(engine);
        auto receiver = make(engine);
        QVERIFY(sender && receiver);
        QmlCall call;
        // A QML signal to a QML function taking its arguments.
        QVERIFY(call.connect(sender.get(), u"said"_s, receiver.get(), u"receiveText"_s));
        QMetaObject::invokeMethod(sender.get(), "said", Q_ARG(QString, u"źdźbło"_s));
        QCOMPARE(receiver->property("seen").toString(), u"źdźbło"_s);
        // A signal to a signal (passed on).
        QVERIFY(call.connect(sender.get(), u"said"_s, receiver.get(), u"said"_s));
        QSignalSpy passed(receiver.get(), SIGNAL(said(QString)));
        QMetaObject::invokeMethod(sender.get(), "said", Q_ARG(QString, u"ćma"_s));
        QCOMPARE(passed.count(), 1);
        QCOMPARE(receiver->property("seen").toString(), u"ćma"_s);
        // A property's change signal to a function taking none of its arguments.
        QVERIFY(call.connect(sender.get(), u"receivedChanged"_s, receiver.get(), u"backStep"_s));
        // Arguments that do not fit, a missing signal or function, no object.
        QVERIFY(!call.connect(sender.get(), u"said"_s, receiver.get(), u"receive"_s));
        QVERIFY(!call.connect(sender.get(), u"missing"_s, receiver.get(), u"receive"_s));
        QVERIFY(!call.connect(sender.get(), u"said"_s, receiver.get(), u"missing"_s));
        QVERIFY(!call.connect(nullptr, u"said"_s, receiver.get(), u"receive"_s));
    }

    void refusesWhatIsNotThere()
    {
        QQmlEngine engine;
        auto object = make(engine);
        QVERIFY(object);
        QmlCall call;
        QVERIFY(!call.call(nullptr, u"backStep"_s).isValid());
        QVERIFY(!call.call(object.get(), u"missing"_s).isValid());
        // Another count of arguments is another function.
        QVERIFY(!QmlCall::invoke(object.get(), "openFor", {u"x"_s}, nullptr));
        // A signal is not called.
        QVERIFY(!QmlCall::invoke(object.get(), "said", {u"x"_s}, nullptr));
        QVERIFY(call.hasFunction(object.get(), u"openFor"_s));
        QVERIFY(!call.hasFunction(object.get(), u"said"_s));
        QVERIFY(!call.hasFunction(nullptr, u"openFor"_s));
    }

    void propertiesByName()
    {
        QQmlEngine engine;
        auto object = make(engine);
        QVERIFY(object);
        QmlCall call;
        QVERIFY(call.hasProperty(object.get(), u"lineCenter"_s));
        QVERIFY(!call.hasProperty(object.get(), u"missing"_s));
        QCOMPARE(call.get(object.get(), u"lineCenter"_s).toDouble(), 4.5);
        QVERIFY(!call.get(object.get(), u"missing"_s).isValid());
        QVERIFY(call.set(object.get(), u"lineCenter"_s, 7));
        QCOMPARE(object->property("lineCenter").toDouble(), 7.0);
        QVERIFY(!call.set(object.get(), u"missing"_s, 1));
        QVERIFY(!call.set(nullptr, u"lineCenter"_s, 1));
        QVERIFY(call.set(object.get(), u"other"_s, QVariant::fromValue<QObject *>(object.get())));
        QCOMPARE(call.object(object.get(), u"other"_s), object.get());
        QCOMPARE(call.object(object.get(), u"lineCenter"_s), nullptr);
        QVERIFY(call.set(object.get(), u"names"_s, QStringList{u"ż"_s}));
        QCOMPARE(object->property("names").toStringList(), QStringList{u"ż"_s});
    }

    void valueFollowsItsProperty()
    {
        QQmlEngine engine;
        auto object = make(engine);
        QVERIFY(object);
        QmlValue value;
        QSignalSpy changed(&value, &QmlValue::valueChanged);
        value.setName(u"lineCenter"_s);
        QCOMPARE(value.number(), 0.0);
        QVERIFY(!value.value().isValid());
        value.setObject(object.get());
        QCOMPARE(value.number(), 4.5);
        QCOMPARE(changed.count(), 1);
        object->setProperty("lineCenter", 12.25);
        QCOMPARE(value.number(), 12.25);
        QCOMPARE(changed.count(), 2);
        object->setProperty("lineCenter", 12.25); // no change, no signal
        QCOMPARE(changed.count(), 2);
        // Another property of the same object.
        value.setName(u"ready"_s);
        QVERIFY(!value.flag());
        object->setProperty("ready", true);
        QVERIFY(value.flag());
        value.setName(u"seen"_s);
        object->setProperty("seen", u"gęś"_s);
        QCOMPARE(value.text(), u"gęś"_s);
        value.setName(u"other"_s);
        object->setProperty("other", QVariant::fromValue<QObject *>(&value));
        QCOMPARE(value.objectValue(), &value);
        // A missing property reads as nothing.
        value.setName(u"missing"_s);
        QVERIFY(!value.value().isValid());
        QCOMPARE(value.text(), QString());
        // The object gone: nothing, and no access to it.
        value.setName(u"lineCenter"_s);
        QCOMPARE(value.number(), 12.25);
        QSignalSpy objectChanged(&value, &QmlValue::objectChanged);
        object.reset();
        QCOMPARE(objectChanged.count(), 1);
        QCOMPARE(value.object(), nullptr);
        QCOMPARE(value.number(), 0.0);
    }
};

QTEST_GUILESS_MAIN(TstQmlCall)
#include "tst_qmlcall.moc"

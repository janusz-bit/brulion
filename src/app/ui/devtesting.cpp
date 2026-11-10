#include "devtesting.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QJSEngine>
#include <QJSValue>
#include <QLocale>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlListReference>
#include <QQuickItem>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

/// `value` with a JavaScript value (a QJSValue passed as var) converted to a plain QVariant.
QVariant unwrapped(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

/// DevTest::valueAt(). A missing key, index or property gives an invalid value, which the next
/// step returns (as does the end of the path).
QVariant valueAtPath(const QVariant &value, const QString &path)
{
    QVariant at = value;
    const QStringList steps = path.isEmpty() ? QStringList() : path.split(u'.');
    for (const QString &step : steps) {
        at = unwrapped(at);
        if (at.canConvert<QObject *>() && at.value<QObject *>()) {
            at = at.value<QObject *>()->property(step.toUtf8().constData());
        } else if (at.metaType() == QMetaType::fromType<QVariantMap>()) {
            at = at.toMap().value(step);
        } else if (at.canConvert<QVariantList>() && at.metaType() != QMetaType::fromType<QString>()) {
            bool isIndex = false;
            const int index = step.toInt(&isIndex);
            if (!isIndex)
                return {};
            at = at.toList().value(index);
        } else {
            return {};
        }
    }
    return at;
}

int lengthAtPath(const QVariant &value, const QString &path)
{
    const QVariant at = valueAtPath(value, path);
    if (at.metaType() == QMetaType::fromType<QString>())
        return int(at.toString().size());
    return at.canConvert<QVariantList>() ? int(at.toList().size()) : 0;
}

bool ownsProperty(const QObject *item, const char *name)
{
    return item->metaObject()->indexOfProperty(name) >= 0;
}

/// `name` is a property, a function or a signal of `object` (object[name] !== undefined).
bool ownsMember(const QObject *object, const QByteArray &name)
{
    const QMetaObject *meta = object->metaObject();
    if (meta->indexOfProperty(name.constData()) >= 0)
        return true;
    for (int i = 0; i < meta->methodCount(); ++i) {
        if (meta->method(i).nameView() == name)
            return true;
    }
    return false;
}

bool ownsMembers(const QObject *object, const QList<QByteArray> &names)
{
    return std::ranges::all_of(names, [&](const QByteArray &name) { return ownsMember(object, name); });
}

QList<QByteArray> utf8(const QStringList &names)
{
    QList<QByteArray> out;
    out.reserve(names.size());
    for (const QString &name : names)
        out.append(name.toUtf8());
    return out;
}

/// A number as JavaScript writes it ("44", "43.5").
QString jsNumber(double value)
{
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

/// The first item under `item` (depth first, itself included) for which `match` holds; an item
/// for which `descend` is false is skipped with its children.
template<typename Match, typename Descend>
QQuickItem *firstWhere(QQuickItem *item, const Match &match, const Descend &descend)
{
    if (!item || !descend(item))
        return nullptr;
    if (match(item))
        return item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = firstWhere(child, match, descend))
            return found;
    }
    return nullptr;
}

/// Every item under `item` (depth first; itself only if `self`) for which `match` holds.
template<typename Match>
void collectWhere(QQuickItem *item, const Match &match, bool self, QList<QQuickItem *> &out)
{
    if (!item)
        return;
    if (self && match(item))
        out.append(item);
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collectWhere(child, match, true, out);
}

template<typename Match>
QList<QQuickItem *> allWhere(QQuickItem *root, bool withRoot, const Match &match)
{
    QList<QQuickItem *> found;
    collectWhere(root, match, withRoot, found);
    return found;
}

constexpr auto everyItem = [](QQuickItem *) { return true; };
constexpr auto shownItem = [](QQuickItem *item) { return item->isVisible(); };

auto namedAs(const QString &name)
{
    return [name](QQuickItem *item) { return item->objectName() == name; };
}

auto withMember(const QString &name)
{
    return [key = name.toUtf8()](QQuickItem *item) { return ownsMember(item, key); };
}

/// A circle on a BTimePicker's face (a number, or the chosen value's bubble), in the face's
/// coordinates.
struct Circle {
    QString label;
    bool outer = false;
    double x = 0, y = 0, r = 0, textWidth = 0, font = 0;
};

/// Hours: "<number>→<labelAt()>" for each point just inside a number, towards a neighbour, that
/// labelAt() of the BTimePicker (`picker`) gives another number for.
QStringList wrongHourPresses(const QJSValue &picker, const QList<Circle> &numbers)
{
    const auto numberOf = [&](int value) -> const Circle * {
        const QString label = picker.property(u"pad"_s).callWithInstance(picker, {value}).toString();
        const auto it = std::ranges::find(numbers, label, &Circle::label);
        return it == numbers.cend() ? nullptr : &*it;
    };
    QStringList wrong;
    for (int h = 0; h < 24; ++h) {
        const Circle *n = numberOf(h);
        if (!n)
            continue;
        for (const int side : {-1, 1}) {
            const Circle *next = numberOf((h % 12 + 12 + side) % 12 + (h >= 12 ? 12 : 0));
            if (!next)
                continue;
            const double d = std::hypot(next->x - n->x, next->y - n->y);
            const double x = n->x + (next->x - n->x) / d * (n->r - 1);
            const double y = n->y + (next->y - n->y) / d * (n->r - 1);
            const QJSValue at = picker.property(u"labelAt"_s).callWithInstance(picker, {x, y});
            if (!at.isNumber() || at.toNumber() != h)
                wrong.append(n->label + u"→"_s + at.toString());
        }
    }
    return wrong;
}

// The deferred deletions that come due while a self-test's pass runs (DevTest::callCatching): an
// application event filter takes their events, and the outermost call deletes the objects once
// the pass returned.
class DeferredDeleteHold : public QObject
{
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::DeferredDelete)
            return false;
        held.append(watched);
        return true;
    }
    QList<QPointer<QObject>> held;
};
std::unique_ptr<DeferredDeleteHold> deferredDeleteHold;
int passDepth = 0;
} // namespace

// --- DevEvents --------------------------------------------------------------------------------

DevEvents::DevEvents(QObject *parent) : QObject(parent) { }

QJSValue DevEvents::testEvent()
{
    QQmlEngine *engine = qmlEngine(this);
    if (!m_tried && engine) {
        m_tried = true;
        // As the self-tests made it before: Qt.createQmlObject('import QtTest; TestEvent {}', …).
        QQmlComponent component(engine);
        component.setData("import QtQuick\nimport QtTest\nTestEvent {}\n", QUrl(u"devEvents"_s));
        m_testEvent = component.create(qmlContext(this));
        if (m_testEvent)
            m_testEvent->setParent(this);
        else
            qWarning("DevEvents: no QtTest TestEvent: %s", qPrintable(component.errorString()));
    }
    return m_testEvent && engine ? engine->toScriptValue(m_testEvent) : QJSValue();
}

bool DevEvents::available()
{
    return !testEvent().isUndefined();
}

bool DevEvents::call(const char *function, const QList<QJSValue> &arguments)
{
    const QJSValue events = testEvent();
    if (events.isUndefined())
        return false;
    const QJSValue result = events.property(QString::fromLatin1(function)).callWithInstance(events, arguments);
    if (result.isError())
        qWarning("DevEvents: %s: %s", function, qPrintable(result.toString()));
    return result.toBool();
}

bool DevEvents::keyClick(int key, int modifiers, int delay)
{
    return call("keyClick", {key, modifiers, delay});
}

bool DevEvents::mousePress(QObject *item, qreal x, qreal y, int button, int modifiers, int delay)
{
    QJSEngine *engine = qjsEngine(this);
    return engine && call("mousePress", {engine->toScriptValue(item), x, y, button, modifiers, delay});
}

bool DevEvents::mouseRelease(QObject *item, qreal x, qreal y, int button, int modifiers, int delay)
{
    QJSEngine *engine = qjsEngine(this);
    return engine && call("mouseRelease", {engine->toScriptValue(item), x, y, button, modifiers, delay});
}

bool DevEvents::mouseClick(QObject *item, qreal x, qreal y, int button, int modifiers, int delay)
{
    QJSEngine *engine = qjsEngine(this);
    return engine && call("mouseClick", {engine->toScriptValue(item), x, y, button, modifiers, delay});
}

bool DevEvents::mouseMove(QObject *item, qreal x, qreal y, int delay, int buttons, int modifiers)
{
    QJSEngine *engine = qjsEngine(this);
    return engine && call("mouseMove", {engine->toScriptValue(item), x, y, delay, buttons, modifiers});
}

bool DevEvents::mouseWheel(QObject *item, qreal x, qreal y, int buttons, int modifiers, int xDelta, int yDelta,
                           int delay)
{
    QJSEngine *engine = qjsEngine(this);
    return engine && call("mouseWheel", {engine->toScriptValue(item), x, y, buttons, modifiers, xDelta, yDelta, delay});
}

void DevEvents::touch(const char *point, QObject *item, qreal x, qreal y)
{
    const QJSValue events = testEvent();
    QJSEngine *engine = qjsEngine(this);
    if (events.isUndefined() || !engine)
        return;
    const QJSValue target = engine->toScriptValue(item);
    const QJSValue sequence = events.property(u"touchEvent"_s).callWithInstance(events, {target});
    if (sequence.isError() || !sequence.isQObject()) {
        qWarning("DevEvents: touchEvent: %s", qPrintable(sequence.toString()));
        return;
    }
    const QJSValue moved = sequence.property(QString::fromLatin1(point)).callWithInstance(sequence, {0, target, x, y});
    const QJSValue committed = sequence.property(u"commit"_s).callWithInstance(sequence);
    if (moved.isError() || committed.isError())
        qWarning("DevEvents: touch %s: %s", point, qPrintable((moved.isError() ? moved : committed).toString()));
    // TestEvent keeps each sequence as its child: one per touch point here, done with now.
    delete sequence.toQObject();
}

void DevEvents::touchPress(QObject *item, qreal x, qreal y)
{
    touch("press", item, x, y);
}

void DevEvents::touchMove(QObject *item, qreal x, qreal y)
{
    touch("move", item, x, y);
}

void DevEvents::touchRelease(QObject *item, qreal x, qreal y)
{
    touch("release", item, x, y);
}

// --- DevBlock, DevRecord -----------------------------------------------------------------------

DevBlock::DevBlock(QObject *parent) : QObject(parent) { }

void DevBlock::read(const QVariantMap &row)
{
    m_row = row;
    Q_EMIT changed();
}

DevRecord::DevRecord(QObject *parent) : QObject(parent) { }

void DevRecord::setValue(const QVariantMap &value)
{
    m_value = value;
    Q_EMIT valueChanged();
}

QVariant DevRecord::at(const QString &path) const
{
    return valueAtPath(m_value, path);
}

int DevRecord::lengthAt(const QString &path) const
{
    return lengthAtPath(m_value, path);
}

QObject *DevRecord::objectAt(const QString &path) const
{
    return valueAtPath(m_value, path).value<QObject *>();
}

// --- DevTest -----------------------------------------------------------------------------------

DevTest::DevTest(QObject *parent) : QObject(parent) { }

QQuickItem *DevTest::findItem(QQuickItem *root, const QString &name) const
{
    return firstWhere(root, namedAs(name), everyItem);
}

int DevTest::countNamed(QQuickItem *root, const QString &name) const
{
    return int(allWhere(root, true, namedAs(name)).size());
}

QQuickItem *DevTest::namedAt(QQuickItem *root, const QString &name, int index) const
{
    return allWhere(root, true, namedAs(name)).value(index);
}

QQuickItem *DevTest::findShown(QQuickItem *root, const QString &name) const
{
    return firstWhere(root, namedAs(name), shownItem);
}

QQuickItem *DevTest::findRendered(QQuickItem *root) const
{
    return firstWhere(
            root,
            [](QQuickItem *item) {
                // Text.RichText and TextEdit.RichText are both 1.
                return ownsProperty(item, "textFormat") && item->property("textFormat").toInt() == 1
                       && ownsMember(item, "linkAt");
            },
            shownItem);
}

QString DevTest::linkAt(QQuickItem *text, qreal x, qreal y) const
{
    QString link;
    if (text && ownsMember(text, "linkAt"))
        QMetaObject::invokeMethod(text, "linkAt", Qt::DirectConnection, qReturnArg(link), x, y);
    return link;
}

QQuickItem *DevTest::findText(QQuickItem *root, const QString &text) const
{
    return firstWhere(
            root,
            [&](QQuickItem *item) {
                return ownsProperty(item, "textFormat") && item->property("text").toString() == text;
            },
            everyItem);
}

QQuickItem *DevTest::findTextContaining(QQuickItem *root, const QString &needle) const
{
    return firstWhere(
            root,
            [&](QQuickItem *item) {
                return ownsProperty(item, "textFormat") && item->property("text").toString().contains(needle);
            },
            shownItem);
}

QQuickItem *DevTest::findWhere(QQuickItem *root, const QString &name, const QVariant &value, bool visibleOnly) const
{
    const QByteArray key = name.toUtf8();
    return firstWhere(
            root,
            [&](QQuickItem *item) {
                return ownsProperty(item, key.constData()) && item->property(key.constData()) == value
                       && (!visibleOnly || item->isVisible());
            },
            everyItem);
}

QQuickItem *DevTest::findWithMembers(QQuickItem *root, const QStringList &names) const
{
    const QList<QByteArray> keys = utf8(names);
    return firstWhere(root, [&](QQuickItem *item) { return ownsMembers(item, keys); }, everyItem);
}

int DevTest::countWithMembers(QObject *object, const QStringList &names) const
{
    if (!object)
        return 0;
    const QList<QByteArray> keys = utf8(names);
    QList<QObject *> found;
    for (const char *list : {"resources", "data"}) {
        const QQmlListReference items(object, list);
        for (qsizetype i = 0; items.isValid() && i < items.size(); ++i) {
            QObject *o = items.at(i);
            if (o && ownsMembers(o, keys) && !found.contains(o))
                found.append(o);
        }
    }
    return int(found.size());
}

QString DevTest::clockNumbers(QQuickItem *clock, int textSm) const
{
    QJSEngine *engine = clock ? qjsEngine(clock) : nullptr;
    auto *face = clock ? clock->property("focusItem").value<QQuickItem *>() : nullptr;
    if (!engine || !face)
        return u"no clock"_s;
    const QJSValue picker = engine->toScriptValue(clock);
    const double center = clock->property("center").toDouble();
    const double outerRadius = clock->property("outerRadius").toDouble();

    // The circles, as BTimePicker draws them.
    QList<Circle> numbers;
    std::optional<Circle> bubble;
    const QList<QQuickItem *> children = face->childItems();
    for (QQuickItem *item : children) {
        if (!item->isVisible() || item->width() <= 0)
            continue;
        const bool hour = ownsMember(item, "selected") && ownsMember(item, "angle");
        const bool minute = !hour && ownsMember(item, "value") && ownsMember(item, "angle");
        // The bubble: a round Rectangle holding only its text (the face's disc holds nothing).
        const QList<QQuickItem *> inside = item->childItems();
        const QVariant radius = item->property("radius");
        const bool isBubble = !hour && !minute && inside.size() == 1 && ownsMember(inside.first(), "text")
                              && radius.isValid() && std::abs(radius.toDouble() - item->width() / 2) < 0.01;
        if (!hour && !minute && !isBubble)
            continue;
        QQuickItem *text = nullptr;
        for (QQuickItem *child : inside) {
            if (ownsMember(child, "text"))
                text = child;
        }
        Circle c;
        c.x = item->x() + item->width() / 2;
        c.y = item->y() + item->height() / 2;
        const QString shown = text ? text->property("text").toString() : QString();
        c.label = isBubble ? u"bubble "_s + shown : text ? shown : u"?"_s;
        c.outer = std::abs(std::hypot(c.x - center, c.y - center) - outerRadius) < 0.5;
        c.r = item->width() / 2;
        c.textWidth = text ? text->property("contentWidth").toDouble() : 0;
        c.font = text ? text->property("font").value<QFont>().pixelSize() : 0;
        if (!isBubble)
            numbers.append(c);
        else if (!bubble)
            bubble = c;
    }

    QStringList overlaps;
    QStringList cramped;
    const auto fits = [](const Circle &n, double minFont) { return n.textWidth <= 2 * n.r - 2 && n.font >= minFont; };
    const auto overlap = [&](const Circle &p, const Circle &q, double d) {
        overlaps.append(u"%1/%2 %3 < %4"_s.arg(p.label, q.label, QString::number(d, 'f', 1), jsNumber(p.r + q.r)));
    };
    for (qsizetype i = 0; i < numbers.size(); ++i) {
        const Circle &a = numbers.at(i);
        if (!fits(a, a.outer ? textSm : std::min(textSm, 12)))
            cramped.append(u"%1 (%2 px text in %3, font %4)"_s.arg(a.label, QString::number(a.textWidth, 'f', 1),
                                                                   jsNumber(2 * a.r), jsNumber(a.font)));
        for (qsizetype j = i + 1; j < numbers.size(); ++j) {
            const Circle &b = numbers.at(j);
            const double d = std::hypot(a.x - b.x, a.y - b.y);
            if (d < a.r + b.r - 0.01)
                overlap(a, b, d);
        }
        const double toBubble = bubble ? std::hypot(a.x - bubble->x, a.y - bubble->y) : 0;
        if (bubble && toBubble > 0.5 && toBubble < a.r + bubble->r - 0.01)
            overlap(*bubble, a, toBubble);
    }
    if (bubble && !fits(*bubble, 10))
        cramped.append(u"%1 (%2 px text in %3)"_s.arg(bubble->label, QString::number(bubble->textWidth, 'f', 1),
                                                      jsNumber(2 * bubble->r)));
    QStringList wrong;
    if (clock->property("mode").toString() == u"hour")
        wrong = wrongHourPresses(picker, numbers);
    QString report = QString::number(numbers.size()) + u" numbers"_s;
    if (!overlaps.isEmpty())
        report += u", overlapping "_s + overlaps.join(u',');
    if (!cramped.isEmpty())
        report += u", cramped "_s + cramped.join(u',');
    if (!wrong.isEmpty())
        report += u", a press near a number's edge gives another one "_s + wrong.join(u',');
    return report;
}

int DevTest::countWithMember(QQuickItem *root, const QString &name, bool withRoot) const
{
    return int(allWhere(root, withRoot, withMember(name)).size());
}

QQuickItem *DevTest::withMemberAt(QQuickItem *root, const QString &name, bool withRoot, int index) const
{
    return allWhere(root, withRoot, withMember(name)).value(index);
}

int DevTest::childCount(QQuickItem *item) const
{
    return item ? int(item->childItems().size()) : 0;
}

QQuickItem *DevTest::childAt(QQuickItem *item, int index) const
{
    return item ? item->childItems().value(index) : nullptr;
}

QVariant DevTest::property(QObject *item, const QString &name) const
{
    return item ? item->property(name.toUtf8().constData()) : QVariant();
}

QObject *DevTest::objectProperty(QObject *item, const QString &name) const
{
    return property(item, name).value<QObject *>();
}

void DevTest::setProperty(QObject *item, const QString &name, const QVariant &value) const
{
    if (item)
        item->setProperty(name.toUtf8().constData(), value);
}

bool DevTest::hasProperty(QObject *item, const QString &name) const
{
    return item && ownsProperty(item, name.toUtf8().constData());
}

QVariant DevTest::valueAt(const QVariant &value, const QString &path) const
{
    return valueAtPath(value, path);
}

int DevTest::lengthAt(const QVariant &value, const QString &path) const
{
    return lengthAtPath(value, path);
}

bool DevTest::hasMember(QObject *item, const QString &name) const
{
    return item && ownsMember(item, name.toUtf8());
}

QString DevTest::lineWith(const QString &text, const QString &needle) const
{
    const QStringList lines = text.split(u'\n');
    for (const QString &line : lines) {
        if (line.contains(needle))
            return line;
    }
    return {};
}

QString DevTest::underlinedHref(const QString &html) const
{
    const qsizetype end = html.indexOf(u";text-decoration:underline"_s);
    if (end < 0)
        return {};
    const QString open = u"<a href=\""_s;
    const qsizetype start = html.lastIndexOf(open, end);
    if (start < 0)
        return {};
    const qsizetype from = start + open.size();
    const qsizetype quote = html.indexOf(u'"', from);
    return html.mid(from, quote < 0 ? -1 : quote - from);
}

QString DevTest::accessibleName(QQuickItem *item) const
{
    QAccessibleInterface *accessible = item ? QAccessible::queryAccessibleInterface(item) : nullptr;
    return accessible ? accessible->text(QAccessible::Name) : QString();
}

int DevTest::accessibleRole(QQuickItem *item) const
{
    QAccessibleInterface *accessible = item ? QAccessible::queryAccessibleInterface(item) : nullptr;
    return accessible ? int(accessible->role()) : 0;
}

QString DevTest::linesWithout(const QString &text, const QStringList &prefixes) const
{
    QStringList lines = text.split(u'\n');
    lines.removeIf([&](const QString &line) {
        return std::ranges::any_of(prefixes, [&](const QString &prefix) { return line.startsWith(prefix); });
    });
    return lines.join(u'\n');
}

bool DevTest::sameWithoutLines(const QString &a, const QString &b, const QStringList &prefixes) const
{
    return linesWithout(a, prefixes) == linesWithout(b, prefixes);
}

int DevTest::indexWithObject(const QVariant &list, const QString &key, QObject *object) const
{
    const QVariantList entries = listOf(list);
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (unwrapped(entries.at(i)).toMap().value(key).value<QObject *>() == object)
            return int(i);
    }
    return -1;
}

QVariantList DevTest::listOf(const QVariant &value) const
{
    return unwrapped(value).toList();
}

QVariantList DevTest::rowsContaining(const QVariant &list, const QString &key, const QString &text) const
{
    QVariantList out;
    const QVariantList rows = listOf(list);
    for (const QVariant &row : rows) {
        const QVariant map = unwrapped(row);
        if (text.isEmpty() || map.toMap().value(key).toString().contains(text))
            out.append(map);
    }
    return out;
}

QVariantList DevTest::rowsOf(const QString &key, const QStringList &values) const
{
    QVariantList out;
    for (const QString &value : values)
        out.append(QVariantMap{{key, value}});
    return out;
}

QString DevTest::callCatching(QObject *object, const QString &function) const
{
    QJSEngine *engine = object ? qjsEngine(object) : nullptr;
    if (!engine)
        return u"no QML engine for %1()"_s.arg(function);
    const QJSValue self = engine->toScriptValue(object);
    const QJSValue fn = self.property(function);
    if (!fn.isCallable())
        return u"%1 is not a function"_s.arg(function);
    // A pass's QML functions keep the objects they used in their compiled locals, which the
    // garbage collector reads during a collection (Qt 6.11: plain pointers). QtTest's touch, key
    // and mouse events process events, so an object a step ends (Escape closes an editor's
    // field: deleteLater) was deleted inside the pass, and a collection later in it (making a
    // menu) read the freed object and crashed the app. Such objects go once the pass returned.
    if (passDepth++ == 0) {
        deferredDeleteHold = std::make_unique<DeferredDeleteHold>();
        QCoreApplication::instance()->installEventFilter(deferredDeleteHold.get());
    }
    const QJSValue result = fn.callWithInstance(self);
    if (--passDepth == 0) {
        QCoreApplication::instance()->removeEventFilter(deferredDeleteHold.get());
        const QList<QPointer<QObject>> held = std::exchange(deferredDeleteHold->held, {});
        deferredDeleteHold.reset();
        for (const QPointer<QObject> &heldObject : held)
            delete heldObject.data();
    }
    return result.isError() ? result.toString() : QString();
}

} // namespace brulion::ui

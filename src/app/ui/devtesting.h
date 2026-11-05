// Drivers for the developer QML's self-tests (qml/dev: the editor playground's step runner and
// the self-tests it loads, `brulion --editor --editor-script <test>`), typed so that the
// self-tests compile to C++ like the rest of the app's QML (docs/UI.md "Compiled QML"):
//
//   DevEvents  QtTest's TestEvent (mouse, touch and key events sent through the window like a
//              real device's), made when first used: the QML imports no QtTest and still loads
//              without it (available() is then false and the events do nothing)
//   DevBlock   a row of a BlockListModel (PageController.blocks.get(row)) with typed fields
//   DevRecord  any other QVariantMap (a suggestion, a query result) whose keys QML reads
//   DevTest    (singleton) the item tree searched by objectName, a step called with its
//              exception caught
//
// App-only (no unit test of their own): the self-tests that use them are the ctest
// tst_editor_qml_* tests.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QJSValue;
class QQuickItem;

namespace brulion::ui {

class DevEvents : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit DevEvents(QObject *parent = nullptr);

    /// QtTest's TestEvent could be made (QtTest's QML module is installed).
    Q_INVOKABLE bool available();

    // TestEvent's own functions, same arguments (-1 delay: TestEvent's default).
    Q_INVOKABLE bool keyClick(int key, int modifiers, int delay);
    Q_INVOKABLE bool mousePress(QObject *item, qreal x, qreal y, int button, int modifiers, int delay);
    Q_INVOKABLE bool mouseRelease(QObject *item, qreal x, qreal y, int button, int modifiers, int delay);
    Q_INVOKABLE bool mouseClick(QObject *item, qreal x, qreal y, int button, int modifiers, int delay);
    Q_INVOKABLE bool mouseMove(QObject *item, qreal x, qreal y, int delay, int buttons, int modifiers);
    Q_INVOKABLE bool mouseWheel(QObject *item, qreal x, qreal y, int buttons, int modifiers, int xDelta, int yDelta,
                                int delay);
    /// touchEvent(item).press(0, item, x, y).commit(): finger 0 down at (x, y) of `item`.
    Q_INVOKABLE void touchPress(QObject *item, qreal x, qreal y);
    /// touchEvent(item).move(0, item, x, y).commit()
    Q_INVOKABLE void touchMove(QObject *item, qreal x, qreal y);
    /// touchEvent(item).release(0, item, x, y).commit()
    Q_INVOKABLE void touchRelease(QObject *item, qreal x, qreal y);

private:
    /// TestEvent as a JavaScript value (undefined when QtTest is missing).
    QJSValue testEvent();
    bool call(const char *function, const QList<QJSValue> &arguments);
    void touch(const char *point, QObject *item, qreal x, qreal y);

    QObject *m_testEvent = nullptr;
    bool m_tried = false;
};

class DevBlock : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // The keys of BlockListModel::get(); empty / 0 / false for a row that is not there.
    Q_PROPERTY(bool exists READ exists NOTIFY changed FINAL)
    Q_PROPERTY(QString view READ view NOTIFY changed FINAL)
    Q_PROPERTY(QString kind READ kind NOTIFY changed FINAL)
    Q_PROPERTY(QString taskStatus READ taskStatus NOTIFY changed FINAL)
    Q_PROPERTY(int depth READ depth NOTIFY changed FINAL)
    Q_PROPERTY(bool hasChildren READ hasChildren NOTIFY changed FINAL)
    Q_PROPERTY(bool collapsed READ collapsed NOTIFY changed FINAL)
    Q_PROPERTY(bool visible READ visible NOTIFY changed FINAL)
    Q_PROPERTY(QString blockId READ blockId NOTIFY changed FINAL)
    Q_PROPERTY(int headingLevel READ headingLevel NOTIFY changed FINAL)
    Q_PROPERTY(QString plainText READ plainText NOTIFY changed FINAL)
    Q_PROPERTY(QString editText READ editText NOTIFY changed FINAL)
    Q_PROPERTY(QString listMarker READ listMarker NOTIFY changed FINAL)
    Q_PROPERTY(QString html READ html NOTIFY changed FINAL)

public:
    explicit DevBlock(QObject *parent = nullptr);

    /// Take the fields of `row` (BlockListModel::get()).
    Q_INVOKABLE void read(const QVariantMap &row);

    bool exists() const { return !m_row.isEmpty(); }
    QString view() const { return text(u"view"); }
    QString kind() const { return text(u"kind"); }
    QString taskStatus() const { return text(u"taskStatus"); }
    int depth() const { return m_row.value(QStringLiteral("depth")).toInt(); }
    bool hasChildren() const { return m_row.value(QStringLiteral("hasChildren")).toBool(); }
    bool collapsed() const { return m_row.value(QStringLiteral("collapsed")).toBool(); }
    bool visible() const { return m_row.value(QStringLiteral("visible")).toBool(); }
    QString blockId() const { return text(u"blockId"); }
    int headingLevel() const { return m_row.value(QStringLiteral("headingLevel")).toInt(); }
    QString plainText() const { return text(u"plainText"); }
    QString editText() const { return text(u"editText"); }
    QString listMarker() const { return text(u"listMarker"); }
    QString html() const { return text(u"html"); }

Q_SIGNALS:
    void changed();

private:
    QString text(QStringView key) const { return m_row.value(key.toString()).toString(); }

    QVariantMap m_row;
};

class DevRecord : public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// The map: `record.value.title` compiles (a FINAL property's QVariantMap).
    Q_PROPERTY(QVariantMap value READ value WRITE setValue NOTIFY valueChanged FINAL)

public:
    explicit DevRecord(QObject *parent = nullptr);

    /// DevTest::valueAt() / lengthAt() of the map ("items.0.task.status"): what QML cannot read
    /// from a local copy of the map (a value-typed local passed to more than one call does not
    /// compile).
    Q_INVOKABLE QVariant at(const QString &path) const;
    Q_INVOKABLE int lengthAt(const QString &path) const;
    /// The object at `path` (null when none).
    Q_INVOKABLE QObject *objectAt(const QString &path) const;

    QVariantMap value() const { return m_value; }
    void setValue(const QVariantMap &value);

Q_SIGNALS:
    void valueChanged();

private:
    QVariantMap m_value;
};

class DevTest : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit DevTest(QObject *parent = nullptr);

    /// The first item under `root` (itself included, depth first through the child items) whose
    /// objectName is `name`, or null.
    Q_INVOKABLE QQuickItem *findItem(QQuickItem *root, const QString &name) const;
    /// How many items under `root` (itself included) are named `name`, and the `index`-th of them
    /// (depth first; null past the end). (Indices, not a list: a QML loop over a list it got from
    /// C++ that calls functions in its body does not compile.)
    Q_INVOKABLE int countNamed(QQuickItem *root, const QString &name) const;
    Q_INVOKABLE QQuickItem *namedAt(QQuickItem *root, const QString &name, int index) const;
    /// The first visible item under `root` (invisible ones and their children skipped) named
    /// `name`, or null.
    Q_INVOKABLE QQuickItem *findShown(QQuickItem *root, const QString &name) const;
    /// The first visible rendered rich text under `root` (invisible items and their children
    /// skipped): a Text or TextEdit whose textFormat is RichText, with linkAt(); null for none.
    Q_INVOKABLE QQuickItem *findRendered(QQuickItem *root) const;
    /// text.linkAt(x, y) of a Text or a TextEdit ("" for anything else).
    Q_INVOKABLE QString linkAt(QQuickItem *text, qreal x, qreal y) const;
    /// The first item under `root` (itself included, depth first) with a property `textFormat`
    /// (a Text, TextEdit) whose `text` is `text`, or null.
    Q_INVOKABLE QQuickItem *findText(QQuickItem *root, const QString &text) const;
    /// The first visible item under `root` (invisible ones and their children skipped) with a
    /// property `textFormat` whose `text` contains `needle`, or null.
    Q_INVOKABLE QQuickItem *findTextContaining(QQuickItem *root, const QString &needle) const;
    /// The first item under `root` (itself included, depth first) whose property `name` equals
    /// `value` (and that is visible, if `visibleOnly`), or null.
    Q_INVOKABLE QQuickItem *findWhere(QQuickItem *root, const QString &name, const QVariant &value,
                                      bool visibleOnly) const;
    /// The first item under `root` (itself included, depth first) that has every member of
    /// `names` (a property, a function or a signal), or null.
    Q_INVOKABLE QQuickItem *findWithMembers(QQuickItem *root, const QStringList &names) const;
    /// How many objects of `object`'s resources and data (the same one counted once) have every
    /// member of `names` (a property, a function or a signal).
    Q_INVOKABLE int countWithMembers(QObject *object, const QStringList &names) const;
    /// How many items under `root` (itself only if `withRoot`, depth first) have a member `name`
    /// (a property, a function, a signal), and the `index`-th of them (null past the end).
    Q_INVOKABLE int countWithMember(QQuickItem *root, const QString &name, bool withRoot) const;
    Q_INVOKABLE QQuickItem *withMemberAt(QQuickItem *root, const QString &name, bool withRoot, int index) const;
    /// item.children.length (0 for null) and item.children[index] (null out of range).
    Q_INVOKABLE int childCount(QQuickItem *item) const;
    Q_INVOKABLE QQuickItem *childAt(QQuickItem *item, int index) const;
    /// item[name] for a property `item` may not have (a Text's lineCount, a delegate's own
    /// properties): its value, undefined without it.
    Q_INVOKABLE QVariant property(QObject *item, const QString &name) const;
    /// The value at `path` in `value` — keys of maps, indices of lists, properties of objects,
    /// separated by dots ("items.0.task.status"): value.items[0].task.status in JavaScript, but
    /// undefined where a step is missing (no exception).
    Q_INVOKABLE QVariant valueAt(const QVariant &value, const QString &path) const;
    /// The length of the list or string at `path` in `value` (valueAt()), 0 for anything else.
    Q_INVOKABLE int lengthAt(const QVariant &value, const QString &path) const;
    /// item[name] for a property that holds an object (a dialog's own field): the object, or null.
    Q_INVOKABLE QObject *objectProperty(QObject *item, const QString &name) const;
    /// item[name] = value, for a property of a component the caller's type does not know.
    Q_INVOKABLE void setProperty(QObject *item, const QString &name, const QVariant &value) const;
    /// `item` has a property `name` (item.textFormat !== undefined).
    Q_INVOKABLE bool hasProperty(QObject *item, const QString &name) const;
    /// `item` has a property, a function or a signal `name` (item.applyTime !== undefined).
    Q_INVOKABLE bool hasMember(QObject *item, const QString &name) const;

    /// The numbers on a BTimePicker's face (`clock`) — the hours' or the minutes' circles and the
    /// chosen value's bubble: "<count> numbers", followed by ", overlapping …" (two circles, or the
    /// bubble and a neighbour of its number, overlap), ", cramped …" (a circle's text does not fit
    /// it, or its font is smaller than `textSm` on the outer ring, min(textSm, 12) on the inner
    /// one, 10 px in the bubble) and ", a press near a number's edge gives another one …" (hours: a
    /// point just inside a number, towards a neighbour, gives another number to labelAt()).
    Q_INVOKABLE QString clockNumbers(QQuickItem *clock, int textSm) const;

    /// The first line of `text` (split at "\n") that contains `needle`, or "".
    Q_INVOKABLE QString lineWith(const QString &text, const QString &needle) const;

    /// The URL of the first link of rendered `html` whose style underlines it
    /// (`<a href="URL" style="…;text-decoration:underline">`), "" for none.
    Q_INVOKABLE QString underlinedHref(const QString &html) const;

    /// An item's Accessible.name and Accessible.role (as a screen reader gets them; "" / 0 without
    /// an accessible interface).
    Q_INVOKABLE QString accessibleName(QQuickItem *item) const;
    Q_INVOKABLE int accessibleRole(QQuickItem *item) const;

    /// `text` without its lines (split at "\n") that start with one of `prefixes`, joined by "\n".
    Q_INVOKABLE QString linesWithout(const QString &text, const QStringList &prefixes) const;
    /// linesWithout(a, prefixes) === linesWithout(b, prefixes)
    Q_INVOKABLE bool sameWithoutLines(const QString &a, const QString &b, const QStringList &prefixes) const;
    /// The index of the first map of `list` whose `key` holds `object`, -1 for none.
    Q_INVOKABLE int indexWithObject(const QVariant &list, const QString &key, QObject *object) const;

    /// `value` (a string list, a list) as a QVariantList — a QML `list<var>`.
    Q_INVOKABLE QVariantList listOf(const QVariant &value) const;
    /// The maps of `list` whose `key` contains `text` (every map for ""), in their order.
    Q_INVOKABLE QVariantList rowsContaining(const QVariant &list, const QString &key, const QString &text) const;

    /// A list of maps with one key: [{ key: values[0] }, …] (a breadcrumb's model).
    Q_INVOKABLE QVariantList rowsOf(const QString &key, const QStringList &values) const;

    /// Call the QML function `function` (no arguments) of `object`: "" when it returned, else the
    /// exception it threw as text ("TypeError: …") — a self-test step that fails with an error
    /// is counted and the next one runs.
    Q_INVOKABLE QString callCatching(QObject *object, const QString &function) const;

private:
    /// The first item under `item` (depth first, itself included) for which `match` holds; with
    /// `descend` false for an item, its children are not looked at.
    static QQuickItem *firstWhere(QQuickItem *item, const std::function<bool(QQuickItem *)> &match,
                                  const std::function<bool(QQuickItem *)> &descend);
};

} // namespace brulion::ui

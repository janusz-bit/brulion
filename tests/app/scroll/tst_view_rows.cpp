// placedRow() (src/app/ui/viewrows.h): which row of a ListView a child of its content item shows.
// A live ListView offscreen (software rendering) as Tasks has it — reuseItems, rows of 44 to 152 px
// whose delegates have no `index` (or, data-driven, a `required property int index`), the cache
// off as in scroll-jumps.sh — scrolled in drag-sized steps to the end and back up: at every step
// each child's row is the view's own (itemAtIndex() over every row), none for a delegate kept in
// the reuse pool. Such a delegate stays in the content item, `visible`, where its row was — also
// under a row the view placed there again with another delegate, at the view's centre: the scroll
// probe took one for its anchor, and when the view took it for a row at the top the probe saw the
// content jump by 442 px (tst_scroll_jumps_qml's Tasks case, 2026-11-18); the sweep must meet that
// case, or it no longer tests it. Plus: a delegate of no size, a Flickable that is no item view.
#include "viewrows.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>
#include <memory>
#include <optional>

using namespace Qt::StringLiterals;
using brulion::ui::placedRow;

namespace {

/// Tasks' list on a phone (scroll-jumps.sh's notebook) as TasksView builds it: a DelegateChooser
/// of section headers (44 px) and tasks (50 to 260 px) — each kind with its own reuse pool, as
/// there.
QString listQml(bool indexed)
{
    return uR"(import QtQuick
import QtQml.Models
ListView {
    id: list
    width: 412; height: 795
    cacheBuffer: 0
    reuseItems: true
    boundsBehavior: Flickable.StopAtBounds
    // Short rows first, taller ones later (Tasks: overdue tasks of one line, then ones with chips
    // and long texts): the delegates made for the short rows outnumber the rows a screen of tall
    // ones shows, and the rest wait in the pool (oldest first out) where their rows were.
    readonly property var tall: [152, 110, 97, 180, 89, 260]
    model: ListModel { id: rowsModel }
    function fill(n) {
        for (let i = 0; i < n; ++i) {
            const header = i % 7 === 0
            rowsModel.append({ kind: header ? "header" : "task",
                               h: header ? 44 : i < n / 3 ? 50 + (i * 7) % 20 : tall[i % tall.length],
                               label: (header ? "Sekcja " : "Zadzwonić do hydraulika ") + i })
        }
    }
    header: Rectangle { width: list.width; height: 250; color: "#eee" }
    delegate: DelegateChooser {
        role: "kind"
        DelegateChoice {
            roleValue: "header"
            Rectangle {
                id: sectionRow
                %1
                required property real h
                required property string label
                width: ListView.view.width
                height: h
                color: "#ddd"
                Text { text: sectionRow.label }
            }
        }
        DelegateChoice {
            roleValue: "task"
            Rectangle {
                id: taskRow
                %1
                required property real h
                required property string label
                width: ListView.view.width
                height: h
                Text { text: taskRow.label }
            }
        }
    }
}
)"_s.arg(indexed ? u"required property int index"_s : QString());
}

} // namespace

class TestViewRows : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;

    QQuickItem *makeItem(const QString &qml, QSize size)
    {
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(size);
        m_engine = std::make_unique<QQmlEngine>();
        QQmlComponent c(m_engine.get());
        c.setData(qml.toUtf8(), QUrl(u"file:///viewrows.qml"_s));
        auto *item = qobject_cast<QQuickItem *>(c.create());
        if (!item) {
            qWarning() << c.errors();
            return nullptr;
        }
        item->setParent(m_window.get());
        item->setParentItem(m_window->contentItem());
        m_window->show();
        if (!QTest::qWaitForWindowExposed(m_window.get()))
            return nullptr;
        return item;
    }

    static double num(const QQuickItem *view, const char *name) { return view->property(name).toDouble(); }
    static QQuickItem *itemAtIndex(QQuickItem *view, int row)
    {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, row));
        return item;
    }

private Q_SLOTS:
    void sweep_data()
    {
        QTest::addColumn<bool>("indexed");
        QTest::newRow("delegates without an index (Tasks)") << false;
        QTest::newRow("required property int index") << true;
    }

    void sweep()
    {
        QFETCH(bool, indexed);
        QQuickItem *view = makeItem(listQml(indexed), QSize(412, 795));
        QVERIFY(view);
        QMetaObject::invokeMethod(view, "fill", Q_ARG(QVariant, 120));
        QMetaObject::invokeMethod(view, "forceLayout");
        auto *content = view->property("contentItem").value<QQuickItem *>();
        const auto *header = view->property("headerItem").value<QQuickItem *>();
        QVERIFY(content && header);
        const int count = view->property("count").toInt();
        QCOMPARE(count, 120);

        int pooled = 0;        // children that are no rows (kept for reuse, the highlight), still `visible`
        int pooledAnchors = 0; // steps with one of them topmost at the view's centre (the probe's old anchor)
        int steps = 0;
        const auto check = [&]() {
            // The view's own rows: every row it placed. (Neither a delegate in the pool nor
            // ListView's highlight item — a plain Item over the current row without a highlight
            // component — is one.)
            QHash<const QQuickItem *, int> rowOfItem;
            for (int row = 0; row < count; ++row) {
                if (const QQuickItem *item = itemAtIndex(view, row))
                    rowOfItem.insert(item, row);
            }
            const qreal centre = num(view, "contentY") + view->height() / 2;
            const QQuickItem *naiveAnchor = nullptr; // the last child at the centre, as z ties go
            const QList<QQuickItem *> children = content->childItems();
            for (const QQuickItem *child : children) {
                if (child == header)
                    continue;
                const std::optional<int> row = placedRow(view, child);
                const auto it = rowOfItem.constFind(child);
                if (it == rowOfItem.constEnd()) {
                    if (row)
                        qWarning() << "step" << steps << "a delegate the view did not place at" << child->y()
                                   << "taken for row" << *row;
                    QVERIFY(!row);
                    QVERIFY(child->isVisible()); // why `visible` does not tell
                    ++pooled;
                } else {
                    QCOMPARE(row, std::optional<int>(*it));
                }
                if (child->y() <= centre && centre < child->y() + child->height())
                    naiveAnchor = child;
            }
            if (naiveAnchor && !rowOfItem.contains(naiveAnchor))
                ++pooledAnchors;
            ++steps;
        };

        // Down to the end in drag-sized steps, then back up to the beginning.
        constexpr qreal step = 22;
        for (int guard = 0; guard < 10000 && !view->property("atYEnd").toBool(); ++guard) {
            const qreal maxY = num(view, "originY") + num(view, "contentHeight") - view->height();
            view->setProperty("contentY", std::min(num(view, "contentY") + step, maxY));
            QMetaObject::invokeMethod(view, "forceLayout");
            check();
            if (QTest::currentTestFailed())
                return;
        }
        QVERIFY(view->property("atYEnd").toBool());
        for (int guard = 0; guard < 10000 && !view->property("atYBeginning").toBool(); ++guard) {
            view->setProperty("contentY", std::max(num(view, "contentY") - step, num(view, "originY")));
            QMetaObject::invokeMethod(view, "forceLayout");
            check();
            if (QTest::currentTestFailed())
                return;
        }
        qInfo() << steps << "steps," << pooled << "children that are no rows met," << pooledAnchors
                << "steps with one topmost at the view's centre";
        // The case the probe met must happen here, or this sweep no longer tests it.
        QVERIFY(pooled > 0);
        QVERIFY(pooledAnchors > 0);
    }

    void noSize()
    {
        // A hidden row (0 px, as a folded block): no point of it is the view's row.
        QQuickItem *view = makeItem(uR"(import QtQuick
ListView {
    width: 200; height: 300
    model: 5
    delegate: Rectangle { width: ListView.view.width; height: modelData === 2 ? 0 : 40 }
}
)"_s,
                                    QSize(200, 300));
        QVERIFY(view);
        QMetaObject::invokeMethod(view, "forceLayout");
        QQuickItem *hidden = itemAtIndex(view, 2);
        QVERIFY(hidden);
        QCOMPARE(placedRow(view, hidden), std::optional<int>(-1));
        QCOMPARE(placedRow(view, itemAtIndex(view, 3)), std::optional<int>(3));
    }

    void notAnItemView()
    {
        // A plain Flickable: every child of its content counts; its row is its own `index`.
        QQuickItem *flickable = makeItem(uR"(import QtQuick
Flickable {
    width: 200; height: 300
    contentHeight: 1000
    Rectangle { objectName: "plain"; width: 200; height: 40 }
    Rectangle { objectName: "indexed"; property int index: 7; y: 40; width: 200; height: 40 }
}
)"_s,
                                         QSize(200, 300));
        QVERIFY(flickable);
        const auto *plain = flickable->findChild<QQuickItem *>(u"plain"_s);
        const auto *indexed = flickable->findChild<QQuickItem *>(u"indexed"_s);
        QVERIFY(plain && indexed);
        QCOMPARE(placedRow(flickable, plain), std::optional<int>(-1));
        QCOMPARE(placedRow(flickable, indexed), std::optional<int>(7));
        QVERIFY(!placedRow(nullptr, plain));
    }
};

QTEST_MAIN(TestViewRows)
#include "tst_view_rows.moc"

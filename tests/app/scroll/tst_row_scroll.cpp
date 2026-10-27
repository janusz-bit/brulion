// RowScroll (src/app/ui/rowscroll.h): the scroll bar of long lists whose rows differ in height.
// The pure row arithmetic (RowFraction) data-driven; then a live ListView offscreen (software
// rendering) with 400 rows of 18 to 600 px and a 300 px header, its cache off (every row created
// at the view's edge, as in scroll-jumps.sh): the progress moves with the content only — while
// Qt's own visibleArea, what an attached bar shows, jumps (the canary: when it stops failing, Qt
// estimates differently and docs/UI.md "The scroll bar: RowScroll" needs a look) — the size stays
// while the list moves, dragging a free bar and the wheel over it, rows inserted above the view,
// and a fold of 100 hidden rows in a model that says which rows it shows.
#include "rowscroll.h"

#include <QAbstractListModel>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>
#include <QtQml/qqml.h>

#include <cmath>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::RowEdges;
using brulion::ui::RowFraction;
using brulion::ui::RowScroll;
using brulion::ui::RowSpan;

namespace {

constexpr int Slowdown = BRULION_TEST_SLOWDOWN;
constexpr qreal NaN = std::numeric_limits<qreal>::quiet_NaN();

/// A list laid out like ListView does: the header, rows (a section header of `gap` px above every
/// third row), the footer; `rows` the created ones (empty: all).
RowEdges layout(qreal header, const QList<qreal> &heights, qreal gap, qreal footer, qreal viewTop, qreal viewBottom,
                const QList<int> &rows = {})
{
    RowEdges e;
    e.count = int(heights.size());
    e.viewTop = viewTop;
    e.viewBottom = viewBottom;
    qreal y = 0;
    if (header > 0) {
        e.hasHeader = true;
        e.headerTop = 0;
        e.headerBottom = header;
        y = header;
    }
    QList<qreal> tops;
    for (qsizetype i = 0; i < heights.size(); ++i) {
        if (i % 3 == 0)
            y += gap;
        tops.append(y);
        y += heights.at(i);
    }
    for (qsizetype i = 0; i < heights.size(); ++i) {
        if (!rows.isEmpty() && !rows.contains(int(i)))
            continue;
        const qreal bottom = i + 1 < heights.size() ? tops.at(i + 1) : tops.at(i) + heights.at(i);
        e.rows.append(RowSpan{int(i), tops.at(i), bottom});
    }
    if (footer > 0) {
        e.hasFooter = true;
        e.footerTop = y;
        e.footerBottom = y + footer;
    }
    return e;
}

QList<qreal> repeated(qreal h, int n)
{
    return QList<qreal>(n, h);
}

/// The shortest of the live list's rows (`heights` in sceneQml: 18 to 600 px).
constexpr qreal MinRow = 18;

QString sceneQml()
{
    return uR"(import QtQuick
import QtQuick.Templates as T
import BrulionTest
ListView {
    id: list
    width: 400; height: 600
    cacheBuffer: 0
    reuseItems: false
    boundsBehavior: Flickable.StopAtBounds
    readonly property var heights: [18, 240, 40, 600, 22, 75, 130, 18, 300, 55]
    model: ListModel { id: rowsModel }
    function fill(n) {
        for (let i = 0; i < n; ++i)
            rowsModel.append({ h: heights[i % heights.length], label: "Zażółć gęślą jaźń — dzień " + i })
    }
    function insertAt(at, n) {
        for (let i = 0; i < n; ++i)
            rowsModel.insert(at, { h: 50, label: "Wstawiony wiersz " + i })
    }
    header: Rectangle { width: list.width; height: 300; color: "#eee" }
    delegate: Rectangle {
        id: row
        required property real h
        required property string label
        width: list.width
        height: h
        visible: h > 0
        Text { text: row.label }
    }
    RowScroll { id: rows; objectName: "rows"; view: list; dragging: bar.pressed }
    // BRowScrollBar's wiring (src/app/qml/components/BRowScrollBar.qml), without the app's theme.
    T.ScrollBar {
        id: bar
        objectName: "bar"
        orientation: Qt.Vertical
        x: list.width - width
        y: 0
        width: 16
        height: list.height
        size: rows.size
        contentItem: Rectangle { implicitWidth: 16; implicitHeight: 16; color: "gray" }
        onPositionChanged: if (pressed) rows.scrollTo(position)
        onPressedChanged: if (!pressed) position = rows.position
        Connections {
            target: rows
            function onPositionChanged() { if (!bar.pressed) bar.position = rows.position }
        }
        Component.onCompleted: { parent = list; position = rows.position }
    }
}
)"_s;
}

/// Rows of 40 px, some hidden (0 px: folded under the row before them).
class PlainFoldModel : public QAbstractListModel {
    Q_OBJECT
public:
    PlainFoldModel(int rows, int hiddenFrom, int hiddenTo, QObject *parent = nullptr)
        : QAbstractListModel(parent), m_rows(rows), m_from(hiddenFrom), m_to(hiddenTo)
    { }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : m_rows; }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::UserRole)
            return hidden(index.row()) ? 0.0 : 40.0;
        return u"Gałąź %1"_s.arg(index.row());
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "h"}, {Qt::UserRole + 1, "label"}}; }
    bool hidden(int row) const { return row >= m_from && row < m_to; }

protected:
    int m_rows, m_from, m_to;
};

/// The same, telling which rows it shows (as the editor's BlockListModel does).
class FoldModel : public PlainFoldModel {
    Q_OBJECT
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY visibleCountChanged)
public:
    using PlainFoldModel::PlainFoldModel;
    int visibleCount() const { return m_rows - (m_to - m_from); }
    Q_INVOKABLE int visibleRowsBefore(int row) const
    {
        return row <= m_from ? row : row - (std::min(row, m_to) - m_from);
    }
    Q_INVOKABLE int visibleRowAt(int n) const
    {
        if (n < 0 || n >= visibleCount())
            return -1;
        return n < m_from ? n : n + (m_to - m_from);
    }
Q_SIGNALS:
    void visibleCountChanged();
};

struct Sample {
    qreal progress = 0;
    qreal qtProgress = 0;
    bool estimated = false;
};

} // namespace

class TestRowScroll : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_view = nullptr;
    RowScroll *m_rows = nullptr;
    QQuickItem *m_bar = nullptr;

    bool makeScene(int rows = 400)
    {
        m_rows = nullptr;
        m_view = nullptr;
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(400, 600);
        m_engine = std::make_unique<QQmlEngine>();
        QQmlComponent c(m_engine.get());
        c.setData(sceneQml().toUtf8(), QUrl(u"file:///rowscroll.qml"_s));
        auto *item = qobject_cast<QQuickItem *>(c.create());
        if (!item) {
            qWarning() << c.errors();
            return false;
        }
        item->setParent(m_window.get());
        item->setParentItem(m_window->contentItem());
        m_view = item;
        m_rows = item->findChild<RowScroll *>(u"rows"_s);
        m_bar = item->findChild<QQuickItem *>(u"bar"_s);
        if (rows > 0)
            QMetaObject::invokeMethod(m_view, "fill", Q_ARG(QVariant, rows));
        m_window->show();
        if (!QTest::qWaitForWindowExposed(m_window.get()) || !m_rows || !m_bar)
            return false;
        settle();
        return true;
    }

    double num(const char *name) const { return m_view->property(name).toDouble(); }
    bool flag(const char *name) const { return m_view->property(name).toBool(); }
    void settle()
    {
        QMetaObject::invokeMethod(m_view, "forceLayout");
        m_rows->refresh();
    }
    void setContentY(qreal y)
    {
        m_view->setProperty("contentY", y);
        settle();
    }
    qreal qtProgress() const
    {
        const auto *area = m_view->property("visibleArea").value<QObject *>();
        const qreal ratio = area->property("heightRatio").toReal();
        return ratio < 1 ? area->property("yPosition").toReal() / (1 - ratio) : 0;
    }
    Sample sample() const { return {m_rows->progress(), qtProgress(), m_rows->estimated()}; }
    int topRow() const
    {
        int index = -1;
        QMetaObject::invokeMethod(m_view, "indexAt", Q_RETURN_ARG(int, index), Q_ARG(qreal, 100),
                                  Q_ARG(qreal, num("contentY") + 1));
        return index;
    }

    /// contentY in `step` px steps to the end (down) or the beginning (up).
    QList<Sample> sweep(qreal step, bool down)
    {
        QList<Sample> out{sample()};
        for (int guard = 0; guard < 100000; ++guard) {
            if (down ? flag("atYEnd") : flag("atYBeginning"))
                break;
            const qreal minY = num("originY");
            const qreal maxY = minY + num("contentHeight") - m_view->height();
            setContentY(down ? std::min(num("contentY") + step, maxY) : std::max(num("contentY") - step, minY));
            out.append(sample());
        }
        return out;
    }

    void scrollToAndWait(qreal position)
    {
        QTest::qWait(RowScroll::ScrollIntervalMs + 4);
        m_rows->scrollTo(position);
        settle();
    }

private Q_SLOTS:
    void initTestCase() { qmlRegisterType<RowScroll>("BrulionTest", 1, 0, "RowScroll"); }

    // ---- RowFraction ------------------------------------------------------------------------------

    void progress_data()
    {
        QTest::addColumn<qreal>("header");
        QTest::addColumn<QList<qreal>>("heights");
        QTest::addColumn<qreal>("gap");
        QTest::addColumn<qreal>("footer");
        QTest::addColumn<qreal>("viewTop");
        QTest::addColumn<qreal>("viewBottom");
        QTest::addColumn<bool>("atBeginning");
        QTest::addColumn<bool>("atEnd");
        QTest::addColumn<QList<int>>("rows");
        QTest::addColumn<qreal>("expected"); // -1: none

        const QList<qreal> ten = repeated(100, 10);
        QTest::newRow("the header's top")
                << 300.0 << ten << 0.0 << 0.0 << 0.0 << 500.0 << false << false << QList<int>{} << 0.0;
        QTest::newRow("at the beginning (overshoot)")
                << 300.0 << ten << 0.0 << 0.0 << -20.0 << 480.0 << true << false << QList<int>{} << 0.0;
        QTest::newRow("at the end") << 300.0 << ten << 0.0 << 0.0 << 800.0 << 1300.0 << false << true << QList<int>{}
                                    << 1.0;
        QTest::newRow("the last row's bottom without the flag")
                << 300.0 << ten << 0.0 << 0.0 << 800.0 << 1300.0 << false << false << QList<int>{} << 1.0;
        QTest::newRow("in the middle") << 0.0 << ten << 0.0 << 0.0 << 250.0 << 650.0 << false << false << QList<int>{}
                                       << 2.5 / 6;
        QTest::newRow("a row taller than the view") << 0.0 << QList<qreal>{100, 2000, 100} << 0.0 << 0.0 << 500.0
                                                    << 900.0 << false << false << QList<int>{} << 1.2 / 2.8;
        QTest::newRow("section headers inside the spans") << 0.0 << repeated(100, 6) << 20.0 << 0.0 << 280.0 << 580.0
                                                          << false << false << QList<int>{} << 2.5 / 3.1;
        QTest::newRow("a header and a footer") << 200.0 << repeated(100, 4) << 0.0 << 300.0 << 450.0 << 750.0 << false
                                               << false << QList<int>{} << 0.875;
        QTest::newRow("only the rows at the edges")
                << 0.0 << ten << 0.0 << 0.0 << 250.0 << 650.0 << false << false << QList<int>{2, 6} << 2.5 / 6;
        QTest::newRow("empty list") << 300.0 << QList<qreal>{} << 0.0 << 0.0 << 0.0 << 500.0 << false << false
                                    << QList<int>{} << -1.0;
        QTest::newRow("no row at the bottom edge")
                << 0.0 << ten << 0.0 << 0.0 << 520.0 << 920.0 << false << false << QList<int>{5} << -1.0;
        QTest::newRow("no row at the top edge")
                << 0.0 << ten << 0.0 << 0.0 << 120.0 << 520.0 << false << false << QList<int>{5} << -1.0;
        QTest::newRow("a NaN height") << 0.0 << QList<qreal>{100, NaN, 100} << 0.0 << 0.0 << 50.0 << 150.0 << false
                                      << false << QList<int>{} << -1.0;
        QTest::newRow("a negative height") << 0.0 << QList<qreal>{100, -150, 100} << 0.0 << 0.0 << 10.0 << 60.0 << false
                                           << false << QList<int>{} << -1.0;
        QTest::newRow("a view of no height")
                << 0.0 << ten << 0.0 << 0.0 << 300.0 << 300.0 << false << false << QList<int>{} << -1.0;
    }

    void progress()
    {
        QFETCH(qreal, header);
        QFETCH(QList<qreal>, heights);
        QFETCH(qreal, gap);
        QFETCH(qreal, footer);
        QFETCH(qreal, viewTop);
        QFETCH(qreal, viewBottom);
        QFETCH(bool, atBeginning);
        QFETCH(bool, atEnd);
        QFETCH(QList<int>, rows);
        QFETCH(qreal, expected);
        RowEdges e = layout(header, heights, gap, footer, viewTop, viewBottom, rows);
        e.atBeginning = atBeginning;
        e.atEnd = atEnd;
        const std::optional<qreal> t = RowFraction::progress(e);
        if (expected < 0) {
            QVERIFY2(!t, qPrintable(QString::number(t.value_or(0))));
            return;
        }
        QVERIFY(t);
        QVERIFY2(std::abs(*t - expected) < 1e-9, qPrintable(u"%1 != %2"_s.arg(*t).arg(expected)));
    }

    void unsortedRows()
    {
        RowEdges e = layout(0, repeated(100, 10), 0, 0, 250, 650, {2, 6});
        std::swap(e.rows[0], e.rows[1]);
        QVERIFY(!RowFraction::valid(e));
        QVERIFY(!RowFraction::progress(e));
    }

    /// An edge exactly on a boundary is the same unit from both sides: no step in the thumb.
    void unitContinuity_data()
    {
        QTest::addColumn<qreal>("gap");
        QTest::addColumn<qreal>("footer");
        QTest::newRow("rows") << 0.0 << 0.0;
        QTest::newRow("section headers and a footer") << 24.0 << 120.0;
    }

    void unitContinuity()
    {
        QFETCH(qreal, gap);
        QFETCH(qreal, footer);
        const QList<qreal> heights{37, 600, 18, 240, 18, 18, 75};
        const RowEdges e = layout(300, heights, gap, footer, 0, 10);
        QList<qreal> boundaries{300};
        for (const RowSpan &r : e.rows)
            boundaries.append(r.bottom);
        if (e.hasFooter)
            boundaries.append(e.footerBottom);
        qreal last = -1;
        for (const qreal b : std::as_const(boundaries)) {
            const std::optional<qreal> before = RowFraction::unitAt(e, b - 1e-7);
            const std::optional<qreal> at = RowFraction::unitAt(e, b);
            QVERIFY(before && at);
            QVERIFY2(std::abs(*before - *at) < 1e-6, qPrintable(u"at %1: %2 / %3"_s.arg(b).arg(*before).arg(*at)));
            QVERIFY2(std::abs(*at - std::round(*at)) < 1e-9, qPrintable(QString::number(*at)));
            QVERIFY(*at > last);
            last = *at;
        }
        QCOMPARE(last, qreal(e.units()));
        // Inside a section header (a gap between two spans): the boundary.
        if (gap > 0) {
            const std::optional<qreal> inGap = RowFraction::unitAt(e, 300 + gap / 2);
            QVERIFY(inGap);
            QCOMPARE(*inGap, 1.0);
        }
    }

    /// target() inverts progress(): 100 rows, a header and a footer of 50 px each, a 400 px view.
    void target_data()
    {
        QTest::addColumn<qreal>("t");
        QTest::addColumn<bool>("header");
        QTest::addColumn<bool>("footer");
        for (const qreal t : {0.0, 0.003, 0.25, 0.5, 0.731, 0.999, 1.0}) {
            QTest::addRow("t=%g, header and footer", t) << t << true << true;
            QTest::addRow("t=%g, rows only", t) << t << false << false;
        }
    }

    void target()
    {
        QFETCH(qreal, t);
        QFETCH(bool, header);
        QFETCH(bool, footer);
        constexpr qreal Unit = 50;
        constexpr qreal View = 400;
        const RowEdges shape = layout(header ? Unit : 0, repeated(Unit, 100), 0, footer ? Unit : 0, 0, View);
        const RowFraction::Target to = RowFraction::target(t, shape, View / Unit);
        const qreal h = header ? Unit : 0;
        const qreal top = to.row < 0 ? to.fraction * Unit : h + (to.row + to.fraction) * Unit;
        const qreal total = h + 100 * Unit + (footer ? Unit : 0);
        QVERIFY2(top >= 0 && top <= total - View + 1e-6, qPrintable(QString::number(top)));
        RowEdges e = layout(header ? Unit : 0, repeated(Unit, 100), 0, footer ? Unit : 0, top, top + View);
        e.atEnd = top + View >= total - 1e-9;
        e.atBeginning = top <= 1e-9;
        const std::optional<qreal> back = RowFraction::progress(e);
        QVERIFY(back);
        QVERIFY2(std::abs(*back - t) < 1e-6, qPrintable(u"%1 -> %2"_s.arg(t).arg(*back)));
    }

    void targetRows()
    {
        const RowEdges shape = layout(300, repeated(10, 20), 0, 100, 0, 1);
        QCOMPARE(RowFraction::target(0, shape, 2).row, -1);
        QCOMPARE(RowFraction::target(1, shape, 2).row, 19); // the last row, then the footer in view
        const RowFraction::Target footer = RowFraction::target(1, shape, 0.5);
        QCOMPARE(footer.row, 20);
        QCOMPARE(footer.fraction, 0.5);
        QCOMPARE(RowFraction::target(NaN, shape, NaN).row, -1);
        const RowFraction::Target mid = RowFraction::target(0.5, shape, 2); // unit 10 of 22 - 2
        QCOMPARE(mid.row, 9);
        QVERIFY(std::abs(mid.fraction) < 1e-9);
    }

    // ---- A live ListView --------------------------------------------------------------------------

    /// Swept down and up in 37 px steps: the progress only moves with the content, in its
    /// direction and by at most the rows the edges crossed. Qt's own estimate jumps.
    void sweepIsMonotonic()
    {
        QVERIFY(makeScene());
        QVERIFY(m_rows->scrollable());
        const int units = 400 + 1;
        constexpr qreal Step = 37;
        // A step crosses at most Step / MinRow units at each edge; t = above / (above + below).
        const qreal bound = 1.05 * (Step / MinRow) / (units - m_view->height() / MinRow);
        for (const bool down : {true, false}) {
            const QList<Sample> samples = sweep(Step, down);
            QVERIFY(samples.size() > 1000);
            QCOMPARE(samples.constLast().progress, down ? 1.0 : 0.0);
            int qtJumps = 0;
            qreal worst = 0;
            for (qsizetype i = 1; i < samples.size(); ++i) {
                const Sample &a = samples.at(i - 1);
                const Sample &b = samples.at(i);
                QVERIFY2(!b.estimated, qPrintable(u"no row at an edge at step %1"_s.arg(i)));
                const qreal d = down ? b.progress - a.progress : a.progress - b.progress;
                QVERIFY2(d >= -1e-9,
                         qPrintable(u"step %1 moved back: %2 -> %3"_s.arg(i).arg(a.progress).arg(b.progress)));
                worst = std::max(worst, d);
                const qreal dq = down ? b.qtProgress - a.qtProgress : a.qtProgress - b.qtProgress;
                if (dq < -1e-6 || dq > 0.02)
                    ++qtJumps;
            }
            QVERIFY2(worst <= bound, qPrintable(u"largest step %1 > %2"_s.arg(worst).arg(bound)));
            // The canary: why RowScroll exists. Fails when Qt stops re-estimating the content.
            QVERIFY2(qtJumps > 0, "visibleArea no longer jumps: see docs/UI.md \"The scroll bar: RowScroll\"");
        }
    }

    /// The size stays while the list flicks; once it stops it is ListView's estimate again.
    void sizeFrozenWhileMoving()
    {
        QVERIFY(makeScene());
        QMetaObject::invokeMethod(m_view, "positionViewAtIndex", Q_ARG(int, 120), Q_ARG(int, 0));
        settle();
        const qreal before = m_rows->size();
        QMetaObject::invokeMethod(m_view, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, -4000));
        QVERIFY(flag("moving"));
        int frames = 0;
        int estimateMoved = 0; // frames in which ListView's estimate was another size
        while (flag("moving") && frames < 2000 * Slowdown) {
            QTest::qWait(16);
            ++frames;
            if (!flag("moving"))
                break; // stopped during the wait: the size may follow
            QCOMPARE(m_rows->size(), before);
            if (std::abs(m_view->height() / num("contentHeight") - before) > 1e-6)
                ++estimateMoved;
        }
        QVERIFY2(estimateMoved > 0, "the estimate never changed: nothing tested");
        QVERIFY(!flag("moving"));
        settle();
        const qreal estimate = m_view->height() / num("contentHeight");
        QVERIFY2(std::abs(m_rows->size() - estimate) < 1e-9,
                 qPrintable(u"%1 != %2"_s.arg(m_rows->size()).arg(estimate)));
        QVERIFY(std::abs(m_rows->position() - m_rows->progress() * (1 - m_rows->size())) < 1e-9);
    }

    void scrollTo()
    {
        QVERIFY(makeScene());
        setContentY(5000);
        // As while the bar is held: the size stays.
        m_rows->setDragging(true);
        scrollToAndWait(0);
        QVERIFY(flag("atYBeginning"));
        QCOMPARE(m_rows->progress(), 0.0);
        scrollToAndWait(1 - m_rows->size());
        QVERIFY(flag("atYEnd"));
        QCOMPARE(m_rows->progress(), 1.0);
        scrollToAndWait(0.5 * (1 - m_rows->size()));
        QVERIFY2(std::abs(m_rows->progress() - 0.5) < 0.01, qPrintable(QString::number(m_rows->progress())));
        const int row = topRow();
        QVERIFY2(std::abs(row - 198) <= 4, qPrintable(QString::number(row)));
        // Calls within a frame: the latest one wins, a frame later.
        m_rows->scrollTo(0.2 * (1 - m_rows->size()));
        m_rows->scrollTo(0.8 * (1 - m_rows->size()));
        QTRY_VERIFY(std::abs(m_rows->progress() - 0.8) < 0.01);
        m_rows->setDragging(false);
    }

    /// Dragging the free bar's thumb moves the list; the wheel over the bar scrolls the list.
    void dragAndWheel()
    {
        QVERIFY(makeScene());
        auto *thumb = m_bar->property("contentItem").value<QQuickItem *>();
        QVERIFY(thumb);
        QPointF p = thumb->mapToScene(QPointF(thumb->width() / 2, thumb->height() / 2));
        QTest::mousePress(m_window.get(), Qt::LeftButton, {}, p.toPoint());
        QTRY_VERIFY(m_rows->dragging());
        for (int i = 0; i < 12; ++i) {
            p.ry() += 20;
            QTest::mouseMove(m_window.get(), p.toPoint());
            QTest::qWait(RowScroll::ScrollIntervalMs + 2);
        }
        QTest::mouseRelease(m_window.get(), Qt::LeftButton, {}, p.toPoint());
        QTRY_VERIFY(!m_rows->dragging());
        settle();
        QVERIFY2(m_rows->progress() > 0.3 && m_rows->progress() < 0.6, qPrintable(QString::number(m_rows->progress())));
        QTRY_COMPARE(m_bar->property("position").toReal(), m_rows->position());

        const qreal before = num("contentY");
        const QPointF at = m_bar->mapToScene(QPointF(m_bar->width() / 2, m_bar->height() / 2));
        QWheelEvent wheel(at, m_window->mapToGlobal(at), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_window.get(), &wheel);
        QTRY_VERIFY(num("contentY") > before + 10);
    }

    /// Rows inserted above the view: they are rows above, the progress moves by them.
    void insertAbove()
    {
        QVERIFY(makeScene());
        QMetaObject::invokeMethod(m_view, "positionViewAtIndex", Q_ARG(int, 200), Q_ARG(int, 0));
        settle();
        const RowEdges e = m_rows->edges();
        const qreal above = *RowFraction::unitAt(e, e.viewTop);
        const qreal below = e.units() - *RowFraction::unitAt(e, e.viewBottom);
        const qreal contentY = num("contentY");
        QMetaObject::invokeMethod(m_view, "insertAt", Q_ARG(QVariant, 5), Q_ARG(QVariant, 10));
        settle();
        QCOMPARE(num("contentY"), contentY);
        QVERIFY(!m_rows->estimated());
        const qreal expected = (above + 10) / (above + 10 + below);
        QVERIFY2(std::abs(m_rows->progress() - expected) < 1e-3,
                 qPrintable(u"%1 != %2"_s.arg(m_rows->progress()).arg(expected)));
    }

    /// 300 rows of 40 px, 100 of them folded (0 px) under row 99: a model that says which rows it
    /// shows makes them no units — without it, crossing the fold jumps by 100 rows.
    void foldedRows_data()
    {
        QTest::addColumn<bool>("tells");
        QTest::newRow("the model tells its shown rows") << true;
        QTest::newRow("a plain model (jumps)") << false;
    }

    void foldedRows()
    {
        QFETCH(bool, tells);
        QVERIFY(makeScene(0));
        std::unique_ptr<PlainFoldModel> model(tells ? new FoldModel(300, 100, 200) : new PlainFoldModel(300, 100, 200));
        m_view->setProperty("model", QVariant::fromValue(static_cast<QObject *>(model.get())));
        settle();
        QTRY_COMPARE(num("count"), 300.0);
        settle();
        constexpr qreal Step = 13;
        const int units = tells ? 201 : 301;
        const qreal bound = 1.05 * (Step / 40) / (units - m_view->height() / 40);
        const QList<Sample> samples = sweep(Step, true);
        qreal worst = 0;
        for (qsizetype i = 1; i < samples.size(); ++i) {
            QVERIFY(!samples.at(i).estimated);
            QVERIFY(samples.at(i).progress >= samples.at(i - 1).progress - 1e-9);
            worst = std::max(worst, samples.at(i).progress - samples.at(i - 1).progress);
        }
        if (tells) {
            QVERIFY2(worst <= bound, qPrintable(u"largest step %1 > %2"_s.arg(worst).arg(bound)));
            scrollToAndWait(0.5 * (1 - m_rows->size()));
            QVERIFY2(std::abs(m_rows->progress() - 0.5) < 0.01, qPrintable(QString::number(m_rows->progress())));
            // Shown rows 0-99 and 200-299: the middle one (minus half a view) is row 92.
            QVERIFY2(std::abs(topRow() - 92) <= 3, qPrintable(QString::number(topRow())));
        } else {
            QVERIFY2(worst > 0.1, qPrintable(QString::number(worst)));
        }
        m_view->setProperty("model", QVariant());
        settle();
    }

    void cleanup()
    {
        m_rows = nullptr;
        m_bar = nullptr;
        m_view = nullptr;
        m_window.reset();
        m_engine.reset();
    }
};

QTEST_MAIN(TestRowScroll)
#include "tst_row_scroll.moc"

// The tabs of the shell (tabs.h): opening, selecting, closing, moving; the split pane; tabs in
// windows of their own; each tab's history and view state; saving and restoring them (also from
// malformed JSON); RouteHistory::restore().
#include "navigation.h"
#include "tabs.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::shell;

namespace {

QVariantMap page(const QString &path)
{
    return {{u"view"_s, u"page"_s}, {u"path"_s, path}};
}

QVariantMap view(const QString &name)
{
    return {{u"view"_s, name}};
}

QString pathOf(const TabSet &tabs, int id)
{
    return tabs.route(id).value(u"path"_s).toString();
}

/// The bar as the paths (or views) of its tabs, e.g. "journals|Zakupy.md|Łódź.md".
QString barText(const TabSet &tabs)
{
    QStringList out;
    for (const int id : tabs.bar()) {
        const QVariantMap r = tabs.route(id);
        out.append(r.contains(u"path"_s) ? r.value(u"path"_s).toString() : r.value(u"view"_s).toString());
    }
    return out.join(u'|');
}

} // namespace

class TestTabs : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void startsWithOneTabAtHome()
    {
        TabSet tabs;
        QCOMPARE(tabs.bar().size(), 1);
        QCOMPARE(tabs.active(), tabs.mainTab());
        QCOMPARE(tabs.splitTab(), 0);
        QCOMPARE(tabs.route(tabs.active()).value(u"view"_s).toString(), u"journals"_s);
        QVERIFY(tabs.windows().isEmpty());
        // An invalid home falls back to the journals.
        TabSet other(view(u"nonsense"_s));
        QCOMPARE(other.home().value(u"view"_s).toString(), u"journals"_s);
        TabSet tasks(view(u"tasks"_s));
        QCOMPARE(tasks.route(tasks.active()).value(u"view"_s).toString(), u"tasks"_s);
    }

    void openSelectsAndInsertsAfterTheFocusedTab()
    {
        TabSet tabs;
        const int first = tabs.active();
        const int a = tabs.open(page(u"Zażółć gęślą jaźń.md"_s));
        QVERIFY(a > 0);
        QCOMPARE(tabs.active(), a);
        QCOMPARE(tabs.mainTab(), a);
        const int b = tabs.open(page(u"Łódź.md"_s));
        QCOMPARE(barText(tabs), u"journals|Zażółć gęślą jaźń.md|Łódź.md"_s);
        // In the background: placed after the focused tab, nothing else changes.
        QVERIFY(tabs.select(first));
        const int c = tabs.open(page(u"Ćma.md"_s), false);
        QCOMPARE(tabs.active(), first);
        QCOMPARE(barText(tabs), u"journals|Ćma.md|Zażółć gęślą jaźń.md|Łódź.md"_s);
        QCOMPARE(tabs.indexOf(c), 1);
        QCOMPARE(tabs.indexOf(b), 3);
        QCOMPARE(tabs.indexOf(12345), -1);
        // A new tab has no history of its own: nothing to go back to.
        QVERIFY(!tabs.history(a)->canGoBack());
        // An invalid route opens the home route.
        const int d = tabs.open(page(QString()));
        QCOMPARE(tabs.route(d).value(u"view"_s).toString(), u"journals"_s);
    }

    void eachTabHasItsOwnHistory()
    {
        TabSet tabs;
        const int first = tabs.active();
        QVERIFY(tabs.navigate(0, page(u"Ogród.md"_s)));
        QVERIFY(tabs.navigate(0, page(u"Śliwki.md"_s)));
        const int second = tabs.open(view(u"tasks"_s));
        QVERIFY(tabs.navigate(0, view(u"calendar"_s)));
        QVERIFY(tabs.back(0));
        QCOMPARE(tabs.route(second).value(u"view"_s).toString(), u"tasks"_s);
        QVERIFY(!tabs.back(second)); // its first route
        // The first tab's history is untouched.
        QCOMPARE(pathOf(tabs, first), u"Śliwki.md"_s);
        QVERIFY(tabs.back(first));
        QCOMPARE(pathOf(tabs, first), u"Ogród.md"_s);
        QVERIFY(tabs.forward(first));
        QCOMPARE(pathOf(tabs, first), u"Śliwki.md"_s);
        QCOMPARE(tabs.active(), second); // navigating another tab by id selects nothing
        QVERIFY(tabs.goHome(first));
        QCOMPARE(tabs.route(first).value(u"view"_s).toString(), u"journals"_s);
        QVERIFY(!tabs.goHome(first)); // already there
        QVERIFY(!tabs.navigate(999, page(u"x.md"_s)));
        QVERIFY(!tabs.back(999));
        QVERIFY(!tabs.forward(999));
        QVERIFY(!tabs.replace(999, page(u"x.md"_s)));
        QVERIFY(tabs.history(999) == nullptr);
    }

    void viewStateFollowsTheRoute()
    {
        TabSet tabs;
        QVERIFY(tabs.navigate(0, page(u"Długa strona.md"_s)));
        const QVariantMap state{{u"row"_s, 12}, {u"offset"_s, 30.5}};
        QVERIFY(tabs.setViewState(0, state));
        QVERIFY(!tabs.setViewState(0, state)); // the same
        QCOMPARE(tabs.tab(tabs.active())->viewState, state);
        // Replacing the route's parameters (a view updating them as the user scrolls) keeps it.
        QVERIFY(tabs.replace(0,
                             QVariantMap{{u"view"_s, u"page"_s}, {u"path"_s, u"Długa strona.md"_s}, {u"line"_s, 3}}));
        QVERIFY(!tabs.replace(0,
                              QVariantMap{{u"view"_s, u"page"_s}, {u"path"_s, u"Długa strona.md"_s}, {u"line"_s, 3}}));
        QCOMPARE(tabs.tab(tabs.active())->viewState, state);
        // Another route: it was for the old one.
        QVERIFY(tabs.navigate(0, page(u"Inna.md"_s)));
        QVERIFY(tabs.tab(tabs.active())->viewState.isEmpty());
        QVERIFY(tabs.setViewState(0, state));
        QVERIFY(tabs.back(0));
        QVERIFY(tabs.tab(tabs.active())->viewState.isEmpty());
        QVERIFY(tabs.setViewState(0, state));
        QVERIFY(tabs.forward(0));
        QVERIFY(tabs.tab(tabs.active())->viewState.isEmpty());
        QVERIFY(!tabs.setViewState(999, state));
    }

    void selectIndexAndNeighbours_data()
    {
        QTest::addColumn<int>("selected");
        QTest::addColumn<int>("step");
        QTest::addColumn<int>("expected"); // bar index
        QTest::newRow("next") << 0 << 1 << 1;
        QTest::newRow("previous wraps to the last") << 0 << -1 << 3;
        QTest::newRow("next wraps to the first") << 3 << 1 << 0;
        QTest::newRow("same") << 2 << 0 << 2;
        QTest::newRow("far") << 1 << 9 << 2;
        QTest::newRow("far back") << 1 << -9 << 0;
    }
    void selectIndexAndNeighbours()
    {
        QFETCH(int, selected);
        QFETCH(int, step);
        QFETCH(int, expected);
        TabSet tabs;
        for (const QString &p : {u"A.md"_s, u"B.md"_s, u"C.md"_s})
            tabs.open(page(p));
        QVERIFY(tabs.selectIndex(selected) || tabs.active() == tabs.bar().at(selected));
        QCOMPARE(tabs.neighbour(step), tabs.bar().at(expected));
    }

    void selectIndexPastTheEndIsTheLast()
    {
        TabSet tabs;
        tabs.open(page(u"A.md"_s));
        tabs.open(page(u"B.md"_s));
        QVERIFY(tabs.selectIndex(0));
        QVERIFY(tabs.selectIndex(-1));
        QCOMPARE(tabs.active(), tabs.bar().last());
        QVERIFY(tabs.selectIndex(0));
        QVERIFY(tabs.selectIndex(8)); // Ctrl+9 with three tabs
        QCOMPARE(tabs.active(), tabs.bar().last());
        QVERIFY(!tabs.select(4242));
        QVERIFY(!tabs.select(tabs.active())); // nothing changes
    }

    void close_data()
    {
        // Four tabs (journals, A, B, C); `shown` is the main pane's (bar index), `closed` the one
        // closed; `expected` the main pane's afterwards.
        QTest::addColumn<int>("shown");
        QTest::addColumn<int>("closed");
        QTest::addColumn<QString>("expectedBar");
        QTest::addColumn<QString>("expectedMain");
        QTest::newRow("the shown one: its next neighbour") << 1 << 1 << u"journals|B.md|C.md"_s << u"B.md"_s;
        QTest::newRow("the last shown: the previous one") << 3 << 3 << u"journals|A.md|B.md"_s << u"B.md"_s;
        QTest::newRow("another one: the shown stays") << 1 << 3 << u"journals|A.md|B.md"_s << u"A.md"_s;
        QTest::newRow("the first shown") << 0 << 0 << u"A.md|B.md|C.md"_s << u"A.md"_s;
    }
    void close()
    {
        QFETCH(int, shown);
        QFETCH(int, closed);
        QFETCH(QString, expectedBar);
        QFETCH(QString, expectedMain);
        TabSet tabs;
        for (const QString &p : {u"A.md"_s, u"B.md"_s, u"C.md"_s})
            tabs.open(page(p));
        tabs.selectIndex(shown);
        QVERIFY(tabs.close(tabs.bar().at(closed)));
        QCOMPARE(barText(tabs), expectedBar);
        QCOMPARE(pathOf(tabs, tabs.mainTab()), expectedMain);
        QCOMPARE(tabs.active(), tabs.mainTab());
        QVERIFY(!tabs.close(777));
    }

    void closingTheLastTabOpensHome()
    {
        TabSet tabs;
        tabs.navigate(0, page(u"Jedyna.md"_s));
        const int only = tabs.active();
        QVERIFY(tabs.close(only));
        QCOMPARE(tabs.bar().size(), 1);
        QVERIFY(tabs.active() != only);
        QCOMPARE(tabs.route(tabs.active()).value(u"view"_s).toString(), u"journals"_s);
        QVERIFY(!tabs.history(0)->canGoBack());
    }

    void closeOthers()
    {
        TabSet tabs;
        const int a = tabs.open(page(u"A.md"_s));
        tabs.open(page(u"B.md"_s));
        tabs.split(a);
        QVERIFY(tabs.closeOthers(a));
        QCOMPARE(barText(tabs), u"A.md"_s);
        QCOMPARE(tabs.splitTab(), 0);
        QCOMPARE(tabs.active(), a);
        QVERIFY(!tabs.closeOthers(a)); // nothing left to close
        QVERIFY(!tabs.closeOthers(31337));
        // A window stays (and keeps the focus it has).
        const int w = tabs.openWindow(page(u"Okno.md"_s));
        tabs.open(page(u"C.md"_s), false);
        QVERIFY(tabs.closeOthers(a));
        QCOMPARE(tabs.windows(), QList<int>{w});
        QCOMPARE(tabs.active(), w);
    }

    void move_data()
    {
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<bool>("moved");
        QTest::addColumn<QString>("expected");
        QTest::newRow("forward") << 0 << 2 << true << u"A.md|B.md|journals"_s;
        QTest::newRow("backward") << 2 << 0 << true << u"B.md|journals|A.md"_s;
        QTest::newRow("same place") << 1 << 1 << false << u"journals|A.md|B.md"_s;
        QTest::newRow("past the end") << 0 << 3 << false << u"journals|A.md|B.md"_s;
        QTest::newRow("negative") << -1 << 0 << false << u"journals|A.md|B.md"_s;
    }
    void move()
    {
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(bool, moved);
        QFETCH(QString, expected);
        TabSet tabs;
        tabs.open(page(u"A.md"_s));
        const int b = tabs.open(page(u"B.md"_s));
        QCOMPARE(tabs.move(from, to), moved);
        QCOMPARE(barText(tabs), expected);
        QCOMPARE(tabs.active(), b); // moving never selects
    }

    void splitPane()
    {
        TabSet tabs;
        QVERIFY(tabs.navigate(0, page(u"Lewa.md"_s)));
        const int left = tabs.active();
        // Podziel widok: a copy of the focused tab, beside it, focused.
        QVERIFY(tabs.split());
        const int right = tabs.splitTab();
        QVERIFY(right != 0 && right != left);
        QCOMPARE(tabs.mainTab(), left);
        QVERIFY(tabs.splitFocused());
        QCOMPARE(tabs.active(), right);
        QCOMPARE(pathOf(tabs, right), u"Lewa.md"_s);
        QCOMPARE(barText(tabs), u"Lewa.md|Lewa.md"_s);
        // Navigation goes to the focused pane.
        QVERIFY(tabs.navigate(0, page(u"Prawa.md"_s)));
        QCOMPARE(pathOf(tabs, right), u"Prawa.md"_s);
        QCOMPARE(pathOf(tabs, left), u"Lewa.md"_s);
        QVERIFY(tabs.focusPane(false));
        QCOMPARE(tabs.active(), left);
        QVERIFY(!tabs.focusPane(false));
        QCOMPARE(tabs.focusedBarTab(), left);
        // A tab chosen in the bar replaces the focused pane's.
        const int third = tabs.open(page(u"Trzecia.md"_s));
        QCOMPARE(tabs.mainTab(), third);
        QCOMPARE(tabs.splitTab(), right);
        // Choosing the split pane's tab focuses that pane.
        QVERIFY(tabs.select(right));
        QVERIFY(tabs.splitFocused());
        QCOMPARE(tabs.mainTab(), third);
        // The main pane's tab cannot go to the split pane.
        QVERIFY(!tabs.split(third));
        QVERIFY(!tabs.split(424242));
        // Another bar tab into the split pane.
        QVERIFY(tabs.split(left));
        QCOMPARE(tabs.splitTab(), left);
        QVERIFY(!tabs.split(left)); // already there and focused
        QVERIFY(tabs.closeSplit());
        QCOMPARE(tabs.splitTab(), 0);
        QVERIFY(!tabs.splitFocused());
        QCOMPARE(tabs.active(), third);
        QVERIFY(!tabs.closeSplit());
        QVERIFY(!tabs.focusPane(true)); // no split pane
    }

    void closingTabsOfTheSplitPanes()
    {
        TabSet tabs;
        tabs.navigate(0, page(u"A.md"_s));
        const int a = tabs.active();
        QVERIFY(tabs.split());
        const int b = tabs.splitTab();
        // Closing the split pane's tab closes the split.
        QVERIFY(tabs.close(b));
        QCOMPARE(tabs.splitTab(), 0);
        QCOMPARE(tabs.active(), a);
        // Closing the main pane's tab when the split one is the only other: it takes the width.
        QVERIFY(tabs.split());
        const int c = tabs.splitTab();
        QVERIFY(tabs.focusPane(false));
        QVERIFY(tabs.close(a));
        QCOMPARE(tabs.mainTab(), c);
        QCOMPARE(tabs.splitTab(), 0);
        QCOMPARE(tabs.active(), c);
        // With more tabs the main pane gets its neighbour, never the split pane's tab; the
        // focused split pane stays focused.
        const int d = tabs.open(page(u"D.md"_s));
        const int e = tabs.open(page(u"E.md"_s));
        QVERIFY(tabs.select(c));
        QVERIFY(tabs.split(d));
        QCOMPARE(barText(tabs), u"A.md|D.md|E.md"_s);
        QVERIFY(tabs.close(c));
        QCOMPARE(tabs.mainTab(), e);
        QCOMPARE(tabs.splitTab(), d);
        QCOMPARE(tabs.active(), d);
    }

    void windows()
    {
        TabSet tabs;
        const int main = tabs.active();
        const int w = tabs.openWindow(page(u"W oknie.md"_s));
        QVERIFY(w > 0);
        QCOMPARE(tabs.windows(), QList<int>{w});
        QCOMPARE(tabs.bar(), QList<int>{main});
        QCOMPARE(tabs.active(), w);
        QCOMPARE(tabs.indexOf(w), -1);
        // Navigation goes to the window while it has the focus; the main window stays.
        QVERIFY(tabs.navigate(0, page(u"Dalej.md"_s)));
        QCOMPARE(pathOf(tabs, w), u"Dalej.md"_s);
        QCOMPARE(tabs.focusedBarTab(), main);
        QVERIFY(tabs.focusMainWindow());
        QVERIFY(!tabs.focusMainWindow());
        QCOMPARE(tabs.active(), main);
        QVERIFY(tabs.select(w));
        QCOMPARE(tabs.active(), w);
        // Back into the bar: after the focused tab, shown and active.
        QVERIFY(tabs.attach(w));
        QCOMPARE(tabs.bar(), (QList<int>{main, w}));
        QVERIFY(tabs.windows().isEmpty());
        QCOMPARE(tabs.mainTab(), w);
        QVERIFY(!tabs.attach(w));
        // Into a window again: the main pane gets its neighbour.
        QVERIFY(tabs.detach(w));
        QCOMPARE(tabs.mainTab(), main);
        QCOMPARE(tabs.active(), w);
        QVERIFY(!tabs.detach(w));
        // Closing the window: the main window's tab is active again.
        QVERIFY(tabs.close(w));
        QVERIFY(tabs.windows().isEmpty());
        QCOMPARE(tabs.active(), main);
        // The only bar tab into a window: a fresh tab at home stays in the bar.
        QVERIFY(tabs.navigate(0, page(u"Sama.md"_s)));
        QVERIFY(tabs.detach(main));
        QCOMPARE(tabs.bar().size(), 1);
        QVERIFY(tabs.bar().first() != main);
        QCOMPARE(tabs.route(tabs.mainTab()).value(u"view"_s).toString(), u"journals"_s);
        QCOMPARE(tabs.active(), main);
    }

    void atMostMaxTabs()
    {
        TabSet tabs;
        for (int i = 1; i < TabSet::MaxTabs; ++i)
            QVERIFY(tabs.open(page(u"Strona %1.md"_s.arg(i)), false) > 0);
        QCOMPARE(tabs.size(), TabSet::MaxTabs);
        QCOMPARE(tabs.open(page(u"Za dużo.md"_s)), 0);
        QCOMPARE(tabs.openWindow(page(u"Za dużo.md"_s)), 0);
        QVERIFY(!tabs.split());
        QCOMPARE(tabs.size(), TabSet::MaxTabs);
    }

    void renamesAndRemovals()
    {
        TabSet tabs;
        tabs.navigate(0, page(u"Stara.md"_s));
        tabs.navigate(0, page(u"Inna.md"_s));
        const int first = tabs.active();
        const int second = tabs.open(page(u"Stara.md"_s));
        const int window = tabs.openWindow(page(u"Stara.md"_s));
        tabs.transform([](QVariantMap &r) {
            if (r.value(u"path"_s) == u"Stara.md"_s)
                r.insert(u"path"_s, u"Nowa.md"_s);
        });
        QCOMPARE(pathOf(tabs, second), u"Nowa.md"_s);
        QCOMPARE(pathOf(tabs, window), u"Nowa.md"_s);
        QCOMPARE(tabs.history(first)->backRoutes().last().value(u"path"_s).toString(), u"Nowa.md"_s);
        tabs.setViewState(second, {{u"row"_s, 4}});
        QList<int> historyOnly;
        const QList<int> changed =
                tabs.removeIf([](const QVariantMap &r) { return r.value(u"path"_s) == u"Nowa.md"_s; }, &historyOnly);
        QCOMPARE(changed, (QList<int>{second, window}));
        QCOMPARE(historyOnly, QList<int>{first});
        QVERIFY(tabs.tab(second)->viewState.isEmpty());
        QCOMPARE(tabs.route(second).value(u"view"_s).toString(), u"journals"_s);
        QCOMPARE(tabs.history(first)->backRoutes().size(), 1);
        QCOMPARE(tabs.history(first)->backRoutes().first().value(u"view"_s).toString(), u"journals"_s);
    }

    void saveAndRestore()
    {
        TabSet tabs;
        tabs.navigate(0, page(u"Zażółć.md"_s));
        tabs.navigate(0, page(u"Gęślą.md"_s));
        tabs.back(0);
        const QVariantMap state{{u"row"_s, 7}, {u"offset"_s, 12}};
        tabs.setViewState(0, state);
        const int second = tabs.open(page(u"Jaźń.md"_s));
        tabs.navigate(second, view(u"tasks"_s));
        tabs.focusPane(false);
        tabs.split(tabs.bar().first());
        tabs.openWindow(page(u"Okno.md"_s));
        tabs.focusMainWindow();

        TabSet back;
        QVERIFY(back.restore(tabs.save()));
        // Windowed tabs come back as tabs of the bar, after the others.
        QCOMPARE(barText(back), u"Zażółć.md|tasks|Okno.md"_s);
        QVERIFY(back.windows().isEmpty());
        const int restored = back.bar().first();
        QCOMPARE(back.history(restored)->forwardRoutes().size(), 1);
        QCOMPARE(back.history(restored)->forwardRoutes().first().value(u"path"_s).toString(), u"Gęślą.md"_s);
        QCOMPARE(back.history(restored)->backRoutes().first().value(u"view"_s).toString(), u"journals"_s);
        QCOMPARE(back.tab(restored)->viewState.value(u"row"_s).toInt(), 7);
        QCOMPARE(back.mainTab(), back.bar().at(1));
        QCOMPARE(back.splitTab(), restored);
        QVERIFY(back.splitFocused());
        QCOMPARE(back.active(), restored);
        QVERIFY(back.history(back.bar().at(1))->canGoBack());
        // Saved again: the same.
        TabSet again;
        QVERIFY(again.restore(back.save()));
        QCOMPARE(again.save(), back.save());
    }

    void savedHistoriesAreTrimmed()
    {
        TabSet tabs;
        for (int i = 0; i < 60; ++i)
            tabs.navigate(0, page(u"Strona %1.md"_s.arg(i)));
        for (int i = 0; i < 30; ++i)
            tabs.back(0);
        TabSet back;
        QVERIFY(back.restore(tabs.save()));
        const RouteHistory *h = back.history(0);
        QCOMPARE(h->backDepth(), TabSet::SavedDepth);
        QCOMPARE(h->forwardDepth(), TabSet::SavedDepth);
        QCOMPARE(h->current().value(u"path"_s).toString(), u"Strona 29.md"_s);
        // The nearest ones are kept.
        QCOMPARE(h->backRoutes().last().value(u"path"_s).toString(), u"Strona 28.md"_s);
        QCOMPARE(h->forwardRoutes().last().value(u"path"_s).toString(), u"Strona 30.md"_s);
    }

    void restoreMalformed_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<QString>("expectedBar");
        QTest::addColumn<int>("splitIndex"); // -1: none
        QTest::newRow("empty") << QByteArray() << false << u"journals"_s << -1;
        QTest::newRow("not JSON") << QByteArray("{tabs: [") << false << u"journals"_s << -1;
        QTest::newRow("an array") << QByteArray("[1, 2]") << false << u"journals"_s << -1;
        QTest::newRow("no tabs") << QByteArray(R"({"main": 0})") << false << u"journals"_s << -1;
        QTest::newRow("tabs not an array") << QByteArray(R"({"tabs": "x"})") << false << u"journals"_s << -1;
        QTest::newRow("only invalid tabs")
                << QByteArray(R"({"tabs": [1, "a", {"current": {"view": "nonsense"}}, {"current": 5}, {}]})") << false
                << u"journals"_s << -1;
        QTest::newRow("invalid ones dropped") << QByteArray(
                R"({"tabs": [{"current": {"view": "page"}}, {"current": {"view": "page", "path": "Łąka.md"}}], "main": 1})")
                                              << true << u"Łąka.md"_s << -1;
        QTest::newRow("main out of range") << QByteArray(
                R"({"tabs": [{"current": {"view": "tasks"}}, {"current": {"view": "calendar"}}], "main": 9, "split": -4})")
                                           << true << u"tasks|calendar"_s << -1;
        QTest::newRow("main of the wrong type") << QByteArray(
                R"({"tabs": [{"current": {"view": "tasks"}}, {"current": {"view": "calendar"}}], "main": "1", "split": 1})")
                                                << true << u"tasks|calendar"_s << 1;
        QTest::newRow("split the same as main") << QByteArray(
                R"({"tabs": [{"current": {"view": "tasks"}}, {"current": {"view": "calendar"}}], "main": 1, "split": 1})")
                                                << true << u"tasks|calendar"_s << -1;
        QTest::newRow("split of a dropped tab")
                << QByteArray(R"({"tabs": [{"current": {"view": "tasks"}}, {"current": {}}], "split": 1})") << true
                << u"tasks"_s << -1;
        QTest::newRow("bad histories and view state") << QByteArray(
                R"({"tabs": [{"back": [3, {"view": "nonsense"}, {"view": "tasks"}], "current": {"view": "tasks"}, "forward": {"x": 1}, "view": [1]}]})")
                                                      << true << u"tasks"_s << -1;
    }
    void restoreMalformed()
    {
        QFETCH(QByteArray, json);
        QFETCH(bool, ok);
        QFETCH(QString, expectedBar);
        QFETCH(int, splitIndex);
        TabSet tabs;
        tabs.navigate(0, page(u"Przed.md"_s));
        tabs.open(page(u"Też przed.md"_s));
        QCOMPARE(tabs.restore(json), ok);
        QCOMPARE(barText(tabs), expectedBar);
        QCOMPARE(tabs.splitTab(), splitIndex < 0 ? 0 : tabs.bar().at(splitIndex));
        QVERIFY(tabs.contains(tabs.active()));
        QVERIFY(tabs.contains(tabs.mainTab()));
        QVERIFY(tabs.splitTab() != tabs.mainTab());
        QVERIFY(tabs.tab(tabs.active())->viewState.isEmpty() || ok);
        // A tab whose back list held only junk and the current route: nothing to go back to.
        if (QTest::currentDataTag() == QByteArrayLiteral("bad histories and view state")) {
            QVERIFY(!tabs.history(0)->canGoBack());
            QVERIFY(!tabs.history(0)->canGoForward());
            QVERIFY(tabs.tab(tabs.active())->viewState.isEmpty());
        }
    }

    void restoreKeepsAtMostMaxTabs()
    {
        QJsonArray many;
        for (int i = 0; i < TabSet::MaxTabs + 20; ++i)
            many.append(
                    QJsonObject{{u"current"_s, QJsonObject{{u"view"_s, u"page"_s}, {u"path"_s, u"S%1.md"_s.arg(i)}}}});
        TabSet tabs;
        QVERIFY(tabs.restore(QJsonDocument(QJsonObject{{u"tabs"_s, many}}).toJson()));
        QCOMPARE(tabs.size(), TabSet::MaxTabs);
    }

    void routeHistoryRestore()
    {
        RouteHistory h;
        QVERIFY(!h.restore({}, page(QString()), {}));
        QCOMPARE(h.current().value(u"view"_s).toString(), u"journals"_s); // unchanged
        // Duplicates next to each other and next to the current route collapse, invalid go.
        QVERIFY(h.restore({view(u"tasks"_s), view(u"tasks"_s), view(u"nonsense"_s), page(u"Ą.md"_s), page(u"Ą.md"_s)},
                          page(u"Ą.md"_s), {view(u"calendar"_s), page(u"Ą.md"_s)}));
        QCOMPARE(h.backDepth(), 1);
        QCOMPARE(h.forwardDepth(), 1);
        QVERIFY(h.back());
        QCOMPARE(h.current().value(u"view"_s).toString(), u"tasks"_s);
        QVERIFY(h.forward());
        QVERIFY(h.forward());
        QCOMPARE(h.current().value(u"view"_s).toString(), u"calendar"_s);
        // At most MaxDepth on a side.
        QList<QVariantMap> long_;
        for (int i = 0; i < RouteHistory::MaxDepth + 10; ++i)
            long_.append(page(u"P%1.md"_s.arg(i)));
        QVERIFY(h.restore(long_, view(u"tasks"_s), {}));
        QCOMPARE(h.backDepth(), RouteHistory::MaxDepth);
        QCOMPARE(h.backRoutes().last().value(u"path"_s).toString(), u"P%1.md"_s.arg(RouteHistory::MaxDepth + 9));
    }
};

QTEST_GUILESS_MAIN(TestTabs)
#include "tst_tabs.moc"

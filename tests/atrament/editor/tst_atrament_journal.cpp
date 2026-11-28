// The journal stream drawn by Atrament as one list (src/app/editor/atrament/journalview.h;
// docs/ATRAMENT.md "The journal stream as one list"): days of a days' model (JournalListModel's
// roles) over FakeEditorHost, drawn headless on Atrament's test platform — the days laid out in
// order with their headings, the view anchored by its day while days above are measured, editing in
// one day and ↑/↓ into the next, Enter and Backspace at a day's edges staying in it, a new day's
// file made by its first edit (from the journal template), the selection over several days
// (StreamSelection), the find bar's matches in every day, only the days near the view made, the
// view's state saved and restored, a date scrolled to, a new day inserted above at midnight.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "journalview.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "streamselection.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QDate>
#include <QStandardItemModel>
#include <QTest>

#include <chrono>
#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
namespace at = brulion::atrament;

namespace {

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf", "BrulionEmoji.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Emoji", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

enum DayRole { DateRole = Qt::UserRole + 1, PathRole, ExistsRole, IsTodayRole };

const QDate Today(2026, 11, 27);

QString dayDate(int daysAgo)
{
    return Today.addDays(-daysAgo).toString(Qt::ISODate);
}

QString dayPath(int daysAgo)
{
    return u"Dziennik/"_s + dayDate(daysAgo) + u".md"_s;
}

// The days of the stream: today (without a note unless `todayWritten`), then `count` earlier days
// with notes, newest first — JournalListModel's rows.
struct Days {
    QStandardItemModel model;
    Days() { model.setItemRoleNames({{DateRole, "date"}, {PathRole, "path"}, {ExistsRole, "exists"}, {IsTodayRole, "isToday"}}); }
    void append(int daysAgo, bool exists) { insert(model.rowCount(), daysAgo, exists); }
    void insert(int row, int daysAgo, bool exists)
    {
        auto *item = new QStandardItem;
        item->setData(dayDate(daysAgo), DateRole);
        item->setData(dayPath(daysAgo), PathRole);
        item->setData(exists, ExistsRole);
        item->setData(daysAgo == 0, IsTodayRole);
        model.insertRow(row, item);
    }
};

// A day's note: `blocks` list items, the first naming the day.
QString note(int daysAgo, int blocks)
{
    QString s;
    for (int b = 0; b < blocks; ++b)
        s += b == 0 ? u"- Dzień %1: zakupy, mleko i chleb\n"_s.arg(daysAgo) : u"- Blok %1 — zażółć gęślą jaźń\n"_s.arg(b);
    return s;
}

struct Harness {
    FakeEditorHost host;
    Days days;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<JournalView> journal;
    StreamSelection stream;
    std::vector<QString> opened; // onOpenAsPage's dates
    int made = 0;

    // Today without a note, then `count` days with notes of `blocks` blocks each (every fifth day a
    // longer one).
    explicit Harness(int count = 8, int blocks = 3, at::Size size = {600, 700}, bool todayWritten = false)
        : tw(platform->newWindow(size, 1))
    {
        host.setNow(u"2026-11-27 10:00"_s);
        days.append(0, todayWritten);
        if (todayWritten)
            host.setFile(dayPath(0), note(0, blocks));
        for (int d = 1; d <= count; ++d) {
            host.setFile(dayPath(d), note(d, d % 5 == 0 ? blocks * 3 : blocks));
            days.append(d, true);
        }
        host.setFile(u"Szablony/Dziennik.md"_s, u"## Plan dnia\n- \n"_s);
        stream.setHost(&host);
        stream.setModel(&days.model);
        journal = app.newEntity<JournalView>(&days.model);
        journal.update(app, [this](JournalView &j, at::Context<JournalView> &cx) {
            j.makeController = [this](const JournalView::DayInfo &info) {
                auto c = std::make_unique<PageController>();
                c->setNow(u"2026-11-27 10:00"_s);
                c->setSaveDelay(1000000); // saved by the tests only
                c->setHost(&host);
                c->setPath(info.path);
                ++made;
                return c;
            };
            j.formatDate = [](const QString &date) {
                return QDate::fromString(date, Qt::ISODate).toString(u"dddd, d MMMM yyyy"_s);
            };
            j.onOpenAsPage = [this](const QString &date, bool) { opened.push_back(date); };
            j.setJournalTemplate(u"Szablony/Dziennik.md"_s);
            j.setStreamSelection(&stream);
            PageMetrics m;
            m.contentX = 20;
            m.contentWidth = 560;
            j.setPageStyle(m, PagePalette{}, m.gutter, cx);
        });
        tw.setRasterize(false);
        app.openWindow(tw, journal);
        tw.settle();
    }
    const JournalView &view() { return journal.read(app); }
    PageController *day(int d) { return view().controller(d); }
    const PageView *page(int d)
    {
        const at::Entity<PageView> *p = view().page(d);
        return p ? &p->read(app) : nullptr;
    }
    std::optional<at::Rect> dayBounds(int d) { return view().dayBounds(d); }
    // The middle of the caret before display offset `offset` of drawn row `row` of day `d`.
    at::Point pointOf(int d, int row, int offset)
    {
        const PageView *p = page(d);
        const at::TextLayoutHandle *t = p ? p->rowText(row) : nullptr;
        if (!t || !t->layout())
            return {-1, -1};
        const at::Rect caret = t->layout()->caretRect(offset).translated(t->bounds().origin);
        return {caret.left() + 0.5f, caret.top() + caret.height() / 2};
    }
    void clickInto(int d, int row, int offset)
    {
        const at::Point p = pointOf(d, row, offset);
        QVERIFY2(p.x >= 0, "the row is drawn");
        tw.simulateClick(p);
        tw.settle();
    }
    void scrollToDay(int d)
    {
        journal.update(app, [d](JournalView &j, at::Context<JournalView> &cx) {
            j.scrollToDay(d, at::ListPosition::Start, cx);
        });
        tw.settle();
    }
    // The bullet of an outline's list item `row` of day `d` (left of its text, in the gutter).
    at::Point bulletOf(int d, int row)
    {
        const PageView *p = page(d);
        const std::optional<at::Rect> b = view().rowBounds(d, row);
        if (!p || !b)
            return {-1, -1};
        const PageMetrics &m = p->metrics();
        const int depth = day(d)->blocks()->index(row).data(BlockListModel::DepthRole).toInt();
        const float textX = m.contentX + m.gutter + float(depth) * m.indentStep;
        return {textX - m.bulletColumn / 2, b->top() + m.rowPad + m.lineHeight / 2};
    }
    // The day showing blocks dragged from another day (−1: none).
    int incomingDropDay()
    {
        for (int d : view().madeDays())
            if (page(d)->dragKind() == PageView::DragKind::None && page(d)->dropTarget())
                return d;
        return -1;
    }
    float scrollY()
    {
        float y = 0;
        journal.update(app, [&](JournalView &j, at::Context<JournalView> &) { y = j.handle().scroll().offset.y; });
        return y;
    }
    // The day the stream selection's focus end is in (−1: none).
    int focusDay()
    {
        for (int d = 0; d < view().dayCount(); ++d)
            if (view().dayInfo(d).path == stream.focusPath())
                return d;
        return -1;
    }
    // No day drags anything, none edits a block.
    bool settledAfterDrag()
    {
        for (int d : view().madeDays()) {
            if (page(d)->dragKind() != PageView::DragKind::None || day(d)->editingRow() >= 0)
                return false;
        }
        return true;
    }
    QString inputText(int d)
    {
        const PageView *p = page(d);
        return p && p->input() ? str(p->input()->read(app).text()) : QString();
    }
};

} // namespace

class TestAtramentJournal : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void daysLaidOutInOrder();
    void headingAndOpenAsPage();
    void dayAboveMeasuredLaterMovesNothing();
    void arrowsCrossIntoTheNextDay();
    void enterAndBackspaceStayInTheDay();
    void newDayFileMadeByTheFirstEdit();
    void selectionAcrossDays();
    void findInEveryDay();
    void onlyDaysNearTheViewAreMade();
    void viewStateRoundTrip();
    void dateScrolledTo();
    void newDayAtMidnightMovesNothing();
    void anotherDayEditedStopsTheFirst();
    void enterAtTheBottomKeepsTheBlockInView();
    void tapUnderTheBlocksAddsOne();
    void composedWordKeptWhenAnotherDayIsActivated();
    void mouseDragAcrossDaysScrollsTheStream();
    void fingerSelectionAcrossDaysScrolls();
    void zoomShowsTheDaysTop();
    void blockMovedIntoAnotherDay();
    void blockDraggedPastTheEdgeLandsInADayScrolledIn();
};

void TestAtramentJournal::daysLaidOutInOrder()
{
    Harness h(4);
    // Today without a note shows its prompt; the earlier days their blocks, one after another.
    QVERIFY(h.view().promptPoint(0).has_value());
    QVERIFY(!h.view().promptPoint(1).has_value());
    float last = -1;
    for (int d = 0; d <= 4; ++d) {
        const std::optional<at::Rect> b = h.dayBounds(d);
        if (!b)
            break; // below the view
        QVERIFY2(b->top() > last, qPrintable(u"day %1 below the one before"_s.arg(d)));
        last = b->top();
        if (d > 0) {
            QVERIFY(h.day(d) && h.day(d)->loaded());
            const std::optional<at::Rect> row = h.view().rowBounds(d, 0);
            QVERIFY2(row && row->top() >= b->top() && row->bottom() <= b->bottom() + 0.5f,
                     qPrintable(u"day %1's first block inside its section"_s.arg(d)));
            QVERIFY(h.page(d)->drawnRows() >= 1);
        }
    }
    QVERIFY2(h.dayBounds(2).has_value(), "at least three days on a 700 px window");
    // Today is the day written in (none was before).
    QCOMPARE(h.view().activeDay(), 0);
}

void TestAtramentJournal::headingAndOpenAsPage()
{
    Harness h(3);
    const std::optional<at::Point> date = h.view().datePoint(1);
    QVERIFY(date.has_value());
    h.tw.simulateClick(*date);
    h.tw.settle();
    QCOMPARE(h.opened.size(), std::size_t(1));
    QCOMPARE(h.opened.back(), dayDate(1));
    const std::optional<at::Point> open = h.view().openAsPagePoint(2);
    QVERIFY(open.has_value());
    h.tw.simulateClick(*open);
    h.tw.settle();
    QCOMPARE(h.opened.back(), dayDate(2));
    // The heading for screen readers: a heading named by the long date in a group of the day.
    h.app.windows().front()->setAccessibilityEnabled(true);
    h.tw.settle();
    h.tw.draw();
    const QString tree = str(h.app.windows().front()->accessibilityTree().dump());
    const QString longDate = QDate::fromString(dayDate(1), Qt::ISODate).toString(u"dddd, d MMMM yyyy"_s);
    QVERIFY2(tree.contains(u"heading 1 \""_s + longDate), qPrintable(tree));
    QVERIFY2(tree.contains(u"group \""_s + longDate) && tree.contains(u"button \"Open as page\""_s), qPrintable(tree));
}

void TestAtramentJournal::dayAboveMeasuredLaterMovesNothing()
{
    Harness h(30);
    // Day 12 at the top: the days above it were never laid out (estimates).
    h.scrollToDay(12);
    const std::optional<at::Rect> before = h.dayBounds(12);
    QVERIFY(before.has_value());
    QVERIFY(std::abs(before->top()) < 1);
    // 40 px up: day 11 comes into view and is measured; day 12 moved by the 40 px only.
    h.tw.simulateScroll({300, 300}, {0, -40});
    h.tw.settle();
    const std::optional<at::Rect> after = h.dayBounds(12);
    QVERIFY(after.has_value());
    QVERIFY2(std::abs(after->top() - before->top() - 40) < 1,
             qPrintable(u"day 12 at %1, was %2"_s.arg(after->top()).arg(before->top())));
    QVERIFY(h.dayBounds(11).has_value());
}

void TestAtramentJournal::arrowsCrossIntoTheNextDay()
{
    Harness h(4);
    PageController *first = h.day(1);
    PageController *second = h.day(2);
    QVERIFY(first && second);
    const int last = first->lastVisibleRow();
    h.clickInto(1, last, 3);
    QCOMPARE(first->editingRow(), last);
    QCOMPARE(h.view().activeDay(), 1);
    // ↓ on the last line of the day's last block: the next day's first block, the first stops.
    h.tw.simulateKeystrokes(u"down");
    h.tw.settle();
    QCOMPARE(second->editingRow(), 0);
    QCOMPARE(first->editingRow(), -1);
    QCOMPARE(h.view().activeDay(), 2);
    QVERIFY(h.inputText(2).startsWith(u"Dzień 2"_s));
    // ↑ on its first line: back to the day above's last block.
    h.tw.simulateKeystrokes(u"up");
    h.tw.settle();
    QCOMPARE(first->editingRow(), last);
    QCOMPARE(second->editingRow(), -1);
    QCOMPARE(h.view().activeDay(), 1);
    // Typing goes to that day.
    h.tw.simulateTyping(u" ąę");
    h.tw.settle();
    QVERIFY2(first->blocks()->editTextAt(last).contains(u"ąę"_s), qPrintable(first->blocks()->editTextAt(last)));
}

void TestAtramentJournal::enterAndBackspaceStayInTheDay()
{
    Harness h(4);
    PageController *first = h.day(1);
    PageController *second = h.day(2);
    const int blocksFirst = first->blocks()->rowCount();
    const int blocksSecond = second->blocks()->rowCount();
    const int last = first->lastVisibleRow();
    const QString lastText = first->blocks()->editTextAt(last);
    h.clickInto(1, last, int(h.page(1)->rowText(last)->layout()->text().size()));
    QCOMPARE(first->editingRow(), last);
    // Enter at the end of a day's last block: a new block in that day, edited, shown.
    h.tw.simulateKeystrokes(u"enter");
    h.tw.settle();
    QCOMPARE(first->blocks()->rowCount(), blocksFirst + 1);
    QCOMPARE(second->blocks()->rowCount(), blocksSecond);
    QCOMPARE(first->editingRow(), last + 1);
    const std::optional<at::Rect> row = h.view().rowBounds(1, last + 1);
    QVERIFY(row && row->top() >= 0 && row->bottom() <= 700);
    // Backspace at the start of the next day's first block merges nothing into the day above.
    h.clickInto(2, 0, 0);
    QCOMPARE(second->editingRow(), 0);
    h.tw.simulateKeystrokes(u"home backspace");
    h.tw.settle();
    QCOMPARE(second->blocks()->rowCount(), blocksSecond);
    QCOMPARE(first->blocks()->rowCount(), blocksFirst + 1);
    QCOMPARE(first->blocks()->editTextAt(last), lastText);
    QCOMPARE(second->editingRow(), 0);
}

void TestAtramentJournal::newDayFileMadeByTheFirstEdit()
{
    Harness h(3);
    const QString path = dayPath(0);
    bool ok = true;
    h.host.read(path, &ok);
    QVERIFY(!ok); // today has no note
    const std::optional<at::Point> prompt = h.view().promptPoint(0);
    QVERIFY(prompt.has_value());
    h.tw.simulateClick(*prompt);
    h.tw.settle();
    PageController *today = h.day(0);
    QVERIFY(today);
    QVERIFY(today->editingRow() >= 0);
    // Starting to write makes no file; the first change does, from the journal template.
    today->save();
    h.host.read(path, &ok);
    QVERIFY(!ok);
    h.tw.simulateTyping(u"Kupić mleko");
    h.tw.settle();
    today->save();
    const QString content = h.host.read(path, &ok);
    QVERIFY(ok);
    QVERIFY2(content.contains(u"Kupić mleko"_s), qPrintable(content));
    QVERIFY2(content.contains(u"Plan dnia"_s), qPrintable(content));
    QVERIFY(!h.view().promptPoint(0).has_value());
}

void TestAtramentJournal::selectionAcrossDays()
{
    Harness h(4);
    PageController *first = h.day(1);
    PageController *second = h.day(2);
    const int last = first->lastVisibleRow();
    // Shift+↓ from the end of a day's last block: the blocks, and past the day into the next one.
    h.clickInto(1, last, 1);
    h.tw.simulateKeystrokes(u"end shift-down");
    h.tw.settle();
    QVERIFY2(h.stream.active(), "a selection over two days");
    QVERIFY(h.stream.count() >= 2);
    QVERIFY(second->selectionCount() >= 1);
    QVERIFY(first->selectionCount() >= 1);
    QCOMPARE(first->editingRow(), -1);
    // Copying takes both days' blocks.
    const QString markdown = h.stream.markdown();
    QVERIFY2(markdown.contains(u"Dzień 2"_s) && markdown.contains(u"Blok"_s), qPrintable(markdown));
    h.stream.clear();
    h.tw.settle();
    QVERIFY(!h.stream.active());
    // A block selected in the day (the selection's keys, no block edited): Shift+↓ at the day's last
    // block goes on into the next day.
    first->setSelectedRow(last);
    h.tw.settle();
    QCOMPARE(h.view().activeDay(), 1);
    h.tw.simulateKeystrokes(u"shift-down");
    h.tw.settle();
    QVERIFY2(h.stream.active(), "the selection's keys past the day's end");
    QVERIFY(second->selectionCount() >= 1);
}

void TestAtramentJournal::findInEveryDay()
{
    Harness h(12);
    h.journal.update(h.app, [](JournalView &j, at::Context<JournalView> &cx) {
        j.setFind(u"mleko"_s, false, false, false, at::rgba(0xffd43b66), cx);
    });
    h.tw.settle();
    for (int d : h.view().madeDays())
        QCOMPARE(h.page(d)->findQuery(), u"mleko"_s);
    // A day made later (scrolled to) tints the matches too.
    h.scrollToDay(11);
    QVERIFY(h.page(11));
    QCOMPARE(h.page(11)->findQuery(), u"mleko"_s);
}

void TestAtramentJournal::onlyDaysNearTheViewAreMade()
{
    Harness h(60);
    const std::size_t atTop = h.view().madeDays().size();
    QVERIFY2(atTop >= 3 && atTop <= 16, qPrintable(QString::number(atTop)));
    QVERIFY(!h.day(50));
    // Far down: the days at the top go (saved: their controllers flush), those near 50 come.
    h.scrollToDay(50);
    h.tw.settle();
    QVERIFY(h.day(50));
    QVERIFY(!h.day(1));
    const std::vector<int> made = h.view().madeDays();
    QVERIFY2(made.size() <= 18, qPrintable(QString::number(made.size())));
    for (int d : made)
        QVERIFY2(d == 0 || std::abs(d - 50) <= 15, qPrintable(u"day %1 made far from the view"_s.arg(d)));
    // An edited day is kept wherever it goes.
    h.clickInto(50, 0, 2);
    QVERIFY(h.day(50)->editingRow() >= 0);
    h.scrollToDay(2);
    h.tw.settle();
    QVERIFY2(h.day(50), "the edited day stays");
    QVERIFY(h.day(2));
}

void TestAtramentJournal::viewStateRoundTrip()
{
    Harness h(30);
    h.scrollToDay(9);
    h.tw.simulateScroll({300, 300}, {0, 25});
    h.tw.settle();
    const JournalView::ViewState state = h.view().viewState();
    QCOMPARE(state.date, dayDate(9));
    QVERIFY2(std::abs(state.offset - 25) < 1, qPrintable(QString::number(state.offset)));
    h.journal.update(h.app, [](JournalView &j, at::Context<JournalView> &cx) { j.restoreViewState({}, cx); });
    h.tw.settle();
    QVERIFY(h.view().viewState().date.isEmpty());
    h.journal.update(h.app, [&](JournalView &j, at::Context<JournalView> &cx) { j.restoreViewState(state, cx); });
    h.tw.settle();
    const std::optional<at::Rect> b = h.dayBounds(9);
    QVERIFY(b.has_value());
    QVERIFY2(std::abs(b->top() + 25) < 1, qPrintable(QString::number(b->top())));
}

void TestAtramentJournal::dateScrolledTo()
{
    Harness h(30);
    // A date without a note: the nearest earlier day.
    h.days.model.removeRow(20); // the day 20 days ago
    h.tw.settle();
    h.journal.update(h.app, [](JournalView &j, at::Context<JournalView> &cx) { j.scrollToDate(dayDate(20), cx); });
    h.tw.settle();
    const int row = h.view().exactRowOfDate(dayDate(21));
    QCOMPARE(row, 20);
    const std::optional<at::Rect> b = h.dayBounds(row);
    QVERIFY(b.has_value());
    QVERIFY(std::abs(b->top()) < 1);
}

void TestAtramentJournal::newDayAtMidnightMovesNothing()
{
    Harness h(20);
    h.scrollToDay(6);
    h.tw.simulateScroll({300, 300}, {0, 10});
    h.tw.settle();
    const std::optional<at::Rect> before = h.dayBounds(6);
    QVERIFY(before.has_value());
    // A new day listed first (midnight): every row moves down one; day 6 stays where it was.
    h.days.insert(0, -1, false);
    h.tw.settle();
    const std::optional<at::Rect> after = h.dayBounds(7);
    QVERIFY(after.has_value());
    QVERIFY2(std::abs(after->top() - before->top()) < 1,
             qPrintable(u"%1 → %2"_s.arg(before->top()).arg(after->top())));
    QCOMPARE(h.view().dayInfo(7).date, dayDate(6));
}

void TestAtramentJournal::anotherDayEditedStopsTheFirst()
{
    Harness h(4);
    PageController *first = h.day(1);
    PageController *second = h.day(2);
    h.clickInto(1, 0, 2);
    QVERIFY(first->editingRow() >= 0);
    h.clickInto(2, 1, 2);
    QCOMPARE(second->editingRow(), 1);
    QCOMPARE(first->editingRow(), -1);
    QCOMPARE(h.view().activeDay(), 2);
}

void TestAtramentJournal::enterAtTheBottomKeepsTheBlockInView()
{
    Harness h(12);
    // A day whose last block is just above the view's bottom (the toolbar's 100 px covering it).
    h.journal.update(h.app, [](JournalView &j, at::Context<JournalView> &) { j.setBottomInset(100); });
    h.scrollToDay(4);
    int day = -1;
    int row = -1;
    for (int d = 4; d < 12 && day < 0; ++d) {
        PageController *c = h.day(d);
        if (!c)
            continue;
        const int last = c->lastVisibleRow();
        const std::optional<at::Rect> b = h.view().rowBounds(d, last);
        if (b && b->bottom() > 450 && b->bottom() < 600) {
            day = d;
            row = last;
        }
    }
    QVERIFY2(day >= 0, "a day's last block near the bottom");
    h.clickInto(day, row, 1);
    h.tw.simulateKeystrokes(u"end enter");
    h.tw.settle();
    h.tw.simulateTyping(u"pierwszy");
    h.tw.simulateKeystrokes(u"enter");
    h.tw.settle();
    h.tw.simulateTyping(u"drugi");
    h.tw.simulateKeystrokes(u"enter");
    h.tw.settle();
    PageController *c = h.day(day);
    QCOMPARE(c->editingRow(), row + 3);
    // Each new block shown above the toolbar, by its real height.
    const std::optional<at::Rect> b = h.view().rowBounds(day, row + 3);
    QVERIFY(b.has_value());
    QVERIFY2(b->top() >= 0 && b->bottom() <= 600 + 0.5f, qPrintable(u"%1..%2"_s.arg(b->top()).arg(b->bottom())));
}

void TestAtramentJournal::tapUnderTheBlocksAddsOne()
{
    Harness h(4);
    PageController *first = h.day(1);
    const int count = first->blocks()->rowCount();
    const std::optional<at::Rect> last = h.view().rowBounds(1, first->lastVisibleRow());
    QVERIFY(last.has_value());
    // The strip under the day's last block: an empty block appended and edited.
    h.tw.simulateClick({last->left() + 200, last->bottom() + 4});
    h.tw.settle();
    QCOMPARE(first->blocks()->rowCount(), count + 1);
    QCOMPARE(first->editingRow(), count);
    QCOMPARE(h.view().activeDay(), 1);
}

void TestAtramentJournal::composedWordKeptWhenAnotherDayIsActivated()
{
    Harness h(4);
    PageController *first = h.day(1);
    h.clickInto(1, 0, 2);
    QCOMPARE(first->editingRow(), 0);
    h.tw.simulateKeystrokes(u"end");
    h.tw.simulateComposition(u" zażółć");
    h.tw.settle();
    QVERIFY(!first->blocks()->editTextAt(0).contains(u"zażółć"_s)); // (the input method's still)
    // Another day becomes the one written in (the find bar's next match there): the word being
    // composed is the day's text before it stops editing.
    h.journal.update(h.app, [](JournalView &j, at::Context<JournalView> &cx) { j.activateDay(2, cx); });
    h.tw.settle();
    QCOMPARE(first->editingRow(), -1);
    QVERIFY2(first->blocks()->editTextAt(0).contains(u"zażółć"_s), qPrintable(first->blocks()->editTextAt(0)));
}

void TestAtramentJournal::zoomShowsTheDaysTop()
{
    // A long day scrolled to its end, zoomed into its last block: the day gets short; its heading
    // and the block come into view (where the view was is past the day's end now).
    Harness h(4, 30);
    PageController *first = h.day(1);
    const int last = first->lastVisibleRow();
    h.scrollToDay(1);
    h.journal.update(h.app, [](JournalView &j, at::Context<JournalView> &cx) {
        j.handle().scrollBy(600);
        cx.notify();
    });
    h.tw.settle();
    QVERIFY(h.dayBounds(1) && h.dayBounds(1)->top() < -400);
    QVERIFY(h.view().rowBounds(1, last)->bottom() > 0);
    first->zoomInto(last);
    h.tw.settle();
    QCOMPARE(first->zoomBlock(), last);
    const std::optional<at::Rect> day = h.dayBounds(1);
    const std::optional<at::Rect> row = h.view().rowBounds(1, last);
    QVERIFY(day && row);
    QVERIFY2(day->top() >= -0.5f && row->bottom() <= 700.5f, qPrintable(u"%1, %2"_s.arg(day->top()).arg(row->bottom())));
}

void TestAtramentJournal::mouseDragAcrossDaysScrollsTheStream()
{
    Harness h(14);
    PageController *first = h.day(1);
    // A press on day 1's first block dragged to its second one, then into the next day.
    const at::Point from = h.pointOf(1, 0, 2);
    h.tw.simulateMouseMove(from);
    h.tw.simulateMouseDown(from);
    h.tw.simulateMouseMove(h.pointOf(1, 1, 2), at::MouseButton::Left);
    QCOMPARE(first->selectionCount(), 2);
    h.tw.settle();
    h.tw.simulateMouseMove(h.pointOf(2, 1, 2), at::MouseButton::Left);
    h.tw.settle();
    QVERIFY(h.stream.active());
    QCOMPARE(h.focusDay(), 2);
    // Held at the view's bottom edge (and past it, out of the window): the stream scrolls, the
    // selection's end follows into the days that come in.
    const float before = h.scrollY();
    h.tw.simulateMouseMove({300, 720}, at::MouseButton::Left);
    h.tw.advance(800ms);
    h.tw.settle();
    QVERIFY2(h.scrollY() > before + 300, qPrintable(QString::number(h.scrollY() - before)));
    QVERIFY2(h.focusDay() > 3, qPrintable(QString::number(h.focusDay())));
    QVERIFY(h.stream.active());
    QVERIFY(!h.dayBounds(1) || h.dayBounds(1)->bottom() < 0); // the day it began in scrolled away
    // Let go there: the drag ends, nothing is edited, the span stays; nothing scrolls any more.
    h.tw.simulateMouseUp({300, 720});
    h.tw.settle();
    QVERIFY(h.settledAfterDrag());
    QVERIFY(h.stream.active());
    const float after = h.scrollY();
    h.tw.advance(200ms);
    QCOMPARE(h.scrollY(), after);
}

void TestAtramentJournal::fingerSelectionAcrossDaysScrolls()
{
    Harness h(14);
    PageController *first = h.day(1);
    // A finger held on a block: the touch selection mode with it selected (BlockEditor.longPressText).
    h.view().page(1)->update(h.app, [first](PageView &v, at::Context<PageView> &) {
        v.onLongPressRow = [first](int row) { first->selectRange(row, row); };
    });
    h.tw.simulateTouchDown(h.pointOf(1, 1, 2));
    h.tw.advance(600ms);
    QCOMPARE(first->selectedRow(), 1);
    QCOMPARE(h.page(1)->dragKind(), PageView::DragKind::Extend);
    // The finger on into the next day, then held at the bottom edge.
    h.tw.simulateTouchMove(h.pointOf(2, 0, 2));
    h.tw.settle();
    QVERIFY(h.stream.active());
    QCOMPARE(h.focusDay(), 2);
    const float before = h.scrollY();
    h.tw.simulateTouchMove({300, 698});
    h.tw.advance(800ms);
    h.tw.settle();
    QVERIFY2(h.scrollY() > before + 300, qPrintable(QString::number(h.scrollY() - before)));
    QVERIFY2(h.focusDay() > 3, qPrintable(QString::number(h.focusDay())));
    h.tw.simulateTouchUp({300, 698});
    h.tw.settle();
    QVERIFY(h.settledAfterDrag());
    QVERIFY(h.stream.active());
}

void TestAtramentJournal::blockMovedIntoAnotherDay()
{
    Harness h(4);
    PageController *first = h.day(1);
    PageController *second = h.day(2);
    const QString firstBefore = first->sourceText();
    const QString secondBefore = second->sourceText();
    // Day 1's first block by its bullet into day 2, between its first and second blocks: the drop
    // line shows there, not in the day it comes from.
    const at::Point bullet = h.bulletOf(1, 0);
    h.tw.simulateMouseMove(bullet);
    h.tw.simulateMouseDown(bullet);
    h.tw.simulateMouseMove({bullet.x, bullet.y + 10}, at::MouseButton::Left);
    QCOMPARE(h.page(1)->dragKind(), PageView::DragKind::Move);
    const at::Point there{bullet.x, h.view().rowBounds(2, 1)->top() + 2};
    h.tw.simulateMouseMove(there, at::MouseButton::Left);
    h.tw.settle();
    QVERIFY(!h.page(1)->dropTarget());
    QCOMPARE(h.incomingDropDay(), 2);
    QCOMPARE(h.page(2)->dropTarget()->place, (BlockPlace{1, 0}));
    QVERIFY(h.page(2)->dropTarget()->line.width() > 0); // drawn there
    // Let go: the Markdown moves from day 1's file to day 2's, the block selected where it went.
    h.tw.simulateMouseUp(there);
    h.tw.settle();
    QCOMPARE(first->sourceText(), firstBefore.mid(firstBefore.indexOf(u'\n') + 1));
    QCOMPARE(second->sourceText(), secondBefore.left(secondBefore.indexOf(u'\n') + 1)
                                           + firstBefore.left(firstBefore.indexOf(u'\n') + 1)
                                           + secondBefore.mid(secondBefore.indexOf(u'\n') + 1));
    QCOMPARE(second->selectedRow(), 1);
    QCOMPARE(first->selectionCount(), 0);
    QCOMPARE(h.view().activeDay(), 2);
    QCOMPARE(h.incomingDropDay(), -1);
    QVERIFY(h.settledAfterDrag());
    // One Ctrl+Z takes it back to where it was in both days, Ctrl+Y moves it again.
    QVERIFY(h.tw.simulateKeystrokes(u"ctrl-z"));
    h.tw.settle();
    QCOMPARE(first->sourceText(), firstBefore);
    QCOMPARE(second->sourceText(), secondBefore);
    QVERIFY(h.tw.simulateKeystrokes(u"ctrl-y"));
    h.tw.settle();
    QVERIFY(!first->sourceText().contains(u"Dzień 1"_s));
    QVERIFY(second->sourceText().contains(u"Dzień 1"_s));
}

void TestAtramentJournal::blockDraggedPastTheEdgeLandsInADayScrolledIn()
{
    Harness h(14);
    PageController *first = h.day(1);
    const at::Point bullet = h.bulletOf(1, 0);
    h.tw.simulateMouseMove(bullet);
    h.tw.simulateMouseDown(bullet);
    h.tw.simulateMouseMove({bullet.x, bullet.y + 10}, at::MouseButton::Left);
    // Held at the stream's bottom edge: it scrolls, the drop line goes on into the days coming in.
    const float before = h.scrollY();
    h.tw.simulateMouseMove({bullet.x, 698}, at::MouseButton::Left);
    h.tw.advance(700ms);
    h.tw.settle();
    QVERIFY2(h.scrollY() > before + 300, qPrintable(QString::number(h.scrollY() - before)));
    const int target = h.incomingDropDay();
    QVERIFY2(target > 3, qPrintable(QString::number(target)));
    PageController *to = h.day(target);
    h.tw.simulateMouseUp({bullet.x, 698});
    h.tw.settle();
    QVERIFY(!first->sourceText().contains(u"Dzień 1:"_s));
    QVERIFY(to->sourceText().contains(u"Dzień 1: zakupy, mleko i chleb"_s));
    QVERIFY(h.settledAfterDrag());
}

QTEST_MAIN(TestAtramentJournal)
#include "tst_atrament_journal.moc"

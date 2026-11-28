// The Calendar view's agenda drawn by Atrament (src/app/views/atrament/agendaview.h; docs/ATRAMENT.md
// "The views in Atrament (M3)", Calendar): days of CalendarModel's roles (a QStandardItemModel with
// them here: the view reads nothing else) drawn headless on Atrament's test platform — a day's disc
// (today's, the selected day's, a past day's), its title and relative words, its "Journal" button,
// its entries as PlanningItemRow shows them or "Nothing planned."; every click's and right-click's
// request; ↑ / ↓ / Enter; the days following the model; a day shown at the top.
#include "agendaview.h"
#include "calendarmodel.h"
#include "pageicons.h"
#include "taskbox.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QStandardItemModel>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::views::atr;
using brulion::planning::CalendarModel;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

struct Day {
    QString date, longLabel;
    bool today = false, past = false, selected = false, journal = false;
    QVariantList entries;
};

QStandardItem *day(const Day &d)
{
    auto *item = new QStandardItem;
    item->setData(d.date, CalendarModel::DateRole);
    item->setData(d.longLabel, CalendarModel::LongLabelRole);
    item->setData(d.date.right(2).toInt(), CalendarModel::DayRole);
    item->setData(d.today, CalendarModel::IsTodayRole);
    item->setData(d.past, CalendarModel::IsPastRole);
    item->setData(d.selected, CalendarModel::IsSelectedRole);
    item->setData(d.journal, CalendarModel::HasJournalRole);
    item->setData(d.entries, CalendarModel::ItemsRole);
    return item;
}

QVariantMap entry(const QString &title, const QVariantMap &more)
{
    QVariantMap e{{u"title"_s, title}, {u"path"_s, u"pages/Remont.md"_s}, {u"line"_s, 3}, {u"pageTitle"_s, u"Remont"_s}};
    for (auto it = more.cbegin(); it != more.cend(); ++it)
        e.insert(it.key(), it.value());
    return e;
}

struct Harness {
    QStandardItemModel model;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<AgendaView> view;
    QStringList actions;
    QVariantMap last;

    explicit Harness(float width = 700) : tw(platform->newWindow({width, 800}, 1))
    {
        model.appendRow(day({u"2026-10-13"_s, u"wtorek, 13 października"_s, false, true, false, false,
                             {entry(u"Wymienić żarówkę"_s, {{u"kind"_s, u"done"_s}, {u"variant"_s, u"done"_s},
                                                            {u"isTask"_s, true}, {u"status"_s, u"done"_s},
                                                            {u"closed"_s, true}, {u"spec"_s, u"2026-10-13"_s}})}}));
        model.appendRow(day({u"2026-10-14"_s, u"środa, 14 października"_s, true, true, true, true,
                             {entry(u"Zadzwonić do fachowca"_s, {{u"kind"_s, u"reminder"_s}, {u"variant"_s, u"alarm"_s},
                                                                 {u"time"_s, u"09:00"_s}, {u"icon"_s, u"bell"_s},
                                                                 {u"spec"_s, u"2026-10-14 09:00"_s}}),
                              entry(u"Kupić fugę"_s, {{u"kind"_s, u"due"_s}, {u"variant"_s, u"due"_s}, {u"isTask"_s, true},
                                                      {u"status"_s, u"todo"_s}, {u"recurring"_s, true},
                                                      {u"spec"_s, u"2026-10-14"_s}}),
                              entry(u"Spotkanie zespołu"_s, {{u"kind"_s, u"event"_s}, {u"variant"_s, u"event"_s},
                                                             {u"time"_s, u"13:00"_s}, {u"endTime"_s, u"14:00"_s},
                                                             {u"color"_s, u"#2f9e44"_s}, {u"calendarName"_s, u"Praca"_s},
                                                             {u"location"_s, u"Biuro"_s}})}}));
        model.appendRow(day({u"2026-10-15"_s, u"czwartek, 15 października"_s, false, false, false, true, {}}));
        model.appendRow(day({u"2026-10-16"_s, u"piątek, 16 października"_s, false, false, false, false, {}}));
        view = app.newEntity<AgendaView>(&model);
        view.update(app, [this](AgendaView &v, at::Context<AgendaView> &) {
            v.formatHuman = [](const QString &date) {
                return date == u"2026-10-15" ? u"jutro"_s : date == u"2026-10-13" ? u"wczoraj"_s : u"16 paź"_s;
            };
            v.journalDay = [](const QString &) { return u"13 paź"_s; }; // (asked of daily notes only)
            v.onAction = [this](const QString &action, const QVariantMap &args) {
                QStringList parts{action};
                for (auto it = args.cbegin(); it != args.cend(); ++it)
                    if (it.key() == u"entry")
                        parts.append(u"entry="_s + it.value().toMap().value(u"title"_s).toString());
                    else if (it.key() != u"x" && it.key() != u"y")
                        parts.append(it.key() + u'=' + it.value().toString());
                actions.append(parts.join(u' '));
                last = args;
            };
        });
        app.openWindow(tw, view);
        window().setAccessibilityEnabled(true);
        tw.settle();
    }
    const AgendaView &agenda() { return view.read(app); }
    at::Window &window() { return *app.windows().front(); }
    at::Rect part(int row, AgendaView::Part p) { return agenda().partBounds(row, p).value_or(at::Rect{}); }
    at::Rect part(int row, AgendaView::Part::Kind k, int entry = 0) { return part(row, AgendaView::Part{k, entry}); }
    QString dump() { return str(window().accessibilityTree().dump()); }
    int quads(at::Color c)
    {
        int n = 0;
        for (const at::Primitive &p : tw.lastScene()->primitives())
            if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == c)
                ++n;
        return n;
    }
    void focus()
    {
        view.update(app, [&](AgendaView &v, at::Context<AgendaView> &) { window().focus(v.focusHandle()); });
        tw.settle();
    }
};

using P = AgendaView::Part;

} // namespace

class TestAtramentAgenda : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { editor::atr::setPageIconFolder(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons")); }
    void daysDrawn();
    void discs();
    void headerAndJournalClicks();
    void entryClicksAndMenus();
    void keysMoveAndPick();
    void kindLabels_data();
    void kindLabels();
    void subtitles();
    void narrowRowsHaveNoChips();
    void dayShownAtTheTop();
    void laidOutAsTheQmlList();
    void daysFollowTheModel();
    void lookFromItsMap();
    void partsByName();
};

// Each day a row: its title (capitalized) as a button (", today" on today's), "Today" / "Jutro" /
// "Wczoraj" under it (formatHuman's one-word answers only), the "Journal" button of a day with a daily note,
// its entries as list items named by their time, title, kind and place; "Nothing planned." or
// "Only a journal entry." for a day without entries.
void TestAtramentAgenda::daysDrawn()
{
    Harness h;
    QCOMPARE(h.agenda().drawnRows(), 4);
    const QString dump = h.dump();
    for (const QString &text :
         {u"Wtorek, 13 października"_s, u"Środa, 14 października, today"_s, u"Czwartek, 15 października"_s,
          u"09:00, Zadzwonić do fachowca, Alarm, Remont"_s, u"Kupić fugę, Due, Remont"_s,
          u"13:00, Spotkanie zespołu, until 14:00 · Praca · Biuro"_s, u"Wymienić żarówkę, Done, Remont"_s,
          u"Only a journal entry."_s, u"Nothing planned."_s, u"Open the journal of środa, 14 października"_s,
          u"Done: Kupić fugę"_s})
        QVERIFY2(dump.contains(text), qPrintable(text + u" in "_s + dump));
    // Under the title: "Today", formatHuman's one-word answers, not one with a space (a date).
    const AgendaView &v = h.agenda();
    QCOMPARE(v.relativeWords(v.rowData(1)), u"Today"_s);
    QCOMPARE(v.relativeWords(v.rowData(2)), u"Jutro"_s);
    QCOMPARE(v.relativeWords(v.rowData(0)), u"Wczoraj"_s);
    QCOMPARE(v.relativeWords(v.rowData(3)), QString());
    QVERIFY(h.agenda().partBounds(1, {P::Journal}));
    QVERIFY(!h.agenda().partBounds(0, {P::Journal}));
    for (int r = 1; r < 4; ++r)
        QVERIFY(h.part(r, P::Day).top() > h.part(r - 1, P::Day).bottom());
    // The days' column at most maxWidth wide, centred.
    Harness wide(1200);
    const at::Rect column = wide.part(0, P::Day);
    QCOMPARE(column.width(), wide.agenda().look().maxWidth);
    QCOMPARE(column.left(), (1200 - column.width()) / 2);
}

// Today's disc red, the selected day's dark (today's stays red when it is selected), another day's
// sunken, a past day's only outlined; the alarm's icon in the reminder's colour, the event's dot in
// its calendar's.
void TestAtramentAgenda::discs()
{
    Harness h;
    const AgendaLook &look = h.agenda().look();
    QCOMPARE(h.quads(look.today), 1);
    QCOMPARE(h.quads(look.selected), 0);
    QCOMPARE(h.quads(look.disc), 2);
    QCOMPARE(h.quads(at::rgb(0x2f9e44)), 1);
    h.model.item(1)->setData(false, CalendarModel::IsSelectedRole);
    h.model.item(2)->setData(true, CalendarModel::IsSelectedRole);
    h.tw.settle();
    QCOMPARE(h.quads(look.selected), 1);
    QCOMPARE(h.quads(look.disc), 1);
    // The past day selected: its disc dark, not outlined.
    h.model.item(2)->setData(false, CalendarModel::IsSelectedRole);
    h.model.item(0)->setData(true, CalendarModel::IsSelectedRole);
    h.tw.settle();
    QCOMPARE(h.quads(look.selected), 1);
    QCOMPARE(h.quads(look.disc), 2);
}

// A header's click picks its day; the "Journal" button opens the daily note (and picks nothing).
void TestAtramentAgenda::headerAndJournalClicks()
{
    Harness h;
    h.tw.simulateClick(h.part(2, P::Header).center());
    h.tw.simulateClick(h.part(1, P::Journal).center());
    QCOMPARE(h.actions, (QStringList{u"day date=2026-10-15"_s, u"journal date=2026-10-14"_s}));
}

// An entry's click opens it; a task's box toggles it (only that), its right-click asks for its
// status menu at the pointer; a right-click on an entry with a token its date's menu, on an event
// (no token) nothing.
void TestAtramentAgenda::entryClicksAndMenus()
{
    Harness h;
    h.tw.simulateClick(h.part(1, P::Entry, 0).center());
    h.tw.simulateClick(h.part(1, P::Box, 1).center());
    QCOMPARE(h.actions, (QStringList{u"open entry=Zadzwonić do fachowca"_s, u"toggle entry=Kupić fugę"_s}));
    h.actions.clear();
    const at::Point onBox = h.part(1, P::Box, 1).center();
    h.tw.simulateClick(onBox, at::MouseButton::Right);
    QCOMPARE(h.actions, QStringList{u"statusMenu entry=Kupić fugę"_s});
    QCOMPARE(h.last.value(u"x"_s).toReal(), qreal(onBox.x));
    QCOMPARE(h.last.value(u"y"_s).toReal(), qreal(onBox.y));
    QCOMPARE(h.last.value(u"entry"_s).toMap().value(u"line"_s).toInt(), 3);
    h.actions.clear();
    const at::Rect alarm = h.part(1, P::Entry, 0);
    h.tw.simulateClick({alarm.right() - 4, alarm.top() + 4}, at::MouseButton::Right);
    h.tw.simulateClick(h.part(1, P::Entry, 2).center(), at::MouseButton::Right);
    QCOMPARE(h.actions, QStringList{u"menu entry=Zadzwonić do fachowca"_s});
    QCOMPARE(h.last.value(u"x"_s).toReal(), qreal(alarm.right() - 4));
}

// ↓ from the selected day (today) makes the next one current, its header ringed; ↑ goes back, and
// stops at the first; Enter picks the current day; a click on a header leaves the keys' day.
void TestAtramentAgenda::keysMoveAndPick()
{
    Harness h;
    const auto ringed = [&h] {
        int ring = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *q = std::get_if<at::Quad>(&p); q && q->borderColor == h.agenda().look().focus && q->borderWidths.left > 1)
                ring += h.part(2, P::Header).contains(q->bounds.center()) ? 1 : 100;
        return ring;
    };
    QCOMPARE(ringed(), 0); // (no focus: no ring)
    h.focus();
    QCOMPARE(h.agenda().currentRow(), -1);
    h.tw.simulateKeystrokes(u"enter");
    QCOMPARE(h.actions, QStringList{u"day date=2026-10-14"_s});
    h.tw.simulateKeystrokes(u"down");
    QCOMPARE(h.agenda().currentRow(), 2);
    QCOMPARE(ringed(), 1);
    h.tw.simulateKeystrokes(u"enter");
    QCOMPARE(h.actions.last(), u"day date=2026-10-15"_s);
    for (int k = 0; k < 4; ++k)
        h.tw.simulateKeystrokes(u"up");
    QCOMPARE(h.agenda().currentRow(), 0);
    h.tw.simulateKeystrokes(u"enter"); // the first day current: it, not the selected one
    QCOMPARE(h.actions.last(), u"day date=2026-10-13"_s);
    for (const int expected : {1, 2, 3, 3}) {
        h.tw.simulateKeystrokes(u"down");
        QCOMPARE(h.agenda().currentRow(), expected);
    }
    h.tw.simulateClick(h.part(0, P::Header).center());
    QCOMPARE(h.agenda().currentRow(), -1);
    // Keys with a modifier, and others, are not the agenda's.
    h.actions.clear();
    QVERIFY(!h.tw.simulateKeystrokes(u"ctrl-enter"));
    QVERIFY(!h.tw.simulateKeystrokes(u"a"));
    QVERIFY(h.actions.isEmpty());
    // No day selected and none current: Enter picks nothing; no days: ↓ moves nothing.
    h.model.item(1)->setData(false, CalendarModel::IsSelectedRole);
    h.tw.settle();
    QVERIFY(!h.tw.simulateKeystrokes(u"enter"));
    QVERIFY(h.actions.isEmpty());
    h.model.clear();
    h.tw.settle();
    QVERIFY(!h.tw.simulateKeystrokes(u"down"));
    QCOMPARE(h.agenda().currentRow(), -1);
}

void TestAtramentAgenda::kindLabels_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<QString>("variant");
    QTest::addColumn<QString>("label");
    QTest::newRow("overdue") << u"due"_s << u"overdue"_s << u"Overdue"_s;
    QTest::newRow("alarm") << u"reminder"_s << u"alarm"_s << u"Alarm"_s;
    QTest::newRow("reminder") << u"reminder"_s << u"reminder"_s << u"Reminder"_s;
    QTest::newRow("scheduled") << u"scheduled"_s << u"scheduled"_s << u"Scheduled"_s;
    QTest::newRow("done") << u"done"_s << u"done"_s << u"Done"_s;
    QTest::newRow("finished: a due date's") << u"due"_s << u"done"_s << u"Finished"_s;
    QTest::newRow("due") << u"due"_s << u"due"_s << u"Due"_s;
    QTest::newRow("starts") << u"start"_s << u"neutral"_s << u"Starts"_s;
    QTest::newRow("journal") << u"journal"_s << u""_s << u"Journal"_s;
    QTest::newRow("event") << u"event"_s << u"event"_s << QString();
}

void TestAtramentAgenda::kindLabels()
{
    QFETCH(QString, kind);
    QFETCH(QString, variant);
    QFETCH(QString, label);
    QCOMPARE(AgendaEntry::from({{u"kind"_s, kind}, {u"variant"_s, variant}}).kindLabel(), label);
}

// Where an entry is: a daily note's "Journal · <its day>", a page's title; an event's end
// ("continues" on its later days), calendar and place.
void TestAtramentAgenda::subtitles()
{
    Harness h;
    const AgendaView &v = h.agenda();
    QCOMPARE(v.subtitle(AgendaEntry::from(entry(u"x"_s, {}))), u"Remont"_s);
    QCOMPARE(v.subtitle(AgendaEntry::from(entry(u"x"_s, {{u"isJournalPage"_s, true}, {u"path"_s, u"journals/2026_10_13.md"_s}}))),
             u"Journal · 13 paź"_s);
    QCOMPARE(v.subtitle(AgendaEntry::from({{u"kind"_s, u"event"_s}, {u"continued"_s, true}, {u"calendarName"_s, u"Praca"_s}})),
             u"continues · Praca"_s);
    QCOMPARE(v.subtitle(AgendaEntry::from({{u"kind"_s, u"event"_s}, {u"location"_s, u"Biuro"_s}})), u"Biuro"_s);
    QVERIFY(AgendaEntry::from({{u"spec"_s, u"2026-10-14"_s}}).hasTokenMenu());
    QVERIFY(!AgendaEntry::from({{u"spec"_s, u"2026-10-14"_s}, {u"kind"_s, u"journal"_s}}).hasTokenMenu());
    QCOMPARE(AgendaEntry::from({}).taskStatus(), u"todo"_s);
}

// A row narrower than chipMinWidth shows no chips (PlanningItemRow); a wide one each entry's with a
// kind: the alarm's, the due task's (with its repeat icon), the done one's.
void TestAtramentAgenda::narrowRowsHaveNoChips()
{
    AgendaLook look;
    look.chips[u"alarm"_s] = {at::rgb(0xfff3bf), at::rgb(0x8f5f00)};
    look.chips[u"neutral"_s] = {at::rgb(0xf1f3f5), at::rgb(0x495057)};
    // (333 px: the days 301 px wide inside the side margins; 330: 298.)
    for (const float width : {700.0f, 333.0f, 330.0f, 300.0f}) {
        Harness h(width);
        h.view.update(h.app, [&look](AgendaView &v, at::Context<AgendaView> &cx) { v.setLook(look, cx); });
        h.tw.settle();
        QCOMPARE(h.quads(at::rgb(0xfff3bf)), width > 332 ? 1 : 0);
        QCOMPARE(h.quads(at::rgb(0xf1f3f5)), width > 332 ? 2 : 0);
    }
    // A day's title and an entry's place stay on one line, however narrow.
    Harness wide, narrow(200);
    QCOMPARE(narrow.part(1, P::Header).height(), wide.part(1, P::Header).height());
    const float oneLine = narrow.part(0, P::Entry, 0).height();
    QVariantMap longPlace = narrow.model.item(0)->data(CalendarModel::ItemsRole).toList().value(0).toMap();
    longPlace[u"pageTitle"_s] = u"Bardzo długi tytuł strony, który się nie mieści"_s;
    narrow.model.item(0)->setData(QVariantList{longPlace}, CalendarModel::ItemsRole);
    narrow.tw.settle();
    QCOMPARE(narrow.part(0, P::Entry, 0).height(), oneLine);
}

// showRow() puts a day at the view's top (the agenda scrolled to today), not making it current.
void TestAtramentAgenda::dayShownAtTheTop()
{
    Harness h;
    for (int k = 0; k < 30; ++k)
        h.model.appendRow(day({u"2026-11-%1"_s.arg(k + 1, 2, 10, QChar(u'0')), u"listopad"_s, false, false, false, false,
                               {entry(u"Zadanie %1"_s.arg(k), {{u"kind"_s, u"due"_s}})}}));
    h.tw.settle();
    QVERIFY(!h.agenda().partBounds(20, {P::Day}));
    h.view.update(h.app, [](AgendaView &v, at::Context<AgendaView> &cx) { v.showRow(20, cx); });
    h.tw.settle();
    QVERIFY(h.agenda().partBounds(20, {P::Day}));
    QVERIFY(!h.agenda().partBounds(0, {P::Day}));
    QVERIFY2(h.part(20, P::Day).top() <= 8.5f, qPrintable(QString::number(h.part(20, P::Day).top())));
    QCOMPARE(h.agenda().currentRow(), -1);
    // A row out of the model: nothing moves; the first row: back at the top.
    const int count = h.model.rowCount();
    h.view.update(h.app, [count](AgendaView &v, at::Context<AgendaView> &cx) {
        v.showRow(count, cx);
        v.showRow(-1, cx);
    });
    h.tw.settle();
    QVERIFY(h.part(20, P::Day).top() <= 8.5f);
    h.view.update(h.app, [](AgendaView &v, at::Context<AgendaView> &cx) { v.showRow(0, cx); });
    h.tw.settle();
    QVERIFY(h.part(0, P::Day).top() <= 8.5f);
}

// The rows laid out as CalendarAgendaList's: the entries 4 px under the header, a task's box in its
// 24 px column 6 px in (centred), the separator 8 px under the last entry; an entry with a place is
// taller than one without (the least height); the alarm's icon in the reminder's colour; a done
// task's box filled, a check in it wider than tall, a cancelled one's cross square.
void TestAtramentAgenda::laidOutAsTheQmlList()
{
    Harness h;
    const AgendaLook &look = h.agenda().look();
    const auto paths = [&h](at::Rect in, at::Color c) {
        std::vector<at::Rect> out;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *path = std::get_if<at::PathPrimitive>(&p); path && path->color == c && in.contains(path->bounds.center()))
                out.push_back(path->bounds);
        return out;
    };
    const auto glyphs = [&h](at::Rect in, at::Color c) {
        int n = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->color == c && in.contains(g->bounds.center()))
                ++n;
        return n;
    };
    QCOMPARE(h.part(1, P::Entry, 0).top() - h.part(1, P::Header).bottom(), 4.0f);
    // (The box's part inside its 1.5 px border.)
    QCOMPARE(h.part(1, P::Box, 1).left() - h.part(1, P::Entry, 1).left(), 6 + (24 - kTaskBoxSize) / 2 + 1.5f);
    QCOMPARE(h.part(1, P::Day).bottom() - h.part(1, P::Entry, 2).bottom(), 8.0f + 1);
    QVERIFY(h.part(1, P::Entry, 1).height() > look.rowHeight);
    // A reminder's icon in its colour, another entry's muted; a title muted when its entry is closed
    // or its day past, else in the text's colour.
    h.model.item(3)->setData(QVariantList{entry(u"Bez miejsca"_s, {{u"pageTitle"_s, QString()}}),
                                          entry(u"Przypomnienie"_s, {{u"variant"_s, u"reminder"_s}, {u"pageTitle"_s, QString()}}),
                                          entry(u"Zamknięte"_s, {{u"closed"_s, true}, {u"pageTitle"_s, QString()}})},
                             CalendarModel::ItemsRole);
    h.tw.settle();
    QCOMPARE(h.part(3, P::Entry, 0).height(), look.rowHeight);
    QVERIFY(!paths(h.part(3, P::Entry, 0), look.muted).empty());
    QVERIFY(paths(h.part(3, P::Entry, 0), look.reminder).empty());
    QVERIFY(!paths(h.part(3, P::Entry, 1), look.reminder).empty());
    QVERIFY(glyphs(h.part(3, P::Entry, 0), look.text) > 0);
    QCOMPARE(glyphs(h.part(3, P::Entry, 2), look.text), 0);
    h.model.item(3)->setData(true, CalendarModel::IsPastRole);
    h.tw.settle();
    QCOMPARE(glyphs(h.part(3, P::Entry, 0), look.text), 0);
    QVERIFY(!paths(h.part(1, P::Entry, 0), look.reminder).empty());
    QVERIFY(paths(h.part(1, P::Entry, 0), look.muted).empty());
    // The event's dot: 10 px.
    int dots = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == at::rgb(0x2f9e44))
            dots += q->bounds.width() == 10 && q->bounds.height() == 10 ? 1 : 100;
    QCOMPARE(dots, 1);
    // "Today" under today's date in its colour.
    QCOMPARE(glyphs(h.part(1, P::Header), look.todayText), 5);
    QCOMPARE(h.quads(look.accent), 1); // the done task's box
    const std::vector<at::Rect> check = paths(h.part(0, P::Box, 0), look.background);
    QVERIFY(!check.empty());
    QVERIFY2(check.front().width() > check.front().height() * 1.2f, qPrintable(QString::number(check.front().height())));
    QVariantMap cancelled = entry(u"Odwołane"_s, {{u"isTask"_s, true}, {u"status"_s, u"cancelled"_s}, {u"closed"_s, true}});
    h.model.item(0)->setData(QVariantList{cancelled}, CalendarModel::ItemsRole);
    h.tw.settle();
    const std::vector<at::Rect> cross = paths(h.part(0, P::Box, 0), look.background);
    QVERIFY(!cross.empty());
    QVERIFY(std::abs(cross.front().width() - cross.front().height()) < 1.5f);
}

// A day's entries changed, a day gone before the current one and then the current one: the rows
// follow, the current day with its row.
void TestAtramentAgenda::daysFollowTheModel()
{
    Harness h;
    h.focus();
    h.tw.simulateKeystrokes(u"down");
    QCOMPARE(h.agenda().currentRow(), 2);
    h.model.item(3)->setData(QVariantList{entry(u"Odebrać paczkę"_s, {{u"kind"_s, u"due"_s}})}, CalendarModel::ItemsRole);
    h.tw.settle();
    QVERIFY2(h.dump().contains(u"Odebrać paczkę"_s), qPrintable(h.dump()));
    QVERIFY(h.part(3, P::Entry, 0).height() > 0);
    h.model.removeRow(0);
    h.tw.settle();
    QCOMPARE(h.agenda().drawnRows(), 3);
    // The current day stays current at its new row; gone, none is.
    QCOMPARE(h.agenda().currentRow(), 1);
    QCOMPARE(h.agenda().selectedRow(), 0);
    h.model.removeRow(1);
    h.tw.settle();
    QCOMPARE(h.agenda().currentRow(), -1);
    QCOMPARE(h.agenda().rowData(7).date, QString());
}

// The look from AtramentView.viewLook: colours and sizes by name, the chips' colours by variant;
// what the map lacks keeps its default, a variant missing takes the neutral one's.
void TestAtramentAgenda::lookFromItsMap()
{
    const AgendaLook look = AgendaLook::fromMap(
            {{u"today"_s, QColor(u"#ff0000"_s)},
             {u"marker"_s, 40},
             {u"uiFamily"_s, u"Brulion Inter"_s},
             {u"chips"_s, QVariantMap{{u"overdue"_s, QVariantMap{{u"background"_s, QColor(u"#ffe3e3"_s)}, {u"text"_s, QColor(u"#c92a2a"_s)}}},
                                      {u"neutral"_s, QVariantMap{{u"background"_s, QColor(u"#f1f3f5"_s)}}}}}});
    QCOMPARE(look.today, at::rgb(0xff0000));
    QCOMPARE(look.marker, 40.0f);
    QCOMPARE(look.text, AgendaLook{}.text);
    QVERIFY(look.uiFamily == u"Brulion Inter");
    QCOMPARE(look.chip(u"overdue"_s).background, at::rgb(0xffe3e3));
    QCOMPARE(look.chip(u"overdue"_s).text, at::rgb(0xc92a2a));
    QCOMPARE(look.chip(u"due"_s).background, at::rgb(0xf1f3f5));
    QCOMPARE(look.chip(u"due"_s).text, AgendaLook::ChipColors{}.text);
    QCOMPARE(AgendaLook{}.chip(u"due"_s).background, AgendaLook::ChipColors{}.background);
}

void TestAtramentAgenda::partsByName()
{
    QVERIFY((AgendaView::partNamed(u"header"_s) == P{P::Header, 0}));
    QVERIFY((AgendaView::partNamed(u"journal"_s) == P{P::Journal, 0}));
    QVERIFY((AgendaView::partNamed(u"entry 2"_s) == P{P::Entry, 2}));
    QVERIFY((AgendaView::partNamed(u"box 1"_s) == P{P::Box, 1}));
    QVERIFY((AgendaView::partNamed(u"day"_s) == P{P::Day, 0}));
    QVERIFY((AgendaView::partNamed(QString()) == P{P::Day, 0}));
}

QTEST_GUILESS_MAIN(TestAtramentAgenda)
#include "tst_atrament_agenda.moc"

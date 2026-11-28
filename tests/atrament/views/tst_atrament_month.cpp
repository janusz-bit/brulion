// The Calendar view's month grid drawn by Atrament (src/app/views/atrament/monthview.h; docs/ATRAMENT.md
// "The views in Atrament (M3)", Calendar): 42 days of CalendarModel's roles (a QStandardItemModel
// with them here) drawn headless on Atrament's test platform — the weekday names, the week numbers,
// the days' discs, the pills with their times in wide cells and "+N more", the icons of narrow
// cells; every click's and key's request.
#include "calendarmodel.h"
#include "monthview.h"
#include "pageicons.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QDate>
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

QVariantMap entry(const QString &title, const QVariantMap &more = {})
{
    QVariantMap e{{u"title"_s, title}, {u"variant"_s, u"due"_s}};
    for (auto it = more.cbegin(); it != more.cend(); ++it)
        e.insert(it.key(), it.value());
    return e;
}

// October 2026's grid from Monday 28 September; today the 14th, selected; entries on some days.
void fill(QStandardItemModel &model, const QDate &selected = QDate(2026, 10, 14))
{
    model.clear();
    const QDate first(2026, 9, 28);
    for (int k = 0; k < 42; ++k) {
        const QDate d = first.addDays(k);
        auto *item = new QStandardItem;
        item->setData(d.toString(Qt::ISODate), CalendarModel::DateRole);
        item->setData(QLocale(QLocale::Polish).toString(d, u"dddd, d MMMM"_s), CalendarModel::LongLabelRole);
        item->setData(d.day(), CalendarModel::DayRole);
        item->setData(d.dayOfWeek(), CalendarModel::DayOfWeekRole);
        item->setData(d.weekNumber(), CalendarModel::WeekNumberRole);
        item->setData(d.month() == 10, CalendarModel::InMonthRole);
        item->setData(d == QDate(2026, 10, 14), CalendarModel::IsTodayRole);
        item->setData(d == selected, CalendarModel::IsSelectedRole);
        item->setData(d == QDate(2026, 10, 14) || d == QDate(2026, 10, 2), CalendarModel::HasJournalRole);
        QVariantList items;
        QStringList kinds;
        if (d == QDate(2026, 10, 14)) {
            items = {entry(u"Spotkanie"_s, {{u"time"_s, u"09:00"_s}, {u"variant"_s, u"alarm"_s}}),
                     entry(u"Kupić fugę"_s, {{u"recurring"_s, true}}), entry(u"Wymienić żarówkę"_s, {{u"closed"_s, true}, {u"variant"_s, u"done"_s}})};
            kinds = {u"reminder"_s, u"due"_s};
        } else if (d == QDate(2026, 10, 20)) {
            for (int n = 0; n < 6; ++n)
                items.append(entry(u"Zadanie %1"_s.arg(n)));
            kinds = {u"due"_s};
        } else if (d == QDate(2026, 10, 22)) {
            items = {entry(u"Odwołane"_s, {{u"closed"_s, true}, {u"status"_s, u"cancelled"_s}, {u"variant"_s, u"done"_s}}),
                     entry(u"Konferencja"_s, {{u"variant"_s, u"event"_s}, {u"color"_s, u"#2f9e44"_s}})};
        }
        item->setData(items, CalendarModel::OrderedItemsRole);
        item->setData(int(items.size()), CalendarModel::ItemCountRole);
        item->setData(kinds, CalendarModel::OpenKindsRole);
        model.appendRow(item);
    }
}

struct Harness {
    QStandardItemModel model;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<MonthView> view;
    QStringList actions;
    QList<int> named;

    explicit Harness(at::Size size = {900, 700}) : tw(platform->newWindow(size, 1))
    {
        fill(model);
        view = app.newEntity<MonthView>(&model);
        view.update(app, [this](MonthView &v, at::Context<MonthView> &) {
            v.weekdayName = [this](int dow) {
                named.append(dow);
                return QStringList{u"pon"_s, u"wt"_s, u"śr"_s, u"czw"_s, u"pt"_s, u"sob"_s, u"nie"_s}.value(dow - 1);
            };
            v.onAction = [this](const QString &action, const QVariantMap &args) {
                QStringList parts{action};
                for (auto it = args.cbegin(); it != args.cend(); ++it)
                    parts.append(it.key() + u'='
                                 + (it.key() == u"entry" ? it.value().toMap().value(u"title"_s).toString() : it.value().toString()));
                actions.append(parts.join(u' '));
            };
        });
        app.openWindow(tw, view);
        window().setAccessibilityEnabled(true);
        tw.settle();
    }
    const MonthView &month() { return view.read(app); }
    at::Window &window() { return *app.windows().front(); }
    at::Rect part(int row, MonthView::Part::Kind k, int index = 0)
    {
        return month().partBounds(row, MonthView::Part{k, index}).value_or(at::Rect{});
    }
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
        view.update(app, [&](MonthView &v, at::Context<MonthView> &) { window().focus(v.focusHandle()); });
        tw.settle();
    }
};

constexpr int kToday = 16;  // 2026-10-14's row
constexpr int kBusy = 22;   // 2026-10-20's: six entries
constexpr int kMixed = 24;  // 2026-10-22's: a cancelled one and an event

} // namespace

class TestAtramentMonth : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { editor::atr::setPageIconFolder(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons")); }
    void gridDrawn();
    void pillsAndMore();
    void narrowCellsShowIcons();
    void clicks();
    void keys();
    void pillsFitting();
    void laidOutAsTheQmlGrid();
    void lookFromItsMap();
    void partsByName();
};

// 42 cells in 6 rows of 7, each named for screen readers by its date, "today", its entries and
// its journal; the weekday names from the first week's days; the ISO week numbers (a weekly note's
// named "open its note"); today's red disc, the selected day's dark one.
void TestAtramentMonth::gridDrawn()
{
    Harness h;
    QCOMPARE(h.month().drawnRows(), 42);
    QCOMPARE(h.named.mid(0, 7), (QList<int>{1, 2, 3, 4, 5, 6, 7}));
    const QString dump = h.dump();
    for (const QString &text : {u"środa, 14 października, today, 3 entries, journal"_s, u"piątek, 2 października, journal"_s,
                                u"Week 40: write its note"_s, u"Week 45: write its note"_s})
        QVERIFY2(dump.contains(text), qPrintable(text + u" in "_s + dump));
    for (int r = 1; r < 42; ++r) {
        const at::Rect a = h.part(r - 1, MonthView::Part::Cell), b = h.part(r, MonthView::Part::Cell);
        if (r % 7)
            QVERIFY(std::abs(b.left() - a.right()) < 1.6f && b.top() == a.top()); // (a cell's part: inside its line)
        else
            QVERIFY(b.top() >= a.bottom() - 0.6f);
    }
    QCOMPARE(h.part(0, MonthView::Part::Cell).left(), h.month().look().weekColumn);
    QCOMPARE(h.part(0, MonthView::Part::Week).width(), h.month().look().weekColumn - 1); // (its line)
    QCOMPARE(h.quads(h.month().look().today), 1);
    QCOMPARE(h.quads(h.month().look().selected), 0);
    fill(h.model, QDate(2026, 10, 15));
    h.view.update(h.app, [](MonthView &v, at::Context<MonthView> &cx) {
        MonthLook look = v.look();
        look.weekNotes = {u"2026-10-12"_s};
        v.setLook(look, cx);
    });
    h.tw.settle();
    QCOMPARE(h.quads(h.month().look().selected), 1);
    QVERIFY2(h.dump().contains(u"Week 42: open its note"_s), qPrintable(h.dump()));
}

// Wide cells: pills with their times; a cell of more entries than fit: "+N more"; a done entry's
// and a cancelled one's names; an event in its calendar's colour.
void TestAtramentMonth::pillsAndMore()
{
    Harness h;
    const QString dump = h.dump();
    for (const QString &text : {u"09:00, Spotkanie"_s, u"Wymienić żarówkę, done"_s, u"Odwołane, cancelled"_s, u"Konferencja"_s})
        QVERIFY2(dump.contains(text), qPrintable(text + u" in "_s + dump));
    const int fitting = h.month().pillsFitting((700 - h.month().look().headerHeight) / 6); // (a row of cells' height)
    QVERIFY(fitting >= 2 && fitting < 6);
    QVERIFY(h.month().partBounds(kBusy, {MonthView::Part::Pill, fitting - 2}));
    QVERIFY(!h.month().partBounds(kBusy, {MonthView::Part::Pill, fitting - 1}));
    QVERIFY(h.month().partBounds(kBusy, {MonthView::Part::More}));
    QVERIFY(!h.month().partBounds(kToday, {MonthView::Part::More}));
    for (int k = 0; k < 3; ++k) {
        const at::Rect p = h.part(kToday, MonthView::Part::Pill, k);
        QVERIFY(p.height() == h.month().look().pillHeight && h.part(kToday, MonthView::Part::Cell).contains(p.center()));
    }
    int event = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == at::rgb(0x2f9e44))
            event += h.part(kMixed, MonthView::Part::Cell).contains(q->bounds.center()) ? 1 : 100;
    QCOMPARE(event, 1); // (its stripe)
}

// Cells under 72 px: no pills, an icon per kind of open entry in its colour.
void TestAtramentMonth::narrowCellsShowIcons()
{
    Harness h({400, 600});
    QVERIFY(!h.month().partBounds(kToday, {MonthView::Part::Pill, 0}));
    MonthLook look = h.month().look();
    look.marks[u"reminder"_s] = at::rgb(0xc98a12);
    h.view.update(h.app, [&look](MonthView &v, at::Context<MonthView> &cx) { v.setLook(look, cx); });
    h.tw.settle();
    int marked = 0;
    const at::Rect cell = h.part(kToday, MonthView::Part::Cell);
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *path = std::get_if<at::PathPrimitive>(&p); path && path->color == at::rgb(0xc98a12) && cell.contains(path->bounds.center()))
            ++marked;
    QVERIFY(marked > 0);
}

// A cell's click selects its day, a double click opens its note, a pill's click its entry (only
// that), a week number its week's note.
void TestAtramentMonth::clicks()
{
    Harness h;
    const at::Rect cell = h.part(3, MonthView::Part::Cell);
    h.tw.simulateClick({cell.center().x, cell.bottom() - 4});
    h.tw.simulateClick({cell.center().x, cell.bottom() - 4}, at::MouseButton::Left, {}, 2);
    h.tw.simulateClick(h.part(kToday, MonthView::Part::Pill, 1).center());
    h.tw.simulateClick(h.part(2, MonthView::Part::Week).center());
    QCOMPARE(h.actions, (QStringList{u"day date=2026-10-01"_s, u"open date=2026-10-01"_s,
                                     u"entry date=2026-10-14 entry=Kupić fugę"_s, u"week monday=2026-10-12"_s}));
}

// ← → ↑ ↓ move the selected day by 1 and 7, Home / End to its week's ends, PageUp / PageDown
// another month, Enter selects it, Ctrl+Enter opens its note; other keys are not the grid's.
void TestAtramentMonth::keys()
{
    Harness h;
    h.focus();
    for (const char16_t *k : {u"left", u"right", u"up", u"down", u"home", u"end", u"pageup", u"pagedown", u"enter", u"ctrl-enter"})
        QVERIFY(h.tw.simulateKeystrokes(k));
    QCOMPARE(h.actions, (QStringList{u"move days=-1"_s, u"move days=1"_s, u"move days=-7"_s, u"move days=7"_s, u"move days=-2"_s,
                                     u"move days=4"_s, u"previous"_s, u"next"_s, u"day date=2026-10-14"_s,
                                     u"open date=2026-10-14"_s}));
    h.actions.clear();
    QVERIFY(!h.tw.simulateKeystrokes(u"ctrl-left"));
    QVERIFY(!h.tw.simulateKeystrokes(u"a"));
    QVERIFY(h.actions.isEmpty());
    // The selected day ringed while the grid has the focus.
    int ring = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *q = std::get_if<at::Quad>(&p); q && q->borderColor == h.month().look().focus && q->borderWidths.left > 1)
            ring += h.part(kToday, MonthView::Part::Cell).contains(q->bounds.center()) ? 1 : 100;
    QCOMPARE(ring, 1);
    // No selected day: Enter selects nothing.
    fill(h.model, QDate());
    h.tw.settle();
    QVERIFY(!h.tw.simulateKeystrokes(u"enter"));
    QVERIFY(h.tw.simulateKeystrokes(u"home"));
    QCOMPARE(h.actions, QStringList{u"move days=0"_s});
}

// The grid fills the window (its last row's bottom at the window's), the last column without a
// right line; the weekday names 8 px into their columns; a resized window drawn anew; a daily
// note's book in wide cells; the time of a pill from 104 px cells on; an alarm's pill in the
// reminder's tint; "+N more" naming the entries left; a cell holding one pill shows "+N more" alone.
void TestAtramentMonth::laidOutAsTheQmlGrid()
{
    Harness h;
    const MonthLook &look = h.month().look();
    QVERIFY(std::abs(h.part(41, MonthView::Part::Cell).bottom() - (700 - 1)) < 1.1f);
    QCOMPARE(h.part(6, MonthView::Part::Cell).right(), 900.0f);
    float firstGlyph = 900;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->bounds.bottom() <= look.headerHeight)
            firstGlyph = std::min(firstGlyph, g->bounds.left());
    QVERIFY2(firstGlyph >= look.weekColumn + 6, qPrintable(QString::number(firstGlyph)));
    const auto paths = [&h](at::Rect in, at::Color c) {
        int n = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *path = std::get_if<at::PathPrimitive>(&p); path && path->color == c && in.contains(path->bounds.center()))
                ++n;
        return n;
    };
    QVERIFY(paths(h.part(4, MonthView::Part::Cell), look.faint) > 0); // 2 October's book
    QCOMPARE(paths(h.part(5, MonthView::Part::Cell), look.faint), 0);
    const QString dump = h.dump();
    const int fitting = h.month().pillsFitting((700 - look.headerHeight) / 6);
    QVERIFY2(dump.contains(u"+%1 more"_s.arg(6 - (fitting - 1))), qPrintable(dump));
    // An alarm in the reminder's tint.
    MonthLook tinted = look;
    tinted.chips[u"reminder"_s] = {at::rgb(0xfff3bf), at::rgb(0x8f5f00)};
    h.view.update(h.app, [&tinted](MonthView &v, at::Context<MonthView> &cx) { v.setLook(tinted, cx); });
    h.tw.settle();
    QCOMPARE(h.quads(at::rgb(0xfff3bf)), 1);
    // Resized: drawn anew.
    h.tw.resize({700, 700});
    h.tw.settle();
    QCOMPARE(h.part(6, MonthView::Part::Cell).right(), 700.0f);
    // Times from 104 px cells on: "09:00 " five glyphs more.
    const auto glyphsIn = [](Harness &w) {
        const at::Rect r = w.part(kToday, MonthView::Part::Pill, 0);
        int n = 0;
        for (const at::Primitive &p : w.tw.lastScene()->primitives())
            if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && r.contains(g->bounds.center()))
                ++n;
        return n;
    };
    // (1428 px: cells of 200 px; times from 200 px on, and from 200.5.)
    Harness wide({1428, 700}), narrower({1428, 700});
    for (const auto &[w, from] : {std::pair{&wide, 200.0f}, std::pair{&narrower, 200.5f}})
        w->view.update(w->app, [from](MonthView &v, at::Context<MonthView> &cx) {
            MonthLook l = v.look();
            l.timesFrom = from;
            v.setLook(l, cx);
        });
    wide.tw.settle();
    narrower.tw.settle();
    QCOMPARE(glyphsIn(wide) - glyphsIn(narrower), 5);
    // A cell holding one pill: "+6 more" alone.
    Harness low({900, 26 + 6 * 52});
    QVERIFY(!low.month().partBounds(kBusy, {MonthView::Part::Pill, 0}));
    QVERIFY(low.month().partBounds(kBusy, {MonthView::Part::More}));
}

void TestAtramentMonth::pillsFitting()
{
    Harness h;
    const MonthLook &look = h.month().look();
    QCOMPARE(h.month().pillsFitting(look.pillsTop + 2 + 3 * (look.pillHeight + 2)), 3);
    QCOMPARE(h.month().pillsFitting(look.pillsTop + 2 + 3 * (look.pillHeight + 2) - 1), 2);
    QCOMPARE(h.month().pillsFitting(10), 0);
}

void TestAtramentMonth::lookFromItsMap()
{
    const MonthLook look = MonthLook::fromMap({{u"today"_s, QColor(u"#ff0000"_s)},
                                               {u"weekColumn"_s, 0},
                                               {u"chips"_s, QVariantMap{{u"due"_s, QVariantMap{{u"background"_s, QColor(u"#ffe3e3"_s)}}}}},
                                               {u"marks"_s, QVariantMap{{u"overdue"_s, QColor(u"#c92a2a"_s)}}}});
    QCOMPARE(look.today, at::rgb(0xff0000));
    QCOMPARE(look.weekColumn, 0.0f);
    QCOMPARE(look.chip(u"due"_s).background, at::rgb(0xffe3e3));
    QCOMPARE(look.mark(u"overdue"_s), at::rgb(0xc92a2a));
    QCOMPARE(look.mark(u"due"_s), look.muted);
    QCOMPARE(look.text, MonthLook{}.text);
    const MonthLook named = MonthLook::fromMap({{u"weekdays"_s, QStringList{u"pon"_s, u"wt"_s}}, {u"weekNotes"_s, QStringList{u"2026-10-12"_s}}});
    QCOMPARE(named.weekdays, (QStringList{u"pon"_s, u"wt"_s}));
    QCOMPARE(named.weekNotes, std::vector<QString>{u"2026-10-12"_s});
    // Without the hook, the look's names (uppercase) over the columns.
    Harness h;
    h.view.update(h.app, [](MonthView &v, at::Context<MonthView> &cx) {
        v.weekdayName = nullptr;
        MonthLook l = v.look();
        l.weekdays = {u"pon"_s, u"wt"_s, u"śr"_s, u"czw"_s, u"pt"_s, u"sob"_s, u"nie"_s};
        v.setLook(l, cx);
    });
    h.tw.settle();
    int glyphs = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->bounds.bottom() <= h.month().look().headerHeight)
            ++glyphs;
    QCOMPARE(glyphs, 3 + 2 + 2 + 3 + 2 + 3 + 3);
}

void TestAtramentMonth::partsByName()
{
    using P = MonthView::Part;
    QVERIFY((MonthView::partNamed(u"pill 2"_s) == P{P::Pill, 2}));
    QVERIFY((MonthView::partNamed(u"more"_s) == P{P::More, 0}));
    QVERIFY((MonthView::partNamed(u"week"_s) == P{P::Week, 0}));
    QVERIFY((MonthView::partNamed(u"cell"_s) == P{P::Cell, 0}));
}

QTEST_GUILESS_MAIN(TestAtramentMonth)
#include "tst_atrament_month.moc"

// The Calendar view's week drawn by Atrament (src/app/views/atrament/weekview.h; docs/ATRAMENT.md "The
// views in Atrament (M3)", Calendar): 7 days of CalendarModel's roles (a QStandardItemModel with
// them and its firstHour / maxAllDay as properties) drawn headless on Atrament's test platform —
// the heads with their discs and all-day pills, the hours scrolled to the first entry, the timed
// entries at their time and in their lanes, now's line; every click's and key's request.
#include "calendarmodel.h"
#include "pageicons.h"
#include "text/fonts.h"
#include "ui/testplatform.h"
#include "weekview.h"

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

QVariantMap timed(const QString &title, int minutes, int lane = 0, int lanes = 1, const QVariantMap &more = {})
{
    QVariantMap e{{u"title"_s, title}, {u"variant"_s, u"reminder"_s}, {u"minutes"_s, minutes},
                  {u"time"_s, u"%1:%2"_s.arg(minutes / 60, 2, 10, QChar(u'0')).arg(minutes % 60, 2, 10, QChar(u'0'))}};
    for (auto it = more.cbegin(); it != more.cend(); ++it)
        e.insert(it.key(), it.value());
    return {{u"entry"_s, e}, {u"lane"_s, lane}, {u"lanes"_s, lanes}};
}

// The week of Monday 12 October 2026; today the 14th, selected.
void fill(QStandardItemModel &model, const QDate &selected = QDate(2026, 10, 14))
{
    model.clear();
    for (int k = 0; k < 7; ++k) {
        const QDate d = QDate(2026, 10, 12).addDays(k);
        auto *item = new QStandardItem;
        item->setData(d.toString(Qt::ISODate), CalendarModel::DateRole);
        item->setData(QLocale(QLocale::Polish).toString(d, u"dddd, d MMMM"_s), CalendarModel::LongLabelRole);
        item->setData(d.day(), CalendarModel::DayRole);
        item->setData(d.dayOfWeek(), CalendarModel::DayOfWeekRole);
        item->setData(d == QDate(2026, 10, 14), CalendarModel::IsTodayRole);
        item->setData(d == selected, CalendarModel::IsSelectedRole);
        item->setData(d.dayOfWeek() >= 6, CalendarModel::IsWeekendRole);
        QVariantList allDay, hours;
        if (d == QDate(2026, 10, 14)) {
            allDay = {QVariantMap{{u"title"_s, u"Kupić fugę"_s}, {u"variant"_s, u"due"_s}}};
            hours = {timed(u"Spotkanie"_s, 9 * 60 + 30, 0, 2), timed(u"Telefon"_s, 9 * 60 + 45, 1, 2),
                     timed(u"Konferencja"_s, 13 * 60, 0, 1,
                           {{u"variant"_s, u"event"_s}, {u"endMinutes"_s, 15 * 60}, {u"color"_s, u"#2f9e44"_s}}),
                     timed(u"Odwołane"_s, 17 * 60, 0, 1, {{u"closed"_s, true}, {u"status"_s, u"cancelled"_s}})};
        } else if (d == QDate(2026, 10, 16)) {
            for (int n = 0; n < 5; ++n)
                allDay.append(QVariantMap{{u"title"_s, u"Zadanie %1"_s.arg(n)}, {u"variant"_s, u"due"_s}});
        }
        item->setData(allDay, CalendarModel::AllDayRole);
        item->setData(hours, CalendarModel::TimedRole);
        model.appendRow(item);
    }
    model.setProperty("firstHour", 8);
    model.setProperty("maxAllDay", 5);
}

struct Harness {
    QStandardItemModel model;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<WeekView> view;
    QStringList actions;
    QVariantMap last;

    explicit Harness(at::Size size = {900, 700}) : tw(platform->newWindow(size, 1))
    {
        fill(model);
        view = app.newEntity<WeekView>(&model);
        view.update(app, [this](WeekView &v, at::Context<WeekView> &cx) {
            v.nowMinutes = [] { return 10 * 60; };
            WeekLook look = v.look();
            look.weekdays = {u"pon"_s, u"wt"_s, u"śr"_s, u"czw"_s, u"pt"_s, u"sob"_s, u"nie"_s};
            v.setLook(look, cx);
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
    const WeekView &week() { return view.read(app); }
    at::Window &window() { return *app.windows().front(); }
    at::Rect part(int row, WeekView::Part::Kind k, int index = 0)
    {
        return week().partBounds(row, WeekView::Part{k, index}).value_or(at::Rect{});
    }
    QString dump() { return str(window().accessibilityTree().dump()); }
    int quads(at::Color c, at::Rect in)
    {
        int n = 0;
        for (const at::Primitive &p : tw.lastScene()->primitives())
            if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == c && in.contains(q->bounds.center()))
                ++n;
        return n;
    }
};

constexpr int kToday = 2;   // Wednesday the 14th
constexpr int kBusy = 4;    // Friday the 16th: five all-day entries

} // namespace

class TestAtramentWeek : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { editor::atr::setPageIconFolder(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons")); }
    void headsAndColumns();
    void hoursScrolledToTheFirstEntry();
    void timedEntriesAtTheirTime();
    void narrowColumnsCascade();
    void clicksAndMenus();
    void keys();
    void lookAndParts();
    void laidOutAsTheQmlGrid();
};

// Seven heads (today's named so) and columns side by side; three rows of all-day pills at most,
// the third "+3 more"; today's red disc; the selected day's column tinted, the weekend's sunken.
void TestAtramentWeek::headsAndColumns()
{
    Harness h;
    QCOMPARE(h.week().drawnRows(), 7);
    const QString dump = h.dump();
    for (const QString &text : {u"środa, 14 października, today"_s, u"Kupić fugę"_s, u"+3 more"_s, u"Zadanie 1"_s, u"Week 42"_s})
        QVERIFY2(dump.contains(text), qPrintable(text + u" in "_s + dump));
    QVERIFY(!dump.contains(u"Zadanie 2"_s));
    for (int c = 1; c < 7; ++c)
        QVERIFY(h.part(c, WeekView::Part::Column).left() >= h.part(c - 1, WeekView::Part::Column).right() - 0.5f);
    QVERIFY(h.week().partBounds(kBusy, {WeekView::Part::AllDay, 2}));
    QVERIFY(!h.week().partBounds(kBusy, {WeekView::Part::AllDay, 3}));
    const WeekLook &look = h.week().look();
    QCOMPARE(h.quads(look.today, h.part(kToday, WeekView::Part::Head)), 1);
    QCOMPARE(h.quads(look.selectedColumn, h.part(kToday, WeekView::Part::Column)), 1);
    QCOMPARE(h.quads(look.weekend, h.part(5, WeekView::Part::Column)), 1);
    QCOMPARE(h.quads(look.weekend, h.part(0, WeekView::Part::Column)), 0);
    // Another day selected: its disc dark, today's still red.
    fill(h.model, QDate(2026, 10, 13));
    h.tw.settle();
    QCOMPARE(h.quads(look.selected, h.part(1, WeekView::Part::Head)), 1);
    QCOMPARE(h.quads(look.today, h.part(kToday, WeekView::Part::Head)), 1);
}

// The hours open at the week's first timed entry (firstHour), a new week anew; scrollToHour.
void TestAtramentWeek::hoursScrolledToTheFirstEntry()
{
    Harness h;
    const float hour = h.week().look().hourHeight;
    QCOMPARE(h.week().hoursOffset(), 8 * hour);
    h.view.update(h.app, [](WeekView &v, at::Context<WeekView> &cx) { v.scrollToHour(2, cx); });
    h.tw.settle();
    QCOMPARE(h.week().hoursOffset(), 2 * hour);
    // The same week refreshed keeps the scroll; another week goes to its first hour.
    fill(h.model);
    h.tw.settle();
    QCOMPARE(h.week().hoursOffset(), 2 * hour);
    h.model.item(0)->setData(u"2026-10-19"_s, CalendarModel::DateRole);
    h.model.setProperty("firstHour", 6);
    h.tw.settle();
    QCOMPARE(h.week().hoursOffset(), 6 * hour);
}

// 9:30 and 9:45 side by side (two lanes), 45 minutes tall (at least two pills); the event from
// 13:00 to 15:00 two hours tall; now's line at 10:00 in today's column; a cancelled entry named so.
void TestAtramentWeek::timedEntriesAtTheirTime()
{
    Harness h;
    h.view.update(h.app, [](WeekView &v, at::Context<WeekView> &cx) { v.scrollToHour(0, cx); });
    h.tw.settle();
    const WeekLook &look = h.week().look();
    const at::Rect hours = h.part(0, WeekView::Part::Hours);
    const at::Rect a = h.part(kToday, WeekView::Part::Timed, 0), b = h.part(kToday, WeekView::Part::Timed, 1);
    const at::Rect column = h.part(kToday, WeekView::Part::Column);
    QVERIFY2(std::abs(a.top() - (hours.top() + 9.5f * look.hourHeight + 1)) < 1.6f, qPrintable(QString::number(a.top() - hours.top())));
    QVERIFY(b.left() > a.right() - 0.5f && b.right() <= column.right() + 0.5f);
    QVERIFY(std::abs(a.width() - b.width()) < 0.6f);
    // (A part inside its 1 px border.)
    QVERIFY2(std::abs(a.height() + 2 - std::max(2 * look.pillHeight - 2, 0.75f * look.hourHeight - 2)) < 0.6f, qPrintable(QString::number(a.height())));
    const at::Rect event = h.part(kToday, WeekView::Part::Timed, 2);
    QVERIFY2(std::abs(event.height() + 2 - (2 * look.hourHeight - 2)) < 0.6f, qPrintable(QString::number(event.height())));
    int now = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == look.now && q->bounds.height() < 3)
            now += column.contains(q->bounds.center()) && std::abs(q->bounds.top() - (hours.top() + 10 * look.hourHeight)) < 1.6f ? 1 : 100;
    QCOMPARE(now, 1);
    QVERIFY2(h.dump().contains(u"17:00, Odwołane, cancelled"_s), qPrintable(h.dump()));
}

// Under 80 px columns the second lane cascades: 6 px in, 10 px down, as wide as the column allows.
void TestAtramentWeek::narrowColumnsCascade()
{
    Harness h({500, 700});
    const WeekView &w = h.week();
    const WeekView::Timed second{{{u"minutes"_s, 600}}, 1, 2};
    const at::Rect r = w.timedRect(second, 60);
    QCOMPARE(r.left(), 2.0f + 6);
    QCOMPARE(r.top(), 600.0f / 60 * w.look().hourHeight + 1 + 10);
    QCOMPARE(r.width(), 60.0f - 4 - 6);
    const at::Rect wide = w.timedRect(second, 120);
    QCOMPARE(wide.left(), 2 + (120.0f - 4) / 2);
    QCOMPARE(wide.width(), (120.0f - 4) / 2 - 2);
    // A night's event (no end): to midnight.
    const WeekView::Timed night{{{u"minutes"_s, 22 * 60}, {u"variant"_s, u"event"_s}}, 0, 1};
    QCOMPARE(w.timedRect(night, 120).height(), 2 * w.look().hourHeight - 2);
}

// A column's click selects its day, a head's double click opens its note, an entry's click opens
// it (only that), a right-click its menu at the pointer, the third all-day row's "+3 more" its day,
// the corner's number its week's note.
void TestAtramentWeek::clicksAndMenus()
{
    Harness h;
    const at::Rect column = h.part(0, WeekView::Part::Column);
    // (Under the heads: the hours' visible top; the Hours part is the scrolled content.)
    h.tw.simulateClick({column.center().x, h.part(0, WeekView::Part::Head).bottom() + 20});
    h.tw.simulateClick(h.part(1, WeekView::Part::Head).center(), at::MouseButton::Left, {}, 2);
    h.tw.simulateClick(h.part(kToday, WeekView::Part::Timed, 0).center());
    h.tw.simulateClick(h.part(kToday, WeekView::Part::AllDay, 0).center());
    h.tw.simulateClick(h.part(kBusy, WeekView::Part::AllDay, 2).center());
    QVERIFY2(h.actions.size() == 5, qPrintable(h.actions.join(u" | "_s)));
    QCOMPARE(h.actions, (QStringList{u"day date=2026-10-12"_s, u"open date=2026-10-13"_s, u"entry date=2026-10-14 entry=Spotkanie"_s,
                                     u"entry date=2026-10-14 entry=Kupić fugę"_s, u"day date=2026-10-16"_s}));
    h.actions.clear();
    const at::Point on = h.part(kToday, WeekView::Part::Timed, 1).center();
    h.tw.simulateClick(on, at::MouseButton::Right);
    QCOMPARE(h.actions, QStringList{u"menu entry=Telefon"_s});
    QCOMPARE(h.last.value(u"x"_s).toReal(), qreal(on.x));
    h.actions.clear();
    const at::AccessibilityNode *corner = nullptr;
    for (const at::AccessibilityNode *n : h.window().accessibilityTree().nodes())
        if (n->role == at::Role::Button && str(n->name).startsWith(u"Week 42"))
            corner = n;
    QVERIFY(corner);
    h.tw.simulateClick(corner->bounds.center());
    QCOMPARE(h.actions, QStringList{u"week monday=2026-10-12"_s});
}

// The grid's keys (calendarkeys.h): Home from Wednesday two days back, Enter the selected day.
void TestAtramentWeek::keys()
{
    Harness h;
    h.view.update(h.app, [&](WeekView &v, at::Context<WeekView> &) { h.window().focus(v.focusHandle()); });
    h.tw.settle();
    for (const char16_t *k : {u"home", u"end", u"pagedown", u"enter", u"ctrl-enter"})
        QVERIFY(h.tw.simulateKeystrokes(k));
    QCOMPARE(h.actions, (QStringList{u"move days=-2"_s, u"move days=4"_s, u"next"_s, u"day date=2026-10-14"_s, u"open date=2026-10-14"_s}));
    QVERIFY(!h.tw.simulateKeystrokes(u"shift-left"));
    QCOMPARE(h.week().currentRow(), kToday);
}

void TestAtramentWeek::lookAndParts()
{
    const WeekLook look = WeekLook::fromMap({{u"now"_s, QColor(u"#ff0000"_s)},
                                             {u"hourHeight"_s, 44},
                                             {u"weekdays"_s, QStringList{u"pon"_s}},
                                             {u"chips"_s, QVariantMap{{u"due"_s, QVariantMap{{u"background"_s, QColor(u"#ffe3e3"_s)}}}}}});
    QCOMPARE(look.now, at::rgb(0xff0000));
    QCOMPARE(look.hourHeight, 44.0f);
    QCOMPARE(look.weekdays, QStringList{u"pon"_s});
    QCOMPARE(look.chip(u"due"_s).background, at::rgb(0xffe3e3));
    using P = WeekView::Part;
    QVERIFY((WeekView::partNamed(u"timed 3"_s) == P{P::Timed, 3}));
    QVERIFY((WeekView::partNamed(u"allday 1"_s) == P{P::AllDay, 1}));
    QVERIFY((WeekView::partNamed(u"head"_s) == P{P::Head, 0}));
    QVERIFY((WeekView::partNamed(u"hours"_s) == P{P::Hours, 0}));
    QVERIFY((WeekView::partNamed(u"column"_s) == P{P::Column, 0}));
}

// The heads as wide as their columns, the weekday names from Monday, the all-day rows' height
// (three pills and 6 px), "all day" in the corner only with all-day rows; 24 hour lines one hour
// apart down the hours; an entry shows its time; a firstHour of 0 opens at midnight; a column of
// exactly 80 px still side by side; a day out of the model empty.
void TestAtramentWeek::laidOutAsTheQmlGrid()
{
    Harness h;
    const WeekLook &look = h.week().look();
    QVERIFY(std::abs(h.part(0, WeekView::Part::Head).width() - h.part(0, WeekView::Part::Column).width()) < 1.6f);
    const auto glyphs = [&h](at::Rect in) {
        int n = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && in.contains(g->bounds.center()))
                ++n;
        return n;
    };
    const at::Rect head0 = h.part(0, WeekView::Part::Head);
    QCOMPARE(glyphs(at::Rect::fromXYWH(head0.left(), head0.top(), head0.width(), look.headerHeight)), 3 + 2); // PON 12
    // The lines an hour apart from midnight down (the ones in view drawn).
    h.view.update(h.app, [](WeekView &v, at::Context<WeekView> &cx) { v.scrollToHour(0, cx); });
    h.tw.settle();
    const at::Rect hours = h.part(0, WeekView::Part::Hours);
    const float viewTop = h.part(0, WeekView::Part::Head).bottom();
    int lines = 0;
    bool midnight = false;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == look.grid && q->bounds.height() <= 1.01f) {
            const float at = q->bounds.top() - hours.top();
            midnight = midnight || std::abs(at) < 0.6f;
            lines += at >= -0.5f && at < 24 * look.hourHeight && std::abs(std::fmod(at + 0.5f, look.hourHeight) - 0.5f) < 0.6f ? 1 : 100;
        }
    QVERIFY(midnight);
    QCOMPARE(lines, int((700 - viewTop) / look.hourHeight) + 1);
    // The time shown: dropped from the entry, fewer glyphs.
    h.view.update(h.app, [](WeekView &v, at::Context<WeekView> &cx) { v.scrollToHour(9, cx); });
    h.tw.settle();
    const int withTime = glyphs(h.part(kToday, WeekView::Part::Timed, 2));
    QVariantList timedList = h.model.item(kToday)->data(CalendarModel::TimedRole).toList();
    QVariantMap event = timedList[2].toMap();
    QVariantMap entry = event.value(u"entry"_s).toMap();
    entry.remove(u"time"_s);
    event[u"entry"_s] = entry;
    timedList[2] = event;
    h.model.item(kToday)->setData(timedList, CalendarModel::TimedRole);
    h.tw.settle();
    QVERIFY2(withTime - glyphs(h.part(kToday, WeekView::Part::Timed, 2)) >= 4, qPrintable(QString::number(withTime)));
    // All-day rows: three pills and 6 px; none: no "all day".
    const float tall = h.part(0, WeekView::Part::Head).height();
    const at::AccessibilityNode *corner = nullptr;
    for (const at::AccessibilityNode *n : h.window().accessibilityTree().nodes())
        if (n->role == at::Role::Button && str(n->name).startsWith(u"Week 42"))
            corner = n;
    QVERIFY(corner);
    const int cornerWithRows = glyphs(corner->bounds);
    h.model.setProperty("maxAllDay", 0);
    h.view.update(h.app, [](WeekView &, at::Context<WeekView> &cx) { cx.notify(); });
    h.tw.settle();
    QCOMPARE(tall - h.part(0, WeekView::Part::Head).height(), 3 * (look.pillHeight + 2) + 6);
    for (const at::AccessibilityNode *n : h.window().accessibilityTree().nodes())
        if (n->role == at::Role::Button && str(n->name).startsWith(u"Week 42"))
            corner = n;
    QCOMPARE(cornerWithRows - glyphs(corner->bounds), 6); // "all day"
    // Midnight; exactly 80 px; out of the model.
    h.model.setProperty("firstHour", 0);
    h.model.item(0)->setData(u"2026-10-26"_s, CalendarModel::DateRole);
    h.tw.settle();
    QCOMPARE(h.week().hoursOffset(), 0.0f);
    const WeekView::Timed second{{{u"minutes"_s, 600}}, 1, 2};
    QCOMPARE(h.week().timedRect(second, 80).left(), 2 + (80.0f - 4) / 2);
    QCOMPARE(h.week().dayData(7).date, QString());
    QCOMPARE(h.week().dayData(-1).date, QString());
}

QTEST_GUILESS_MAIN(TestAtramentWeek)
#include "tst_atrament_week.moc"

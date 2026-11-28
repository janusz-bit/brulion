// The Calendar's day panel drawn by Atrament (src/app/views/atrament/daypanelview.h; docs/ATRAMENT.md
// "The views in Atrament (M3)", Calendar): the selected day of a model with CalendarModel's roles,
// its anchorDate, overdue, overdueCount and its dayEntries() / dayHasJournal() — the panel's
// heading, actions, today's overdue tasks and the day's plan; every click's request.
#include "calendarmodel.h"
#include "daypanelview.h"
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

// A month's days with what the panel reads of a CalendarModel.
class PanelModel : public QStandardItemModel {
    Q_OBJECT
    Q_PROPERTY(QString anchorDate MEMBER anchorDate)
    Q_PROPERTY(QVariantList overdue MEMBER overdue)
    Q_PROPERTY(int overdueCount MEMBER overdueCount)
public:
    QString anchorDate = u"2026-10-14"_s;
    QVariantList overdue;
    int overdueCount = 0;
    QHash<QString, QVariantList> entries;
    QSet<QString> journals;
    Q_INVOKABLE QVariantList dayEntries(const QString &date) const { return entries.value(date); }
    Q_INVOKABLE bool dayHasJournal(const QString &date) const { return journals.contains(date); }
};

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
    QVariantMap e{{u"title"_s, title}, {u"pageTitle"_s, u"Remont"_s}, {u"path"_s, u"pages/Remont.md"_s}, {u"line"_s, 2}};
    for (auto it = more.cbegin(); it != more.cend(); ++it)
        e.insert(it.key(), it.value());
    return e;
}

struct Harness {
    PanelModel model;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<DayPanelView> view;
    QStringList actions;

    explicit Harness(at::Size size = {360, 800}) : tw(platform->newWindow(size, 1))
    {
        for (int k = 0; k < 7; ++k) {
            const QDate d = QDate(2026, 10, 12).addDays(k);
            auto *item = new QStandardItem;
            item->setData(d.toString(Qt::ISODate), CalendarModel::DateRole);
            item->setData(d == QDate(2026, 10, 14), CalendarModel::IsTodayRole);
            model.appendRow(item);
        }
        model.entries.insert(u"2026-10-14"_s, {entry(u"Kupić fugę"_s, {{u"isTask"_s, true}, {u"status"_s, u"todo"_s}, {u"kind"_s, u"due"_s},
                                                                       {u"variant"_s, u"due"_s}, {u"spec"_s, u"2026-10-14"_s}}),
                                               entry(u"Zadzwonić"_s, {{u"kind"_s, u"reminder"_s}, {u"time"_s, u"18:00"_s}})});
        for (int n = 0; n < 7; ++n)
            model.overdue.append(entry(u"Zaległe %1"_s.arg(n), {{u"isTask"_s, true}, {u"dueLabel"_s, u"10 paź"_s}}));
        model.overdueCount = 7;
        model.journals.insert(u"2026-10-14"_s);
        view = app.newEntity<DayPanelView>(&model);
        view.update(app, [this](DayPanelView &v, at::Context<DayPanelView> &) {
            v.formatHuman = [](const QString &date) { return date == u"2026-10-14" ? u"dzisiaj"_s : u"16 paź"_s; };
            v.formatLong = [](const QString &date) { return u"środa, "_s + date; };
            v.onAction = [this](const QString &action, const QVariantMap &args) {
                QStringList parts{action};
                for (auto it = args.cbegin(); it != args.cend(); ++it)
                    if (it.key() == u"entry")
                        parts.append(u"entry="_s + it.value().toMap().value(u"title"_s).toString());
                    else if (it.key() != u"x" && it.key() != u"y")
                        parts.append(it.key() + u'=' + it.value().toString());
                actions.append(parts.join(u' '));
            };
        });
        app.openWindow(tw, view);
        window().setAccessibilityEnabled(true);
        tw.settle();
    }
    const DayPanelView &panel() { return view.read(app); }
    at::Window &window() { return *app.windows().front(); }
    at::Rect part(DayPanelView::Part::Kind k, int index = 0)
    {
        return panel().partBounds(0, DayPanelView::Part{k, index}).value_or(at::Rect{});
    }
    QString dump() { return str(window().accessibilityTree().dump()); }
    // The model's selected day moved (the days' data changes, as CalendarModel's isSelected does).
    void select(const QString &date)
    {
        model.anchorDate = date;
        model.item(0)->setData(date, Qt::UserRole + 99);
        tw.settle();
    }
};

} // namespace

class TestAtramentDayPanel : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { editor::atr::setPageIconFolder(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons")); }
    void todayDrawn();
    void anotherDay();
    void clicks();
    void overdueFolds();
    void partsByName();
    void edgesAndLook();
};

// Today: "Dzisiaj" over its date, "Open journal" (it has one), "Add task", "Add reminder", the
// overdue header with its count and five of the seven with their due dates, "All 7 overdue in
// Tasks", "Plan of the day" with its two entries.
void TestAtramentDayPanel::todayDrawn()
{
    Harness h;
    const QString dump = h.dump();
    for (const QString &text : {u"Środa, 2026-10-14"_s, u"Open journal"_s, u"Add task"_s, u"Add reminder"_s, u"Overdue  7"_s,
                                u"Zaległe 4, 10 paź · Remont"_s, u"All 7 overdue in Tasks"_s, u"Plan of the day  2"_s,
                                u"Kupić fugę, Due, Remont"_s, u"18:00, Zadzwonić, Remont"_s, u"Done: Kupić fugę"_s})
        QVERIFY2(dump.contains(text), qPrintable(text + u" in "_s + dump));
    QVERIFY(!dump.contains(u"Zaległe 5"_s));
    QVERIFY(h.panel().partBounds(0, {DayPanelView::Part::Overdue, 4}));
    QVERIFY(!h.panel().partBounds(0, {DayPanelView::Part::Overdue, 5}));
    QVERIFY(h.part(DayPanelView::Part::Entry, 0).top() >= h.part(DayPanelView::Part::PlanHeader).bottom() - 0.5f);
    QVERIFY(h.part(DayPanelView::Part::PlanHeader).top() > h.part(DayPanelView::Part::AllOverdue).bottom());
    QVERIFY(h.part(DayPanelView::Part::OverdueHeader).top() > h.part(DayPanelView::Part::Action, 2).bottom());
    QCOMPARE(h.panel().drawnRows(), 1);
}

// Another day: no overdue section, "Write in journal" without a note, "Nothing planned" and why.
void TestAtramentDayPanel::anotherDay()
{
    Harness h;
    h.select(u"2026-10-16"_s);
    const QString dump = h.dump();
    for (const QString &text : {u"Write in journal"_s, u"Plan of the day  0"_s})
        QVERIFY2(dump.contains(text), qPrintable(text + u" in "_s + dump));
    QVERIFY(!dump.contains(u"Overdue"_s));
    QVERIFY(!h.panel().partBounds(0, {DayPanelView::Part::OverdueHeader}));
    QCOMPARE(h.panel().dayData().date, u"2026-10-16"_s);
    QVERIFY(!h.panel().dayData().hasJournal);
    // A day with a journal and nothing planned says so.
    h.model.journals.insert(u"2026-10-16"_s);
    h.select(u"2026-10-16"_s);
    QVERIFY(h.panel().dayData().hasJournal);
}

// The actions ask with the day's date, an entry's click opens it, its box toggles it, "All 7
// overdue in Tasks" asks for the Tasks view.
void TestAtramentDayPanel::clicks()
{
    Harness h;
    for (int k = 0; k < 3; ++k)
        h.tw.simulateClick(h.part(DayPanelView::Part::Action, k).center());
    h.tw.simulateClick(h.part(DayPanelView::Part::Entry, 1).center());
    h.tw.simulateClick(h.part(DayPanelView::Part::Box, 0).center());
    h.tw.simulateClick(h.part(DayPanelView::Part::Overdue, 0).center());
    h.tw.simulateClick(h.part(DayPanelView::Part::AllOverdue).center());
    QCOMPARE(h.actions, (QStringList{u"journal date=2026-10-14"_s, u"addTask date=2026-10-14"_s, u"addReminder date=2026-10-14"_s,
                                     u"open entry=Zadzwonić"_s, u"toggle entry=Kupić fugę"_s, u"open entry=Zaległe 0"_s,
                                     u"allOverdue"_s}));
}

// A click on the overdue header folds its tasks (and "All N"), another opens them.
void TestAtramentDayPanel::overdueFolds()
{
    Harness h;
    h.tw.simulateClick(h.part(DayPanelView::Part::OverdueHeader).center());
    QVERIFY(!h.panel().partBounds(0, {DayPanelView::Part::Overdue, 0}));
    QVERIFY(!h.panel().partBounds(0, {DayPanelView::Part::AllOverdue}));
    h.tw.simulateClick(h.part(DayPanelView::Part::OverdueHeader).center());
    QVERIFY(h.panel().partBounds(0, {DayPanelView::Part::Overdue, 0}));
    QVERIFY(h.actions.isEmpty());
}

void TestAtramentDayPanel::partsByName()
{
    using P = DayPanelView::Part;
    QVERIFY((DayPanelView::partNamed(u"action 2"_s) == P{P::Action, 2}));
    QVERIFY((DayPanelView::partNamed(u"overdue 1"_s) == P{P::Overdue, 1}));
    QVERIFY((DayPanelView::partNamed(u"plan"_s) == P{P::PlanHeader, 0}));
    QVERIFY((DayPanelView::partNamed(u"entry 1"_s) == P{P::Entry, 1}));
    QVERIFY((DayPanelView::partNamed(u"box"_s) == P{P::Box, 0}));
    QVERIFY((DayPanelView::partNamed(u"anything"_s) == P{P::Panel, 0}));
}

// "Dzisiaj" over today's date; an event's place its end and calendar; an entry's time drawn; one
// overdue task still a section, five without "All N"; a new look's colours; no model: no day.
void TestAtramentDayPanel::edgesAndLook()
{
    Harness h;
    QVERIFY2(h.dump().contains(u"Dzisiaj"_s), qPrintable(h.dump()));
    const auto glyphs = [&h](at::Rect in) {
        int n = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && in.contains(g->bounds.center()))
                ++n;
        return n;
    };
    const int withTime = glyphs(h.part(DayPanelView::Part::Entry, 1));
    QVariantList entries = h.model.entries.value(u"2026-10-14"_s);
    QVariantMap second = entries[1].toMap();
    second.remove(u"time"_s);
    entries[1] = second;
    entries.append(entry(u"Spotkanie"_s, {{u"kind"_s, u"event"_s}, {u"variant"_s, u"event"_s}, {u"endTime"_s, u"15:00"_s},
                                          {u"calendarName"_s, u"Praca"_s}}));
    h.model.entries.insert(u"2026-10-14"_s, entries);
    h.model.overdueCount = 5;
    h.select(u"2026-10-14"_s);
    QVERIFY2(withTime - glyphs(h.part(DayPanelView::Part::Entry, 1)) >= 4, qPrintable(QString::number(withTime)));
    QVERIFY2(h.dump().contains(u"Spotkanie, until 15:00 · Praca"_s), qPrintable(h.dump()));
    QVERIFY(!h.panel().partBounds(0, {DayPanelView::Part::AllOverdue}));
    h.model.overdueCount = 1;
    h.model.overdue = {h.model.overdue.first()};
    h.select(u"2026-10-14"_s);
    QVERIFY(h.panel().partBounds(0, {DayPanelView::Part::OverdueHeader}));
    // A new look.
    h.view.update(h.app, [](DayPanelView &v, at::Context<DayPanelView> &cx) {
        AgendaLook look = v.look();
        look.todayText = at::rgb(0x00aa00);
        v.setLook(look, cx);
    });
    h.tw.settle();
    int green = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->color == at::rgb(0x00aa00))
            ++green;
    QCOMPARE(green, 7); // "Dzisiaj"
    // Today in the first row; no model at all.
    h.model.item(0)->setData(true, CalendarModel::IsTodayRole);
    h.select(u"2026-10-12"_s);
    QVERIFY(h.panel().dayData().isToday);
    at::App app{std::make_shared<at::TestPlatform>(), loadFonts()};
    at::Entity<DayPanelView> none = app.newEntity<DayPanelView>(static_cast<QAbstractItemModel *>(nullptr));
    QCOMPARE(none.read(app).dayData().date, QString());
}

QTEST_GUILESS_MAIN(TestAtramentDayPanel)
#include "tst_atrament_daypanel.moc"

#include "daypanelview.h"

#include "calendarmodel.h" // (its roles)
#include "pageicons.h"
#include "planningrow.h"
#include "renderedspans.h"

#include <QCoreApplication>
#include <QHash>

namespace brulion::views::atr {

namespace at = brulion::atrament;
using editor::atr::u16View;
using planning::CalendarModel;
using namespace Qt::StringLiterals;

namespace {

// CalendarDayPanel's words (its translations; not lupdate's).
QString panelWords(const char *text, int n = -1)
{
    return QCoreApplication::translate("CalendarDayPanel", text, nullptr, n);
}

QString capitalized(const QString &s)
{
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1);
}

std::vector<AgendaEntry> entriesOf(const QVariantList &list)
{
    std::vector<AgendaEntry> out;
    for (const QVariant &e : list)
        out.push_back(AgendaEntry::from(e.toMap()));
    return out;
}

} // namespace

DayPanelView::Part DayPanelView::partNamed(const QString &name)
{
    static const QHash<QString, Part::Kind> kinds{{u"action"_s, Part::Action},   {u"overdueHeader"_s, Part::OverdueHeader},
                                                  {u"overdue"_s, Part::Overdue}, {u"overdueBox"_s, Part::OverdueBox},
                                                  {u"allOverdue"_s, Part::AllOverdue}, {u"plan"_s, Part::PlanHeader},
                                                  {u"entry"_s, Part::Entry},
                                                  {u"box"_s, Part::Box}};
    return partFromName<Part>(name, kinds, Part::Panel);
}

DayPanelView::DayPanelView(at::Context<DayPanelView> &cx, QAbstractItemModel *model)
    : m_model(model)
    , m_rows(0)
{
    // (The selected day and its entries change the days' data: the panel follows the rows.)
    m_rows.followFor(model, cx, [](DayPanelView &) {});
}

DayPanelView::~DayPanelView() = default;

void DayPanelView::setLook(AgendaLook look, at::Context<DayPanelView> &cx)
{
    m_rows.restyle(m_look, std::move(look), cx);
}

DayPanelView::DayData DayPanelView::dayData() const
{
    DayData d;
    if (!m_model)
        return d;
    d.date = m_model->property("anchorDate").toString();
    const int row = rowWhere(m_model, CalendarModel::DateRole, d.date);
    d.isToday = row >= 0 && m_model->index(row, 0).data(CalendarModel::IsTodayRole).toBool();
    QVariantList entries;
    QMetaObject::invokeMethod(m_model.data(), "dayEntries", Q_RETURN_ARG(QVariantList, entries), Q_ARG(QString, d.date));
    QMetaObject::invokeMethod(m_model.data(), "dayHasJournal", Q_RETURN_ARG(bool, d.hasJournal), Q_ARG(QString, d.date));
    d.entries = entriesOf(entries);
    if (d.isToday) {
        d.overdue = entriesOf(m_model->property("overdue").toList());
        d.overdueCount = m_model->property("overdueCount").toInt();
    }
    return d;
}

QString DayPanelView::subtitle(const AgendaEntry &e, bool withDate) const
{
    if (e.isEvent())
        return e.eventDetails();
    const QString day = e.isJournalPage && journalDay ? journalDay(e.path) : QString();
    const QString where = day.isEmpty() ? e.pageTitle : entryWords("Journal · %1").arg(day);
    const QString due = withDate ? e.map.value(u"dueLabel"_s).toString() : QString();
    return joined({due, where}, u" · "_s);
}

at::AnyElement DayPanelView::render(at::Window &window, at::Context<DayPanelView> &)
{
    m_parts.clear();
    m_width = window.viewportSize().width;
    const DayData d = dayData();
    const std::u16string family = m_look.uiFamily;
    return at::div()
            .id(u"daypanel")
            .sizeFull()
            .overflowYScroll()
            .trackScroll(m_scroll)
            .bg(m_look.background)
            .textColor(m_look.text)
            .when(!family.empty(), [family](at::Div &&p) { return std::move(p).fontFamily(family); })
            .ariaLabel(u"Day")
            .child(at::div()
                           .relative()
                           .wFull()
                           .flexCol()
                           .gap(8)
                           .pb(16)
                           .child(heading(d))
                           .child(at::div()
                                          .flexCol()
                                          .child(action(0, u"book-open"_s,
                                                        d.hasJournal ? panelWords("Open journal") : panelWords("Write in journal"),
                                                        u"journal"_s, d.date))
                                          .child(action(1, u"plus"_s, panelWords("Add task"), u"addTask"_s, d.date))
                                          .child(action(2, u"bell"_s, panelWords("Add reminder"), u"addReminder"_s, d.date)))
                           .child(overdue(d))
                           .child(plan(d))
                           .child(tracker({Part::Panel})));
}

// The day's relative word (today's in the accent) and its date in words.
at::AnyElement DayPanelView::heading(const DayData &d)
{
    const QString human = formatHuman ? formatHuman(d.date) : QString();
    const QString relative = human.contains(u' ') ? QString() : capitalized(human);
    at::Div column = at::div().flexCol().px(8);
    if (!relative.isEmpty())
        std::move(column).child(at::div()
                                        .textSize(m_look.smallSize)
                                        .textColor(d.isToday ? m_look.todayText : m_look.muted)
                                        .child(at::text(u16View(relative))));
    const QString date = capitalized(formatLong ? formatLong(d.date) : d.date);
    return std::move(column).child(at::div()
                                           .textSize(m_look.dayTextSize)
                                           .fontSemibold()
                                           .role(at::Role::Heading)
                                           .ariaLabel(u16View(date))
                                           .child(at::text(u16View(date))));
}

// An action of the day: a full-width row, its icon and its words (BListItem, dense).
at::AnyElement DayPanelView::action(int index, const QString &icon, const QString &words, const QString &asked, const QString &date)
{
    const DayPanelView *self = this;
    return at::div()
            .id(at::ElementId(u"action", quint64(index)))
            .relative()
            .flexRow()
            .itemsCenter()
            .gap(10)
            .h(m_look.rowHeight)
            .px(8)
            .rounded(m_look.radius)
            .hover([hover = m_look.hover](at::Style &s) { s.background = at::Background::solid(hover); })
            .cursorPointer()
            .role(at::Role::Button)
            .ariaLabel(u16View(words))
            .onClick([self, asked, date](const at::ClickEvent &, at::Window &, at::App &) { self->act(asked, {{u"date"_s, date}}); })
            .child(editor::atr::iconElement(icon, 16, m_look.muted))
            .child(at::text(u16View(words)))
            .child(tracker({Part::Action, index}));
}

// A section's header (BSectionHeader): its words, its count; a folding one's chevron.
at::AnyElement DayPanelView::sectionHeader(const QString &words, int count, std::optional<bool> expanded, Part part)
{
    at::Div line = at::div()
                           .relative()
                           .flexRow()
                           .itemsCenter()
                           .gap(6)
                           .mt(8)
                           .px(8)
                           .textSize(m_look.chipSize)
                           .fontSemibold()
                           .textColor(m_look.muted)
                           .role(at::Role::Heading)
                           .ariaLabel(u16View(words + u"  "_s + QString::number(count)));
    if (expanded) {
        DayPanelView *self = this;
        std::move(line)
                .cursorPointer()
                .expanded(*expanded)
                .onClick([self](const at::ClickEvent &, at::Window &w, at::App &) {
                    self->m_overdueOpen = !self->m_overdueOpen;
                    w.refresh();
                })
                .child(editor::atr::iconElement(*expanded ? u"chevron-down"_s : u"chevron-right"_s, 14, m_look.muted));
    }
    return std::move(line)
            .child(at::text(u16View(words.toUpper())))
            .child(at::text(u16View(QString::number(count))))
            .child(tracker(part));
}

// Today's overdue tasks (five at most) under their folding header, then "All N overdue in Tasks".
at::AnyElement DayPanelView::overdue(const DayData &d)
{
    at::Div column = at::div().flexCol();
    if (d.overdueCount <= 0)
        return std::move(column);
    std::move(column).child(sectionHeader(panelWords("Overdue"), d.overdueCount, m_overdueOpen, {Part::OverdueHeader}));
    if (!m_overdueOpen)
        return std::move(column);
    DayPanelView *self = this;
    const EntryRows rows{m_look, m_width,
                         [self](const QString &a, const QVariantMap &args) { self->act(a, args); },
                         [self](bool box, int k) { return self->tracker({box ? Part::OverdueBox : Part::Overdue, k}); },
                         [self](const AgendaEntry &e) { return self->subtitle(e, true); }};
    const int shown = std::min(int(d.overdue.size()), overdueLimit);
    for (int k = 0; k < shown; ++k)
        std::move(column).child(rows.row(k, d.overdue[std::size_t(k)], false));
    if (d.overdueCount > overdueLimit)
        std::move(column).child(at::div()
                                        .id(u"allOverdue")
                                        .relative()
                                        .flexRow()
                                        .itemsCenter()
                                        .gap(6)
                                        .h(28)
                                        .px(8)
                                        .rounded(m_look.radius)
                                        .textSize(m_look.smallSize)
                                        .fontMedium()
                                        .cursorPointer()
                                        .role(at::Role::Button)
                                        .ariaLabel(u16View(panelWords("All %n overdue in Tasks", d.overdueCount)))
                                        .onClick([self](const at::ClickEvent &, at::Window &, at::App &) { self->act(u"allOverdue"_s, {}); })
                                        .child(editor::atr::iconElement(u"list-todo"_s, 14, m_look.muted))
                                        .child(at::text(u16View(panelWords("All %n overdue in Tasks", d.overdueCount))))
                                        .child(tracker({Part::AllOverdue})));
    return std::move(column);
}

// The day's entries under "Plan of the day", or "Nothing planned" and why.
at::AnyElement DayPanelView::plan(const DayData &d)
{
    at::Div column = at::div().flexCol().child(sectionHeader(panelWords("Plan of the day"), int(d.entries.size()), std::nullopt, {Part::PlanHeader}));
    if (d.entries.empty())
        return std::move(column).child(
                at::div()
                        .flexCol()
                        .gap(2)
                        .px(8)
                        .pt(4)
                        .child(at::div().fontMedium().child(at::text(u16View(panelWords("Nothing planned")))))
                        .child(at::div()
                                       .textSize(m_look.smallSize)
                                       .textColor(m_look.muted)
                                       .child(at::text(u16View(d.hasJournal
                                                                       ? panelWords("This day has a journal entry, but no dates or reminders.")
                                                                       : panelWords("Tasks and reminders you add here go into this day's journal."))))));
    DayPanelView *self = this;
    const EntryRows rows{m_look, m_width,
                         [self](const QString &a, const QVariantMap &args) { self->act(a, args); },
                         [self](bool box, int k) { return self->tracker({box ? Part::Box : Part::Entry, k}); },
                         [self](const AgendaEntry &e) { return self->subtitle(e, false); }};
    for (std::size_t k = 0; k < d.entries.size(); ++k)
        std::move(column).child(rows.row(int(k), d.entries[k], false));
    return std::move(column);
}

void DayPanelView::act(const QString &action, const QVariantMap &args) const
{
    if (onAction)
        onAction(action, args);
}

} // namespace brulion::views::atr

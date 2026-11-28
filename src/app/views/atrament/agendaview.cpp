#include "agendaview.h"

#include "lookmap.h"
#include "planningrow.h"

#include "calendarmodel.h" // (its roles)
#include "pageicons.h"
#include "renderedspans.h"

#include <QCoreApplication>
#include <QColor>
#include <QHash>

#include <algorithm>

namespace brulion::views::atr {

namespace at = brulion::atrament;
using editor::atr::u16View;
using planning::CalendarModel;
using namespace Qt::StringLiterals;

namespace {

// PlanningItemRow's and CalendarAgendaList's words (their translations; not lupdate's).
QString dayWords(const char *text)
{
    return QCoreApplication::translate("CalendarAgendaList", text);
}

// `s` with its first letter upper case ("today" → "Today").
QString capitalized(const QString &s)
{
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1);
}


constexpr ColorField<AgendaLook> kColors[] = {
        {"background", &AgendaLook::background}, {"text", &AgendaLook::text},
        {"muted", &AgendaLook::muted},           {"faint", &AgendaLook::faint},
        {"hover", &AgendaLook::hover},           {"today", &AgendaLook::today},
        {"onToday", &AgendaLook::onToday},       {"todayText", &AgendaLook::todayText},
        {"selected", &AgendaLook::selected},     {"onSelected", &AgendaLook::onSelected},
        {"disc", &AgendaLook::disc},             {"border", &AgendaLook::border},
        {"separator", &AgendaLook::separator},   {"focus", &AgendaLook::focus},
        {"box", &AgendaLook::box},               {"accent", &AgendaLook::accent},
        {"reminder", &AgendaLook::reminder}};
constexpr SizeField<AgendaLook> kSizes[] = {
        {"textSize", &AgendaLook::textSize},     {"smallSize", &AgendaLook::smallSize},
        {"dayTextSize", &AgendaLook::dayTextSize}, {"numberSize", &AgendaLook::numberSize},
        {"chipSize", &AgendaLook::chipSize},     {"lineHeight", &AgendaLook::lineHeight},
        {"maxWidth", &AgendaLook::maxWidth},     {"sideMargin", &AgendaLook::sideMargin},
        {"radius", &AgendaLook::radius},         {"marker", &AgendaLook::marker},
        {"entryMarker", &AgendaLook::entryMarker}, {"rowHeight", &AgendaLook::rowHeight},
        {"chipMinWidth", &AgendaLook::chipMinWidth}};

} // namespace

AgendaLook::ChipColors AgendaLook::chip(const QString &variant) const
{
    return chipOf(chips, variant);
}

AgendaLook AgendaLook::fromMap(const QVariantMap &look)
{
    AgendaLook t = lookFrom(look, kColors, kSizes);
    t.chips = chipsFrom(look.value(u"chips"_s));
    return t;
}

AgendaEntry AgendaEntry::from(const QVariantMap &map)
{
    AgendaEntry e;
    e.map = map;
    const auto text = [&map](const char *key) { return map.value(QString::fromLatin1(key)).toString(); };
    const auto flag = [&map](const char *key) { return map.value(QString::fromLatin1(key)).toBool(); };
    e.kind = text("kind");
    e.variant = text("variant");
    e.icon = text("icon");
    e.time = text("time");
    e.title = text("title");
    e.status = text("status");
    e.path = text("path");
    e.pageTitle = text("pageTitle");
    e.spec = text("spec");
    e.color = text("color");
    e.endTime = text("endTime");
    e.calendarName = text("calendarName");
    e.location = text("location");
    e.isTask = flag("isTask");
    e.closed = flag("closed");
    e.recurring = flag("recurring");
    e.continued = flag("continued");
    e.isJournalPage = flag("isJournalPage");
    return e;
}

QString AgendaEntry::kindLabel() const
{
    static const QHash<QString, const char *> variants{{u"overdue"_s, QT_TRANSLATE_NOOP("PlanningItemRow", "Overdue")},
                                                       {u"alarm"_s, QT_TRANSLATE_NOOP("PlanningItemRow", "Alarm")},
                                                       {u"reminder"_s, QT_TRANSLATE_NOOP("PlanningItemRow", "Reminder")},
                                                       {u"scheduled"_s, QT_TRANSLATE_NOOP("PlanningItemRow", "Scheduled")}};
    static const QHash<QString, const char *> kinds{{u"due"_s, QT_TRANSLATE_NOOP("PlanningItemRow", "Due")},
                                                    {u"start"_s, QT_TRANSLATE_NOOP("PlanningItemRow", "Starts")},
                                                    {u"journal"_s, QT_TRANSLATE_NOOP("PlanningItemRow", "Journal")}};
    if (variant == u"done")
        return entryWords(kind == u"done" ? "Done" : "Finished");
    if (const char *words = variants.value(variant))
        return entryWords(words);
    const char *words = kinds.value(kind);
    return words ? entryWords(words) : QString();
}

QString AgendaEntry::eventDetails() const
{
    const QString until = !endTime.isEmpty() ? entryWords("until %1").arg(endTime)
                        : continued          ? entryWords("continues")
                                             : QString();
    return joined({until, calendarName, location}, u" · "_s);
}

AgendaView::Part AgendaView::partNamed(const QString &name)
{
    static const QHash<QString, Part::Kind> kinds{
            {u"header"_s, Part::Header}, {u"journal"_s, Part::Journal}, {u"entry"_s, Part::Entry}, {u"box"_s, Part::Box}};
    return partFromName<Part>(name, kinds, Part::Day);
}

AgendaView::AgendaView(at::Context<AgendaView> &cx, QAbstractItemModel *model)
    : m_model(model)
    , m_id(cx.entityId())
    , m_rows(m_look.marker + m_look.lineHeight * 2 + 48)
{
    // (The current day is its date: it stays current as days come and go around it.)
    m_rows.followFor(model, cx, [](AgendaView &) {});
}

AgendaView::~AgendaView() = default;

void AgendaView::setLook(AgendaLook look, at::Context<AgendaView> &cx)
{
    m_rows.restyle(m_look, std::move(look), cx);
}

AgendaView::RowData AgendaView::rowData(int row) const
{
    RowData d;
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return d;
    const QModelIndex i = m_model->index(row, 0);
    d.date = i.data(CalendarModel::DateRole).toString();
    d.longLabel = i.data(CalendarModel::LongLabelRole).toString();
    d.day = i.data(CalendarModel::DayRole).toInt();
    d.isToday = i.data(CalendarModel::IsTodayRole).toBool();
    d.isPast = i.data(CalendarModel::IsPastRole).toBool();
    d.isSelected = i.data(CalendarModel::IsSelectedRole).toBool();
    d.hasJournal = i.data(CalendarModel::HasJournalRole).toBool();
    for (const QVariant &entry : i.data(CalendarModel::ItemsRole).toList())
        d.entries.push_back(AgendaEntry::from(entry.toMap()));
    return d;
}

int AgendaView::rowOfDate(const QString &date) const
{
    return date.isEmpty() ? -1 : rowWhere(m_model, CalendarModel::DateRole, date);
}

int AgendaView::selectedRow() const
{
    return rowWhere(m_model, CalendarModel::IsSelectedRole, true);
}

QString AgendaView::relativeWords(const RowData &d) const
{
    if (d.isToday)
        return dayWords("Today");
    const QString human = formatHuman ? formatHuman(d.date) : QString();
    return human.contains(u' ') ? QString() : capitalized(human);
}

QString AgendaView::subtitle(const AgendaEntry &e) const
{
    if (e.isEvent())
        return e.eventDetails();
    const QString day = e.isJournalPage && journalDay ? journalDay(e.path) : QString();
    return day.isEmpty() ? e.pageTitle : entryWords("Journal · %1").arg(day);
}

at::AnyElement AgendaView::render(at::Window &window, at::Context<AgendaView> &cx)
{
    m_parts.clear();
    if (!m_focus)
        m_focus = cx.newFocusHandle();
    m_ringRow = m_focus.isFocused(window) ? currentRow() : -1;
    m_width = std::min(window.viewportSize().width - 2 * m_look.sideMargin, m_look.maxWidth);
    AgendaView *self = this;
    const at::EntityId id = cx.entityId();
    const std::u16string family = m_look.uiFamily;
    return at::div()
            .id(u"agenda")
            .sizeFull()
            .flexCol()
            .bg(m_look.background)
            .when(!family.empty(), [family](at::Div &&d) { return std::move(d).fontFamily(family); })
            .trackFocus(m_focus)
            .onKeyDown([id](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
                a.updateEntity<AgendaView>(id, [&](AgendaView &v, at::Context<AgendaView> &c) {
                    if (v.keyDown(e, c))
                        w.stopPropagation();
                });
            })
            .child(at::list(u"agenda", m_rows.heights(),
                            [self](std::size_t row, at::Window &, at::App &) -> at::AnyElement { return self->renderRow(row); })
                           .trackScroll(m_rows.list())
                           .ariaLabel(u"Agenda")
                           .flex1());
}

// A day: its header, its entries (or "Nothing planned."), a separator; centred, at most maxWidth.
at::AnyElement AgendaView::renderRow(std::size_t row)
{
    const RowData d = rowData(int(row));
    const int r = int(row);
    at::Div entries = at::div().flexCol().mt(4);
    for (std::size_t k = 0; k < d.entries.size(); ++k)
        std::move(entries).child(entryRow(r, int(k), d, d.entries[k]));
    if (d.entries.empty())
        std::move(entries).child(at::div()
                                         .ml(m_look.marker + 12)
                                         .textSize(m_look.smallSize)
                                         .textColor(m_look.faint)
                                         .child(at::text(u16View(d.hasJournal ? dayWords("Only a journal entry.")
                                                                              : dayWords("Nothing planned.")))));
    return at::div()
            .id(at::ElementId::number(qHash(d.date)))
            .px(m_look.sideMargin)
            .pt(8)
            .pb(16)
            .child(at::div()
                           .relative()
                           .wFull()
                           .maxW(m_look.maxWidth)
                           .mxAuto()
                           .flexCol()
                           .textColor(m_look.text)
                           .child(header(r, d))
                           .child(std::move(entries))
                           .child(at::div().mt(8).h(1).bg(m_look.separator))
                           .child(tracker(r, {Part::Day})));
}

// The day's header (a button that picks the day): its disc, its title and relative words, its
// "Journal" button.
at::AnyElement AgendaView::header(int row, const RowData &d)
{
    const QString date = d.date;
    const QString title = capitalized(d.longLabel);
    const QString relative = relativeWords(d);
    at::Div labels = at::div()
                             .flex1()
                             .minW0()
                             .flexCol()
                             .child(at::div()
                                            .textSize(m_look.dayTextSize)
                                            .fontSemibold()
                                            .child(at::text(u16View(title)).maxLines(1)));
    if (!relative.isEmpty())
        std::move(labels).child(at::div()
                                        .textSize(m_look.smallSize)
                                        .textColor(d.isToday ? m_look.todayText : m_look.muted)
                                        .child(at::text(u16View(relative)).maxLines(1)));
    const bool ring = row == m_ringRow;
    at::Div line = at::div()
                           .relative()
                           .flexRow()
                           .itemsCenter()
                           .gap(12)
                           .rounded(m_look.radius)
                           .border(ring ? 2.0f : 0.0f)
                           .borderColor(m_look.focus)
                           .cursorPointer()
                           .role(at::Role::Button)
                           .ariaLabel(u16View(d.isToday ? title + u", "_s + dayWords("today") : title))
                           .selected(d.isSelected)
                           .onClick([id = m_id, date](const at::ClickEvent &, at::Window &, at::App &a) {
                               a.updateEntity<AgendaView>(id, [&](AgendaView &v, at::Context<AgendaView> &c) {
                                   v.m_currentDate.clear();
                                   c.notify();
                                   v.act(u"day"_s, {{u"date"_s, date}});
                               });
                           })
                           .child(dayDisc(d))
                           .child(std::move(labels));
    if (d.hasJournal)
        std::move(line).child(journalButton(row, d));
    return std::move(line).child(tracker(row, {Part::Header}));
}

// The day's number on its disc: today's red, the selected day's dark, a past day's outlined.
at::AnyElement AgendaView::dayDisc(const RowData &d)
{
    const bool outlined = d.pastDay() && !d.isSelected;
    const at::Color disc = d.isToday ? m_look.today : d.isSelected ? m_look.selected : outlined ? at::transparent() : m_look.disc;
    const at::Color number = d.isToday ? m_look.onToday : d.isSelected ? m_look.onSelected : outlined ? m_look.muted : m_look.text;
    return at::div()
            .size(m_look.marker)
            .flexShrink0()
            .roundedFull()
            .bg(disc)
            .border(outlined ? 1.0f : 0.0f)
            .borderColor(m_look.border)
            .flexRow()
            .itemsCenter()
            .justifyCenter()
            .textSize(m_look.numberSize)
            .fontBold()
            .fontFeatures(u"tnum")
            .textColor(number)
            .ariaHidden()
            .child(at::text(u16View(QString::number(d.day))));
}

// A day with a daily note: its "Journal" button (a ghost button, its icon and word).
at::AnyElement AgendaView::journalButton(int row, const RowData &d)
{
    const AgendaView *self = this;
    const QString date = d.date;
    return at::div()
            .relative()
            .flexShrink0()
            .flexRow()
            .itemsCenter()
            .gap(6)
            .px(10)
            .py(4)
            .rounded(m_look.radius)
            .hover([look = m_look.hover](at::Style &s) { s.background = at::Background::solid(look); })
            .textSize(m_look.smallSize)
            .fontMedium()
            .cursorPointer()
            .role(at::Role::Button)
            .ariaLabel(u16View(dayWords("Open the journal of %1").arg(d.longLabel)))
            .onClick([self, date](const at::ClickEvent &, at::Window &w, at::App &) {
                self->act(u"journal"_s, {{u"date"_s, date}});
                w.stopPropagation();
            })
            .child(editor::atr::iconElement(u"book-open"_s, 16, m_look.muted))
            .child(at::text(u16View(dayWords("Journal"))))
            .child(tracker(row, {Part::Journal}));
}

// An entry as PlanningItemRow (planningrow.h), its parts the agenda's.
at::AnyElement AgendaView::entryRow(int row, int index, const RowData &d, const AgendaEntry &e)
{
    AgendaView *self = this;
    const EntryRows rows{m_look, m_width,
                         [self](const QString &action, const QVariantMap &args) { self->act(action, args); },
                         [self, row](bool box, int k) { return self->tracker(row, {box ? Part::Box : Part::Entry, k}); },
                         [self](const AgendaEntry &entry) { return self->subtitle(entry); }};
    return rows.row(index, e, d.pastDay());
}

void AgendaView::act(const QString &action, const QVariantMap &args) const
{
    if (onAction)
        onAction(action, args);
}

void AgendaView::showRow(int row, at::Context<AgendaView> &cx)
{
    if (row < 0)
        return; // (a row past the last: the list shows none)
    m_rows.list().scrollToRow(std::size_t(row), at::ListPosition::Start);
    cx.notify();
}

void AgendaView::revealRow(int row, at::Context<AgendaView> &cx)
{
    m_currentDate = rowData(row).date;
    m_rows.reveal(row);
    cx.notify();
}

// ↑ / ↓ move the current day (the first from the selected one), Enter picks it.
bool AgendaView::keyDown(const at::KeyDownEvent &e, at::Context<AgendaView> &cx)
{
    const at::Keystroke &k = e.keystroke;
    if (k.modifiers.any())
        return false;
    if (k.key == u"down" || k.key == u"up")
        return moveCurrent(k.key == u"down" ? 1 : -1, cx);
    return k.key == u"enter" && pickCurrent();
}

// The day the keys start from: the current one, else the selected one (−1: none).
int AgendaView::keysDay() const
{
    const int current = currentRow();
    return current >= 0 ? current : selectedRow();
}

bool AgendaView::moveCurrent(int step, at::Context<AgendaView> &cx)
{
    const int count = m_model ? m_model->rowCount() : 0;
    if (count == 0)
        return false;
    const int from = keysDay();
    revealRow(std::clamp(from < 0 ? 0 : from + step, 0, count - 1), cx);
    return true;
}

bool AgendaView::pickCurrent() const
{
    const int day = keysDay();
    if (day < 0)
        return false;
    act(u"day"_s, {{u"date"_s, rowData(day).date}});
    return true;
}

} // namespace brulion::views::atr

#include "monthview.h"

#include "calendarmodel.h" // (its roles)
#include "pageicons.h"
#include "renderedspans.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace brulion::views::atr {

namespace at = brulion::atrament;
using editor::atr::u16View;
using planning::CalendarModel;
using namespace Qt::StringLiterals;

namespace {

// CalendarMonthGrid's words (its translations; not lupdate's).
QString gridWords(const char *text, int n = -1)
{
    return QCoreApplication::translate("CalendarMonthGrid", text, nullptr, n);
}

constexpr ColorField<MonthLook> kColors[] = {
        {"background", &MonthLook::background}, {"text", &MonthLook::text},         {"muted", &MonthLook::muted},
        {"faint", &MonthLook::faint},           {"grid", &MonthLook::grid},         {"today", &MonthLook::today},
        {"onToday", &MonthLook::onToday},       {"selected", &MonthLook::selected}, {"onSelected", &MonthLook::onSelected},
        {"focus", &MonthLook::focus},           {"weekNote", &MonthLook::weekNote}};
constexpr SizeField<MonthLook> kSizes[] = {
        {"smallSize", &MonthLook::smallSize},   {"tinySize", &MonthLook::tinySize}, {"headerHeight", &MonthLook::headerHeight},
        {"weekColumn", &MonthLook::weekColumn}, {"pillHeight", &MonthLook::pillHeight}, {"pillsTop", &MonthLook::pillsTop},
        {"disc", &MonthLook::disc},             {"radiusSm", &MonthLook::radiusSm}, {"dotsBelow", &MonthLook::dotsBelow},
        {"timesFrom", &MonthLook::timesFrom}};

// A narrow cell's icon of a kind of open entry (CalendarMonthGrid.markIcon).
QString markIcon(const QString &kind)
{
    static const QHash<QString, QString> icons{{u"overdue"_s, u"circle-alert"_s},
                                               {u"reminder"_s, u"bell"_s},
                                               {u"scheduled"_s, u"hourglass"_s},
                                               {u"event"_s, u"calendar-clock"_s}};
    return icons.value(kind, u"calendar"_s);
}

QString text(const QVariantMap &m, const char *key)
{
    return m.value(QString::fromLatin1(key)).toString();
}

} // namespace

at::Color MonthLook::mark(const QString &kind) const
{
    const auto it = marks.find(kind);
    return it != marks.end() ? it->second : muted;
}

MonthLook MonthLook::fromMap(const QVariantMap &look)
{
    MonthLook t = lookFrom(look, kColors, kSizes);
    t.chips = chipsFrom(look.value(u"chips"_s));
    const QVariantMap marks = look.value(u"marks"_s).toMap();
    for (auto it = marks.cbegin(); it != marks.cend(); ++it)
        t.marks[it.key()] = colorAt(marks, it.key(), t.muted);
    t.weekdays = look.value(u"weekdays"_s).toStringList();
    for (const QString &monday : look.value(u"weekNotes"_s).toStringList())
        t.weekNotes.push_back(monday);
    return t;
}

MonthView::Part MonthView::partNamed(const QString &name)
{
    static const QHash<QString, Part::Kind> kinds{{u"pill"_s, Part::Pill}, {u"more"_s, Part::More}, {u"week"_s, Part::Week}};
    return partFromName<Part>(name, kinds, Part::Cell);
}

MonthView::MonthView(at::Context<MonthView> &cx, QAbstractItemModel *model)
    : m_model(model)
    , m_rows(0)
{
    m_rows.followFor(model, cx, [](MonthView &) {});
}

MonthView::~MonthView() = default;

void MonthView::setLook(MonthLook look, at::Context<MonthView> &cx)
{
    m_rows.restyle(m_look, std::move(look), cx);
}

MonthView::DayData MonthView::dayData(int row) const
{
    DayData d;
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return d;
    const QModelIndex i = m_model->index(row, 0);
    d.date = i.data(CalendarModel::DateRole).toString();
    d.longLabel = i.data(CalendarModel::LongLabelRole).toString();
    d.day = i.data(CalendarModel::DayRole).toInt();
    d.dayOfWeek = i.data(CalendarModel::DayOfWeekRole).toInt();
    d.weekNumber = i.data(CalendarModel::WeekNumberRole).toInt();
    d.itemCount = i.data(CalendarModel::ItemCountRole).toInt();
    d.inMonth = i.data(CalendarModel::InMonthRole).toBool();
    d.isToday = i.data(CalendarModel::IsTodayRole).toBool();
    d.isSelected = i.data(CalendarModel::IsSelectedRole).toBool();
    d.hasJournal = i.data(CalendarModel::HasJournalRole).toBool();
    for (const QVariant &e : i.data(CalendarModel::OrderedItemsRole).toList())
        d.entries.push_back(e.toMap());
    d.openKinds = i.data(CalendarModel::OpenKindsRole).toStringList();
    return d;
}

int MonthView::selectedRow() const
{
    return rowWhere(m_model, CalendarModel::IsSelectedRole, true);
}

QString MonthView::cellName(const DayData &d)
{
    QString out = d.longLabel;
    if (d.isToday)
        out += u", "_s + gridWords("today");
    if (d.itemCount > 0)
        out += u", "_s + gridWords("%n entries", d.itemCount);
    if (d.hasJournal)
        out += u", "_s + gridWords("journal");
    return out;
}

QString MonthView::pillName(const QVariantMap &entry)
{
    const bool closed = entry.value(u"closed"_s).toBool();
    const QString state = !closed ? QString() : text(entry, "status") == u"cancelled" ? gridWords("cancelled") : gridWords("done");
    QStringList parts;
    for (const QString &p : {text(entry, "time"), text(entry, "title"), state})
        if (!p.isEmpty())
            parts.append(p);
    return parts.join(u", "_s);
}

int MonthView::pillsFitting(float height) const
{
    return std::max(0, int(std::floor((height - m_look.pillsTop - 2) / (m_look.pillHeight + 2))));
}

at::AnyElement MonthView::render(at::Window &window, at::Context<MonthView> &cx)
{
    m_parts.clear();
    if (!m_focus)
        m_focus = cx.newFocusHandle();
    m_focused = m_focus.isFocused(window);
    const at::Size size = window.viewportSize();
    m_cellWidth = (size.width - m_look.weekColumn) / 7;
    const float cellHeight = std::max(1.0f, (size.height - m_look.headerHeight) / 6);
    const at::EntityId id = cx.entityId();
    const std::u16string family = m_look.uiFamily;
    at::Div grid = at::div()
                           .id(u"month")
                           .sizeFull()
                           .flexCol()
                           .bg(m_look.background)
                           .textColor(m_look.text)
                           .when(!family.empty(), [family](at::Div &&d) { return std::move(d).fontFamily(family); })
                           .trackFocus(m_focus)
                           .role(at::Role::Table)
                           .ariaLabel(u"Month")
                           .onKeyDown([id](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
                               a.updateEntity<MonthView>(id, [&](MonthView &v, at::Context<MonthView> &) {
                                   if (v.keyDown(e))
                                       w.stopPropagation();
                               });
                           })
                           .child(header());
    for (int w = 0; w < 6; ++w)
        std::move(grid).child(week(w, cellHeight));
    return std::move(grid);
}

// The weekday names over the columns (a weekend's faint), from the first week's days.
at::AnyElement MonthView::header()
{
    const bool dots = m_cellWidth < m_look.dotsBelow;
    at::Div row = at::div()
                          .flexRow()
                          .flexShrink0()
                          .h(m_look.headerHeight)
                          .pl(m_look.weekColumn)
                          .textSize(m_look.tinySize)
                          .fontSemibold()
                          .ariaHidden();
    for (int c = 0; c < 7; ++c) {
        const int dow = dayData(c).dayOfWeek;
        const QString name = (weekdayName ? weekdayName(dow) : m_look.weekdays.value(dow - 1)).toUpper();
        std::move(row).child(at::div()
                                     .flex1()
                                     .flexRow()
                                     .itemsCenter()
                                     .when(dots, [](at::Div &&d) { return std::move(d).justifyCenter(); })
                                     .when(!dots, [](at::Div &&d) { return std::move(d).pl(8); })
                                     .textColor(dow >= 6 ? m_look.faint : m_look.muted)
                                     .child(at::text(u16View(name))));
    }
    return std::move(row);
}

// A week: its number, its seven days.
at::AnyElement MonthView::week(int week, float cellHeight)
{
    at::Div row = at::div().flexRow().h(cellHeight).flexShrink0();
    if (m_look.weekColumn > 0)
        std::move(row).child(weekNumber(week));
    for (int c = 0; c < 7; ++c)
        std::move(row).child(cell(week * 7 + c, cellHeight));
    return std::move(row);
}

// The ISO week of the row's middle day; a click opens its note (its Monday's).
at::AnyElement MonthView::weekNumber(int week)
{
    const DayData middle = dayData(week * 7 + 3);
    const DayData first = dayData(week * 7);
    // (The week's Monday: the first day's date moved to its Monday.)
    const QDate start = QDate::fromString(first.date, Qt::ISODate);
    const QString monday = start.isValid() ? start.addDays(1 - first.dayOfWeek).toString(Qt::ISODate) : QString();
    const bool note = std::find(m_look.weekNotes.begin(), m_look.weekNotes.end(), monday) != m_look.weekNotes.end();
    const MonthView *self = this;
    const QString words = QString::number(middle.weekNumber);
    return at::div()
            .id(at::ElementId(u"week", quint64(week)))
            .relative()
            .w(m_look.weekColumn)
            .flexShrink0()
            .pt(8)
            .flexRow()
            .justifyCenter()
            .borderR(1)
            .borderColor(m_look.grid)
            .textSize(m_look.tinySize)
            .textColor(note ? m_look.weekNote : m_look.faint)
            .when(note, [](at::Div &&d) { return std::move(d).fontSemibold(); })
            .cursorPointer()
            .role(at::Role::Button)
            .ariaLabel(u16View(QCoreApplication::translate("CalendarWeekNumber", note ? "Week %1: open its note" : "Week %1: write its note")
                                       .arg(middle.weekNumber)))
            .onClick([self, monday](const at::ClickEvent &, at::Window &, at::App &) {
                self->act(u"week"_s, {{u"monday"_s, monday}});
            })
            .child(at::text(u16View(words)))
            .child(tracker(week, {Part::Week}));
}

// A day: its number, its daily note's book, its pills and "+N more" (or a narrow cell's icons); a
// click selects it (on a pill: opens that entry), a double click opens its daily note.
at::AnyElement MonthView::cell(int row, float cellHeight)
{
    const DayData d = dayData(row);
    const bool ring = d.isSelected && m_focused;
    const MonthView *self = this;
    const QString date = d.date;
    at::Div column = at::div().absolute().inset(0).flexCol().gap(2).px(4).child(cellTop(d));
    if (m_cellWidth < m_look.dotsBelow)
        std::move(column).child(marks(d));
    else
        pills(column, row, d, pillsFitting(cellHeight));
    return at::div()
            .id(at::ElementId::number(qHash(d.date)))
            .relative()
            .flex1()
            .minW0()
            .borderR(row % 7 < 6 ? 1.0f : 0.0f)
            .borderB(1)
            .borderColor(m_look.grid)
            .overflowHidden()
            .cursorPointer()
            .role(at::Role::Cell)
            .ariaLabel(u16View(cellName(d)))
            .selected(d.isSelected)
            .onClick([self, date](const at::ClickEvent &click, at::Window &, at::App &) {
                self->act(click.clickCount() >= 2 ? u"open"_s : u"day"_s, {{u"date"_s, date}});
            })
            .child(std::move(column))
            .when(ring, [focus = m_look.focus](at::Div &&c) {
                return std::move(c).child(at::div().absolute().inset(1).border(2).borderColor(focus).rounded(2));
            })
            .child(tracker(row, {Part::Cell}));
}

// A cell's top line: the day's number, a daily note's book (a narrow cell: the number centred).
at::AnyElement MonthView::cellTop(const DayData &d)
{
    const bool dots = m_cellWidth < m_look.dotsBelow;
    at::Div top = at::div().flexRow().itemsCenter().gap(4).h(m_look.pillsTop - 2).pl(dots ? 0 : 2).child(number(d));
    if (dots)
        std::move(top).justifyCenter();
    if (d.hasJournal && !dots)
        std::move(top).child(editor::atr::iconElement(u"book-open"_s, 12, m_look.faint));
    return std::move(top);
}

// The day's pills that fit (`fitting`), the last place "+N more" when they do not all.
void MonthView::pills(at::Div &column, int row, const DayData &d, int fitting)
{
    const int count = int(d.entries.size());
    const bool overflow = count > fitting;
    const int shown = overflow ? std::max(0, fitting - 1) : count;
    for (int k = 0; k < shown; ++k)
        std::move(column).child(pill(row, k, d, d.entries[std::size_t(k)]));
    const QString more = gridWords("+%n more", count - shown);
    if (overflow)
        std::move(column).child(at::div()
                                        .relative()
                                        .h(m_look.pillHeight)
                                        .pl(5)
                                        .flexRow()
                                        .itemsCenter()
                                        .textSize(m_look.tinySize)
                                        .textColor(m_look.faint)
                                        .role(at::Role::Text)
                                        .ariaLabel(u16View(more))
                                        .child(at::text(u16View(more)))
                                        .child(tracker(row, {Part::More})));
}

// The day's number: today on a red disc, the selected day on a dark one, another month's faint.
at::AnyElement MonthView::number(const DayData &d)
{
    const bool marked = d.isToday || d.isSelected;
    const at::Color disc = d.isToday ? m_look.today : d.isSelected ? m_look.selected : at::transparent();
    const at::Color ink = marked ? (d.isToday ? m_look.onToday : m_look.onSelected) : d.inMonth ? m_look.text : m_look.faint;
    const std::uint16_t weight = marked ? at::FontWeight::Bold : d.inMonth ? at::FontWeight::Medium : at::FontWeight::Normal;
    return at::div()
            .size(m_look.disc)
            .flexShrink0()
            .roundedFull()
            .bg(disc)
            .flexRow()
            .itemsCenter()
            .justifyCenter()
            .textSize(m_look.smallSize)
            .fontFeatures(u"tnum")
            .fontWeight(weight)
            .textColor(ink)
            .child(at::text(u16View(QString::number(d.day))));
}

// An entry's pill: its variant's colours (an alarm in the reminder's, an event in its calendar's),
// a check / a cross / an alarm clock / a repeat icon, its time in wide cells and its title.
at::AnyElement MonthView::pill(int row, int index, const DayData &d, const QVariantMap &entry)
{
    const QString variant = text(entry, "variant");
    const bool event = variant == u"event";
    const ChipColors colors = m_look.chip(variant == u"alarm" ? u"reminder"_s : variant);
    const QColor eventColor(text(entry, "color"));
    const at::Color calendar = eventColor.isValid()
                                       ? at::Color{float(eventColor.redF()), float(eventColor.greenF()), float(eventColor.blueF()), 1}
                                       : m_look.muted;
    const at::Color ink = event ? m_look.text : colors.text;
    const QString icon = pillIcon(entry);
    std::vector<at::TextSpan> spans;
    const QString words = pillWords(entry, spans);
    const MonthView *self = this;
    const QString date = d.date;
    at::Div p = at::div()
                        .id(at::ElementId(u"pill", quint64(index)))
                        .relative()
                        .h(m_look.pillHeight)
                        .flexShrink0()
                        .flexRow()
                        .itemsCenter()
                        .gap(3)
                        .pl(5)
                        .pr(3)
                        .rounded(m_look.radiusSm)
                        .bg(event ? at::Color{calendar.r, calendar.g, calendar.b, 0.2f} : colors.background)
                        .textSize(m_look.tinySize)
                        .textColor(ink)
                        .overflowHidden()
                        .role(at::Role::Button)
                        .ariaLabel(u16View(pillName(entry)))
                        .onClick([self, date, entry](const at::ClickEvent &, at::Window &w, at::App &) {
                            self->act(u"entry"_s, {{u"date"_s, date}, {u"entry"_s, entry}});
                            w.stopPropagation();
                        });
    if (event)
        std::move(p).child(at::div().absolute().left(0).top(0).bottom(0).w(3).rounded(1.5f).bg(calendar));
    if (!icon.isEmpty())
        std::move(p).child(editor::atr::iconElement(icon, 10, ink));
    return std::move(p)
            .child(at::div().flex1().minW0().child(at::text(u16View(words)).spans(spans).maxLines(1)))
            .child(tracker(row, {Part::Pill, index}));
}

// A pill's icon: a done entry's check, a cancelled one's cross, an alarm's clock, a series' repeat.
QString MonthView::pillIcon(const QVariantMap &entry)
{
    if (entry.value(u"closed"_s).toBool())
        return text(entry, "status") == u"cancelled" ? u"x"_s : u"check"_s;
    if (text(entry, "variant") == u"alarm")
        return u"alarm-clock"_s;
    return entry.value(u"recurring"_s).toBool() ? u"repeat"_s : QString();
}

// A pill's words: its title, after its time in bold in cells wide enough.
QString MonthView::pillWords(const QVariantMap &entry, std::vector<at::TextSpan> &spans) const
{
    const QString time = text(entry, "time");
    const QString title = text(entry, "title");
    if (m_cellWidth < m_look.timesFrom || time.isEmpty())
        return title;
    at::TextStyleRefinement bold;
    bold.fontWeight = at::FontWeight{at::FontWeight::Bold};
    spans.push_back({at::Range{0, int(time.size())}, bold});
    return time + u' ' + title;
}

// A narrow cell's icons: one per kind of open entry, in its colour.
at::AnyElement MonthView::marks(const DayData &d)
{
    at::Div row = at::div().flexRow().justifyCenter().gap(2);
    for (const QString &kind : d.openKinds)
        std::move(row).child(editor::atr::iconElement(markIcon(kind), 14, m_look.mark(kind)));
    return std::move(row);
}

void MonthView::act(const QString &action, const QVariantMap &args) const
{
    if (onAction)
        onAction(action, args);
}

// The month's keys (CalendarMonthGrid's): the selected day moved, another month, Enter.
bool MonthView::keyDown(const at::KeyDownEvent &e)
{
    const at::Keystroke &k = e.keystroke;
    const QString key = QString::fromStdU16String(std::u16string(k.key));
    const bool ctrl = k.modifiers.control;
    if (k.modifiers.any() && !(ctrl && key == u"enter"))
        return false;
    if (const std::optional<int> days = moveOf(key)) {
        act(u"move"_s, {{u"days"_s, *days}});
        return true;
    }
    if (key == u"pageup" || key == u"pagedown") {
        act(key == u"pageup" ? u"previous"_s : u"next"_s, {});
        return true;
    }
    return key == u"enter" && pickSelected(ctrl);
}

// How far a key moves the selected day: ± 1, ± 7, to its week's first or last day.
std::optional<int> MonthView::moveOf(const QString &key) const
{
    static const QHash<QString, int> moves{{u"left"_s, -1}, {u"right"_s, 1}, {u"up"_s, -7}, {u"down"_s, 7}};
    if (moves.contains(key))
        return moves.value(key);
    if (key != u"home" && key != u"end")
        return std::nullopt;
    const int selected = selectedRow();
    const int inWeek = selected < 0 ? 0 : selected % 7; // (the grid's columns start the week)
    return key == u"home" ? -inWeek : 6 - inWeek;
}

// Enter: the selected day picked (Ctrl: its daily note opened); none selected: nothing.
bool MonthView::pickSelected(bool ctrl) const
{
    const DayData d = dayData(selectedRow());
    if (d.date.isEmpty())
        return false;
    act(ctrl ? u"open"_s : u"day"_s, {{u"date"_s, d.date}});
    return true;
}

} // namespace brulion::views::atr

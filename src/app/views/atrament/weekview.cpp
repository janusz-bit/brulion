#include "weekview.h"

#include "calendarkeys.h"
#include "calendarmodel.h" // (its roles)
#include "pageicons.h"
#include "renderedspans.h"

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QHash>

#include <algorithm>

namespace brulion::views::atr {

namespace at = brulion::atrament;
using editor::atr::u16View;
using planning::CalendarModel;
using namespace Qt::StringLiterals;

namespace {

// CalendarWeekGrid's words (its translations; not lupdate's).
QString weekWords(const char *text, int n = -1)
{
    return QCoreApplication::translate("CalendarWeekGrid", text, nullptr, n);
}

QString text(const QVariantMap &m, const char *key)
{
    return m.value(QString::fromLatin1(key)).toString();
}

at::Color colorOf(const QString &name, at::Color fallback, float alpha)
{
    const QColor c(name);
    return c.isValid() ? at::Color{float(c.redF()), float(c.greenF()), float(c.blueF()), alpha} : fallback;
}

constexpr ColorField<WeekLook> kColors[] = {
        {"background", &WeekLook::background}, {"text", &WeekLook::text},         {"muted", &WeekLook::muted},
        {"faint", &WeekLook::faint},           {"grid", &WeekLook::grid},         {"border", &WeekLook::border},
        {"today", &WeekLook::today},           {"onToday", &WeekLook::onToday},   {"selected", &WeekLook::selected},
        {"onSelected", &WeekLook::onSelected}, {"selectedColumn", &WeekLook::selectedColumn},
        {"weekend", &WeekLook::weekend},       {"now", &WeekLook::now},           {"focus", &WeekLook::focus}};
constexpr SizeField<WeekLook> kSizes[] = {
        {"gutter", &WeekLook::gutter},         {"hourHeight", &WeekLook::hourHeight}, {"headerHeight", &WeekLook::headerHeight},
        {"pillHeight", &WeekLook::pillHeight}, {"narrowBelow", &WeekLook::narrowBelow}, {"smallSize", &WeekLook::smallSize},
        {"tinySize", &WeekLook::tinySize},     {"radiusSm", &WeekLook::radiusSm}};

} // namespace

WeekLook WeekLook::fromMap(const QVariantMap &look)
{
    WeekLook t = lookFrom(look, kColors, kSizes);
    t.chips = chipsFrom(look.value(u"chips"_s));
    t.weekdays = look.value(u"weekdays"_s).toStringList();
    return t;
}

WeekView::Part WeekView::partNamed(const QString &name)
{
    static const QHash<QString, Part::Kind> kinds{
            {u"head"_s, Part::Head}, {u"allday"_s, Part::AllDay}, {u"timed"_s, Part::Timed}, {u"hours"_s, Part::Hours}};
    return partFromName<Part>(name, kinds, Part::Column);
}

WeekView::WeekView(at::Context<WeekView> &cx, QAbstractItemModel *model)
    : m_model(model)
    , m_rows(0)
{
    m_rows.followFor(model, cx, [](WeekView &) {});
}

WeekView::~WeekView() = default;

void WeekView::setLook(WeekLook look, at::Context<WeekView> &cx)
{
    m_rows.restyle(m_look, std::move(look), cx);
}

int WeekView::currentRow() const
{
    return rowWhere(m_model, CalendarModel::IsSelectedRole, true);
}

WeekView::DayData WeekView::dayData(int row) const
{
    DayData d;
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return d;
    const QModelIndex i = m_model->index(row, 0);
    d.date = i.data(CalendarModel::DateRole).toString();
    d.longLabel = i.data(CalendarModel::LongLabelRole).toString();
    d.day = i.data(CalendarModel::DayRole).toInt();
    d.dayOfWeek = i.data(CalendarModel::DayOfWeekRole).toInt();
    d.isToday = i.data(CalendarModel::IsTodayRole).toBool();
    d.isSelected = i.data(CalendarModel::IsSelectedRole).toBool();
    d.isWeekend = i.data(CalendarModel::IsWeekendRole).toBool();
    d.hasJournal = i.data(CalendarModel::HasJournalRole).toBool();
    for (const QVariant &e : i.data(CalendarModel::AllDayRole).toList())
        d.allDay.push_back(e.toMap());
    for (const QVariant &v : i.data(CalendarModel::TimedRole).toList()) {
        const QVariantMap t = v.toMap();
        d.timed.push_back({t.value(u"entry"_s).toMap(), t.value(u"lane"_s).toInt(), std::max(1, t.value(u"lanes"_s).toInt())});
    }
    return d;
}

int WeekView::allDayRows() const
{
    return std::min(m_model ? m_model->property("maxAllDay").toInt() : 0, 3);
}

void WeekView::scrollToHour(int hour, at::Context<WeekView> &cx)
{
    m_pendingHour = std::clamp(hour, 0, 23);
    cx.notify();
}

at::Rect WeekView::timedRect(const Timed &t, float width) const
{
    const bool narrow = width < m_look.narrowBelow;
    const float minutes = t.entry.value(u"minutes"_s).toFloat();
    const bool event = text(t.entry, "variant") == u"event";
    const float end = t.entry.value(u"endMinutes"_s, -1).toFloat();
    const float duration = event ? (end > minutes ? end - minutes : 24 * 60 - minutes) : 0;
    const float laneWidth = (width - 4) / float(t.lanes);
    const float x = narrow ? 2 + float(t.lane) * 6 : 2 + float(t.lane) * laneWidth;
    const float y = minutes / 60 * m_look.hourHeight + 1 + (narrow ? float(t.lane) * 10 : 0);
    const float w = narrow ? width - 4 - float(t.lane) * 6 : laneWidth - 2;
    const float h = std::max(2 * m_look.pillHeight - 2, event ? duration / 60 * m_look.hourHeight - 2 : m_look.hourHeight * 0.75f - 2);
    return at::Rect::fromXYWH(x, y, w, h);
}

at::AnyElement WeekView::render(at::Window &window, at::Context<WeekView> &cx)
{
    m_parts.clear();
    if (!m_focus)
        m_focus = cx.newFocusHandle();
    m_focused = m_focus.isFocused(window);
    m_columnWidth = std::max(1.0f, (window.viewportSize().width - m_look.gutter) / 7);
    // A new week: the hours at its first timed entry (CalendarModel.firstHour).
    if (const QString first = dayData(0).date; first != m_scrolledFor) {
        m_scrolledFor = first;
        m_pendingHour = std::clamp(m_model ? m_model->property("firstHour").toInt() : 7, 0, 23);
    }
    const at::EntityId id = cx.entityId();
    const std::u16string family = m_look.uiFamily;
    return at::div()
            .id(u"week")
            .sizeFull()
            .flexCol()
            .bg(m_look.background)
            .textColor(m_look.text)
            .when(!family.empty(), [family](at::Div &&d) { return std::move(d).fontFamily(family); })
            .trackFocus(m_focus)
            .role(at::Role::Table)
            .ariaLabel(u"Week")
            .onKeyDown([id](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
                a.updateEntity<WeekView>(id, [&](WeekView &v, at::Context<WeekView> &) {
                    if (v.keyDown(e))
                        w.stopPropagation();
                });
            })
            .child(heads())
            .child(hours());
}

// The days' heads over the columns, the corner's week number.
at::AnyElement WeekView::heads()
{
    const int rows = allDayRows();
    const float allDayHeight = rows > 0 ? float(rows) * (m_look.pillHeight + 2) + 6 : 0;
    const DayData middle = dayData(3);
    const QDate firstDay = QDate::fromString(dayData(0).date, Qt::ISODate);
    const QDate mid = QDate::fromString(middle.date, Qt::ISODate);
    const QString monday = mid.isValid() ? mid.addDays(1 - mid.dayOfWeek()).toString(Qt::ISODate) : QString();
    const WeekView *self = this;
    at::Div row = at::div()
                          .flexRow()
                          .flexShrink0()
                          .h(m_look.headerHeight + allDayHeight)
                          .borderB(1)
                          .borderColor(m_look.border)
                          .child(at::div()
                                         .id(u"weeknumber")
                                         .w(m_look.gutter)
                                         .flexShrink0()
                                         .flexCol()
                                         .itemsEnd()
                                         .pr(8)
                                         .pt(6)
                                         .gap(4)
                                         .textSize(m_look.tinySize)
                                         .textColor(m_look.faint)
                                         .cursorPointer()
                                         .role(at::Role::Button)
                                         .ariaLabel(u16View(QCoreApplication::translate("CalendarWeekNumber", "Week %1: write its note")
                                                                    .arg(mid.isValid() ? mid.weekNumber() : 0)))
                                         .onClick([self, monday](const at::ClickEvent &, at::Window &, at::App &) {
                                             self->act(u"week"_s, {{u"monday"_s, monday}});
                                         })
                                         .child(at::text(u16View(firstDay.isValid() ? QString::number(mid.weekNumber()) : QString())))
                                         .when(rows > 0, [](at::Div &&d) {
                                             return std::move(d).child(at::div().mt(14).child(at::text(u16View(weekWords("all day")))));
                                         }));
    for (int c = 0; c < 7; ++c)
        std::move(row).child(head(c, dayData(c)));
    return std::move(row);
}

// A day's head: its weekday and number (a disc), its all-day pills; a click selects it, a double
// click opens its daily note.
at::AnyElement WeekView::head(int row, const DayData &d)
{
    const QString weekday = m_look.weekdays.value(d.dayOfWeek - 1).toUpper();
    const WeekView *self = this;
    const QString date = d.date;
    at::Div column = at::div()
                             .flex1()
                             .minW0()
                             .flexCol()
                             .gap(2)
                             .px(2)
                             .child(at::div()
                                            .flexRow()
                                            .itemsCenter()
                                            .justifyCenter()
                                            .gap(6)
                                            .h(m_look.headerHeight)
                                            .child(at::div()
                                                           .textSize(m_look.tinySize)
                                                           .fontSemibold()
                                                           .textColor(d.dayOfWeek >= 6 ? m_look.faint : m_look.muted)
                                                           .child(at::text(u16View(weekday))))
                                            .child(dayDisc({m_look.today, m_look.onToday, m_look.selected, m_look.onSelected,
                                                            m_look.text, m_look.faint, 26, m_look.smallSize},
                                                           d.day, d.isToday, d.isSelected)));
    const int rows = allDayRows();
    for (int k = 0; k < std::min(rows, int(d.allDay.size())); ++k)
        std::move(column).child(allDayPill(row, k, d));
    return at::div()
            .id(at::ElementId(u"head", quint64(row)))
            .relative()
            .flex1()
            .minW0()
            .flexCol()
            .cursorPointer()
            .role(at::Role::Button)
            .ariaLabel(u16View(d.isToday ? d.longLabel + u", "_s + QCoreApplication::translate("CalendarMonthGrid", "today") : d.longLabel))
            .selected(d.isSelected)
            .onClick([self, date](const at::ClickEvent &click, at::Window &, at::App &) {
                self->act(click.clickCount() >= 2 ? u"open"_s : u"day"_s, {{u"date"_s, date}});
            })
            .child(std::move(column))
            .child(tracker(row, {Part::Head}));
}

// An all-day entry's pill (the last row "+N more" when there are more); a click opens the entry.
at::AnyElement WeekView::allDayPill(int row, int index, const DayData &d)
{
    const int rows = allDayRows();
    const bool more = index == rows - 1 && int(d.allDay.size()) > rows;
    const QVariantMap entry = d.allDay[std::size_t(index)];
    const QString variant = more ? u"neutral"_s : text(entry, "variant");
    const bool event = variant == u"event";
    const ChipColors colors = m_look.chip(variant);
    const QString words = more ? weekWords("+%n more", int(d.allDay.size()) - rows + 1) : text(entry, "title");
    const WeekView *self = this;
    const QString date = d.date;
    return at::div()
            .id(at::ElementId(u"allday", quint64(index)))
            .relative()
            .h(m_look.pillHeight)
            .flexShrink0()
            .flexRow()
            .itemsCenter()
            .px(5)
            .rounded(m_look.radiusSm)
            .bg(event ? colorOf(text(entry, "color"), m_look.muted, 0.2f) : colors.background)
            .textSize(m_look.tinySize)
            .textColor(event ? m_look.text : colors.text)
            .overflowHidden()
            .role(at::Role::Button)
            .ariaLabel(u16View(words))
            .onClick([self, date, entry, more](const at::ClickEvent &, at::Window &w, at::App &) {
                self->act(more ? u"day"_s : u"entry"_s, more ? QVariantMap{{u"date"_s, date}} : QVariantMap{{u"date"_s, date}, {u"entry"_s, entry}});
                w.stopPropagation();
            })
            .child(at::div().flex1().minW0().child(at::text(u16View(words)).maxLines(1)))
            .child(tracker(row, {Part::AllDay, index}));
}

// The 24 hours, scrolled: their lines and labels, the days' columns with their timed entries.
at::AnyElement WeekView::hours()
{
    const float height = 24 * m_look.hourHeight;
    at::Div area = at::div().relative().wFull().h(height).flexRow();
    at::Div labels = at::div().relative().w(m_look.gutter).flexShrink0().hFull();
    for (int hour = 1; hour < 24; ++hour)
        std::move(labels).child(at::div()
                                        .absolute()
                                        .left(0)
                                        .w(m_look.gutter - 8)
                                        .top(float(hour) * m_look.hourHeight - 7)
                                        .textRight()
                                        .textSize(m_look.tinySize)
                                        .textColor(m_look.faint)
                                        .ariaHidden()
                                        .child(at::text(u16View(QStringLiteral("%1:00").arg(hour, 2, 10, QChar(u'0'))))));
    // (The hours' lines first: under the columns' tints and entries.)
    const WeekView *self = this;
    std::move(area)
            .child(at::canvas([self](at::Rect b, at::Window &w, at::App &) { self->paintHours(b, w); }).absolute().inset(0))
            .child(std::move(labels));
    for (int c = 0; c < 7; ++c)
        std::move(area).child(column(c, dayData(c)));
    // A scroll asked (a new week, scrollToHour): once the hours are laid out, in the next frame.
    WeekView *pending = this;
    std::move(area).child(at::canvas({})
                                  .onPrepaint([pending](at::Rect, at::Window &w, at::App &) {
                                      if (pending->m_pendingHour < 0)
                                          return;
                                      pending->m_hours.setOffset({0, float(pending->m_pendingHour) * pending->m_look.hourHeight});
                                      pending->m_pendingHour = -1;
                                      w.refresh();
                                  })
                                  .absolute()
                                  .size(0));
    return at::div()
            .id(u"hours")
            .relative()
            .flex1()
            .minH(0)
            .overflowYScroll()
            .trackScroll(m_hours)
            .child(std::move(area))
            .child(tracker(0, {Part::Hours}));
}

// The hours' lines across the columns.
void WeekView::paintHours(at::Rect b, at::Window &w) const
{
    for (int hour = 0; hour < 24; ++hour)
        w.paintQuad(at::Rect::fromXYWH(b.left() + m_look.gutter, b.top() + float(hour) * m_look.hourHeight,
                                       b.width() - m_look.gutter, 1),
                    at::Background::solid(m_look.grid), {}, at::transparent(), {});
}

// A day's column: tinted when selected (sunken on a weekend), its timed entries, now's line in
// today's; a click selects the day.
at::AnyElement WeekView::column(int row, const DayData &d)
{
    const WeekView *self = this;
    const QString date = d.date;
    at::Div col = at::div()
                          .id(at::ElementId(u"column", quint64(row)))
                          .relative()
                          .flex1()
                          .minW0()
                          .hFull()
                          .borderL(1)
                          .borderColor(m_look.grid)
                          .bg(d.isSelected ? m_look.selectedColumn : d.isWeekend ? m_look.weekend : at::transparent())
                          .cursorPointer()
                          .role(at::Role::Cell)
                          .ariaLabel(u16View(d.longLabel))
                          .onClick([self, date](const at::ClickEvent &, at::Window &, at::App &) {
                              self->act(u"day"_s, {{u"date"_s, date}});
                          });
    for (std::size_t k = 0; k < d.timed.size(); ++k)
        std::move(col).child(timedEntry(row, int(k), d, m_columnWidth - 1));
    const int now = d.isToday && nowMinutes ? nowMinutes() : -1;
    if (now >= 0)
        std::move(col).child(at::div().absolute().left(0).right(0).top(float(now) / 60 * m_look.hourHeight).h(2).bg(m_look.now));
    return std::move(col).child(tracker(row, {Part::Column}));
}

// A timed entry at its time: its time (a check or a cross when closed) and its title; a click opens
// it, a right-click its date's menu.
at::AnyElement WeekView::timedEntry(int row, int index, const DayData &d, float width)
{
    const Timed &t = d.timed[std::size_t(index)];
    const at::Rect r = timedRect(t, width);
    const QString variant = text(t.entry, "variant");
    const bool event = variant == u"event";
    const bool closed = t.entry.value(u"closed"_s).toBool();
    const ChipColors colors = m_look.chip(variant);
    const at::Color calendar = colorOf(text(t.entry, "color"), m_look.muted, 1);
    const at::Color ink = event ? m_look.text : colors.text;
    const WeekView *self = this;
    const QString date = d.date;
    const QVariantMap entry = t.entry;
    const QString title = text(t.entry, "title");
    const QString state = !closed ? QString() : text(t.entry, "status") == u"cancelled" ? weekWords("cancelled") : weekWords("done");
    QStringList name{text(t.entry, "time"), title};
    if (!state.isEmpty())
        name.append(state);
    at::Div time = at::div().flexRow().itemsCenter().gap(3).fontSemibold();
    if (closed)
        std::move(time).child(editor::atr::iconElement(text(t.entry, "status") == u"cancelled" ? u"x"_s : u"check"_s, 10, ink));
    std::move(time).child(at::text(u16View(text(t.entry, "time"))));
    return at::div()
            .id(at::ElementId(u"timed", quint64(index)))
            .absolute()
            .left(r.left())
            .top(r.top())
            .w(r.width())
            .h(r.height())
            .flexCol()
            .px(4)
            .py(2)
            .rounded(m_look.radiusSm)
            .bg(event ? at::Color{calendar.r, calendar.g, calendar.b, 0.2f} : colors.background)
            .border(1)
            .borderColor(event ? at::Color{calendar.r, calendar.g, calendar.b, 0.6f} : at::Color{ink.r, ink.g, ink.b, ink.a * 0.25f})
            .overflowHidden()
            .textSize(m_look.tinySize)
            .textColor(ink)
            .cursorPointer()
            .role(at::Role::Button)
            .ariaLabel(u16View(name.join(u", "_s)))
            .onClick([self, date, entry](const at::ClickEvent &, at::Window &w, at::App &) {
                self->act(u"entry"_s, {{u"date"_s, date}, {u"entry"_s, entry}});
                w.stopPropagation();
            })
            .onMouseDown(at::MouseButton::Right,
                         [self, entry](const at::MouseDownEvent &e, at::Window &w, at::App &) {
                             self->act(u"menu"_s, {{u"entry"_s, entry}, {u"x"_s, qreal(e.position.x)}, {u"y"_s, qreal(e.position.y)}});
                             w.stopPropagation();
                         })
            .child(std::move(time))
            .child(at::text(u16View(title)).maxLines(2))
            .child(tracker(row, {Part::Timed, index}));
}

void WeekView::act(const QString &action, const QVariantMap &args) const
{
    if (onAction)
        onAction(action, args);
}

// The week's keys (calendarkeys.h; the columns start the week).
bool WeekView::keyDown(const at::KeyDownEvent &e)
{
    const int selected = currentRow();
    const std::optional<GridKey> asked = gridKey(e.keystroke, std::max(0, selected), dayData(selected).date);
    if (!asked)
        return false;
    act(asked->action, asked->args);
    return true;
}

} // namespace brulion::views::atr

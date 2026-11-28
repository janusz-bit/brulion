#include "agendaview.h"

#include "lookmap.h"
#include "taskbox.h"

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
QString entryWords(const char *text)
{
    return QCoreApplication::translate("PlanningItemRow", text);
}
QString dayWords(const char *text)
{
    return QCoreApplication::translate("CalendarAgendaList", text);
}

// `s` with its first letter upper case ("today" → "Today").
QString capitalized(const QString &s)
{
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1);
}

QString joined(const QStringList &parts, const QString &separator)
{
    QStringList kept;
    for (const QString &p : parts)
        if (!p.isEmpty())
            kept.append(p);
    return kept.join(separator);
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
    const auto it = chips.find(variant);
    if (it != chips.end())
        return it->second;
    const auto neutral = chips.find(u"neutral"_s);
    return neutral != chips.end() ? neutral->second : ChipColors{};
}

AgendaLook AgendaLook::fromMap(const QVariantMap &look)
{
    AgendaLook t = lookFrom(look, kColors, kSizes);
    const QVariantMap chips = look.value(u"chips"_s).toMap();
    for (auto it = chips.cbegin(); it != chips.cend(); ++it) {
        const QVariantMap c = it.value().toMap();
        const ChipColors fallback;
        t.chips[it.key()] = {colorAt(c, u"background"_s, fallback.background), colorAt(c, u"text"_s, fallback.text)};
    }
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
    const QStringList words = name.split(u' ');
    return {kinds.value(words.value(0), Part::Day), words.value(1).toInt()};
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

// The first row whose `role` is `value` (−1: none).
int AgendaView::rowWhere(int role, const QVariant &value) const
{
    const int count = m_model ? m_model->rowCount() : 0;
    for (int r = 0; r < count; ++r)
        if (m_model->index(r, 0).data(role) == value)
            return r;
    return -1;
}

int AgendaView::rowOfDate(const QString &date) const
{
    return date.isEmpty() ? -1 : rowWhere(CalendarModel::DateRole, date);
}

int AgendaView::selectedRow() const
{
    return rowWhere(CalendarModel::IsSelectedRole, true);
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

// An entry as PlanningItemRow: its marker, its time, its title and place, its chip; a click opens
// it, a right-click its date's menu.
at::AnyElement AgendaView::entryRow(int row, int index, const RowData &d, const AgendaEntry &e)
{
    const AgendaView *self = this;
    const QVariantMap entry = e.map;
    const QString name = joined({e.time, e.title, e.kindLabel(), subtitle(e)}, u", "_s);
    at::Div line = at::div()
                           .id(at::ElementId::number(quint64(index)))
                           .relative()
                           .flexRow()
                           .itemsStart()
                           .gap(8)
                           .pl(6)
                           .pr(8)
                           .py(5)
                           .minH(m_look.rowHeight)
                           .rounded(m_look.radius)
                           .hover([look = m_look.hover](at::Style &s) { s.background = at::Background::solid(look); })
                           .cursorPointer()
                           .role(at::Role::ListItem)
                           .ariaLabel(u16View(name))
                           .onClick([self, entry](const at::ClickEvent &, at::Window &, at::App &) {
                               self->act(u"open"_s, {{u"entry"_s, entry}});
                           })
                           .child(marker(row, index, e));
    if (e.hasTokenMenu())
        std::move(line).onMouseDown(at::MouseButton::Right,
                                    [self, e](const at::MouseDownEvent &m, at::Window &, at::App &) {
                                        self->menuAt(u"menu"_s, e, m.position);
                                    });
    if (!e.time.isEmpty())
        std::move(line).child(at::div()
                                      .flexShrink0()
                                      .mt(4)
                                      .textSize(m_look.smallSize)
                                      .fontSemibold()
                                      .fontFeatures(u"tnum")
                                      .textColor(e.closed ? m_look.faint : m_look.text)
                                      .child(at::text(u16View(e.time))));
    std::move(line).child(entryTexts(d, e));
    if (!e.kindLabel().isEmpty() && m_width > m_look.chipMinWidth)
        std::move(line).child(chip(e));
    return std::move(line).child(tracker(row, {Part::Entry, index}));
}

// The entry's first column: a task's box, an event's colour, the entry's icon.
at::AnyElement AgendaView::marker(int row, int index, const AgendaEntry &e)
{
    at::Div column = at::div().size(m_look.entryMarker).flexShrink0().flexRow().itemsCenter().justifyCenter();
    if (e.isTask) {
        const AgendaView *self = this;
        return std::move(column).child(
                taskBoxShape(e.taskStatus(), {m_look.box, m_look.accent, m_look.background})
                        .ariaLabel(u16View(entryWords("Done: %1").arg(e.title)))
                        .onClick([self, e](const at::ClickEvent &, at::Window &w, at::App &) {
                            self->act(u"toggle"_s, {{u"entry"_s, e.map}});
                            w.stopPropagation();
                        })
                        .onMouseDown(at::MouseButton::Right,
                                     [self, e](const at::MouseDownEvent &m, at::Window &w, at::App &) {
                                         self->menuAt(u"statusMenu"_s, e, m.position);
                                         w.stopPropagation();
                                     })
                        .child(tracker(row, {Part::Box, index})));
    }
    if (e.isEvent()) {
        const QColor c(e.color);
        const at::Color color = c.isValid() ? at::Color{float(c.redF()), float(c.greenF()), float(c.blueF()), 1} : m_look.muted;
        return std::move(column).child(at::div().size(10).roundedFull().bg(color));
    }
    const bool marked = e.variant == u"alarm" || e.variant == u"reminder";
    return std::move(column).child(
            editor::atr::iconElement(e.icon.isEmpty() ? u"calendar"_s : e.icon, 16, marked ? m_look.reminder : m_look.muted));
}

// The entry's title (two lines at most; done or past: muted, cancelled: struck out) and where it is.
at::AnyElement AgendaView::entryTexts(const RowData &d, const AgendaEntry &e)
{
    at::Div title = at::div()
                            .textSize(m_look.textSize)
                            .lineHeight(m_look.lineHeight)
                            .textColor(e.closed || d.pastDay() ? m_look.muted : m_look.text)
                            .when(e.status == u"cancelled", [](at::Div &&t) { return std::move(t).lineThrough(); })
                            .child(at::text(u16View(e.title)).maxLines(2));
    at::Div column = at::div().flex1().minW0().flexCol().gap(1).child(std::move(title));
    const QString where = subtitle(e);
    if (!where.isEmpty())
        std::move(column).child(at::div()
                                        .textSize(m_look.smallSize)
                                        .textColor(m_look.muted)
                                        .child(at::text(u16View(where)).maxLines(1)));
    return std::move(column);
}

// The kind of date as a small chip in its variant's colours (a recurring one's with its icon).
at::AnyElement AgendaView::chip(const AgendaEntry &e)
{
    const AgendaLook::ChipColors colors = m_look.chip(e.variant);
    at::Div c = at::div()
                        .flexShrink0()
                        .mt(2)
                        .flexRow()
                        .itemsCenter()
                        .gap(4)
                        .px(8)
                        .py(2)
                        .roundedFull()
                        .bg(colors.background)
                        .textSize(m_look.chipSize)
                        .fontMedium()
                        .textColor(colors.text)
                        .ariaHidden();
    if (e.recurring)
        std::move(c).child(editor::atr::iconElement(u"repeat"_s, 12, colors.text));
    return std::move(c).child(at::text(u16View(e.kindLabel())));
}

void AgendaView::act(const QString &action, const QVariantMap &args) const
{
    if (onAction)
        onAction(action, args);
}

void AgendaView::menuAt(const QString &action, const AgendaEntry &e, at::Point at) const
{
    act(action, {{u"entry"_s, e.map}, {u"x"_s, qreal(at.x)}, {u"y"_s, qreal(at.y)}});
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

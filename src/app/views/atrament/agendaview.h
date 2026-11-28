// AgendaView — the Calendar view's agenda drawn by Atrament (docs/ATRAMENT.md "The views in Atrament
// (M3)", Calendar, step 3): the days of a CalendarModel in "agenda" mode (src/app/planning/
// calendarmodel.h: read through its roles, so any model with them is one — the tests'
// QStandardItemModel) in a measured list, in place of CalendarAgendaList.qml's ListView. A day shows
// its number on a disc (today's red, the selected day's dark, a past day's outlined), its weekday and
// date, "Today" / "Tomorrow" / "Yesterday", the "Journal" button of a day with a daily note, its
// entries as PlanningItemRow shows them (a task's box or the entry's icon or an event's colour, the
// time, the title on two lines at most, where it is written, the kind of date as a chip) or "Nothing
// planned." / "Only a journal entry.", and a separator.
//
// The view does nothing itself: what a row asks goes to `onAction(action, args)`, which
// CalendarView.qml answers with what its agenda's signals do:
//
//   "day"        {date}               a day's header clicked, Enter on the current day
//   "journal"    {date}               the day's "Journal" button
//   "open"       {entry}              an entry clicked (the CalendarModel's entry map)
//   "toggle"     {entry}              a task's box clicked
//   "statusMenu" {entry, x, y}        a right-click on a task's box (its status menu)
//   "menu"       {entry, x, y}        a right-click on an entry with a token (its date's menu)
//
// (x, y: window px.) Keys while it has the focus: ↑ / ↓ move the current day (from the selected
// one, shown by a focus ring), Enter picks it ("day"). For screen readers the list's items are its
// days: a header a button named by its date (", today"), its entries list items named by their
// time, title, kind and place.
#pragma once

#include "lookmap.h"
#include "modelrows.h"
#include "partrecorder.h"
#include "ui/atrament.h"

#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace brulion::views::atr {

// CalendarAgendaList.qml's look (Theme).
struct AgendaLook {
    atrament::Color background = atrament::rgb(0xffffff);
    atrament::Color text = atrament::rgb(0x1f2328);        // Theme.fg
    atrament::Color muted = atrament::rgb(0x6e7781);       // Theme.fgMuted
    atrament::Color faint = atrament::rgb(0x8c959f);       // Theme.fgFaint
    atrament::Color hover = atrament::rgba(0x8c959f1a);    // Theme.bgHover
    atrament::Color today = atrament::rgb(0xe03131);       // today's disc (Theme.accentSolid)
    atrament::Color onToday = atrament::rgb(0xffffff);     // its number (Theme.fgOnAccent)
    atrament::Color todayText = atrament::rgb(0xc92a2a);   // "Today" (Theme.accentText)
    atrament::Color selected = atrament::rgb(0x1f2328);    // the selected day's disc (Theme.primary)
    atrament::Color onSelected = atrament::rgb(0xffffff);  // its number (Theme.primaryFg)
    atrament::Color disc = atrament::rgb(0xf0f2f4);        // another day's disc (Theme.bgSunken)
    atrament::Color border = atrament::rgb(0xd0d7de);      // a past day's outline (Theme.border)
    atrament::Color separator = atrament::rgb(0xeaeef2);   // between days (Theme.borderSubtle)
    atrament::Color focus = atrament::rgb(0x3b7dc0);       // the current day's ring (Theme.focusRing)
    atrament::Color box = atrament::rgb(0x8c959f);         // an open task's box (Theme.borderStrong)
    atrament::Color accent = atrament::rgb(0x3b5bdb);      // a done task's box (Theme.accent)
    atrament::Color reminder = atrament::rgb(0xc98a12);    // a reminder's or alarm's icon (Theme.markColor)
    float textSize = 15;   // an entry's title (Theme.textBase)
    float smallSize = 13;  // its time, its place, "Nothing planned." (Theme.textSm)
    float dayTextSize = 16; // the day's title (Theme.textMd)
    float numberSize = 18; // the day's number (Theme.textLg)
    float chipSize = 11;   // a small chip's text
    float lineHeight = 20; // an entry title's line (Theme.lineHeightPx(textBase, 1.35))
    float maxWidth = 760;  // the days' column on wide windows (Theme.measureList)
    float sideMargin = 16; // (Theme.space4)
    float radius = 8;      // (Theme.radius)
    float marker = 36;     // the day's disc (touch: 40)
    float entryMarker = 24; // an entry's first column: its box, icon or colour (touch: Theme.touchTarget)
    float rowHeight = 34;  // an entry's least height (Theme.listItemHeight)
    float chipMinWidth = 300; // a row narrower than this shows no chip (PlanningItemRow)
    // A chip's colours by its variant (lookmap.h).
    using ChipColors = atr::ChipColors;
    ChipMap chips;
    std::u16string uiFamily;

    ChipColors chip(const QString &variant) const;
    // From AtramentView.viewLook: its fields by name, `chips` a map of variant → {background, text}.
    static AgendaLook fromMap(const QVariantMap &look);
};

// A CalendarModel entry (its map; what it lacks "" / false).
struct AgendaEntry {
    QVariantMap map;
    QString kind, variant, icon, time, title, status, path, pageTitle, spec;
    QString color, endTime, calendarName, location;
    bool isTask = false, closed = false, recurring = false, continued = false, isJournalPage = false;

    static AgendaEntry from(const QVariantMap &map);
    bool isEvent() const { return kind == u"event"; }
    // The entry names a token of a note (its date's menu); a journal's names none.
    bool hasTokenMenu() const { return !spec.isEmpty() && kind != u"journal"; }
    // The task box's state (a task without one is open).
    QString taskStatus() const { return status.isEmpty() ? QStringLiteral("todo") : status; }
    // The chip's words: "Overdue", "Alarm", "Reminder", "Scheduled", "Done" / "Finished", "Due",
    // "Starts", "Journal"; "" none (PlanningItemRow.kindLabel).
    QString kindLabel() const;
    // An event's "until 15:00" / "continues", its calendar and its place, joined with " · ".
    QString eventDetails() const;
};

class AgendaView {
public:
    using Look = AgendaLook;
    // A part of a day the last frame drew: the day, its header, its "Journal" button, an entry, an
    // entry's task box (`entry`: which, from 0).
    struct Part {
        enum Kind { Day, Header, Journal, Entry, Box } kind = Day;
        int entry = 0;
        bool operator==(const Part &) const = default;
    };
    // A part by its name: "header", "journal", "entry 2", "box 0"; anything else: the day.
    static Part partNamed(const QString &name);

    AgendaView(atrament::Context<AgendaView> &cx, QAbstractItemModel *model);
    ~AgendaView();
    AgendaView(const AgendaView &) = delete;
    AgendaView &operator=(const AgendaView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<AgendaView> &cx);
    void setLook(AgendaLook look, atrament::Context<AgendaView> &cx);
    const AgendaLook &look() const { return m_look; }

    std::function<void(const QString &action, const QVariantMap &args)> onAction;
    // A date ("YYYY-MM-DD") in words relative to today ("today", "tomorrow", "in 3 days", "Oct 15"…:
    // DatesHelper.formatHuman); the agenda shows only the one-word ones. Unset: none shown.
    std::function<QString(const QString &date)> formatHuman;
    // A daily note's place: its date in short words ("Oct 15": DatesHelper.formatShort of
    // Workspace.journalDate(path)); unset: the page's title.
    std::function<QString(const QString &path)> journalDay;

    // The day the next frame shows at the view's top (the agenda scrolled to today or the selected
    // day), not made current; a row out of the model: nothing.
    void showRow(int row, atrament::Context<AgendaView> &cx);
    // ↑ / ↓'s current day, shown in the next frame; it stays current while the model's days change
    // around it (−1: the keyboard not used yet, the day gone).
    void revealRow(int row, atrament::Context<AgendaView> &cx);
    int currentRow() const { return rowOfDate(m_currentDate); }
    atrament::FocusHandle focusHandle() const { return m_focus; }
    std::optional<atrament::Rect> partBounds(int row, Part part) const { return m_parts.bounds(row, part); }
    int drawnRows() const { return m_parts.rows(); }

    // A day as the model's roles give it.
    struct RowData {
        QString date, longLabel;
        int day = 0;
        bool isToday = false, isPast = false, isSelected = false, hasJournal = false;
        std::vector<AgendaEntry> entries;
        bool pastDay() const { return isPast && !isToday; }
    };
    RowData rowData(int row) const;
    // The row of the selected day, of the day `date` (−1: none).
    int selectedRow() const;
    int rowOfDate(const QString &date) const;
    // "Today" for today, else formatHuman's word when it is one ("Tomorrow"); "" none.
    QString relativeWords(const RowData &d) const;
    // Where the entry is written ("Journal · Oct 15", the page's title, an event's details).
    QString subtitle(const AgendaEntry &e) const;

private:
    atrament::AnyElement renderRow(std::size_t row);
    atrament::AnyElement header(int row, const RowData &d);
    atrament::AnyElement dayDisc(const RowData &d);
    atrament::AnyElement journalButton(int row, const RowData &d);
    atrament::AnyElement entryRow(int row, int index, const RowData &d, const AgendaEntry &e);
    atrament::AnyElement marker(int row, int index, const AgendaEntry &e);
    atrament::AnyElement entryTexts(const RowData &d, const AgendaEntry &e);
    atrament::AnyElement chip(const AgendaEntry &e);
    atrament::AnyElement tracker(int row, Part part) { return m_parts.tracker(row, part, part.kind == Part::Day); }
    void act(const QString &action, const QVariantMap &args) const;
    void menuAt(const QString &action, const AgendaEntry &e, atrament::Point at) const;
    bool keyDown(const atrament::KeyDownEvent &e, atrament::Context<AgendaView> &cx);
    int keysDay() const;
    bool moveCurrent(int step, atrament::Context<AgendaView> &cx);
    bool pickCurrent() const;

    QPointer<QAbstractItemModel> m_model;
    atrament::EntityId m_id;
    AgendaLook m_look;
    ModelRows m_rows;
    atrament::FocusHandle m_focus;
    QString m_currentDate;  // the keys' current day ("" none)
    int m_ringRow = -1;     // the current day's row as the view rendered, when it had the focus (its ring)
    float m_width = 0;      // the days' width as it rendered (a narrow one shows no chips)
    PartRecorder<Part> m_parts;
};

} // namespace brulion::views::atr

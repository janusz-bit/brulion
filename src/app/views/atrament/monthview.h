// MonthView — the Calendar view's month grid drawn by Atrament (docs/ATRAMENT.md "The views in Atrament
// (M3)", Calendar): the 42 days of a CalendarModel in "month" mode (src/app/planning/calendarmodel.h:
// read through its roles, so any model with them is one — the tests' QStandardItemModel) as 6 weeks
// × 7 days filling the view, in place of CalendarMonthGrid.qml. The weekday names over the columns,
// the ISO week numbers on the left (a click: that week's note), each day's number on a disc (today's
// red, the selected day's dark, another month's faint), a book for a daily note, the day's entries as
// pills (open ones first: "09:00 Meeting" in cells ≥ 104 px, the title alone in narrower ones; done
// ones with a check, cancelled ones with a cross, an alarm with its clock, an event in its
// calendar's colour) and "+N more"; cells under 72 px show an icon per kind of open entry instead.
//
// The view does nothing itself: what a cell asks goes to `onAction(action, args)`, which
// CalendarView.qml answers with what CalendarMonthGrid's signals and its keys do:
//
//   "day"      {date}           a cell clicked (it becomes the selected day), Enter
//   "entry"    {date, entry}    a pill clicked (the day selected, the entry opened)
//   "open"     {date}           a cell double-clicked, Ctrl+Enter (the daily note)
//   "week"     {monday}         a week number clicked
//   "move"     {days}           ← → ↑ ↓ Home End (the selected day moved by `days`)
//   "previous" / "next"  {}     PageUp / PageDown (another month)
#pragma once

#include "lookmap.h"
#include "modelrows.h"
#include "partrecorder.h"
#include "ui/atrament.h"

#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace brulion::views::atr {

// CalendarMonthGrid.qml's look (Theme).
struct MonthLook {
    atrament::Color background = atrament::rgb(0xffffff);
    atrament::Color text = atrament::rgb(0x1f2328);       // Theme.fg
    atrament::Color muted = atrament::rgb(0x6e7781);      // weekday names (Theme.fgMuted)
    atrament::Color faint = atrament::rgb(0x8c959f);      // another month's days, weekends' names, "+N more"
    atrament::Color grid = atrament::rgb(0xeaeef2);       // the cells' lines (Theme.borderSubtle)
    atrament::Color today = atrament::rgb(0xe03131);      // Theme.accentSolid
    atrament::Color onToday = atrament::rgb(0xffffff);
    atrament::Color selected = atrament::rgb(0x1f2328);   // Theme.primary
    atrament::Color onSelected = atrament::rgb(0xffffff);
    atrament::Color focus = atrament::rgb(0x3b7dc0);      // the selected day's ring (keys)
    atrament::Color weekNote = atrament::rgb(0x3b5bdb);   // a week number with a weekly note (Theme.accentText)
    float smallSize = 13;   // the days' numbers, the pills (Theme.textSm)
    float tinySize = 11;    // weekday names, week numbers, pills (Theme.textXs)
    float headerHeight = 26; // (touch: 30)
    float weekColumn = 28;  // (touch: 26; 0: no week numbers)
    float pillHeight = 18;  // (touch: 20)
    float pillsTop = 30;    // (touch: 34)
    float disc = 24;        // the day's number's disc (touch: 28)
    float radiusSm = 6;     // a pill's (Theme.radiusSm)
    float dotsBelow = 72;   // cells narrower than this show icons, not pills (touch: 96)
    float timesFrom = 104;  // cells this wide show the pills' times
    // A pill's colours by its variant (lookmap.h).
    ChipMap chips;
    // A narrow cell's icons' colours by the kind of entry (Theme.markColor).
    std::map<QString, atrament::Color> marks;
    QStringList weekdays;           // the short names, Monday first (DatesHelper.weekdayNames)
    std::vector<QString> weekNotes; // the Mondays of the shown weeks with a weekly note (in the accent)
    std::u16string uiFamily;

    ChipColors chip(const QString &variant) const { return chipOf(chips, variant); }
    atrament::Color mark(const QString &kind) const;
    // From AtramentView.viewLook: its fields by name, `chips` variant → {background, text},
    // `marks` kind → colour, `weekdays`, `weekNotes`.
    static MonthLook fromMap(const QVariantMap &look);
};

class MonthView {
public:
    using Look = MonthLook;
    // A part the last frame drew: a day's cell, one of its pills (`index`, from 0), its "+N more", a
    // week's number (`row`: the week, 0–5).
    struct Part {
        enum Kind { Cell, Pill, More, Week } kind = Cell;
        int index = 0;
        bool operator==(const Part &) const = default;
    };
    // "pill 2", "more", "week"; anything else: the cell.
    static Part partNamed(const QString &name);

    MonthView(atrament::Context<MonthView> &cx, QAbstractItemModel *model);
    ~MonthView();
    MonthView(const MonthView &) = delete;
    MonthView &operator=(const MonthView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<MonthView> &cx);
    void setLook(MonthLook look, atrament::Context<MonthView> &cx);
    const MonthLook &look() const { return m_look; }

    std::function<void(const QString &action, const QVariantMap &args)> onAction;
    // A weekday's short name (1 = Monday … 7 = Sunday); unset: the look's `weekdays`.
    std::function<QString(int dayOfWeek)> weekdayName;

    // AtramentView's: the keys' day is the model's selected day (no current row of its own).
    void revealRow(int, atrament::Context<MonthView> &) { }
    int currentRow() const { return selectedRow(); }
    std::optional<atrament::Rect> partBounds(int row, Part part) const { return m_parts.bounds(row, part); }
    int drawnRows() const { return m_parts.rows(); }
    atrament::FocusHandle focusHandle() const { return m_focus; }

    // A day as the model's roles give it.
    struct DayData {
        QString date, longLabel;
        int day = 0, dayOfWeek = 0, weekNumber = 0, itemCount = 0;
        bool inMonth = false, isToday = false, isSelected = false, hasJournal = false;
        std::vector<QVariantMap> entries; // open ones first (orderedItems)
        QStringList openKinds;
    };
    DayData dayData(int row) const;
    int selectedRow() const;
    // A day's name for screen readers: its date, ", today", ", N entries", ", journal".
    static QString cellName(const DayData &d);
    // A pill's words: its time (wide cells) and title; its name: time, title, "done" / "cancelled".
    static QString pillName(const QVariantMap &entry);
    // The pills a cell of `height` px holds (the last one "+N more" when they do not all fit).
    int pillsFitting(float height) const;

private:
    atrament::AnyElement header();
    atrament::AnyElement week(int week, float cellHeight);
    atrament::AnyElement weekNumber(int week);
    atrament::AnyElement cell(int row, float cellHeight);
    atrament::AnyElement cellTop(const DayData &d);
    void pills(atrament::Div &column, int row, const DayData &d, int fitting);
    atrament::AnyElement number(const DayData &d);
    static QString pillIcon(const QVariantMap &entry);
    QString pillWords(const QVariantMap &entry, std::vector<atrament::TextSpan> &spans) const;
    atrament::AnyElement pill(int row, int index, const DayData &d, const QVariantMap &entry);
    atrament::AnyElement marks(const DayData &d);
    atrament::AnyElement tracker(int row, Part part) { return m_parts.tracker(row, part, part.kind == Part::Cell); }
    void act(const QString &action, const QVariantMap &args) const;
    bool keyDown(const atrament::KeyDownEvent &e);

    QPointer<QAbstractItemModel> m_model;
    MonthLook m_look;
    ModelRows m_rows; // (the model's signals followed; the grid keeps no heights of its own)
    atrament::FocusHandle m_focus;
    float m_cellWidth = 0;
    bool m_focused = false;
    PartRecorder<Part> m_parts;
};

} // namespace brulion::views::atr

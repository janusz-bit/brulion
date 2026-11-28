// WeekView — the Calendar view's week drawn by Atrament (docs/ATRAMENT.md "The views in Atrament (M3)",
// Calendar): the 7 days of a CalendarModel in "week" mode (calendarmodel.h: read through its roles
// and its `firstHour`, so any model with them is one — the tests' QStandardItemModel) in place of
// CalendarWeekGrid.qml. Over the columns each day's weekday and number (today's red disc, the
// selected day's dark one) and its all-day entries as pills (three rows at most, the third "+N
// more"); under them 24 hour rows scrolled to the week's first timed entry, the timed entries at
// their time — 45 minutes tall, an event as long as it lasts — side by side when they start within
// 45 minutes of each other (cascaded in narrow columns), a done one's check, a cancelled one's
// cross; the selected day's column tinted, the weekend's sunken, now as a red line in today's.
//
// What a part asks goes to `onAction(action, args)` (CalendarView.qml answers it as
// CalendarWeekGrid's signals): "day" {date} a column or a day's head clicked, Enter; "open" {date}
// a head double-clicked, Ctrl+Enter; "entry" {date, entry} an entry clicked; "menu" {entry, x, y}
// a right-click on one; "week" {monday} the corner's week number; the keys of calendarkeys.h.
#pragma once

#include "lookmap.h"
#include "modelrows.h"
#include "partrecorder.h"
#include "ui/atrament.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace brulion::views::atr {

// CalendarWeekGrid.qml's look (Theme).
struct WeekLook {
    atrament::Color background = atrament::rgb(0xffffff);
    atrament::Color text = atrament::rgb(0x1f2328);
    atrament::Color muted = atrament::rgb(0x6e7781);
    atrament::Color faint = atrament::rgb(0x8c959f);
    atrament::Color grid = atrament::rgb(0xeaeef2);        // the hours' lines (Theme.borderSubtle)
    atrament::Color border = atrament::rgb(0xd0d7de);      // under the heads (Theme.border)
    atrament::Color today = atrament::rgb(0xe03131);
    atrament::Color onToday = atrament::rgb(0xffffff);
    atrament::Color selected = atrament::rgb(0x1f2328);
    atrament::Color onSelected = atrament::rgb(0xffffff);
    atrament::Color selectedColumn = atrament::rgba(0x3b5bdb10); // (Theme.bgSelected at half)
    atrament::Color weekend = atrament::rgba(0xf3f1ec73);  // (Theme.bgSunken at 0.45)
    atrament::Color now = atrament::rgb(0xe03131);         // the current time's line
    atrament::Color focus = atrament::rgb(0x3b7dc0);
    float gutter = 50;       // the hours' labels (touch: 44)
    float hourHeight = 40;   // (touch: 44)
    float headerHeight = 46; // the days' heads (touch: 52)
    float pillHeight = 18;   // (touch: 20)
    float narrowBelow = 80;  // columns narrower: timed entries cascaded
    float smallSize = 13;
    float tinySize = 11;
    float radiusSm = 6;
    ChipMap chips;
    QStringList weekdays; // the short names, Monday first
    std::u16string uiFamily;

    ChipColors chip(const QString &variant) const { return chipOf(chips, variant); }
    static WeekLook fromMap(const QVariantMap &look);
};

class WeekView {
public:
    using Look = WeekLook;
    // A part the last frame drew: a day's head, one of its all-day pills, its column, one of its
    // timed entries (`index`: in the model's order), the hours' scrolled area (row 0).
    struct Part {
        enum Kind { Head, AllDay, Column, Timed, Hours } kind = Column;
        int index = 0;
        bool operator==(const Part &) const = default;
    };
    static Part partNamed(const QString &name);

    WeekView(atrament::Context<WeekView> &cx, QAbstractItemModel *model);
    ~WeekView();
    WeekView(const WeekView &) = delete;
    WeekView &operator=(const WeekView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<WeekView> &cx);
    void setLook(WeekLook look, atrament::Context<WeekView> &cx);
    const WeekLook &look() const { return m_look; }

    std::function<void(const QString &action, const QVariantMap &args)> onAction;
    // Now in minutes since midnight (DatesHelper.now); unset or < 0: no line.
    std::function<int()> nowMinutes;

    void revealRow(int, atrament::Context<WeekView> &) { }
    int currentRow() const;
    std::optional<atrament::Rect> partBounds(int row, Part part) const { return m_parts.bounds(row, part); }
    int drawnRows() const { return m_parts.rows(); }
    atrament::FocusHandle focusHandle() const { return m_focus; }
    // The hours scrolled to `hour` at the top (the week's first timed entry: the model's firstHour).
    void scrollToHour(int hour, atrament::Context<WeekView> &cx);
    float hoursOffset() const { return m_hours.offset().y; }

    // A timed entry and its place among the ones starting near it.
    struct Timed {
        QVariantMap entry;
        int lane = 0, lanes = 1;
    };
    struct DayData {
        QString date, longLabel;
        int day = 0, dayOfWeek = 0;
        bool isToday = false, isSelected = false, isWeekend = false, hasJournal = false;
        std::vector<QVariantMap> allDay;
        std::vector<Timed> timed;
    };
    DayData dayData(int row) const;
    // A timed entry's place in its column of `width` px: x, y, width and height.
    atrament::Rect timedRect(const Timed &t, float width) const;

private:
    atrament::AnyElement heads();
    atrament::AnyElement head(int row, const DayData &d);
    atrament::AnyElement allDayPill(int row, int index, const DayData &d);
    atrament::AnyElement hours();
    atrament::AnyElement column(int row, const DayData &d);
    atrament::AnyElement timedEntry(int row, int index, const DayData &d, float width);
    void paintHours(atrament::Rect bounds, atrament::Window &window) const;
    atrament::AnyElement tracker(int row, Part part) { return m_parts.tracker(row, part, part.kind == Part::Column); }
    void act(const QString &action, const QVariantMap &args) const;
    bool keyDown(const atrament::KeyDownEvent &e);
    int allDayRows() const;

    QPointer<QAbstractItemModel> m_model;
    WeekLook m_look;
    ModelRows m_rows;
    atrament::FocusHandle m_focus;
    atrament::ScrollHandle m_hours;
    QString m_scrolledFor; // the range the hours were scrolled for (its first day)
    int m_pendingHour = -1; // the hour to scroll to once the hours are laid out (−1: none)
    float m_columnWidth = 0;
    bool m_focused = false;
    PartRecorder<Part> m_parts;
};

} // namespace brulion::views::atr

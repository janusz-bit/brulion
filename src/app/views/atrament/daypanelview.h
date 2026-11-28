// DayPanelView — the Calendar's day panel drawn by Atrament (docs/ATRAMENT.md "The views in Atrament
// (M3)", Calendar): CalendarDayPanel.qml's column for the selected day of a CalendarModel (its
// `anchorDate`; the model's dayEntries(), dayHasJournal(), overdue and overdueCount — called by
// name, so a test's model with them is one). Its relative word ("Today") and date, three actions
// ("Open journal" / "Write in journal", "Add task", "Add reminder"), today's overdue tasks under a
// folding "Overdue" header (five at most, then "All N overdue in Tasks"), the day's entries under
// "Plan of the day" as PlanningItemRows (planningrow.h) or "Nothing planned".
//
// What it asks goes to `onAction(action, args)` (CalendarView.qml answers as the QML panel's
// signals): "journal" / "addTask" / "addReminder" {date}, "allOverdue" {}, and the entries' "open",
// "toggle", "statusMenu", "menu" (planningrow.h).
#pragma once

#include "agendaview.h"
#include "modelrows.h"
#include "partrecorder.h"
#include "ui/atrament.h"

#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <vector>

namespace brulion::views::atr {

class DayPanelView {
public:
    using Look = AgendaLook;
    // A part the last frame drew (row 0: the panel): an action (`index` 0–2), the overdue header,
    // an overdue entry, its box, "All N overdue", "Plan of the day", a day's entry, its box.
    struct Part {
        enum Kind { Panel, Action, OverdueHeader, Overdue, OverdueBox, AllOverdue, PlanHeader, Entry, Box } kind = Panel;
        int index = 0;
        bool operator==(const Part &) const = default;
    };
    static Part partNamed(const QString &name);

    DayPanelView(atrament::Context<DayPanelView> &cx, QAbstractItemModel *model);
    ~DayPanelView();
    DayPanelView(const DayPanelView &) = delete;
    DayPanelView &operator=(const DayPanelView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<DayPanelView> &cx);
    void setLook(AgendaLook look, atrament::Context<DayPanelView> &cx);
    const AgendaLook &look() const { return m_look; }

    std::function<void(const QString &action, const QVariantMap &args)> onAction;
    // As the agenda's: a date in words relative to today; a daily note's day in short words.
    std::function<QString(const QString &date)> formatHuman;
    std::function<QString(const QString &date)> formatLong;
    std::function<QString(const QString &path)> journalDay;
    // Overdue tasks listed on today's panel (more: "All N overdue in Tasks").
    int overdueLimit = 5;

    void revealRow(int, atrament::Context<DayPanelView> &) { }
    int currentRow() const { return -1; }
    std::optional<atrament::Rect> partBounds(int row, Part part) const { return m_parts.bounds(row, part); }
    int drawnRows() const { return m_parts.rows(); }

    // What the panel shows of the model.
    struct DayData {
        QString date;
        bool isToday = false, hasJournal = false;
        std::vector<AgendaEntry> entries, overdue;
        int overdueCount = 0;
    };
    DayData dayData() const;

private:
    atrament::AnyElement heading(const DayData &d);
    atrament::AnyElement action(int index, const QString &icon, const QString &words, const QString &asked, const QString &date);
    atrament::AnyElement sectionHeader(const QString &words, int count, std::optional<bool> expanded, Part part);
    atrament::AnyElement overdue(const DayData &d);
    atrament::AnyElement plan(const DayData &d);
    atrament::AnyElement tracker(Part part) { return m_parts.tracker(0, part, part.kind == Part::Panel); }
    QString subtitle(const AgendaEntry &e, bool withDate) const;
    void act(const QString &action, const QVariantMap &args) const;

    QPointer<QAbstractItemModel> m_model;
    AgendaLook m_look;
    ModelRows m_rows;
    atrament::ScrollHandle m_scroll;
    bool m_overdueOpen = true;
    float m_width = 0;
    PartRecorder<Part> m_parts;
};

} // namespace brulion::views::atr

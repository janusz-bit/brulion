// EntryRows — PlanningItemRow drawn by Atrament (docs/ATRAMENT.md "The views in Atrament (M3)",
// Calendar): a CalendarModel entry's row — a task's box (taskbox.h) or an event's colour or the
// entry's icon, the time, the title on two lines at most (done or a past day's: muted, cancelled:
// struck out), where it is, the kind of date as a chip (rows wider than chipMinWidth) — as the
// agenda's days (agendaview.h) and the day panel (daypanelview.h) show them. A click asks "open"
// {entry}, the box "toggle" {entry}, a right-click on the box "statusMenu" {entry, x, y}, on an entry
// with a token "menu" {entry, x, y}; `track` gives the view's part trackers (the row's, the box's).
#pragma once

#include "agendaview.h"

#include <QCoreApplication>
#include <QStringList>

#include <functional>

namespace brulion::views::atr {

// PlanningItemRow's words (its translations; not lupdate's).
inline QString entryWords(const char *text)
{
    return QCoreApplication::translate("PlanningItemRow", text);
}

// The parts not empty, joined with `separator`.
inline QString joined(const QStringList &parts, const QString &separator)
{
    QStringList kept;
    for (const QString &p : parts)
        if (!p.isEmpty())
            kept.append(p);
    return kept.join(separator);
}

struct EntryRows {
    using Act = std::function<void(const QString &action, const QVariantMap &args)>;
    const AgendaLook &look;
    float width = 0; // the rows' width (a narrow one shows no chips)
    Act act;
    std::function<atrament::AnyElement(bool box, int index)> track;
    std::function<QString(const AgendaEntry &e)> subtitle;

    atrament::AnyElement row(int index, const AgendaEntry &e, bool past) const;

private:
    atrament::AnyElement marker(int index, const AgendaEntry &e) const;
    atrament::AnyElement texts(const AgendaEntry &e, bool past) const;
    atrament::AnyElement chip(const AgendaEntry &e) const;
    static void menuAt(const Act &act, const QString &action, const AgendaEntry &e, atrament::Point at);
};

} // namespace brulion::views::atr

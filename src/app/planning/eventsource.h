// EventSource — events the calendar views show that are not in the notes (docs/UI.md
// "Calendars"): the occurrences of ICS subscriptions and CalDAV calendars, in the device's wall
// clock, one entry per day an occurrence covers. Implemented by the shell's WorkspaceCalendars
// (`Workspace.calendars`); CalendarModel { events: Workspace.calendars } merges them into its days.
#pragma once

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QTime>

namespace brulion::planning {

/// One day of an event occurrence.
struct ExternalEvent {
    QString key;          ///< stable within a revision: calendar | event | instance (eventDetails())
    QString calendarId;
    QString calendarName;
    QString color;        ///< "#rrggbb" of its calendar (or its own COLOR)
    QString title;
    QString location;
    QDate date;           ///< the day this entry is shown on
    QDate firstDay;       ///< the occurrence's first and last day (multi-day events)
    QDate lastDay;
    QTime start;          ///< on its first day (invalid: all-day)
    QTime end;            ///< on its last day (invalid: all-day, or it ends at midnight)
    bool allDay = false;
    bool recurring = false;
    bool readOnly = true; ///< a subscription, or a calendar without the write privilege
    friend bool operator==(const ExternalEvent &, const ExternalEvent &) = default;
};

class EventSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    /// The entries of the days [from, to], each day's in order (all-day first, then by time).
    virtual QList<ExternalEvent> eventsBetween(QDate from, QDate to) const = 0;

Q_SIGNALS:
    /// What eventsBetween() gives changed (a refresh, a toggle, an edit).
    void eventsChanged();
};

} // namespace brulion::planning

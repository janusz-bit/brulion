// Changes of CalDAV events as one decision, for the app's event dialog and for brulion-cli alike
// (docs/UI.md "Calendars", docs/CLI.md "Calendar events"): what is asked (EventIntent — create an
// event, change it or delete it, the whole series or one occurrence, only the fields given), what
// the event is now (eventFields(): the dialog's fields of an event or of one occurrence, on the
// device's clock), what the change makes of it (editedFields(), plannedChange(): the EventChange
// a run sends) and how it waits in the calendar's state (addToOutbox()). Pure functions — but the
// inbox: brulion-cli leaves an intent as a file of its own (postIntent()), and only the app's run
// (CalendarSync::takeInbox()) applies it to the calendar and deletes it, so the device's calendar
// state keeps one writer. Part of the caldav module — see caldav/caldav.h.
#pragma once

#include "caldav/eventedit.h"
#include "caldav/events.h"
#include "caldav/store.h"
#include "types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTime>
#include <QTimeZone>

#include <optional>
#include <variant>

namespace brulion::caldav {

/// A day (with or without a time), or a time alone: on the event's own day.
using EventMoment = std::variant<DateSpec, QTime>;

/// The times an edit sets; each one not given is kept.
struct EventTimes {
    /// The first day and the start time. A day without a time keeps the event's time of day (an
    /// all-day event stays all-day, a new one is all-day); a time alone keeps the day.
    std::optional<EventMoment> start;
    /// The last day (all-day: inclusive) and the end time; a time alone is on the start's day
    /// (the next one when it is before the start). Not given: the event keeps its length (a new
    /// one: an hour, or its one day).
    std::optional<EventMoment> end;
    /// true: the days without times; false: at a time (the start's, else 09:00).
    std::optional<bool> allDay;
    friend bool operator==(const EventTimes &, const EventTimes &) = default;
};

/// The fields an edit sets; each one not given is kept as the event has it.
struct EventPatch {
    std::optional<QString> title;
    std::optional<QString> location;    ///< "" removes it
    std::optional<QString> description; ///< "" removes it
    EventTimes times;
    std::optional<QString> rule;        ///< Brulion's rule ("every week", "co tydzień"); "" none
    std::optional<int> reminderMinutes; ///< a reminder this long before the start; -1 none
    friend bool operator==(const EventPatch &, const EventPatch &) = default;
};

/// The addresses an answer intent carries at most.
inline constexpr int MaxAnswerAddresses = 100;

/// An event to create, change or delete in a CalDAV calendar, or the account's answer to an
/// invitation.
struct EventIntent {
    enum class Kind { Create, Change, Delete, Answer };
    Kind kind = Kind::Change;
    QString id; ///< its inbox file's name (newIntentId())
    QString calendarId;
    QString uid;
    /// One occurrence of a series: its original start (Occurrence::instance); none: the whole
    /// event.
    std::optional<io::ical::DateTimeValue> instance;
    EventPatch patch;
    /// Answer: the account's answer to the invitation (Accepted, Declined, Tentative), for the
    /// whole event (a series: every occurrence) — answerInvitation().
    std::optional<Participation> answer;
    /// Answer: how invitations name the account (Account::addresses; at most 100).
    QStringList addresses;
    QDateTime at; ///< when it was asked: the newer change wins a conflict

    QJsonObject toJson() const;
    /// nullopt when `json` is no intent: an unknown kind or version, no calendar or UID, a
    /// malformed date, time or occurrence, a text longer than the limits.
    static std::optional<EventIntent> fromJson(const QJsonObject &json);
};

/// Why an intent cannot be carried out.
enum class EventProblem {
    None,
    NoCalendar,   ///< no calendar of that id
    Subscription, ///< an ICS subscription: a copy of another program's feed, read only
    ReadOnly,     ///< the server does not let the account write the calendar
    NoEvents,     ///< the calendar holds tasks only
    EventsHidden, ///< its events are not shown (the calendar's "Events in the calendar" is off)
    NotFound,     ///< no event of that UID in the calendar
    NoOccurrence, ///< the series has no occurrence that started there
    NoTitle,
    NoStart, ///< a new event needs its day
    EndBeforeStart,
    AllDayWithTime, ///< all-day, yet a time given
    UnknownRule,    ///< the recurrence is not understood
    OccurrenceRule, ///< one occurrence has no recurrence of its own
    NotInvited,     ///< an answer, yet the event does not invite the account
    NoAnswer,       ///< an answer without one
};

/// "no_calendar", "subscription", "read_only", "end_before_start" …: stable (the `reason` of
/// brulion-cli's errors).
QString problemCode(EventProblem problem);
/// The problem in English words (the log's technical text).
QString problemText(EventProblem problem);

/// Whether events can be created and changed in the calendar `id`: None, or why not
/// (NoCalendar, Subscription, ReadOnly, NoEvents, EventsHidden).
EventProblem writableProblem(const CalendarSettings &settings, const QString &id);

/// An event (or one occurrence of a series) as the event dialog shows it.
struct EventFields {
    EventEdit edit;       ///< its fields on the device's clock (ruleChanged, reminderChanged false)
    QString ruleText;     ///< RRULE as written ("" none); edit.rule is "" when it is not representable
    QString path;         ///< its CalDAV resource ("" for a subscription's)
    qsizetype event = -1; ///< its index in the data: the occurrence's override, else the series
    bool recurring = false;
};

/// What `uid` (one occurrence of it: `instance`) is in `data` on the clock of `zone`; nullopt
/// with `problem` NotFound (no such UID) or NoOccurrence (the series has none there: left out,
/// cancelled, never one).
std::optional<EventFields> eventFields(const CalendarData &data, const QString &uid,
                                       const std::optional<io::ical::DateTimeValue> &instance, const QTimeZone &zone,
                                       EventProblem *problem = nullptr);

/// The fields `current` (nullopt: a new event) has after `patch` — times as EventTimes says, the
/// rule and the reminder marked changed when given. nullopt with `problem` when that is no event:
/// no title, no start, the end before the start, a time on an all-day event, a rule not understood.
std::optional<EventEdit> editedFields(const std::optional<EventFields> &current, const EventPatch &patch,
                                      EventProblem *problem = nullptr);

/// The change `intent` makes of `calendar` as `state` holds it: the EventChange to queue
/// (CalendarSync::queueEvent(); its path and time set). A new event (a UID the calendar already
/// has: that event changed — an intent taken twice makes one event), the event or one occurrence
/// changed (an override), the event deleted or one occurrence left out (EXDATE). nullopt with
/// `problem` when it cannot be done; `result` receives the event's fields after a create or change.
std::optional<EventChange> plannedChange(const CalendarEntry &calendar, const CalendarState &state,
                                         const EventIntent &intent, const TodoContext &context,
                                         EventProblem *problem = nullptr, EventEdit *result = nullptr);

/// `state` with `change` waiting to be sent, as CalendarSync::queueEvent() keeps it: shown at once
/// (its resources), one change per member (a later one takes an earlier one's place; deleting an
/// event never sent drops both). `change.path` must be set.
void addToOutbox(CalendarState &state, EventChange change);

/// `state` of `calendar` as it will be once the app's run took `intents` in their order (those of
/// other calendars and those that cannot be made left out): what brulion-cli shows, and checks a
/// new intent against, while the app has not taken the ones it left before.
CalendarState withIntents(CalendarState state, const CalendarEntry &calendar, const QList<EventIntent> &intents,
                          const TodoContext &context);

/// The events of a CalDAV calendar as `state` holds them (its VEVENT members, with their paths).
CalendarData eventsOf(const CalendarState &state);

/// Where a new event of `calendar` goes on the server (its path, from the UID).
QString newEventPath(const CalendarEntry &calendar, const QString &uid);
/// A new event's UID ("brulion-event-" and 16 random letters and digits).
QString newEventUid();

/// An occurrence's original start as text — the app's event keys, `calendar events`' instance and
/// `calendar edit --occurrence`: "20261118T093000@Europe/Warsaw", "20261118T083000Z", "20261118".
QString instanceText(const io::ical::DateTimeValue &instance);
/// The value back; nullopt when `text` is none.
std::optional<io::ical::DateTimeValue> instanceFromText(const QString &text);

// ---- the inbox -----------------------------------------------------------------------------------

/// The changes brulion-cli asks for, waiting for the app: `<calendars folder>/inbox`.
QString inboxFolder(const QString &calendarsFolder);
/// A new intent's id, ordered by time: "20261127T091500123-k3j9x0qa" (`at` in UTC).
QString newIntentId(const QDateTime &at);
/// Leave `intent` for the app's run: `<inbox>/<intent.id>.json`, written once and atomically,
/// owner-only; never written again (the run deletes it). False when it could not be written.
bool postIntent(const QString &calendarsFolder, const EventIntent &intent);

/// One file of the inbox.
struct PendingIntent {
    QString file;                      ///< its name in the inbox
    std::optional<EventIntent> intent; ///< none: the file is no intent (broken, too big)
};
/// The intents waiting in the inbox, oldest first (at most `max`). Reads only.
QList<PendingIntent> pendingIntents(const QString &calendarsFolder, int max = 1000);

} // namespace brulion::caldav

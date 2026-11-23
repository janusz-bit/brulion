// Tasks of a CalDAV calendar as task lines of the notes (docs/SYNTAX.md §5 "Tasks synced with
// CalDAV"): the fields both sides have in common (TaskFields), read from a task line and from a
// VTODO, written into a task line (only the tokens that change, the rest of the line and the note
// byte for byte) and into a VTODO (the server's version patched: properties Brulion does not know
// travel back untouched), and how a line keeps its task's UID — its block id (`^c1x2y3z4a5b6`,
// hidden in Brulion's editor and in Obsidian's reading view). Part of the caldav module — see
// caldav/caldav.h.
#pragma once

#include "caldav/zones.h"
#include "io/ical.h"
#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <optional>

namespace brulion::caldav {

/// A reminder of a task: a wall-clock time, loud or not (⏰ / ⏰!).
struct TaskReminder {
    DateSpec at;
    bool alarm = false;
    friend bool operator==(const TaskReminder &, const TaskReminder &) = default;
};

/// What a task line and a VTODO share, normalised so that one side's form compares equal to the
/// other's: priorities in the three classes iCalendar clients know (high = 1–4, medium = 5, low =
/// 6–9), "waiting" ([>]) as to do, reminders sorted, categories sorted case-insensitively.
struct TaskFields {
    QString summary;
    TaskStatus status = TaskStatus::Todo;
    std::optional<DateSpec> due;   ///< 📅 ↔ DUE
    std::optional<DateSpec> start; ///< ⏳ (or 🛫 when the line has it) ↔ DTSTART
    Priority priority = Priority::None;
    std::optional<QDate> completed; ///< ✅ ↔ COMPLETED
    QString recurrence;             ///< 🔁, canonical English ("" none or not representable)
    QList<TaskReminder> reminders;  ///< ⏰ ↔ VALARM (DISPLAY, AUDIO)
    QStringList categories;         ///< the line's trailing #tags ↔ CATEGORIES
    QString description;            ///< the line's continuation text ↔ DESCRIPTION
    QString parentUid;              ///< RELATED-TO;RELTYPE=PARENT / the task the line is nested in

    /// The same fields after normalisation (normalized() of both compare equal).
    TaskFields normalized() const;
    friend bool operator==(const TaskFields &, const TaskFields &) = default;

    QJsonObject toJson() const;
    static TaskFields fromJson(const QJsonObject &json);
};

/// Which fields differ (bit set of Field).
enum Field : unsigned {
    FieldSummary = 1u << 0,
    FieldStatus = 1u << 1,
    FieldDue = 1u << 2,
    FieldStart = 1u << 3,
    FieldPriority = 1u << 4,
    FieldCompleted = 1u << 5,
    FieldRecurrence = 1u << 6,
    FieldReminders = 1u << 7,
    FieldCategories = 1u << 8,
    FieldDescription = 1u << 9,
    FieldParent = 1u << 10, ///< RELATED-TO;RELTYPE=PARENT ↔ the task the line is nested in
    AllFields = (1u << 11) - 1,
};
unsigned changedFields(const TaskFields &a, const TaskFields &b);
/// `to` with the fields of `mask` taken from `from`.
TaskFields withFields(const TaskFields &to, const TaskFields &from, unsigned mask);

/// The block id a line of the task with `uid` carries: the id itself for a task Brulion made
/// ("brulion-<id>"), else "c" and 12 characters of the UID's hash (base 36, FNV-1a).
QString blockIdForUid(const QString &uid);
/// The UID of a task Brulion makes from a line with block id `id`: "brulion-<id>".
QString uidForBlockId(const QString &id);
/// A new block id for a task line: 12 characters [a-z0-9], random.
QString newTaskBlockId();

/// The fields of a task block (its body as BlockDocument holds it — "[ ] text 📅 … ^id\nmore
/// text" — and its parse).
TaskFields fieldsFromBlock(const Block &block, QTime defaultReminderTime);
/// The fields of a VTODO; times converted to `zone`'s wall clock (the device's) — TZIDs through
/// `zones` (the resource's VTIMEZONEs) where the system does not know them.
TaskFields fieldsFromTodo(const io::ical::Component &vtodo, const ZoneTable &zones, const QTimeZone &zone,
                          QTime defaultReminderTime);
/// The first VTODO of an iCalendar object (nullptr none).
const io::ical::Component *firstTodo(const QList<io::ical::Component> &roots);

/// The body of a new task line (BlockDocument body: checkbox, text, tags, tokens, block id, then
/// the description's lines).
QString newTaskBody(const TaskFields &fields, const QString &blockId);
/// `body` with the fields of `mask` set to `fields`' values: the status in the checkbox (or Logseq
/// keyword) with ✅ / ❌ dates, the text and its trailing tags, and each token edited where it is —
/// everything else of the body kept. `today`: the ❌ date of a task cancelled on the server.
QString applyFieldsToBody(const QString &body, const TaskFields &fields, unsigned mask, QDate today);

/// Remote text made safe for a task line: one line, no task tokens (📅 ⏳ … would be read as
/// dates), no trailing block id.
QString lineSafeText(const QString &text);

/// What a VTODO is written with.
struct TodoContext {
    QDateTime now;   ///< DTSTAMP, LAST-MODIFIED (any zone; written in UTC)
    QTimeZone zone;  ///< the device's: wall-clock times are written in it (TZID + VTIMEZONE)
    QString prodId = QStringLiteral("-//Brulion//Brulion//EN");
};

/// A new VCALENDAR holding one VTODO with `uid` and `fields`.
QByteArray newTodo(const QString &uid, const TaskFields &fields, const TodoContext &context);
/// The server's VCALENDAR `ics` with its VTODO's fields of `mask` set to `fields`; SEQUENCE,
/// DTSTAMP, LAST-MODIFIED updated, every other property and component kept. Empty when `ics`
/// holds no VTODO.
QByteArray patchTodo(const QByteArray &ics, const TaskFields &fields, unsigned mask, const TodoContext &context);

/// iCalendar components written back as text (CRLF, folded lines; values as they were read).
QByteArray serialize(const QList<io::ical::Component> &roots);
/// A VTIMEZONE of `zone` for the years around `year` (its rules as RRULEs when its changes follow
/// a weekday-of-month pattern, else the year's changes as dates); nullopt for UTC and zones
/// without a name.
std::optional<io::ical::Component> vtimezoneFor(const QTimeZone &zone, int year);

} // namespace brulion::caldav

// Notifications for reminders (docs/SYNTAX.md §6-7): the platform-neutral notification record,
// the sink interface implemented by the platform layer, and the computation of the reminder
// occurrences to schedule. Part of the reminders module — see reminders/reminders.h.
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringView>

namespace brulion {

class VaultIndex;

/// One notification to show at a local wall-clock time: one occurrence of a `⏰` / `⏰!` /
/// `(@…)` reminder.
struct ScheduledNotification {
    /// Longest title (in UTF-16 code units, "…" included).
    static constexpr int MaxTitleLength = 120;
    /// Default ring time of alarms before they turn into a missed-alarm notification.
    static constexpr int DefaultRingMinutes = 10;

    /// Stable id: notificationId(path, at, text). It does not depend on the line, so editing
    /// other parts of the note keeps it; every occurrence of a recurring reminder has its own.
    QString id;
    /// Positive 31-bit integer id for native APIs (Android); unique within one batch (see
    /// assignNativeIds()). 0 until assigned.
    int nativeId = 0;
    /// Plain text of the reminder's block (tokens and Markdown syntax removed, whitespace
    /// collapsed, at most MaxTitleLength); the page title when the block has no text.
    QString title;
    /// Page title, plus the due date of the reminder's task ("Dom · termin: dziś 17:00").
    QString body;
    /// Local wall-clock time (Qt::LocalTime), seconds 0.
    QDateTime at;
    /// `⏰!`: loud, stays until dismissed.
    bool alarm = false;
    /// Alarms: minutes to ring before turning into a missed-alarm notification (1–60).
    int ringMinutes = DefaultRingMinutes;
    /// Where the reminder is written (tapping the notification opens the note at that line).
    VaultPath path;
    /// 0-based line of the reminder token.
    int line = 0;
    /// 0-based first line of the open task the reminder belongs to (its own block's task, or for
    /// a bare reminder bullet the enclosing item's); -1: the reminder is not on a task. The
    /// notification's "Done" marks this task done (markdown::setTaskStatus).
    int taskLine = -1;

    friend bool operator==(const ScheduledNotification &, const ScheduledNotification &) = default;
};

/// Where notifications go: implemented by the platform layer (freedesktop notifications,
/// Android AlarmManager, the browser Notification API) and by fakes in tests.
class NotificationSink {
public:
    virtual ~NotificationSink() = default;

    /// True when notifications given to schedule() fire by themselves, also while the app is
    /// closed (Android alarms). False when the app has to time them: ReminderScheduler then
    /// calls showNow() at the right moment.
    virtual bool firesWhenClosed() const = 0;
    /// Replace the whole set of pending notifications with `notifications` (sorted by time).
    /// An empty list cancels everything.
    virtual void schedule(const QList<ScheduledNotification> &notifications) = 0;
    /// Show a notification immediately.
    virtual void showNow(const ScheduledNotification &notification) = 0;
};

/// Stable id of a reminder occurrence: FNV-1a 64 (16 hex digits) of
/// "path|yyyy-MM-ddTHH:mm|<text::contentHash(text)>" (`at` in local time), where `text` is the
/// reminder's Markdown text (the block's text, or the parent block's text for a bare `- ⏰ …`
/// bullet; see upcomingNotifications()). The text enters as its hash, so the cost of an id does
/// not grow with the block.
QString notificationId(const VaultPath &path, const QDateTime &at, QStringView text);

/// The preferred native id of a notification id: its FNV-1a 64 hash reduced to 1…2^31-1.
int nativeIdFor(QStringView id);

/// Give every notification a nativeId unique within `notifications`: nativeIdFor(id), on a
/// collision the next free value (linear probing, wrapping from 2^31-1 to 1). Processed in list
/// order, so equal ids get consecutive values.
void assignNativeIds(QList<ScheduledNotification> &notifications);

/// All reminder occurrences of the vault whose time lies within [from, to] (inclusive; any time
/// spec), sorted by time then id, with nativeIds assigned. Same rules as
/// upcomingNotifications(), without the limit.
QList<ScheduledNotification> notificationsBetween(const VaultIndex &index, const QDateTime &from, const QDateTime &to,
                                                  dates::UiLocale locale = dates::UiLocale::Polish);

/// The notifications to schedule at `now`, sorted by time (then id), at most `max`:
///
/// * every reminder of the index (`⏰`, `⏰!` alarms, `(@…)`), except reminders on tasks that are
///   done or cancelled (SYNTAX §6);
/// * a one-off reminder when its time lies within [now - 1 min, now + horizonDays days] (the
///   minute of grace lets a reminder set for "now" still be shown);
/// * a reminder whose block has a valid `🔁` rule (or a recurring task) is expanded with
///   dates::occurrencesBetween() — the written date is the first occurrence, each occurrence
///   keeps the time — and every occurrence within the same window is included; a reminder of a
///   recurring task with a due / scheduled / start date keeps its distance in days to that
///   reference date (due first) — the date the series follows, as in the next instance written
///   when the task is completed (SYNTAX §5): "📅 15th 🔁 every month on the 15th ⏰ 14th 18:00"
///   reminds on the 14th of every month;
/// * a bare reminder bullet (a list item with no text or task of its own nested under another
///   list item, e.g. `- ⏰ …` written by snoozeReminder() under a recurring task) belongs to its
///   parent item — through further bare bullets: it takes that item's text and task, so it does
///   not fire once the task is done or cancelled;
/// * taskLine = the first line of that task (-1 when the reminder is not on a task);
/// * title = plain text of the reminder's block (links show their display text, `**`/`==`/
///   `~~`/`` ` `` removed, whitespace collapsed), at most 120 characters, else the page title;
///   body = page title, plus " · termin: <date>" ("due <date>" in English) when the task has a
///   due date, formatted relative to the occurrence's day ("dziś 17:00", "pt 17 paź");
/// * occurrences with equal ids (the same text and time in one note) are merged into one
///   (an alarm if any of them is), nativeIds are assigned with assignNativeIds().
///
/// `now` may use any time spec (it is converted to local time; the horizon counts local
/// calendar days). `horizonDays` is clamped to 0…3660; `max` <= 0 gives an empty list.
/// `dropped` (optional) receives the number of occurrences left out because of `max`. `locale`
/// is the language of the body.
QList<ScheduledNotification> upcomingNotifications(const VaultIndex &index, const QDateTime &now, int horizonDays = 14,
                                                   int max = 64, int *dropped = nullptr,
                                                   dates::UiLocale locale = dates::UiLocale::Polish);

} // namespace brulion

Q_DECLARE_METATYPE(brulion::ScheduledNotification)

// Internal helpers of the reminders module (not part of the public API): where a reminder takes
// its text and task from, recurrence of a reminder, plain-text titles, timestamps for the
// LocalStore and native-id probing.
#pragma once

#include "dates/recurrence.h"
#include "index/vaultindex.h"
#include "types.h"

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::reminders_detail {

/// The text, task and page a reminder belongs to.
struct ReminderSource {
    /// Markdown text of the reminder's block; for a bare reminder bullet (a list item without
    /// text or task of its own, nested under another list item) the text of the nearest
    /// enclosing list item that has text or a task (through further bare bullets).
    QString text;
    /// The block's task; for a bare bullet the task of that enclosing item.
    std::optional<Task> task;
    /// Title of the note.
    QString pageTitle;
};

/// Resolve the source of `ref` (see ReminderSource).
ReminderSource sourceOf(const VaultIndex &index, const ReminderRef &ref);

/// Done or cancelled: reminders of such tasks never fire, recurring tasks stop expanding.
bool isClosed(const Task &task);
/// isClosed() of the task, false without one.
bool isClosed(const std::optional<Task> &task);

/// The parsed rule of a canonical recurrence text, nullopt when empty or invalid.
std::optional<dates::RecurrenceRule> ruleOf(const QString &recurrence);

/// The date a recurring task's series follows (Obsidian Tasks, markdown::setTaskStatus): due,
/// else scheduled, else start; nullopt when the task has none of them.
std::optional<DateSpec> seriesReference(const Task &task);

/// Dates of `date` within [from, to] (each with `date`'s time). Without a rule: just `date`
/// when within the range. With a rule: the series — without a `reference` the occurrences of
/// `rule` from `date`; with one, `date` keeps its distance in days to `reference`, whose
/// occurrences follow `rule` (a task's other dates and reminders move with its due date, like
/// the next instance written on completion). At most MaxOccurrences.
QList<DateSpec> datesWithin(const DateSpec &date, const std::optional<dates::RecurrenceRule> &rule, QDate from,
                            QDate to, const std::optional<DateSpec> &reference = std::nullopt);
inline constexpr int MaxOccurrences = 500;

/// The dates of a reminder within [from, to]: one-off, or (when `expand` and the block has a
/// valid rule) its series — a reminder of a recurring task keeps its distance to the task's
/// seriesReference(), any other follows the rule from its own date.
QList<DateSpec> reminderDates(const ReminderRef &ref, bool expand, QDate from, QDate to);

/// Plain text of inline Markdown for titles: links replaced by their display text (wiki
/// display/target, Markdown link text, `((id))` by the referenced block's text — elided to a
/// title's length — when `index` knows it; display texts without markup, page names and URLs
/// as written), code spans by their content (verbatim), `**` `__` `~~` `==` pairs elsewhere
/// removed (`__` not inside words), whitespace (incl. line breaks) collapsed to single spaces,
/// trimmed. Linear time.
QString plainText(QStringView markdown, const VaultIndex *index = nullptr);

/// `text` cut to at most `max` UTF-16 units with "…" (never splitting a surrogate pair).
QString elide(const QString &text, int max);

/// Date-time as local "yyyy-MM-ddTHH:mm:ss.zzz" (no zone: wall-clock time; other time specs
/// are converted to local time first) and back; an unparseable string gives an invalid
/// QDateTime.
QString formatStamp(const QDateTime &dt);
QDateTime parseStamp(QStringView text);

/// First value >= `candidate` (wrapping from 2^31-1 to 1) not in `used`.
int probeNativeId(int candidate, const QSet<int> &used);

} // namespace brulion::reminders_detail

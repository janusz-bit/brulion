// Snoozing a reminder: a pure content edit (content in -> content out; line endings and all
// other bytes preserved). Part of the reminders module — see reminders/reminders.h.
#pragma once

#include "types.h"

#include <QDateTime>
#include <QString>

namespace brulion {

/// Move the reminder on `line` (0-based) of `content` to `newAt` (written in local wall-clock
/// time — another time spec is converted —, seconds dropped).
///
/// * One-off reminder: the first `⏰` / `⏰!` token on the line is replaced by
///   `⏰ yyyy-MM-dd HH:mm` (the `!` of an alarm is kept). A line with only an obsidian-reminder
///   token `(@…)` gets the new time in that syntax, `(@yyyy-MM-dd HH:mm)`. Only tokens the
///   parser reads as reminders count (not ones in inline code or hidden comments).
/// * Recurring reminder (its block has a valid `🔁` rule, or is a recurring task): the series
///   must not move, so the line is left alone and a one-off reminder is added instead — a child
///   bullet `- ⏰ newAt` (`⏰!` when that token is an alarm) inserted as the block's first
///   child, indented and marked like its siblings (markdown::BlockDocument::insertAfter). Such a
///   bare bullet takes the parent's text for its notification and stops firing when the parent
///   task is done (see upcomingNotifications()). A recurring reminder outside a list item
///   (paragraph, heading, callout) gets the bullet as a new top-level list item right after its
///   block.
///
/// Returns `content` unchanged when `line` is out of range, holds no reminder the parser
/// recognises (tokens in code are not reminders), or `newAt` is invalid.
QString snoozeReminder(const QString &content, int line, const QDateTime &newAt);

} // namespace brulion

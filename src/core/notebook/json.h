// JSON of notes, tasks, reminders and query results — what brulion-cli prints and its MCP tools
// return, with the keys of the app's own maps (Workspace::runQuery, search, calendarItems,
// vaultConfig) and of the JSON export "brulion-export v1" (the io module uses the names below).
// Dates are local "YYYY-MM-DD" / "YYYY-MM-DD HH:mm", never UTC; `line` is 0-based. Part of the
// notebook module — see notebook/notebook.h.
#pragma once

#include "index/query.h"
#include "reminders/calendar.h"
#include "reminders/notifications.h"
#include "types.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::notebook {

// ---- Names ----------------------------------------------------------------------------------

/// "todo" | "doing" | "done" | "cancelled" | "deferred".
QString taskStatusName(TaskStatus status);
/// A status from English or Polish words, case- and diacritic-insensitive: todo / open /
/// "do zrobienia", doing / in-progress / "w toku", done / zrobione / wykonane, cancelled /
/// canceled / cancel / anulowane, deferred / defer / waiting / odłożone / czeka. nullopt otherwise.
std::optional<TaskStatus> taskStatusFromName(QStringView name);
/// "highest" … "lowest"; "" for none.
QString priorityName(Priority priority);
/// A priority from English or Polish words (najwyższy, wysoki, średni, niski, najniższy, none /
/// brak / normal); nullopt otherwise.
std::optional<Priority> priorityFromName(QStringView name);
QString blockKindName(BlockKind kind);
QString linkKindName(LinkKind kind);
QString queryTypeName(QueryType type);
QString calendarKindName(CalendarItem::Kind kind);

// ---- Values ---------------------------------------------------------------------------------

/// {status, marker, text, priority, due?, scheduled?, start?, created?, done?, cancelled?,
/// recurrence?} — the keys of Workspace::runQuery's `task`.
QJsonObject taskJson(const Task &task);
/// {at "YYYY-MM-DD HH:mm", alarm, recurrence?}.
QJsonObject reminderJson(const Reminder &reminder);
/// One block of a note: {line, endLine, depth, kind, text, raw, blockId?, headingLevel?, task?,
/// reminders?, tags?}.
QJsonObject blockJson(const Block &block);
/// A query item as Workspace::runQuery gives it: {path, title, folder, line, depth, text, raw,
/// blockId?, task?, reminder?}.
QJsonObject queryItemJson(const VaultIndex &index, const QueryItem &item);
/// {type, total, items, groups: [{key, label, items}]}.
QJsonObject queryResultJson(const VaultIndex &index, const QueryResult &result);
/// {path, title, folder, line, excerpt, score, journal} (journal: the daily note's date or "").
QJsonObject searchHitJson(const SearchHit &hit, const VaultConfig &config);
/// {date, time, kind, title, path, line, status, recurring, alarm} (Workspace::calendarItems).
QJsonObject calendarItemJson(const CalendarItem &item);
/// {id, at, title, body, alarm, path, line}.
QJsonObject notificationJson(const ScheduledNotification &notification);
/// The keys of Workspace::vaultConfig(): journalsFolder, journalFileFormat, newPagesFolder,
/// attachmentsFolder, templatesFolder, journalTemplate, defaultReminderTime, weekStartsOn,
/// journalMode, pageMode.
QJsonObject vaultConfigJson(const VaultConfig &config);

} // namespace brulion::notebook

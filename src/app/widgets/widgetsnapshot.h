// The snapshot the Android home-screen widgets show (docs/ANDROID.md "Widgety"): the app computes,
// Kotlin renders. Built on every platform (pure, unit-tested in tests/app/widgets); only the
// Android publisher (widgetpublisher.h) hands it to the widgets through AndroidBridge.
//
// JSON (version 1; plain text only, never HTML — RemoteViews show text as given):
//
//   { "version": 1, "built": "2026-10-27T14:05" (local wall clock), "today": "2026-10-27",
//     "weekStart": 1 (Monday … 7 Sunday), "locale": "pl" | "en",
//     "vault": { "id", "name", "root" (absolute folder) } | null (no notebook: "Otwórz Brulion"),
//     "tasks": [ { "key": "path:line", "text", "status", "priority" (0 none … 5 highest), "path",
//                  "line" (0-based), "page" (title), "due", "scheduled", "start", "reminder"
//                  ("YYYY-MM-DD[ HH:mm]" or ""), "alarm", "recurring", "section" } ],
//     "taskCounts": { "overdue", "today", "tomorrow" },
//     "calendar": { "from", "to", "days": { "YYYY-MM-DD": { "journal" (path or ""), "kinds"
//                   [due, overdue, scheduled, reminder, alarm, done, journal], "items": [ { "time"
//                   ("HH:mm" or ""), "minutes" (-1 all-day), "title", "kind", "variant", "path",
//                   "line", "isTask", "closed", "alarm", "recurring" } ] } } },
//     "notes": { "favorites": [ { "path", "title" } ], "recent": [ … ] },
//     "capture": { "days": [ { "date", "path", "exists", "newText" } ] } }
//
// tasks: the open tasks of TasksView (same sections and "due" order, TaskListModel) that are
// overdue or filed at most taskDays ahead; their raw dates let the widget file them again for a
// later day (the snapshot survives midnight: WidgetSnapshot.kt mirrors TaskListModel::sectionOf).
// calendar: CalendarModel's agenda from a week before today to eight weeks ahead (days without
// entries left out). capture: where the widget's quick add writes while Brulion is closed — the
// daily note of each of the next days and, for a missing one, the text it starts with (the
// journal template filled in).
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace brulion {
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::widgets {

/// A page of the notes widget's lists.
struct PageRef {
    QString path;
    QString title;
};

/// The daily note quick add writes to on `date`.
struct CaptureDay {
    QDate date;
    QString path;    ///< vault-relative
    bool exists = false;
    QString newText; ///< the text a missing note starts with (the journal template; often "")
};

struct SnapshotOptions {
    QDateTime now;                          ///< local wall clock
    Qt::DayOfWeek weekStart = Qt::Monday;
    QString locale = QStringLiteral("pl");  ///< "pl" | "en" (the app's language)
    QString vaultId;
    QString vaultName;
    QString vaultRoot;                      ///< absolute folder of the notebook
    QList<PageRef> favorites;
    QList<PageRef> recent;
    QList<CaptureDay> captureDays;
    int taskDays = 14;                      ///< open tasks filed at most this many days ahead
    int maxTasks = 200;
    int calendarDaysBefore = 7;
    int calendarDaysAfter = 56;
    int maxDayItems = 40;
    int maxPages = 10;                      ///< favourites / recent pages each
};

/// Version of the JSON (WidgetSnapshot.kt ignores other versions).
inline constexpr int SnapshotVersion = 1;

/// The snapshot of a notebook (see the file comment).
QJsonObject buildSnapshot(const VaultIndex &index, const SnapshotOptions &options);

/// The snapshot without a notebook ("vault": null): the widgets ask to open Brulion.
QJsonObject emptySnapshot(const QDateTime &now, const QString &locale);

/// The daily notes of `days` days from `today` (notebook::journalFileFor: also a note under
/// another name the journal folder accepts), each missing one with notebook::newJournalText.
/// Never writes.
QList<CaptureDay> captureDays(Vault &vault, QDate today, int days, const QDateTime &now, dates::UiLocale locale);

} // namespace brulion::widgets

// Quick add of the Android widgets (docs/ANDROID.md "Widgety"): what an entry added from a widget
// writes into today's daily note. QuickAddActivity.kt usually writes the line itself, at once
// (JournalAppend.kt ports widgetActionBlock() and notebook::appendBlock(); both are checked
// against tests/data/widgets/journal-cases.json); an entry it could not write (no snapshot of the
// notebook yet, an I/O error) waits in WidgetInbox.kt until the app takes it
// (AndroidBridge::takeWidgetActions()) and writes it here. Pure, built on every platform.
//
// An entry (JSON, as WidgetInbox.kt keeps it):
//   { "id": "…", "kind": "task" | "note", "text", "date": "YYYY-MM-DD" | "", "time": "HH:mm" | "",
//     "alarm": bool, "vault": "<Vault::id()>" | "", "added": "YYYY-MM-DDTHH:mm" }
#pragma once

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTime>

#include <optional>

namespace brulion::widgets {

struct WidgetAction {
    enum class Kind { Task, Note };
    QString id;
    Kind kind = Kind::Task;
    QString text;
    QDate date;                ///< the chosen day ("Dziś", "Jutro", "Data…"); invalid = no date
    std::optional<QTime> time; ///< "Godzina…": a reminder on `date`
    bool alarm = false;        ///< the reminder is an alarm (⏰!)
    QString vault;             ///< the notebook's id ("" = any)
};

/// One entry; nullopt without an id or with an unknown kind. A time without a date is dropped.
std::optional<WidgetAction> widgetActionFromJson(const QJsonObject &json);
/// The entries of `array` in order, broken ones and repeated ids skipped.
QList<WidgetAction> widgetActions(const QJsonArray &array);

/// The block an entry writes ("" for blank text): a task is planning::taskLine() — "- [ ] text"
/// with " 📅 <date>" for a day without a time, " ⏰ <date> <time>" (" ⏰! …" for an alarm) with
/// one, nothing without a day (the app's quick add: a time makes a reminder, a bare day a due
/// date); a note is notebook::blockFromText() ("- text", further lines indented).
QString widgetActionBlock(const WidgetAction &action);

/// Toasts after the app wrote queued entries (translated).
QString widgetActionsAddedMessage(int count);
/// The entry with `text` could not be written (it is gone from the queue: the toast names it).
QString widgetActionFailedMessage(const QString &text);

} // namespace brulion::widgets

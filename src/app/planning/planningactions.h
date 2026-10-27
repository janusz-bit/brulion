// PlanningActions — the edits the planning views make, through an EditorHost (the Workspace in
// the app: atomic writes, history, index update, open editors reload):
//
//   PlanningActions { id: actions; host: Workspace; vaultIndex: PlanningContext.vaultIndex
//                     today: DatesHelper.today; now: DatesHelper.now() }
//
//   toggleTask(path, line, text)        todo/doing/deferred <-> done (markdown::toggleTask:
//                                       ✅ date, recurring tasks get their next instance)
//   setTaskStatus(path, line, status, text)   the status menus ("" = no status)
//   editToken(path, line, text, kind, value, action, a, b)   the token menus of Tasks and the
//                                       calendar (DateChipMenu): a date, the reminder, the 🔁 rule,
//                                       the priority of the block on `line`, only that token's bytes
//                                       written (markdown::editBlockToken)
//   snoozeReminder(path, line, at)      one occurrence of a recurring reminder later (a one-off
//                                       child bullet, the series untouched: reminders/snooze.h)
//   linkText(url, fromPath) / copyText(text)   the link menu's "Kopiuj"
//   undo()                              puts the file back as it was before the last change made
//                                       here — only while nobody changed it since (canUndo)
//   appendBlock(path, block)            a new block at the end of a page (a trailing empty
//                                       template bullet is filled), e.g. the day's journal
//                                       ("Dodaj zadanie" / "Dodaj przypomnienie" in the calendar)
//   taskLine / reminderLine / parseQuickAdd   the Markdown lines those write
//
// Before changing a task the file is read again and the task is looked up at `line`; when the
// line moved (the index was a moment behind) the task with the same text nearest to it is used,
// and when it is gone nothing is written (false + failed()). Pure line edits: everything else in
// the file stays byte-for-byte.
#pragma once

#include "editorhost.h"
#include "types.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

namespace brulion {
class VaultIndex;
} // namespace brulion

namespace brulion::planning {

class PlanningActions : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    /// Optional: the vault config (journal folder, default reminder time) for parsing.
    Q_PROPERTY(QObject *vaultIndex READ vaultIndexObject WRITE setVaultIndexObject NOTIFY vaultIndexChanged FINAL)
    /// Today ("YYYY-MM-DD") for ✅ dates; "" = the system clock.
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY todayChanged FINAL)
    /// Now ("YYYY-MM-DD HH:mm") for natural-language dates; "" = the system clock.
    Q_PROPERTY(QString now READ now WRITE setNow NOTIFY nowChanged FINAL)
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged FINAL)
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// undo() can put the last change back.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged FINAL)

public:
    explicit PlanningActions(QObject *parent = nullptr);
    ~PlanningActions() override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    VaultIndex *vaultIndex() const;
    void setVaultIndex(VaultIndex *index);
    QObject *vaultIndexObject() const;
    void setVaultIndexObject(QObject *index);
    QString today() const { return m_today; }
    void setToday(const QString &today);
    QString now() const { return m_now; }
    void setNow(const QString &now);
    int weekStart() const { return m_weekStart; }
    void setWeekStart(int weekStart);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    bool canUndo() const;

    /// Toggle the task on `line` of `path` (see the file comment). `text` (the task's plain text,
    /// optional) finds it again when the line moved. Returns whether the file changed.
    Q_INVOKABLE bool toggleTask(const QString &path, int line, const QString &text = QString());
    /// Set a status ("todo" | "doing" | "done" | "cancelled" | "deferred"; "" = no status: the
    /// task marker goes, the item and its text stay).
    Q_INVOKABLE bool setTaskStatus(const QString &path, int line, const QString &status,
                                   const QString &text = QString());
    /// Undo the last change made through this object (false when the file changed since).
    Q_INVOKABLE bool undo();
    /// Append `block` at the end of `path` (created when missing). Returns whether it was written.
    Q_INVOKABLE bool appendBlock(const QString &path, const QString &block);

    /// A token menu's edit (markdown::applyTokenAction(): "set", "move", "shift", "alarm",
    /// "reminder", "remove", "recurrence", "priority", "blockRecurrence", "blockPriority") on the
    /// token of `kind` ("due" | "scheduled" | "start" | "done" | "cancelled" | "created" |
    /// "reminder" | "recurrence" | "priority") whose value is `value` (as the models give it: a
    /// date spec, the canonical rule, the priority name) in the block on `line` of `path` — or, when
    /// the line moved, the task with plain text `text` nearest to it. Only the token's bytes change;
    /// undo() puts the file back. False (and failed()) when the token is not there any more.
    Q_INVOKABLE bool editToken(const QString &path, int line, const QString &text, const QString &kind,
                               const QString &value, const QString &action, const QVariant &a = {},
                               const QVariant &b = {});
    /// Snooze the reminder on `line` of `path` to `at` ("YYYY-MM-DD HH:mm"): a recurring one gets a
    /// one-off child bullet, a one-off one moves (reminders::snoozeReminder). undo() puts it back.
    Q_INVOKABLE bool snoozeReminder(const QString &path, int line, const QString &at);

    /// Follow a link of rendered text (markdown::InlineRenderer URLs: page:, tag:, block:, ext:)
    /// written in `fromPath`, through the host's navigation (`newPane`: in the side panel). False
    /// for unknown schemes.
    Q_INVOKABLE bool openLink(const QString &url, const QString &fromPath = QString(), bool newPane = false);
    /// What a link menu's "Kopiuj" copies for `url` written in `fromPath`: `[[Page#Heading]]` by the
    /// page's shortest link target (as written when the page does not exist), `#tag` /
    /// `#[[tag with spaces]]`, `((id))`, the address; "" for other URLs.
    Q_INVOKABLE QString linkText(const QString &url, const QString &fromPath = QString()) const;
    /// Put `text` on the clipboard; false when the platform has none or it did not take it.
    Q_INVOKABLE bool copyText(const QString &text) const;

    /// "- [ ] text 📅 date" (date "" = none).
    Q_INVOKABLE QString taskLine(const QString &text, const QString &date = QString()) const;
    /// "- text ⏰ at" (`⏰!` for an alarm, " 🔁 rule" when recurring); at = "YYYY-MM-DD HH:mm".
    Q_INVOKABLE QString reminderLine(const QString &text, const QString &at, bool alarm = false,
                                     const QString &recurrence = QString()) const;
    /// Quick add preview: { ok, text, line, phrase, date, time, hasTime, kind ("reminder" |
    /// "due" | "task"), when (human: "jutro 09:00") }.
    Q_INVOKABLE QVariantMap parseQuickAdd(const QString &input) const;

    /// The task on `line` of `content` (or the one with plain text `text` nearest to it): its
    /// line, or -1. Exposed for tests.
    int locateTask(const QString &path, const QString &content, int line, const QString &text) const;

Q_SIGNALS:
    void hostChanged();
    void vaultIndexChanged();
    void todayChanged();
    void nowChanged();
    void weekStartChanged();
    void localeChanged();
    void undoChanged();
    /// A task's status was written (`status`: the new status name, "" for no status).
    void taskChanged(const QString &path, int line, const QString &status);
    /// Something could not be done (translated message for a toast).
    void failed(const QString &message);

private:
    QDate currentDay() const;
    QDateTime currentTime() const;
    VaultConfig config() const;
    bool writeChange(const QString &path, const QString &before, const QString &after);

    QPointer<EditorHost> m_host;
    QPointer<VaultIndex> m_index;
    QString m_today;
    QString m_now;
    int m_weekStart = 0;
    QString m_locale = QStringLiteral("pl");
    // Undo snapshot of the last change.
    QString m_undoPath;
    QString m_undoBefore;
    QString m_undoAfter;
};

} // namespace brulion::planning

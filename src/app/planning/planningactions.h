// PlanningActions — the edits the planning views make, through an EditorHost (the Workspace in
// the app: atomic writes, history, index update, open editors reload):
//
//   PlanningActions { id: actions; host: Workspace; vaultIndex: PlanningContext.vaultIndex
//                     today: DatesHelper.today; now: DatesHelper.now() }
//
//   toggleTask(path, line, text)        todo/doing/deferred <-> done (markdown::toggleTask:
//                                       ✅ date, recurring tasks get their next instance)
//   setTaskStatus(path, line, status, text)
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
    /// Set a status ("todo" | "doing" | "done" | "cancelled" | "deferred").
    Q_INVOKABLE bool setTaskStatus(const QString &path, int line, const QString &status,
                                   const QString &text = QString());
    /// Undo the last change made through this object (false when the file changed since).
    Q_INVOKABLE bool undo();
    /// Append `block` at the end of `path` (created when missing). Returns whether it was written.
    Q_INVOKABLE bool appendBlock(const QString &path, const QString &block);

    /// Follow a link of rendered text (markdown::InlineRenderer URLs: page:, tag:, block:, ext:)
    /// written in `fromPath`, through the host's navigation. False for unknown schemes.
    Q_INVOKABLE bool openLink(const QString &url, const QString &fromPath = QString());

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
    /// A task's status was written (`status`: the new status name).
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

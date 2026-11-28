// The tasks of one CalDAV calendar reconciled with the task lines of the notes (docs/SYNTAX.md §5
// "Tasks synced with CalDAV"): what changed on each side since the last sync, field by field
// (TaskFields), decides what is written where. The notes are written here (BlockDocument edits,
// compare-and-swap writes), the server's writes are handed back as RemoteOps for the run to send.
// Internal to the caldav module (calendarsync.cpp drives it).
#pragma once

#include "caldav/store.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QTimeZone>

namespace brulion {
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::caldav::detail {

/// A write to the server the reconciliation asks for.
struct RemoteOp {
    enum class Kind { Put, Create, Delete };
    Kind kind = Kind::Put;
    QString uid;
    QString path;
    QString ifMatch;
    QByteArray data;
    TaskFields local; ///< the line's fields once the server has it (the new local base)
    QString blockId;
    QString note;
};

struct TaskReport {
    int downloaded = 0;   ///< lines written from the server's changes (new or changed)
    int deletedLocal = 0; ///< lines removed (their task deleted on the server)
    int merged = 0;       ///< changed on both sides, merged field by field
    QStringList conflicts; ///< summaries of tasks changed in the same field on both sides
    QStringList restored;  ///< summaries of tasks kept although the other side deleted them
    QStringList changedNotes;
    QStringList errors;    ///< technical
    bool needsConfirmation = false;
    QStringList pendingDeletions; ///< summaries held back by the guard
};

struct TaskSyncInput {
    CalendarEntry entry;
    ConflictPolicy policy = ConflictPolicy::Newer;
    Vault *vault = nullptr;
    const VaultIndex *index = nullptr;
    QTimeZone zone;
    QDateTime now;
    bool confirmDeletions = false;
    /// Block ids other calendars track (their lines are not this calendar's new tasks).
    QSet<QString> claimedIds;
};

/// One reconciliation. run() reads and writes the notes and fills `state.tasks` for everything
/// settled here; the RemoteOps follow — remoteDone() records each one's outcome.
class TaskReconciler {
public:
    TaskReconciler(TaskSyncInput input, CalendarState &state);
    void run();
    QList<RemoteOp> remoteOps() const { return m_ops; }
    /// `etag` of the written member ("" unknown), or the failure (412: retried next run).
    void remoteDone(const RemoteOp &op, const QString &etag, const QString &path, bool ok, int status);
    const TaskReport &report() const { return m_report; }

    /// A change of one note planned by run() (tasksync.cpp; the note's helpers there use it).
    struct NoteEdit;

private:
    struct Located;
    struct NoteInfo;

    NoteInfo *note(const QString &path);
    std::optional<Located> locate(const TrackedTask &task);
    TaskFields fieldsOf(const NoteInfo &info, int block) const;
    QString parentUidOf(const ParsedNote &parsed, int block) const;
    /// The server moved the task of `line` under `parentUid` ("" the top level) while its line did
    /// not move: the line follows where it can (nestingEdit()), `target` then nested there too.
    void followServerNesting(const Located &line, const QString &blockId, const QString &parentUid,
                             const QHash<QString, Located> &localOf, TaskFields &target) const;
    /// The edit moving `line` (block `blockId`) under the task `parentUid` ("" the top level) —
    /// nullopt when it cannot follow there: a task of another note, or one nested in the line.
    std::optional<NoteEdit> nestingEdit(const Located &line, const QString &blockId, const QString &parentUid,
                                        const QHash<QString, Located> &localOf) const;
    void writeNotes();
    QDateTime remoteTime(const QByteArray &ics) const;
    QDateTime localTime(const QString &path) const;
    TodoContext todoContext() const;

    TaskSyncInput m_in;
    CalendarState &m_state;
    QHash<QString, NoteInfo *> m_notes;
    QList<NoteInfo *> m_ownedNotes;
    QList<RemoteOp> m_ops;
    TaskReport m_report;
    QHash<QString, QString> m_uidOfBlock; ///< block id -> uid, of this calendar's tracked tasks

public:
    ~TaskReconciler();
};

} // namespace brulion::caldav::detail

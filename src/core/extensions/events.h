#pragma once
// What the extensions' hooks hear of a burst of changes (docs/EXTENSIONS.md "Hooks"): the notes that
// changed, the tasks whose status changed, the daily notes made — and who changed them, as each
// extension's hooks get it. Pure: the app (WorkspaceExtensions) feeds it the vault's changes, the
// tests their own.
//
//   ChangeBatch batch;
//   batch.add({u"Dom.md"_s, u"Dom"_s, Origin::Extension, u"przeglad-zadan"_s, 1});
//   batch.addTasks(change, changedTasks(taskStates(before, oldText), taskStates(after, newText)));
//   for (const HookInfo &h : engine.hooks(u"change"_s))
//       if (const QVariantMap e = batch.changeEvent(h.extension); !e.isEmpty())
//           engine.runHook(h.id, e);
//
// Who hears what:
//  * a hook never hears what its own extension wrote (no echoes, no loop of one extension);
//  * every other change it hears — the editor, a sync, another program, an import, another
//    extension (one extension's tick in its view reaches the page another one keeps);
//  * a chain of hooks answering hooks ends: what a call writes is one deeper than what it ran for
//    (a command, an action, the console: 0, so their writes are 1; a hook that ran for changes of
//    depth d writes d + 1), and a change deeper than kMaxDepth reaches no hook — add() refuses
//    it and counts it (dropped()).
//
// A task's change is its status (" " → "x", "x" → " ", "/" …): a task is found again after an edit
// by its text without its status and its done or cancelled date (✅ ❌), the n-th task of a text
// before as the n-th after; a recurring task completed is the done one (its next occurrence has
// another date). A task added, removed or rewritten is no task's change.

#include "types.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace brulion::extensions {

/// Where a change comes from (an event's `source`).
enum class Origin {
    App, ///< this device's app: the editor, a capture, a journal opened for the first time
    Extension, ///< an extension on this device (NoteChange::by says which)
    Sync, ///< a sync brought it from another device
    Other, ///< another program, an import
};

/// "app", "extension", "sync" or "other".
QString originName(Origin origin);

/// One note's change as the hooks hear of it.
struct NoteChange
{
    VaultPath path;
    QString page; ///< its name (title)
    Origin origin = Origin::App;
    QString by; ///< the extension that wrote it (Origin::Extension), else ""
    int depth = 0; ///< 0: the user, a sync, another program; see the header
    bool created = false; ///< the note was not there before
    bool deleted = false;
    QString date; ///< a daily note's day "2026-11-14", else ""
};

/// A task of a note as taskStates() finds it.
struct TaskState
{
    int line = -1; ///< 0-based
    QString status; ///< its checkbox character (a Logseq keyword as one): " ", "x", "/", "-", ">"
    QString key; ///< what finds it again after an edit (its text without status and ✅ / ❌ dates)
    QString raw; ///< the line as written (for its ref)
    QString text; ///< as a block's `text`
    QString plain; ///< as a block's `plain`
};

/// The tasks of a note (`content` is its text, `note` the same text parsed), in the order of lines.
QList<TaskState> taskStates(const ParsedNote &note, const QString &content);

struct TaskChange
{
    TaskState before, after;
};

/// The tasks found in both lists whose status differs, in the order of `after`.
QList<TaskChange> changedTasks(const QList<TaskState> &before, const QList<TaskState> &after);

/// The changes of one burst and the events each extension's hooks get of them.
class ChangeBatch
{
public:
    static constexpr int kMaxDepth = 4; ///< hooks answering hooks, at most this many rounds
    static constexpr int kMaxTasks = 200; ///< task events of one burst (a sync of a year: no flood)

    /// Adds one note's change; false (nothing added, dropped() counts it) when it is deeper than
    /// kMaxDepth.
    bool add(const NoteChange &change);
    /// The tasks whose status the change of `note` changed (add() it too).
    void addTasks(const NoteChange &note, const QList<TaskChange> &tasks);
    bool isEmpty() const { return m_changes.isEmpty() && m_tasks.isEmpty(); }
    void clear();
    /// Changes left out: too deep (add()) or too many tasks.
    int dropped() const { return m_dropped; }
    /// The deepest change `extension`'s hooks hear: what they write is one deeper.
    int depthFor(const QString &extension) const;

    /// The "change" event of `extension`'s hooks — {pages, paths, by, sources}: the pages changed
    /// by anything but itself (a deleted one too), `by` the other extensions among the writers,
    /// `sources` the origins ("app", "extension", "sync", "other") —; {} when there is none for it.
    QVariantMap changeEvent(const QString &extension) const;
    /// The "task" events of `extension`'s hooks: a block map each (ref, page, path, line, text,
    /// plain, task, date?) with `previous` (the status before), `source` and `by`.
    QList<QVariantMap> taskEvents(const QString &extension) const;
    /// The "journal" events: {date, page, path, source, by} of every daily note made.
    QList<QVariantMap> journalEvents(const QString &extension) const;

private:
    struct Task
    {
        NoteChange note;
        TaskChange change;
    };
    QList<NoteChange> m_changes;
    QList<Task> m_tasks;
    int m_dropped = 0;
};

} // namespace brulion::extensions

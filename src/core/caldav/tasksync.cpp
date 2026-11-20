#include "caldav/tasksync_p.h"

#include "index/vaultindex.h"
#include "markdown/blockdocument.h"
#include "markdown/parse.h"
#include "markdown/tokens.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QUrl>

#include <algorithm>

namespace brulion::caldav::detail {

using namespace Qt::StringLiterals;

struct TaskReconciler::NoteEdit {
    enum class Kind { SetFields, MoveId, AssignId, InsertAfter, InsertUnder, Append, Remove };
    Kind kind = Kind::SetFields;
    QString blockId; ///< the block (SetFields, Remove, MoveId: the done line), the anchor (InsertAfter, InsertUnder)
    int line = -1;   ///< MoveId: the new occurrence's first line; AssignId: the block's first line
    unsigned mask = 0;
    TaskFields fields;
    QString body;  ///< inserts
    QString newId; ///< MoveId, AssignId
};

struct TaskReconciler::NoteInfo {
    QString path;
    bool exists = false;
    QString original;
    ParsedNote parsed;
    QList<NoteEdit> edits;
    bool written = false; ///< the edits are on disk (or there were none)
    bool failed = false;
    ParsedNote after; ///< the note after the edits
};

struct TaskReconciler::Located {
    NoteInfo *info = nullptr;
    int block = -1; ///< index into info->parsed.blocks
};

namespace {

/// Per UID: what to record once the notes are written (or not).
struct Pending {
    enum class Kind { Track, Drop } kind = Kind::Track;
    QString uid;
    QString blockId;
    QString note;
    QString path;
    std::optional<TaskFields> remote; ///< the new remote base (no remote write follows)
    bool remoteWrite = false;         ///< a RemoteOp sets the bases
};

int findBlock(const ParsedNote &note, const QString &id)
{
    const auto it = note.blockIds.constFind(id);
    if (it == note.blockIds.constEnd())
        return -1;
    return *it;
}

QString fileNameFor(const QString &uid)
{
    QString out;
    for (const QChar c : uid) {
        if (c.isLetterOrNumber() || c == u'-' || c == u'_' || c == u'.')
            out += c;
        else
            out += u'-';
    }
    if (out.isEmpty())
        out = u"task"_s;
    return out.left(120) + u".ics"_s;
}

bool isOpen(TaskStatus s)
{
    return s == TaskStatus::Todo || s == TaskStatus::Doing || s == TaskStatus::Deferred;
}

} // namespace

TaskReconciler::TaskReconciler(TaskSyncInput input, CalendarState &state) : m_in(std::move(input)), m_state(state) { }

TaskReconciler::~TaskReconciler()
{
    qDeleteAll(m_ownedNotes);
}

TaskReconciler::NoteInfo *TaskReconciler::note(const QString &path)
{
    if (path.isEmpty() || !m_in.vault)
        return nullptr;
    if (NoteInfo *known = m_notes.value(path))
        return known;
    auto *info = new NoteInfo;
    m_ownedNotes.append(info);
    info->path = path;
    bool ok = false;
    if (m_in.vault->exists(path)) {
        info->original = m_in.vault->readText(path, &ok);
        info->exists = ok;
    }
    info->parsed = markdown::parseNote(path, info->original, m_in.vault->config());
    m_notes.insert(path, info);
    return info;
}

std::optional<TaskReconciler::Located> TaskReconciler::locate(const TrackedTask &task)
{
    QStringList candidates{task.note};
    if (m_in.index) {
        if (const auto ref = m_in.index->blockById(task.blockId))
            candidates.append(ref->path);
    }
    candidates.append(m_in.entry.taskPage);
    for (const QString &path : std::as_const(candidates)) {
        NoteInfo *info = note(path);
        if (!info || !info->exists)
            continue;
        const int b = findBlock(info->parsed, task.blockId);
        if (b >= 0 && info->parsed.blocks.at(b).task)
            return Located{info, b};
    }
    return std::nullopt;
}

TaskFields TaskReconciler::fieldsOf(const NoteInfo &info, int block) const
{
    return fieldsFromBlock(info.parsed.blocks.at(block), m_in.vault->config().defaultReminderTime);
}

QString TaskReconciler::parentUidOf(const NoteInfo &info, int block) const
{
    const int parent = info.parsed.blocks.at(block).parent;
    if (parent < 0)
        return {};
    const Block &p = info.parsed.blocks.at(parent);
    if (!p.task || p.id.isEmpty())
        return {};
    return m_uidOfBlock.value(p.id);
}

TodoContext TaskReconciler::todoContext() const
{
    TodoContext ctx;
    ctx.now = m_in.now;
    ctx.zone = m_in.zone;
    return ctx;
}

QDateTime TaskReconciler::remoteTime(const QByteArray &ics) const
{
    const QList<io::ical::Component> roots = io::ical::parse(ics);
    const io::ical::Component *todo = firstTodo(roots);
    if (!todo)
        return {};
    for (const QStringView name : {u"LAST-MODIFIED", u"DTSTAMP"}) {
        if (const io::ical::Property *p = todo->property(name)) {
            if (const auto v = io::ical::parseDateTime(*p); v && v->time)
                return QDateTime(v->date, *v->time, v->utc ? QTimeZone(QTimeZone::UTC) : m_in.zone);
        }
    }
    return {};
}

QDateTime TaskReconciler::localTime(const QString &path) const
{
    if (const auto st = m_in.vault->stat(path))
        return QDateTime::fromMSecsSinceEpoch(st->mtime, QTimeZone::UTC);
    return {};
}

void TaskReconciler::run()
{
    if (!m_in.vault)
        return;
    const QTime defaultTime = m_in.vault->config().defaultReminderTime;
    struct Remote {
        QString path;
        QString etag;
        QByteArray data;
        TaskFields fields;
    };
    QHash<QString, Remote> remote;
    for (auto it = m_state.resources.constBegin(); it != m_state.resources.constEnd(); ++it) {
        if (it->kind != u"VTODO" || it->uid.isEmpty() || remote.contains(it->uid))
            continue;
        const QList<io::ical::Component> roots = io::ical::parse(it->data);
        const io::ical::Component *todo = firstTodo(roots);
        if (!todo)
            continue;
        ZoneTable zones;
        for (const io::ical::Component &root : roots)
            zones.addCalendar(root);
        remote.insert(it->uid, Remote{it.key(), it->etag, it->data, fieldsFromTodo(*todo, zones, m_in.zone, defaultTime)});
    }
    for (auto it = m_state.tasks.constBegin(); it != m_state.tasks.constEnd(); ++it)
        m_uidOfBlock.insert(it->blockId, it.key());

    const QString calendarPath = [this] {
        QString p = QUrl(m_in.entry.url).path(QUrl::FullyDecoded);
        if (!p.endsWith(u'/'))
            p += u'/';
        return p;
    }();
    NoteInfo *page = note(m_in.entry.taskPage);
    QList<Pending> pending;
    const TodoContext ctx = todoContext();

    // ---- where the tracked tasks are -----------------------------------------------------------
    QHash<QString, Located> localOf;
    QSet<QString> takenLines; // "path#line" of blocks this run already gave a role
    for (auto it = m_state.tasks.constBegin(); it != m_state.tasks.constEnd(); ++it) {
        const std::optional<Located> l = locate(*it);
        if (!l)
            continue;
        const Block &b = l->info->parsed.blocks.at(l->block);
        // A recurring task completed in Brulion: its next occurrence was written above it (SYNTAX
        // §5). The series lives on in that new line: the id moves there, the done line stays as
        // the note's record (not synced).
        if (b.task->status == TaskStatus::Done && !b.task->recurrence.isEmpty() && l->block > 0) {
            const int prevIndex = l->block - 1;
            const Block &prev = l->info->parsed.blocks.at(prevIndex);
            if (prev.parent == b.parent && prev.task && prev.id.isEmpty() && isOpen(prev.task->status)
                && prev.task->recurrence == b.task->recurrence && prev.endLine < b.line) {
                NoteEdit move;
                move.kind = NoteEdit::Kind::MoveId;
                move.blockId = it->blockId;
                move.line = prev.line;
                move.newId = it->blockId;
                l->info->edits.append(move);
                takenLines.insert(l->info->path + u'#' + QString::number(prev.line));
                localOf.insert(it.key(), Located{l->info, prevIndex});
                continue;
            }
        }
        localOf.insert(it.key(), *l);
    }

    // ---- lines of the page not tracked yet -------------------------------------------------------
    QHash<QString, Located> paired; // uid -> line carrying its block id (another device synced it)
    QList<int> newLocal;
    if (page && page->exists) {
        for (int i = 0; i < page->parsed.blocks.size(); ++i) {
            const Block &b = page->parsed.blocks.at(i);
            if (!b.task || takenLines.contains(page->path + u'#' + QString::number(b.line)))
                continue;
            if (!b.id.isEmpty() && (m_uidOfBlock.contains(b.id) || m_in.claimedIds.contains(b.id)))
                continue;
            if (!b.id.isEmpty()) {
                bool found = false;
                for (auto r = remote.constBegin(); r != remote.constEnd(); ++r) {
                    if (!m_state.tasks.contains(r.key()) && !paired.contains(r.key()) && blockIdForUid(r.key()) == b.id) {
                        paired.insert(r.key(), Located{page, i});
                        found = true;
                        break;
                    }
                }
                if (found)
                    continue;
            }
            if (isOpen(b.task->status))
                newLocal.append(i);
        }
    }
    // Lines elsewhere carrying the id of a server task this device does not track yet.
    if (m_in.index) {
        for (auto r = remote.constBegin(); r != remote.constEnd(); ++r) {
            if (m_state.tasks.contains(r.key()) || paired.contains(r.key()))
                continue;
            const QString id = blockIdForUid(r.key());
            if (const auto ref = m_in.index->blockById(id)) {
                NoteInfo *info = note(ref->path);
                const int b = info ? findBlock(info->parsed, id) : -1;
                if (b >= 0 && info->parsed.blocks.at(b).task)
                    paired.insert(r.key(), Located{info, b});
            }
        }
    }

    // ---- the mass-deletion guard -------------------------------------------------------------------
    int localRemovals = 0;
    int remoteDeletes = 0;
    QStringList deletionNames;
    for (auto it = m_state.tasks.constBegin(); it != m_state.tasks.constEnd(); ++it) {
        const bool hasRemote = remote.contains(it.key());
        const bool hasLocal = localOf.contains(it.key());
        if (hasRemote && !hasLocal && !changedFields(it->remote, remote.value(it.key()).fields)) {
            ++remoteDeletes;
            deletionNames.append(it->remote.summary);
        } else if (!hasRemote && hasLocal) {
            const Located &l = localOf[it.key()];
            if (!changedFields(it->local, fieldsOf(*l.info, l.block))) {
                ++localRemovals;
                deletionNames.append(it->local.summary);
            }
        }
    }
    const int tracked = int(m_state.tasks.size());
    const bool holdDeletions =
            !m_in.confirmDeletions && (localRemovals + remoteDeletes) > std::max(5, tracked / 2);
    if (holdDeletions) {
        m_report.needsConfirmation = true;
        deletionNames.sort(Qt::CaseInsensitive);
        m_report.pendingDeletions = deletionNames;
    }

    // ---- tracked tasks -------------------------------------------------------------------------------
    const auto setFields = [](NoteInfo *info, const QString &blockId, unsigned mask, const TaskFields &fields) {
        NoteEdit e;
        e.kind = NoteEdit::Kind::SetFields;
        e.blockId = blockId;
        e.mask = mask;
        e.fields = fields;
        info->edits.append(e);
    };
    const QStringList trackedUids = m_state.tasks.keys();
    for (const QString &uid : trackedUids) {
        const TrackedTask base = m_state.tasks.value(uid);
        const bool hasRemote = remote.contains(uid);
        const auto lit = localOf.constFind(uid);
        const bool hasLocal = lit != localOf.constEnd();
        if (hasRemote && hasLocal) {
            const Remote &r = remote[uid];
            const Located l = *lit;
            const TaskFields L = fieldsOf(*l.info, l.block);
            const unsigned rc = changedFields(base.remote, r.fields);
            const unsigned lc = changedFields(base.local, L);
            const bool moved = l.info->path != base.note;
            if (!rc && !lc) {
                if (moved || !l.info->edits.isEmpty())
                    pending.append({Pending::Kind::Track, uid, base.blockId, l.info->path, r.path, r.fields, false});
                continue;
            }
            const unsigned differ = changedFields(L, r.fields);
            const unsigned conflict = lc & rc & differ;
            TaskFields target = withFields(L, r.fields, rc & ~lc);
            bool keepBoth = false;
            if (conflict) {
                const QString summary = L.summary.isEmpty() ? r.fields.summary : L.summary;
                if (m_in.policy == ConflictPolicy::KeepBoth) {
                    keepBoth = true;
                } else {
                    const QDateTime rt = remoteTime(r.data);
                    const QDateTime lt = localTime(l.info->path);
                    if (rt.isValid() && (!lt.isValid() || rt > lt))
                        target = withFields(target, r.fields, conflict);
                }
                m_report.conflicts.append(summary);
            } else if (lc && rc) {
                ++m_report.merged;
            }
            const unsigned pull = changedFields(L, target);
            if (pull) {
                setFields(l.info, base.blockId, pull, target);
                ++m_report.downloaded;
            }
            const unsigned push = changedFields(r.fields, target);
            if (push) {
                RemoteOp op;
                op.kind = RemoteOp::Kind::Put;
                op.uid = uid;
                op.path = r.path;
                op.ifMatch = r.etag;
                op.data = patchTodo(r.data, target, push, ctx);
                op.blockId = base.blockId;
                op.note = l.info->path;
                op.local = target;
                if (!op.data.isEmpty())
                    m_ops.append(op);
            }
            pending.append({Pending::Kind::Track, uid, base.blockId, l.info->path, r.path,
                            push ? std::nullopt : std::optional(r.fields), push != 0});
            if (keepBoth) {
                // The server's version as a task of its own, right after the line.
                const QString id = newTaskBlockId();
                NoteEdit insert;
                insert.kind = NoteEdit::Kind::InsertAfter;
                insert.blockId = base.blockId;
                TaskFields copy = r.fields;
                copy.parentUid.clear();
                insert.body = newTaskBody(copy, id);
                l.info->edits.append(insert);
                RemoteOp create;
                create.kind = RemoteOp::Kind::Create;
                create.uid = uidForBlockId(id);
                create.path = calendarPath + fileNameFor(create.uid);
                create.data = newTodo(create.uid, copy, ctx);
                create.blockId = id;
                create.note = l.info->path;
                create.local = copy;
                m_ops.append(create);
            }
        } else if (hasRemote) {
            const Remote &r = remote[uid];
            if (changedFields(base.remote, r.fields)) {
                // Changed on the server after the line was deleted here: the change wins, the line
                // comes back.
                if (!page)
                    continue;
                NoteEdit add;
                add.kind = NoteEdit::Kind::Append;
                TaskFields fields = r.fields;
                fields.parentUid.clear();
                add.body = newTaskBody(fields, base.blockId);
                page->edits.append(add);
                pending.append({Pending::Kind::Track, uid, base.blockId, page->path, r.path, r.fields, false});
                m_report.restored.append(r.fields.summary);
                ++m_report.downloaded;
            } else if (!holdDeletions) {
                RemoteOp del;
                del.kind = RemoteOp::Kind::Delete;
                del.uid = uid;
                del.path = r.path;
                del.ifMatch = r.etag;
                del.blockId = base.blockId;
                m_ops.append(del);
            }
        } else if (hasLocal) {
            const Located l = *lit;
            const TaskFields L = fieldsOf(*l.info, l.block);
            if (changedFields(base.local, L)) {
                // Changed here after it was deleted on the server: written there again.
                RemoteOp create;
                create.kind = RemoteOp::Kind::Create;
                create.uid = uid;
                create.path = base.path.isEmpty() ? calendarPath + fileNameFor(uid) : base.path;
                create.data = newTodo(uid, L, ctx);
                create.blockId = base.blockId;
                create.note = l.info->path;
                create.local = L;
                m_ops.append(create);
                pending.append({Pending::Kind::Track, uid, base.blockId, l.info->path, create.path, std::nullopt, true});
                m_report.restored.append(L.summary);
            } else if (!holdDeletions) {
                NoteEdit remove;
                remove.kind = NoteEdit::Kind::Remove;
                remove.blockId = base.blockId;
                l.info->edits.append(remove);
                pending.append({Pending::Kind::Drop, uid, base.blockId, l.info->path, {}, std::nullopt, false});
                ++m_report.deletedLocal;
            }
        } else {
            m_state.tasks.remove(uid); // gone on both sides
        }
    }

    // ---- lines paired with server tasks this device had not tracked ------------------------------
    for (auto it = paired.constBegin(); it != paired.constEnd(); ++it) {
        const Remote &r = remote[it.key()];
        const Located l = *it;
        const TaskFields L = fieldsOf(*l.info, l.block);
        const QString id = l.info->parsed.blocks.at(l.block).id;
        const unsigned differ = changedFields(L, r.fields);
        bool remoteWins = true;
        if (differ) {
            const QDateTime rt = remoteTime(r.data);
            const QDateTime lt = localTime(l.info->path);
            remoteWins = !lt.isValid() || (rt.isValid() && rt >= lt);
        }
        if (differ && remoteWins) {
            setFields(l.info, id, differ, r.fields);
            ++m_report.downloaded;
            pending.append({Pending::Kind::Track, it.key(), id, l.info->path, r.path, r.fields, false});
        } else if (differ) {
            RemoteOp op;
            op.kind = RemoteOp::Kind::Put;
            op.uid = it.key();
            op.path = r.path;
            op.ifMatch = r.etag;
            op.data = patchTodo(r.data, L, differ, ctx);
            op.blockId = id;
            op.note = l.info->path;
            op.local = L;
            if (!op.data.isEmpty())
                m_ops.append(op);
            pending.append({Pending::Kind::Track, it.key(), id, l.info->path, r.path, std::nullopt, true});
        } else {
            m_state.tasks.insert(it.key(), TrackedTask{r.path, id, l.info->path, L, r.fields});
        }
        m_uidOfBlock.insert(id, it.key());
    }

    // ---- new on the server ------------------------------------------------------------------------
    QList<QString> newRemote;
    for (auto r = remote.constBegin(); r != remote.constEnd(); ++r) {
        if (!m_state.tasks.contains(r.key()) && !paired.contains(r.key()))
            newRemote.append(r.key());
    }
    // Parents before their subtasks, then by due date and title.
    std::ranges::sort(newRemote, [&remote](const QString &a, const QString &b) {
        const TaskFields &fa = remote[a].fields;
        const TaskFields &fb = remote[b].fields;
        if (fa.due != fb.due) {
            if (!fa.due || !fb.due)
                return fa.due.has_value();
            return fa.due->toLocalDateTime() < fb.due->toLocalDateTime();
        }
        return fa.summary.compare(fb.summary, Qt::CaseInsensitive) < 0;
    });
    QHash<QString, std::pair<NoteInfo *, QString>> placed; // uid -> (note, block id) of lines added now
    QList<QString> ordered;
    {
        QSet<QString> done;
        std::function<void(const QString &, int)> visit = [&](const QString &uid, int depth) {
            if (done.contains(uid) || depth > 16)
                return;
            const QString parent = remote[uid].fields.parentUid;
            if (!parent.isEmpty() && newRemote.contains(parent))
                visit(parent, depth + 1);
            if (done.contains(uid))
                return;
            done.insert(uid);
            ordered.append(uid);
        };
        for (const QString &uid : std::as_const(newRemote))
            visit(uid, 0);
    }
    for (const QString &uid : std::as_const(ordered)) {
        const Remote &r = remote[uid];
        const QString id = blockIdForUid(uid);
        TaskFields fields = r.fields;
        NoteEdit add;
        add.body = newTaskBody(fields, id);
        NoteInfo *target = page;
        const QString parent = fields.parentUid;
        if (!parent.isEmpty()) {
            if (placed.contains(parent)) {
                target = placed[parent].first;
                add.kind = NoteEdit::Kind::InsertUnder;
                add.blockId = placed[parent].second;
            } else if (m_state.tasks.contains(parent)) {
                const TrackedTask &pt = m_state.tasks[parent];
                if (const auto pl = localOf.constFind(parent); pl != localOf.constEnd() && pl->info->parsed.blocks.at(pl->block).id == pt.blockId) {
                    target = pl->info;
                    add.kind = NoteEdit::Kind::InsertUnder;
                    add.blockId = pt.blockId;
                }
            }
        }
        if (add.blockId.isEmpty())
            add.kind = NoteEdit::Kind::Append;
        if (!target)
            continue;
        target->edits.append(add);
        placed.insert(uid, {target, id});
        m_uidOfBlock.insert(id, uid);
        pending.append({Pending::Kind::Track, uid, id, target->path, r.path, r.fields, false});
        ++m_report.downloaded;
    }

    // ---- new here -----------------------------------------------------------------------------------
    if (page && !m_in.entry.readOnly) {
        QHash<int, QString> newUidOfBlock; // block index -> UID of the lines sent now
        for (int i : std::as_const(newLocal)) {
            const Block &b = page->parsed.blocks.at(i);
            QString id = b.id;
            if (id.isEmpty()) {
                id = newTaskBlockId();
                NoteEdit assign;
                assign.kind = NoteEdit::Kind::AssignId;
                assign.line = b.line;
                assign.newId = id;
                page->edits.append(assign);
            }
            const QString uid = uidForBlockId(id);
            TaskFields fields = fieldsOf(*page, i);
            fields.parentUid = parentUidOf(*page, i);
            if (fields.parentUid.isEmpty() && b.parent >= 0)
                fields.parentUid = newUidOfBlock.value(b.parent);
            newUidOfBlock.insert(i, uid);
            RemoteOp create;
            create.kind = RemoteOp::Kind::Create;
            create.uid = uid;
            create.path = calendarPath + fileNameFor(uid);
            create.data = newTodo(uid, fields, ctx);
            create.blockId = id;
            create.note = page->path;
            create.local = fields;
            m_ops.append(create);
            m_uidOfBlock.insert(id, uid);
        }
    }

    // ---- the notes ----------------------------------------------------------------------------------
    writeNotes();
    for (const Pending &p : std::as_const(pending)) {
        NoteInfo *info = m_notes.value(p.note);
        if (!info || info->failed)
            continue;
        if (p.kind == Pending::Kind::Drop) {
            m_state.tasks.remove(p.uid);
            continue;
        }
        const int b = findBlock(info->after, p.blockId);
        if (b < 0 || !info->after.blocks.at(b).task)
            continue;
        const TaskFields now = fieldsFromBlock(info->after.blocks.at(b), defaultTime);
        TrackedTask t = m_state.tasks.value(p.uid);
        t.blockId = p.blockId;
        t.note = p.note;
        t.path = p.path;
        if (!p.remoteWrite) {
            t.local = now;
            if (p.remote)
                t.remote = *p.remote;
        } else if (!m_state.tasks.contains(p.uid)) {
            continue; // created when the server has it
        }
        m_state.tasks.insert(p.uid, t);
    }
    // Remote writes that wait for a line written here.
    for (RemoteOp &op : m_ops) {
        NoteInfo *info = m_notes.value(op.note);
        if (op.kind == RemoteOp::Kind::Delete || !info)
            continue;
        if (info->failed) {
            op.data.clear(); // the line is not as planned: next run
            continue;
        }
        const int b = findBlock(info->after, op.blockId);
        if (b >= 0 && info->after.blocks.at(b).task) {
            TaskFields fields = fieldsFromBlock(info->after.blocks.at(b), defaultTime);
            fields.parentUid = op.local.parentUid;
            op.local = fields;
        } else if (op.kind == RemoteOp::Kind::Create) {
            op.data.clear();
        }
    }
    m_ops.removeIf([](const RemoteOp &op) { return op.kind != RemoteOp::Kind::Delete && op.data.isEmpty(); });
}

void TaskReconciler::writeNotes()
{
    for (NoteInfo *info : std::as_const(m_ownedNotes)) {
        if (info->edits.isEmpty()) {
            info->written = true;
            info->after = info->parsed;
            continue;
        }
        markdown::BlockDocument doc(info->original);
        const QDate today = m_in.now.toTimeZone(m_in.zone.isValid() ? m_in.zone : QTimeZone(QTimeZone::LocalTime)).date();
        // Targets as block uids first: edits change line numbers and indices.
        const auto uidOfId = [&doc](const QString &id) -> quint64 {
            for (int i = 0; i < doc.count(); ++i) {
                if (doc.at(i).blockId == id)
                    return doc.at(i).uid;
            }
            return 0;
        };
        struct Resolved {
            const NoteEdit *edit;
            quint64 target;
            quint64 second; ///< MoveId: the done line
        };
        QList<Resolved> list;
        for (const NoteEdit &e : std::as_const(info->edits)) {
            Resolved r{&e, 0, 0};
            switch (e.kind) {
            case NoteEdit::Kind::MoveId:
                r.second = uidOfId(e.blockId);
                [[fallthrough]];
            case NoteEdit::Kind::AssignId: {
                const int i = doc.indexAtLine(e.line);
                r.target = i >= 0 ? doc.at(i).uid : 0;
                break;
            }
            case NoteEdit::Kind::Append: break;
            default: r.target = uidOfId(e.blockId); break;
            }
            list.append(r);
        }
        // Bodies first, then insertions, then removals.
        for (const Resolved &r : std::as_const(list)) {
            const NoteEdit &e = *r.edit;
            const int i = r.target ? doc.indexOfUid(r.target) : -1;
            if (e.kind == NoteEdit::Kind::SetFields) {
                if (i < 0)
                    continue;
                doc.setBody(i, applyFieldsToBody(doc.at(i).body, e.fields, e.mask, today));
            } else if (e.kind == NoteEdit::Kind::AssignId || e.kind == NoteEdit::Kind::MoveId) {
                if (i < 0)
                    continue;
                QString body = doc.at(i).body;
                const qsizetype nl = body.indexOf(u'\n');
                QString first = nl < 0 ? body : body.first(nl);
                const QString rest = nl < 0 ? QString() : body.sliced(nl);
                while (!first.isEmpty() && first.back().isSpace())
                    first.chop(1);
                doc.setBody(i, first + u" ^"_s + e.newId + rest);
                if (e.kind == NoteEdit::Kind::MoveId) {
                    const int done = r.second ? doc.indexOfUid(r.second) : -1;
                    if (done >= 0) {
                        QString doneBody = doc.at(done).body;
                        const qsizetype dnl = doneBody.indexOf(u'\n');
                        QString dfirst = dnl < 0 ? doneBody : doneBody.first(dnl);
                        const QString drest = dnl < 0 ? QString() : doneBody.sliced(dnl);
                        if (const auto id = markdown::blockIdAtEnd(dfirst))
                            dfirst = dfirst.first(id->from);
                        while (!dfirst.isEmpty() && dfirst.back().isSpace())
                            dfirst.chop(1);
                        doc.setBody(done, dfirst + drest);
                    }
                }
            }
        }
        for (const Resolved &r : std::as_const(list)) {
            const NoteEdit &e = *r.edit;
            if (e.kind == NoteEdit::Kind::Append) {
                if (doc.count() == 0)
                    doc.insertAfter(-1, e.body, 0);
                else
                    doc.insertAfter(doc.count() - 1, e.body, 0);
            } else if (e.kind == NoteEdit::Kind::InsertAfter || e.kind == NoteEdit::Kind::InsertUnder) {
                // Found now: the anchor may be a line added a moment ago (a parent and its subtask).
                const quint64 anchor = uidOfId(e.blockId);
                const int i = anchor ? doc.indexOfUid(anchor) : -1;
                if (i < 0) {
                    doc.insertAfter(doc.count() - 1, e.body, 0);
                    continue;
                }
                const int depth = doc.at(i).depth + (e.kind == NoteEdit::Kind::InsertUnder ? 1 : 0);
                doc.insertAfter(doc.subtreeRange(i).last(), e.body, depth);
            }
        }
        for (const Resolved &r : std::as_const(list)) {
            if (r.edit->kind != NoteEdit::Kind::Remove)
                continue;
            const int i = r.target ? doc.indexOfUid(r.target) : -1;
            if (i >= 0)
                doc.remove(i, false);
        }
        const QString text = doc.toString();
        if (text == info->original) {
            info->written = true;
            info->after = info->parsed;
            continue;
        }
        if (!info->exists) {
            const QString folder = paths::dirname(info->path);
            if (!folder.isEmpty())
                m_in.vault->createFolder(folder);
        }
        const WriteOutcome outcome = m_in.vault->writeTextIfUnchanged(info->path, text, info->original, ChangeSource::Sync);
        if (outcome != WriteOutcome::Written) {
            info->failed = true;
            m_report.errors.append(outcome == WriteOutcome::Changed ? u"%1 changed while it was synced"_s.arg(info->path)
                                                                     : u"%1 could not be written"_s.arg(info->path));
            continue;
        }
        info->written = true;
        info->after = markdown::parseNote(info->path, text, m_in.vault->config());
        m_report.changedNotes.append(info->path);
    }
}

void TaskReconciler::remoteDone(const RemoteOp &op, const QString &etag, const QString &path, bool ok, int status)
{
    if (!ok) {
        if (status != 412)
            m_report.errors.append(u"%1 %2: HTTP %3"_s.arg(op.kind == RemoteOp::Kind::Delete ? u"DELETE"_s : u"PUT"_s, op.path)
                                           .arg(status));
        return;
    }
    if (op.kind == RemoteOp::Kind::Delete) {
        m_state.resources.remove(op.path);
        m_state.tasks.remove(op.uid);
        return;
    }
    const QString where = path.isEmpty() ? op.path : path;
    if (where != op.path)
        m_state.resources.remove(op.path);
    m_state.resources.insert(where, Resource{etag, op.uid, u"VTODO"_s, op.data});
    const QList<io::ical::Component> roots = io::ical::parse(op.data);
    TaskFields remoteFields;
    if (const io::ical::Component *todo = firstTodo(roots)) {
        ZoneTable zones;
        for (const io::ical::Component &root : roots)
            zones.addCalendar(root);
        remoteFields = fieldsFromTodo(*todo, zones, m_in.zone,
                                      m_in.vault ? m_in.vault->config().defaultReminderTime : QTime(9, 0));
    }
    TrackedTask t = m_state.tasks.value(op.uid);
    t.path = where;
    t.blockId = op.blockId;
    t.note = op.note;
    t.local = op.local;
    t.remote = remoteFields;
    m_state.tasks.insert(op.uid, t);
}

} // namespace brulion::caldav::detail

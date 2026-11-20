#include "extensions/events.h"

#include "extensions/blockedit.h"
#include "markdown/tasks.h"
#include "notebook/itemref.h"
#include "text/text.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

QString originName(Origin origin)
{
    switch (origin) {
    case Origin::App: return u"app"_s;
    case Origin::Extension: return u"extension"_s;
    case Origin::Sync: return u"sync"_s;
    case Origin::Other: return u"other"_s;
    }
    return u"other"_s;
}

namespace {

// A task's text without what ticking it changes: the done and cancelled dates (with a time).
QString taskKey(const QString &text)
{
    static const QRegularExpression stamps(u"\\s*[✅❌]\\x{FE0F}?\\s*\\d{4}-\\d{2}-\\d{2}(?:[ T]\\d{1,2}:\\d{2})?"_s);
    QString key = text;
    key.remove(stamps);
    return key.simplified();
}

} // namespace

QList<TaskState> taskStates(const ParsedNote &note, const QString &content)
{
    QList<TaskState> out;
    const QList<text::Line> lines = text::splitLines(content);
    for (const Block &block : note.blocks) {
        if (!block.task || block.line < 0 || block.line >= lines.size())
            continue;
        TaskState t;
        t.line = block.line;
        t.raw = lines[block.line].text;
        const std::optional<QString> c = taskCharacter(t.raw);
        t.status = c ? *c : QString(markdown::statusChar(block.task->status));
        t.text = blockLineText(t.raw);
        t.plain = plainWords(block.task->text);
        t.key = taskKey(t.text);
        out << t;
    }
    std::ranges::sort(out, {}, &TaskState::line);
    return out;
}

QList<TaskChange> changedTasks(const QList<TaskState> &before, const QList<TaskState> &after)
{
    // The n-th task of a key before is the n-th of that key after.
    QHash<QString, QList<const TaskState *>> earlier;
    for (const TaskState &t : before)
        earlier[t.key] << &t;
    QHash<QString, int> seen;
    QList<TaskChange> out;
    for (const TaskState &t : after) {
        const int n = seen[t.key]++;
        const auto it = earlier.constFind(t.key);
        if (it == earlier.constEnd() || n >= it->size())
            continue;
        const TaskState *old = it->at(n);
        if (old->status != t.status)
            out.append({*old, t});
    }
    return out;
}

bool ChangeBatch::add(const NoteChange &change)
{
    if (change.depth > kMaxDepth) {
        ++m_dropped;
        return false;
    }
    m_changes << change;
    return true;
}

void ChangeBatch::addTasks(const NoteChange &note, const QList<TaskChange> &tasks)
{
    if (note.depth > kMaxDepth)
        return;
    for (const TaskChange &t : tasks) {
        if (m_tasks.size() >= kMaxTasks) {
            ++m_dropped;
            continue;
        }
        m_tasks.append({note, t});
    }
}

void ChangeBatch::clear()
{
    m_changes.clear();
    m_tasks.clear();
    m_dropped = 0;
}

int ChangeBatch::depthFor(const QString &extension) const
{
    int depth = 0;
    for (const NoteChange &c : m_changes)
        if (c.by != extension || extension.isEmpty())
            depth = std::max(depth, c.depth);
    for (const Task &t : m_tasks)
        if (t.note.by != extension || extension.isEmpty())
            depth = std::max(depth, t.note.depth);
    return depth;
}

QVariantMap ChangeBatch::changeEvent(const QString &extension) const
{
    QStringList pages, paths, by, sources;
    for (const NoteChange &c : m_changes) {
        if (!extension.isEmpty() && c.by == extension)
            continue; // its own write: no echo
        if (!paths.contains(c.path)) {
            paths << c.path;
            if (!pages.contains(c.page))
                pages << c.page;
        }
        if (!c.by.isEmpty() && !by.contains(c.by))
            by << c.by;
        if (const QString source = originName(c.origin); !sources.contains(source))
            sources << source;
    }
    if (paths.isEmpty())
        return {};
    by.sort();
    return {{u"pages"_s, pages}, {u"paths"_s, paths}, {u"by"_s, by}, {u"sources"_s, sources}};
}

QList<QVariantMap> ChangeBatch::taskEvents(const QString &extension) const
{
    QList<QVariantMap> out;
    for (const Task &t : m_tasks) {
        if (!extension.isEmpty() && t.note.by == extension)
            continue;
        const TaskState &now = t.change.after;
        QVariantMap e{{u"ref"_s, notebook::formatLineRef(t.note.path, now.line, now.raw)},
                      {u"page"_s, t.note.page},
                      {u"path"_s, t.note.path},
                      {u"line"_s, now.line + 1},
                      {u"text"_s, now.text},
                      {u"plain"_s, now.plain},
                      {u"task"_s, now.status},
                      {u"previous"_s, t.change.before.status},
                      {u"source"_s, originName(t.note.origin)},
                      {u"by"_s, t.note.by}};
        if (!t.note.date.isEmpty())
            e.insert(u"date"_s, t.note.date);
        out << e;
    }
    return out;
}

QList<QVariantMap> ChangeBatch::journalEvents(const QString &extension) const
{
    QList<QVariantMap> out;
    QSet<QString> seen;
    for (const NoteChange &c : m_changes) {
        if (!c.created || c.deleted || c.date.isEmpty() || (!extension.isEmpty() && c.by == extension))
            continue;
        if (seen.contains(c.path))
            continue;
        seen.insert(c.path);
        out << QVariantMap{{u"date"_s, c.date},
                           {u"page"_s, c.page},
                           {u"path"_s, c.path},
                           {u"source"_s, originName(c.origin)},
                           {u"by"_s, c.by}};
    }
    return out;
}

} // namespace brulion::extensions

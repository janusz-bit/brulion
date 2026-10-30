// compareIndexes(): everything a VaultIndex derives (paths, links both ways, unlinked references,
// missing pages, tags, tasks, reminders, journals, notes with queries, page properties, block ids,
// link resolution, the graph, full-text search) compared between two indexes that hold the same
// files — the oracle of the
// index's incremental maintenance (tst_index_consistency) and of loading it on worker threads or
// from the index cache (tst_notebook_indexloader, tst_index_cache). Header-only, for tests.
#pragma once

#include "index/index.h"

#include <QStringList>
#include <QTest>

namespace brulion::testing {

using namespace Qt::StringLiterals;

inline QStringList describeBacklinks(const QList<Backlink> &links)
{
    QStringList out;
    for (const Backlink &b : links)
        out.append(u"%1@%2#%3"_s.arg(b.path).arg(b.link.from).arg(b.blockIndex));
    return out;
}

inline QStringList describeOutgoing(const QList<ResolvedLink> &links)
{
    QStringList out;
    for (const ResolvedLink &l : links)
        out.append(u"%1->%2"_s.arg(l.link.from).arg(l.target));
    return out;
}

inline QStringList describeTags(const QList<TagCount> &tags)
{
    QStringList out;
    for (const TagCount &t : tags)
        out.append(u"%1(%2)=%3:%4"_s.arg(t.tag, t.display).arg(t.count).arg(t.paths.join(u',')));
    return out;
}

inline QStringList describeMissing(const QList<MissingPage> &missing)
{
    QStringList out;
    for (const MissingPage &m : missing)
        out.append(m.name + u':' + m.sources.join(u','));
    return out;
}

inline QStringList describeGraph(const GraphData &g)
{
    QStringList out;
    for (const GraphNode &n : g.nodes)
        out.append(u"%1|%2|%3|%4%5"_s.arg(n.id, n.title).arg(n.degree).arg(int(n.isTag)).arg(int(n.isMissing)));
    for (const GraphEdge &e : g.edges)
        out.append(e.source + u"->"_s + e.target);
    out.sort();
    return out;
}

inline QString describeTask(const Task &t)
{
    return u"%1|%2|%3|%4|%5|%6"_s.arg(QString::number(t.line), QString::number(int(t.status)),
                                      QString::number(int(t.priority)), t.text, t.due ? t.due->toString() : QString(),
                                      t.recurrence);
}

inline QStringList describeTasks(const QList<TaskRef> &tasks)
{
    QStringList out;
    for (const TaskRef &t : tasks)
        out.append(t.path + u'#' + QString::number(t.blockIndex) + u':' + describeTask(t.task));
    return out;
}

inline QStringList describeReminders(const QList<ReminderRef> &reminders)
{
    QStringList out;
    for (const ReminderRef &r : reminders) {
        out.append(u"%1#%2:%3|%4|%5|%6|%7"_s.arg(r.path, QString::number(r.blockIndex), r.reminder.at.toString(),
                                                 QString::number(int(r.reminder.alarm)), r.reminder.text,
                                                 r.reminder.recurrence, r.task ? describeTask(*r.task) : u"-"_s));
    }
    return out;
}

inline QStringList describePropertyKeys(const QList<PropertyKeyInfo> &keys)
{
    QStringList out;
    for (const PropertyKeyInfo &k : keys)
        out.append(u"%1(%2):%3"_s.arg(k.key, k.type).arg(k.count));
    return out;
}

inline QStringList describePropertyValues(const QList<PropertyValueCount> &values)
{
    QStringList out;
    for (const PropertyValueCount &v : values)
        out.append(u"%1:%2"_s.arg(v.value).arg(v.count));
    return out;
}

inline QStringList describeSearch(const QList<SearchHit> &hits)
{
    QStringList out;
    for (const SearchHit &h : hits)
        out.append(u"%1:%2:%3"_s.arg(h.path).arg(h.line).arg(h.excerpt));
    return out;
}

/// Compare everything derived by `a` and `b` (which must hold the same files).
inline void compareIndexes(const VaultIndex &a, const VaultIndex &b, const QString &step)
{
    const QByteArray ctx = step.toUtf8();
    QVERIFY2(a.paths() == b.paths(), ctx.constData());
    QVERIFY2(a.attachments() == b.attachments(), ctx.constData());
    const QList<VaultPath> all = a.paths() + a.attachments();
    for (const VaultPath &p : all) {
        QCOMPARE(describeOutgoing(a.outgoingLinks(p)), describeOutgoing(b.outgoingLinks(p)));
        QCOMPARE(describeBacklinks(a.backlinks(p)), describeBacklinks(b.backlinks(p)));
        QCOMPARE(a.linksTo(p), b.linksTo(p));
        QCOMPARE(a.unlinkedReferences(p).size(), b.unlinkedReferences(p).size());
    }
    QCOMPARE(describeMissing(a.missingPages()), describeMissing(b.missingPages()));
    QCOMPARE(describeTags(a.tags()), describeTags(b.tags()));
    QCOMPARE(describeTasks(a.tasks()), describeTasks(b.tasks()));
    QCOMPARE(describeReminders(a.reminders()), describeReminders(b.reminders()));
    QCOMPARE(a.journals(), b.journals());
    QCOMPARE(a.notesWithQueries(), b.notesWithQueries());
    QCOMPARE(describePropertyKeys(a.propertyKeys()), describePropertyKeys(b.propertyKeys()));
    for (const PropertyKeyInfo &k : a.propertyKeys())
        QCOMPARE(describePropertyValues(a.propertyValues(k.key, {}, 100)),
                 describePropertyValues(b.propertyValues(k.key, {}, 100)));
    for (const QString &id : {u"id1"_s, u"id2"_s}) {
        const std::optional<BlockRef> ra = a.blockById(id);
        const std::optional<BlockRef> rb = b.blockById(id);
        QCOMPARE(ra.has_value(), rb.has_value());
        if (ra)
            QCOMPARE(*ra, *rb);
    }
    for (const QString &t : {u"A"_s, u"a"_s, u"b"_s, u"C"_s, u"D"_s, u"E"_s, u"Ż"_s, u"img.png"_s, u"Brak"_s, u"x/A"_s,
                             u"sub/A"_s, u"W"_s, u"q"_s, u"c"_s})
        QCOMPARE(a.resolveLink(t, u"x/Q.md"_s), b.resolveLink(t, u"x/Q.md"_s));
    QCOMPARE(describeGraph(a.graph(true, true)), describeGraph(b.graph(true, true)));
    for (const QString &q : {u"zolw"_s, u"zadanie"_s, u"tekst inne"_s, u"nagl"_s})
        QCOMPARE(describeSearch(a.search(q)), describeSearch(b.search(q)));
}

/// The note paths of suggestPages("") (recently updated first): the update order.
inline QStringList describeRecency(const VaultIndex &index)
{
    QStringList out;
    for (const PageSuggestion &s : index.suggestPages(QString(), 100000))
        out.append(s.path);
    return out;
}

} // namespace brulion::testing

// Incremental maintenance: after every update / remove / rename of a deterministic random
// sequence, everything VaultIndex derives must equal a fresh index built from the same files —
// also when notes are prepared on other threads (VaultIndex::prepare() + updatePrepared(), the
// index loader's path) and when a full-text index built elsewhere is adopted (adoptSearch()).
// The change log (changesSince()) names every note whose tasks, reminders, links or shown block
// texts changed, and a QueryCache kept across the sequence equals runQuery() at every step.
#include "index/index.h"
#include "index/querycache.h"
#include "indexcompare.h"
#include "markdown/parse.h"

#include <QRandomGenerator>
#include <QTest>

#include <thread>
#include <vector>

using namespace brulion;
using namespace brulion::testing;
using namespace Qt::StringLiterals;

namespace {

const QStringList Paths = {u"A.md"_s, u"B.md"_s,  u"x/A.md"_s,  u"x/C.md"_s,    u"y/z/B.md"_s,
                           u"Ż.md"_s, u"C.md"_s,  u"img.png"_s, u"x/img.png"_s, u"journals/2026-10-14.md"_s,
                           u"D.md"_s, u"y/Ż.md"_s};

const QStringList Lines = {
        u"- [[A]]"_s,
        u"- [[b#Nagłówek]]"_s,
        u"- [[C|ce]] i [[x/A]]"_s,
        u"- [[Ż]] oraz [[ż]]"_s,
        u"- ![[img.png]]"_s,
        u"- ((id1)) i !((id2))"_s,
        u"- tekst ^id1"_s,
        u"- inne ^id2"_s,
        u"- #tag1 i #Tag2/sub"_s,
        u"- [ ] zadanie 📅 2026-10-15 #tag1"_s,
        u"- przypomnienie ⏰ 2026-10-16 10:00"_s,
        u"- [link](../A.md) i [b](B.md)"_s,
        u"- [[D]] [[E]] [[Brak]]"_s,
        u"- samo [[#Nagłówek]] i [[#^id1]]"_s,
        u"Akapit o żółwiu i A oraz D."_s,
};

const QStringList Headers = {
        QString(),
        u"---\naliases: [E]\n---\n"_s,
        u"---\naliases: [A, Brak]\ntags: [Strona]\n---\n"_s,
        u"alias:: D\ntags:: logseq\n\n"_s,
};

QString randomContent(QRandomGenerator &rng)
{
    QString out = Headers[rng.bounded(int(Headers.size()))];
    const int n = 1 + rng.bounded(5);
    for (int i = 0; i < n; ++i)
        out += Lines[rng.bounded(int(Lines.size()))] + u'\n';
    return out;
}

// More tasks, reminders and nesting for the query cache: priorities, statuses, two reminders on
// one line (ties), a recurring task, a child block, a ```query block.
const QStringList QueryLines = Lines
                               + QStringList{
                                       u"- [ ] pilne ⏫ 📅 2026-10-12 #tag2/sub"_s,
                                       u"- [/] w toku 🔼 ⏳ 2026-10-14 [[A]]"_s,
                                       u"- [x] zrobione ✅ 2026-10-13 #tag1"_s,
                                       u"- dwa ⏰ 2026-10-14 08:00 i ⏰ 2026-10-14 08:00"_s,
                                       u"- [ ] cykliczne 🔁 every week 📅 2026-10-16 ⏰ 2026-10-15 18:00"_s,
                                       u"  - podpunkt o żółwiu #tag1 [[Ż]]"_s,
                                       u"- [-] anulowane 📅 2026-10-01 ⏰ 2026-10-13 07:00"_s,
                                       u"```query\ntag:tag1\n```"_s,
                               };

const QStringList QueryHeaders =
        Headers + QStringList{u"---\ntags: [strona]\nstatus: aktywny\n---\n"_s, u"typ:: projekt\n\n"_s};

QString queryContent(QRandomGenerator &rng)
{
    QString out = QueryHeaders[rng.bounded(int(QueryHeaders.size()))];
    const int n = 1 + rng.bounded(6);
    for (int i = 0; i < n; ++i)
        out += QueryLines[rng.bounded(int(QueryLines.size()))] + u'\n';
    return out;
}

/// What a note shows that may depend on other notes or changes with it: its tasks, reminders,
/// journal date, title, where its links resolve and the texts its `((id))` show.
QString fingerprint(const VaultIndex &index, const VaultPath &path)
{
    const ParsedNote *note = index.note(path);
    if (!note)
        return {};
    QStringList parts = describeTasks(index.tasksOf(path)) + describeReminders(index.remindersOf(path));
    parts << (note->journalDate ? note->journalDate->toString(Qt::ISODate) : u"-"_s) << note->title;
    for (const ResolvedLink &l : index.outgoingLinks(path)) {
        parts << u"%1->%2"_s.arg(l.link.from).arg(l.target);
        const bool shown = l.link.kind == LinkKind::BlockRef || l.link.kind == LinkKind::BlockEmbed;
        if (shown && !l.link.blockId.isEmpty()) { // ((id)) shows the block (`[[#^id]]` does not)
            if (const auto ref = index.blockById(l.link.blockId)) {
                const ParsedNote *owner = index.note(ref->path);
                parts << (owner ? owner->blocks.value(ref->blockIndex).text : u"?"_s);
            }
        }
    }
    return parts.join(u'\n');
}

QHash<VaultPath, QString> fingerprints(const VaultIndex &index)
{
    QHash<VaultPath, QString> out;
    for (const VaultPath &p : index.paths())
        out.insert(p, fingerprint(index, p));
    return out;
}

QString describeItem(const QueryItem &item)
{
    return u"%1#%2|%3|%4"_s.arg(
            item.path, QString::number(item.blockIndex), item.task ? describeTask(*item.task) : u"-"_s,
            item.reminder ? item.reminder->at.toString() + u'/' + QString::number(item.reminder->line) : u"-"_s);
}

QStringList describeResult(const QueryResult &r)
{
    QStringList out{u"type %1 total %2"_s.arg(int(r.type)).arg(r.total)};
    for (const QueryItem &item : r.items)
        out.append(describeItem(item));
    for (const QueryGroup &g : r.groups) {
        out.append(u"group "_s + g.key + u'|' + g.label);
        for (const QueryItem &item : g.items)
            out.append(u"  "_s + describeItem(item));
    }
    return out;
}

// Harder names for the relink invariant: case variants, NFD spellings, `.markdown`, nested
// folders, relative / suffix / root-anchored links, aliases with folders or extensions.
const QStringList TrickyPaths = {u"A.md"_s,
                                 u"a.md"_s,
                                 u"x/A.md"_s,
                                 u"x/sub/A.md"_s,
                                 u"Ż.md"_s,
                                 u"x/Z\u0307.md"_s,
                                 u"y/z/c.markdown"_s,
                                 u"C.md"_s,
                                 u"img.png"_s,
                                 u"x/IMG.PNG"_s,
                                 u"Spotkanie 14.10.md"_s,
                                 u"q/Q.md"_s};

const QStringList TrickyLines = {
        u"- [[a]] i [[A]]"_s,
        u"- [[./A]] [[../A]] [[/A]]"_s,
        u"- [[sub/A]] [[x/sub/a]]"_s,
        u"- [[ż]] [[Ż]] [[x/ż.md]] [[z\u0307]]"_s,
        u"- [[c]] [[C.markdown]] [[y/z/c]]"_s,
        u"- ![[img.png]] ![[IMG.png]] ![[x/img.png]]"_s,
        u"- [r](../C.md) [s](./A.md) [t](/q/Q.md) [u](Spotkanie%2014.10.md)"_s,
        u"- [[Spotkanie 14.10]] [[q]] [[Q|kju]]"_s,
        u"- [[Alias/W]] [[W]] [[img]]"_s,
        u"- ((id1)) tekst ^id1"_s,
};

const QStringList TrickyHeaders = {
        QString(),
        u"---\naliases: [Alias/W, img]\n---\n"_s,
        u"---\naliases: [q, ż]\n---\n"_s,
        u"alias:: W, c\n\n"_s,
};

QString trickyContent(QRandomGenerator &rng)
{
    QString out = TrickyHeaders[rng.bounded(int(TrickyHeaders.size()))];
    const int n = 1 + rng.bounded(4);
    for (int i = 0; i < n; ++i)
        out += TrickyLines[rng.bounded(int(TrickyLines.size()))] + u'\n';
    return out;
}

} // namespace

class TestIndexConsistency : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void randomEditsMatchFreshIndex_data()
    {
        QTest::addColumn<quint32>("seed");
        for (const quint32 seed : {1u, 7u, 42u, 2026u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    void randomEditsMatchFreshIndex()
    {
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        VaultIndex index;
        QHash<QString, QString> files; // the "disk"
        for (int step = 0; step < 150; ++step) {
            const QString path = Paths[rng.bounded(int(Paths.size()))];
            const int op = rng.bounded(100);
            QString what;
            if (op < 55) {
                const QString content = path.endsWith(u".md"_s) ? randomContent(rng) : QString();
                files.insert(path, content);
                index.update(path, content);
                what = u"update "_s + path;
            } else if (op < 70) {
                files.remove(path);
                index.remove(path);
                what = u"remove "_s + path;
            } else if (op < 90) {
                const QString to = Paths[rng.bounded(int(Paths.size()))];
                if (!files.contains(path) || to == path || to.endsWith(u".md"_s) != path.endsWith(u".md"_s))
                    continue;
                const QString content = rng.bounded(2) ? files.value(path) : randomContent(rng);
                files.remove(path);
                files.insert(to, to.endsWith(u".md"_s) ? content : QString());
                index.rename(path, to, files.value(to));
                what = u"rename "_s + path + u" -> "_s + to;
            } else {
                if (!files.contains(path))
                    continue;
                index.update(path, files.value(path)); // unchanged: no-op
                what = u"touch "_s + path;
            }
            VaultIndex fresh;
            QStringList sorted = files.keys();
            sorted.sort();
            for (const QString &p : std::as_const(sorted))
                fresh.update(p, files.value(p));
            compareIndexes(index, fresh, u"step %1: %2"_s.arg(step).arg(what));
            if (QTest::currentTestFailed())
                return;
        }
    }

    void trickyNamesMatchFreshIndex_data()
    {
        QTest::addColumn<quint32>("seed");
        for (const quint32 seed : {3u, 11u, 123u, 4242u, 77777u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    void trickyNamesMatchFreshIndex()
    {
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        VaultIndex index;
        QHash<QString, QString> files;
        auto isNote = [](const QString &p) { return p.endsWith(u".md"_s) || p.endsWith(u".markdown"_s); };
        for (int step = 0; step < 200; ++step) {
            const QString path = TrickyPaths[rng.bounded(int(TrickyPaths.size()))];
            const int op = rng.bounded(100);
            QString what;
            if (op < 60) {
                const QString content = isNote(path) ? trickyContent(rng) : QString();
                files.insert(path, content);
                index.update(path, content);
                what = u"update "_s + path;
            } else if (op < 75) {
                files.remove(path);
                index.remove(path);
                what = u"remove "_s + path;
            } else {
                const QString to = TrickyPaths[rng.bounded(int(TrickyPaths.size()))];
                if (!files.contains(path) || to == path || isNote(to) != isNote(path))
                    continue;
                const QString content = rng.bounded(2) ? files.value(path) : trickyContent(rng);
                files.remove(path);
                files.insert(to, isNote(to) ? content : QString());
                index.rename(path, to, files.value(to));
                what = u"rename "_s + path + u" -> "_s + to;
            }
            VaultIndex fresh;
            QStringList sorted = files.keys();
            sorted.sort();
            for (const QString &p : std::as_const(sorted))
                fresh.update(p, files.value(p));
            compareIndexes(index, fresh, u"tricky step %1: %2"_s.arg(step).arg(what));
            if (QTest::currentTestFailed())
                return;
        }
    }

    // Every other update prepared on another thread and merged with updatePrepared(): the same
    // index as update() alone (and as a fresh one) after every step.
    void preparedUpdatesMatchFreshIndex_data()
    {
        QTest::addColumn<quint32>("seed");
        for (const quint32 seed : {5u, 99u, 31337u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    void preparedUpdatesMatchFreshIndex()
    {
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        VaultIndex index;
        QHash<QString, QString> files;
        auto isNote = [](const QString &p) { return p.endsWith(u".md"_s) || p.endsWith(u".markdown"_s); };
        for (int step = 0; step < 160; ++step) {
            const QString path = TrickyPaths[rng.bounded(int(TrickyPaths.size()))];
            const int op = rng.bounded(100);
            QString what;
            if (op < 70) {
                const QString content = isNote(path) ? trickyContent(rng) : QString();
                files.insert(path, content);
                if (step % 2 == 0 && isNote(path)) {
                    PreparedNote prepared;
                    const VaultConfig config = index.config();
                    std::thread worker([&] { prepared = VaultIndex::prepare(path, content, config); });
                    worker.join();
                    QVERIFY(prepared.isValid());
                    QCOMPARE(prepared.path(), path);
                    QCOMPARE(prepared.contentKey(), VaultIndex::contentKey(content));
                    index.updatePrepared(std::move(prepared));
                    what = u"prepared "_s + path;
                } else {
                    index.update(path, content);
                    what = u"update "_s + path;
                }
            } else {
                files.remove(path);
                index.remove(path);
                what = u"remove "_s + path;
            }
            VaultIndex fresh;
            QStringList sorted = files.keys();
            sorted.sort();
            for (const QString &p : std::as_const(sorted))
                fresh.update(p, files.value(p));
            compareIndexes(index, fresh, u"prepared step %1: %2"_s.arg(step).arg(what));
            if (QTest::currentTestFailed())
                return;
        }
    }

    void updatePreparedRules()
    {
        VaultIndex index;
        index.update(u"A.md"_s, u"- [[B]] #żółw"_s);
        const int version = index.version();
        // Unchanged content: a no-op, as update().
        index.updatePrepared(VaultIndex::prepare(u"A.md"_s, u"- [[B]] #żółw"_s, index.config()));
        QCOMPARE(index.version(), version);
        // Prepared with another config (journal folder): parsed again with the index's.
        VaultConfig other;
        other.journalsFolder = u"dziennik"_s;
        index.updatePrepared(VaultIndex::prepare(u"dziennik/2026-10-28.md"_s, u"- dzień"_s, other));
        QVERIFY(!index.note(u"dziennik/2026-10-28.md"_s)->journalDate.has_value());
        VaultIndex journals(other);
        journals.updatePrepared(VaultIndex::prepare(u"dziennik/2026-10-28.md"_s, u"- dzień"_s, VaultConfig()));
        QCOMPARE(journals.note(u"dziennik/2026-10-28.md"_s)->journalDate, std::optional<QDate>(QDate(2026, 10, 28)));
        // Not indexable / attachments.
        index.updatePrepared(VaultIndex::prepare(u".trash/X.md"_s, u"- x"_s, index.config()));
        QVERIFY(!index.note(u".trash/X.md"_s));
        index.updatePrepared(
                VaultIndex::prepare(u"X.sync-conflict-20261028-120000-ABCDEFG.md"_s, u"- x"_s, index.config()));
        QCOMPARE(index.paths(), QList<VaultPath>({u"A.md"_s, u"dziennik/2026-10-28.md"_s}));
        index.updatePrepared(PreparedNote()); // invalid: nothing
        QCOMPARE(index.paths().size(), 2);
        // Fingerprints: the fields parsing depends on.
        VaultConfig c;
        const QString base = VaultIndex::parseFingerprint(c);
        c.newPagesFolder = u"nowe"_s;
        QCOMPARE(VaultIndex::parseFingerprint(c), base);
        c.defaultReminderTime = QTime(7, 30);
        QVERIFY(VaultIndex::parseFingerprint(c) != base);
        QVERIFY(VaultIndex::contentKey(u"żółw"_s) != VaultIndex::contentKey(u"zolw"_s));
        QCOMPARE(VaultIndex::contentKey(u""_s), Q_UINT64_C(0xcbf29ce484222325));
    }

    // Recency (suggestPages("")): notes merged in any order with reserved sequence numbers
    // rank as when loaded one by one in path order; an edit made meanwhile is newer.
    void reservedSequencesKeepPathOrder()
    {
        const QStringList notes = {u"a.md"_s, u"b.md"_s, u"c.md"_s, u"journals/2026-10-27.md"_s,
                                   u"journals/2026-10-28.md"_s};
        VaultIndex serial;
        for (const QString &p : notes)
            serial.update(p, u"- "_s + p);
        VaultIndex loaded;
        const quint64 base = loaded.reserveSequence(notes.size());
        for (qsizetype i = notes.size() - 1; i >= 0; --i) // merged backwards
            loaded.updatePrepared(VaultIndex::prepare(notes[i], u"- "_s + notes[i], loaded.config()),
                                  base + quint64(i));
        QCOMPARE(describeRecency(loaded), describeRecency(serial));
        loaded.update(u"a.md"_s, u"- zmieniona"_s);
        QCOMPARE(describeRecency(loaded).first(), u"a.md"_s);
    }

    // A full-text index built elsewhere: documents of notes as they are now are kept, others
    // (edited since, deleted, never indexed) dropped and rebuilt; search equals a fresh index's.
    void adoptSearchValidates()
    {
        QHash<QString, QString> files = {{u"Żółw.md"_s, u"- spotkanie z żółwiem"_s},
                                         {u"Zmieniona.md"_s, u"- stara treść spotkanie"_s},
                                         {u"Usunięta.md"_s, u"- spotkanie usunięte"_s},
                                         {u"Bez dokumentu.md"_s, u"- żółw bez dokumentu"_s}};
        SearchIndex built;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (it.key() == u"Bez dokumentu.md")
                continue;
            const ParsedNote n = markdown::parseNote(it.key(), it.value());
            built.set(it.key(), SearchIndex::prepare(n.title, n.aliases, it.value()),
                      VaultIndex::contentKey(it.value()));
        }
        built.set(u"Obca.md"_s, SearchIndex::prepare(u"Obca"_s, {}, u"spotkanie"_s), 1);
        files.insert(u"Zmieniona.md"_s, u"- nowa treść spotkanei"_s);
        files.remove(u"Usunięta.md"_s);
        VaultIndex index;
        index.setSearchBuiltElsewhere(true);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            index.update(it.key(), it.value());
        index.adoptSearch(std::move(built));
        QCOMPARE(index.pendingSearchDocuments(), 2); // the edited note and the one without a document
        VaultIndex fresh;
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            fresh.update(it.key(), it.value());
        for (const QString &q : {u"zolw"_s, u"żółw"_s, u"spotkanie"_s, u"spot"_s, u"spotkanei"_s, u"stara"_s})
            QCOMPARE(describeSearch(index.search(q)), describeSearch(fresh.search(q)));
        QCOMPARE(index.pendingSearchDocuments(), 0);
    }

    // Every note whose fingerprint (tasks, reminders, title, journal date, link targets, the
    // texts its ((id)) show) changed is named by changesSince(); names is set whenever notes or
    // attachments came or went.
    void changeLogNamesEveryDependent_data()
    {
        QTest::addColumn<quint32>("seed");
        for (const quint32 seed : {1u, 7u, 42u, 2026u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    void changeLogNamesEveryDependent()
    {
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        VaultIndex index;
        QHash<QString, QString> files;
        for (int step = 0; step < 200; ++step) {
            const QHash<VaultPath, QString> before = fingerprints(index);
            const QList<VaultPath> files0 = index.paths() + index.attachments();
            const int version = index.version();
            const QString path = Paths[rng.bounded(int(Paths.size()))];
            const int op = rng.bounded(100);
            QString what;
            if (op < 60) {
                const QString content = path.endsWith(u".md"_s) ? randomContent(rng) : QString();
                files.insert(path, content);
                index.update(path, content);
                what = u"update "_s + path;
            } else if (op < 75) {
                files.remove(path);
                index.remove(path);
                what = u"remove "_s + path;
            } else {
                const QString to = Paths[rng.bounded(int(Paths.size()))];
                if (!files.contains(path) || to == path || to.endsWith(u".md"_s) != path.endsWith(u".md"_s))
                    continue;
                const QString content = rng.bounded(2) ? files.value(path) : randomContent(rng);
                files.remove(path);
                files.insert(to, to.endsWith(u".md"_s) ? content : QString());
                index.rename(path, to, files.value(to));
                what = u"rename "_s + path + u" -> "_s + to;
            }
            const QByteArray ctx = u"step %1: %2"_s.arg(step).arg(what).toUtf8();
            const IndexChanges changes = index.changesSince(version);
            QVERIFY2(changes.complete, ctx.constData());
            QVERIFY2(std::is_sorted(changes.notes.cbegin(), changes.notes.cend()), ctx.constData());
            const QHash<VaultPath, QString> after = fingerprints(index);
            QSet<VaultPath> all(before.keyBegin(), before.keyEnd());
            all.unite(QSet<VaultPath>(after.keyBegin(), after.keyEnd()));
            for (const VaultPath &p : std::as_const(all)) {
                if (before.value(p) != after.value(p) || before.contains(p) != after.contains(p))
                    QVERIFY2(changes.notes.contains(p), QByteArray(ctx + " — not logged: " + p.toUtf8()).constData());
            }
            const QList<VaultPath> files1 = index.paths() + index.attachments();
            if (files0 != files1)
                QVERIFY2(changes.names, ctx.constData());
            if (version == index.version())
                QVERIFY2(changes.notes.isEmpty() && !changes.names, ctx.constData());
            if (QTest::currentTestFailed())
                return;
        }
    }

    // A QueryCache per query, kept across a random sequence of edits, equals runQuery() on the
    // edited index and on a fresh one at every step.
    void queryCacheEqualsRunQuery_data()
    {
        QTest::addColumn<quint32>("seed");
        for (const quint32 seed : {3u, 17u, 404u, 2026u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    void queryCacheEqualsRunQuery()
    {
        QFETCH(quint32, seed);
        const QStringList sources = {
                u"type:tasks status:open due:<=+7d sort:due"_s,
                u"#tag1 sort:-priority"_s,
                u"ref:[[A]]"_s,
                u"page:Ż"_s,
                u"page:this"_s,
                u"[[x/A]] -tag:tag2/sub"_s,
                u"type:reminders sort:remind"_s,
                u"type:blocks \"żółw\""_s,
                u"type:pages tag:strona"_s,
                u"type:pages sort:page"_s,
                u"group:tag"_s,
                u"group:page limit:3"_s,
                u"path:x/"_s,
                u"due:none"_s,
                u"remind:overdue"_s,
                u"type:tasks sort:page,-status limit:5"_s,
        };
        QList<Query> queries;
        for (const QString &source : sources) {
            const auto parsed = parseQuery(source);
            QVERIFY2(std::holds_alternative<Query>(parsed), qPrintable(source));
            queries.append(std::get<Query>(parsed));
        }
        QueryOptions options;
        options.currentPath = u"x/A.md"_s;
        options.now = QTime(12, 0);
        const QDate today(2026, 10, 14);
        std::vector<QueryCache> caches(queries.size());
        QRandomGenerator rng(seed);
        VaultIndex index;
        QHash<QString, QString> files;
        int partial = 0;
        for (int step = 0; step < 120; ++step) {
            const QString path = Paths[rng.bounded(int(Paths.size()))];
            const int op = rng.bounded(100);
            QString what;
            if (op < 65) {
                const QString content = path.endsWith(u".md"_s) ? queryContent(rng) : QString();
                files.insert(path, content);
                index.update(path, content);
                what = u"update "_s + path;
            } else if (op < 78) {
                files.remove(path);
                index.remove(path);
                what = u"remove "_s + path;
            } else {
                const QString to = Paths[rng.bounded(int(Paths.size()))];
                if (!files.contains(path) || to == path || to.endsWith(u".md"_s) != path.endsWith(u".md"_s))
                    continue;
                const QString content = rng.bounded(2) ? files.value(path) : queryContent(rng);
                files.remove(path);
                files.insert(to, to.endsWith(u".md"_s) ? content : QString());
                index.rename(path, to, files.value(to));
                what = u"rename "_s + path + u" -> "_s + to;
            }
            VaultIndex fresh;
            for (auto it = files.cbegin(); it != files.cend(); ++it)
                fresh.update(it.key(), it.value());
            // Every other step two of them, so changes accumulate between updates too.
            for (qsizetype q = 0; q < queries.size(); ++q) {
                if ((step + q) % 3 == 1)
                    continue;
                const QueryResult cached = caches[q].run(queries[q], index, today, Qt::Monday, options);
                if (!caches[q].lastSync().full)
                    ++partial;
                const QByteArray ctx = u"step %1 (%2), query %3"_s.arg(step).arg(what, sources[q]).toUtf8();
                QVERIFY2(describeResult(cached)
                                 == describeResult(runQuery(queries[q], index, today, Qt::Monday, options)),
                         ctx.constData());
                QCOMPARE(describeResult(cached),
                         describeResult(runQuery(queries[q], fresh, today, Qt::Monday, options)));
                QCOMPARE(caches[q].total(), cached.total);
            }
            if (QTest::currentTestFailed())
                return;
        }
        QVERIFY(partial > 100); // the incremental path was taken
    }

    // Page-property lists (propertyKeys(), propertyValues()) follow property edits, body edits,
    // renames and removals.
    void propertyListsFollowEdits()
    {
        VaultIndex index;
        QHash<QString, QString> files = {
                {u"Projekt.md"_s, u"---\nstatus: aktywny\ntags: [praca]\n---\n- treść\n"_s},
                {u"Żółw.md"_s, u"status:: Aktywny\nmiasto:: Łódź\n\n- tekst\n"_s},
                {u"Inny.md"_s, u"- bez właściwości\n"_s},
        };
        const auto check = [&](const QString &step) {
            VaultIndex fresh;
            for (auto it = files.cbegin(); it != files.cend(); ++it)
                fresh.update(it.key(), it.value());
            compareIndexes(index, fresh, step);
        };
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            index.update(it.key(), it.value());
        check(u"start"_s);
        const auto edit = [&](const QString &path, const QString &content) {
            files.insert(path, content);
            index.update(path, content);
        };
        edit(u"Projekt.md"_s, u"---\nstatus: zamknięty\ntags: [praca]\npriorytet: 2\n---\n- treść\n"_s);
        check(u"property edit"_s);
        edit(u"Żółw.md"_s, u"status:: Aktywny\nmiasto:: Łódź\n\n- inny tekst o żółwiu\n"_s);
        check(u"body edit"_s);
        files.insert(u"Archiwum/Żółw.md"_s, files.take(u"Żółw.md"_s));
        index.rename(u"Żółw.md"_s, u"Archiwum/Żółw.md"_s, files.value(u"Archiwum/Żółw.md"_s));
        check(u"rename"_s);
        files.remove(u"Projekt.md"_s);
        index.remove(u"Projekt.md"_s);
        check(u"remove"_s);
        edit(u"Inny.md"_s, u"miasto:: Kraków\n\n- teraz z właściwością\n"_s);
        check(u"property added"_s);
    }

    void configChangeMatchesFreshIndex()
    {
        QRandomGenerator rng(99);
        VaultIndex index;
        QHash<QString, QString> files;
        for (const QString &p : Paths) {
            const QString content = p.endsWith(u".md"_s) ? randomContent(rng) : QString();
            files.insert(p, content);
            index.update(p, content);
        }
        files.insert(u"daily/14.10.2026.md"_s, u"- ⏰ 2026-10-15"_s);
        index.update(u"daily/14.10.2026.md"_s, files.value(u"daily/14.10.2026.md"_s));
        VaultConfig config;
        config.journalsFolder = u"daily"_s;
        config.journalFileFormat = u"dd.MM.yyyy"_s;
        config.defaultReminderTime = QTime(6, 0);
        index.setConfig(config);
        VaultIndex fresh(config);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            fresh.update(it.key(), it.value());
        compareIndexes(index, fresh, u"config"_s);
        QCOMPARE(index.journals().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestIndexConsistency)
#include "tst_index_consistency.moc"

// Incremental maintenance: after every update / remove / rename of a deterministic random
// sequence, everything VaultIndex derives must equal a fresh index built from the same files.
#include "index/index.h"

#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
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

QStringList describeBacklinks(const QList<Backlink> &links)
{
    QStringList out;
    for (const Backlink &b : links)
        out.append(u"%1@%2#%3"_s.arg(b.path).arg(b.link.from).arg(b.blockIndex));
    return out;
}

QStringList describeOutgoing(const QList<ResolvedLink> &links)
{
    QStringList out;
    for (const ResolvedLink &l : links)
        out.append(u"%1->%2"_s.arg(l.link.from).arg(l.target));
    return out;
}

QStringList describeTags(const QList<TagCount> &tags)
{
    QStringList out;
    for (const TagCount &t : tags)
        out.append(u"%1(%2)=%3:%4"_s.arg(t.tag, t.display).arg(t.count).arg(t.paths.join(u',')));
    return out;
}

QStringList describeMissing(const QList<MissingPage> &missing)
{
    QStringList out;
    for (const MissingPage &m : missing)
        out.append(m.name + u':' + m.sources.join(u','));
    return out;
}

QStringList describeGraph(const GraphData &g)
{
    QStringList out;
    for (const GraphNode &n : g.nodes)
        out.append(u"%1|%2|%3|%4%5"_s.arg(n.id, n.title).arg(n.degree).arg(int(n.isTag)).arg(int(n.isMissing)));
    for (const GraphEdge &e : g.edges)
        out.append(e.source + u"->"_s + e.target);
    out.sort();
    return out;
}

QStringList describeSearch(const QList<SearchHit> &hits)
{
    QStringList out;
    for (const SearchHit &h : hits)
        out.append(u"%1:%2:%3"_s.arg(h.path).arg(h.line).arg(h.excerpt));
    return out;
}

/// Compare everything derived by `a` and `b` (which must hold the same files).
void compareIndexes(const VaultIndex &a, const VaultIndex &b, const QString &step)
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
    QCOMPARE(a.tasks().size(), b.tasks().size());
    QCOMPARE(a.reminders().size(), b.reminders().size());
    QCOMPARE(a.journals(), b.journals());
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

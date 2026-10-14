// Performance of VaultIndex on a synthetic vault of 3000 notes (~30 lines each): bulk indexing,
// the first search (builds the full-text index), other queries and incremental edits. The bounds
// are generous (Raspberry Pi 4 under load); the measured times are printed.
#include "index/index.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

const QStringList Words = {u"notatka"_s, u"zadanie"_s, u"żółw"_s, u"łódź"_s,  u"projekt"_s, u"spotkanie"_s,
                           u"raport"_s,  u"zakupy"_s,  u"kawa"_s, u"ogród"_s, u"dom"_s,     u"praca"_s};

struct Note {
    QString path;
    QString content;
};

/// Deterministic synthetic note number `i`: a journal-ish outline with tasks, links, block ids,
/// block references, reminders, tags and URLs.
Note syntheticNote(int i, int total)
{
    QStringList lines;
    const QString day = u"%1"_s.arg(i % 28 + 1, 2, 10, QChar(u'0'));
    const QString month = u"%1"_s.arg(i % 12 + 1, 2, 10, QChar(u'0'));
    const bool journal = i % 3 == 0;
    const QString path = journal ? u"journals/2025-%1-%2-%3.md"_s.arg(month, day).arg(i)
                                 : u"Strony/Folder %1/Strona %2.md"_s.arg(i % 40).arg(i);
    if (i % 5 == 0)
        lines << u"---"_s << u"aliases: [Alias %1]"_s.arg(i) << u"tags: [temat%1]"_s.arg(i % 30) << u"---"_s;
    lines << u"# Notatka %1"_s.arg(i) << QString();
    for (int k = 0; k < 24; ++k) {
        const QString &w = Words[(i + k) % Words.size()];
        const QString target = u"Strona %1"_s.arg((i * 7 + k * 13) % total);
        switch (k % 6) {
        case 0: lines << u"- [ ] %1 numer %2 📅 2026-%3-%4 #%1"_s.arg(w).arg(k).arg(month, day); break;
        case 1: lines << u"  - szczegóły %1 [[%2]] ^b%3x%4"_s.arg(w, target).arg(i).arg(k); break;
        case 2:
            lines << u"- Tekst o %1 i %2 w kontekście ((b%3x1))"_s.arg(w, Words[(k * 3) % Words.size()])
                             .arg((i + 1) % total);
            break;
        case 3: lines << u"- [x] zrobione %1 ✅ 2026-%2-%3"_s.arg(w, month, day); break;
        case 4: lines << u"- przypomnienie %1 ⏰ 2026-%2-%3 09:30"_s.arg(w, month, day); break;
        default: lines << u"Akapit %1: %2 %2 [[Nieistniejąca %1]] https://example.com/%3"_s.arg(k).arg(w).arg(i);
        }
    }
    return {path, lines.join(u'\n')};
}

} // namespace

class TestIndexPerf : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void bulkIndexAndQueries()
    {
        constexpr int total = 3000;
        QList<Note> notes;
        notes.reserve(total);
        for (int i = 0; i < total; ++i)
            notes.append(syntheticNote(i, total));

        VaultIndex index;
        QElapsedTimer timer;
        timer.start();
        {
            VaultIndex::Batch batch(index);
            for (const Note &n : std::as_const(notes))
                index.update(n.path, n.content);
        }
        const qint64 indexing = timer.elapsed();
        QCOMPARE(index.paths().size(), total);
        QVERIFY2(indexing < 20000, qPrintable(u"indexing took %1 ms"_s.arg(indexing)));

        timer.restart();
        const QList<SearchHit> hits = index.search(u"zolw"_s);
        const qint64 firstSearch = timer.elapsed();
        QVERIFY(!hits.isEmpty());
        QVERIFY2(firstSearch < 20000, qPrintable(u"first search took %1 ms"_s.arg(firstSearch)));

        timer.restart();
        QVERIFY(!index.search(u"spotkanie raport"_s).isEmpty());
        QVERIFY(!index.search(u"spotkanei"_s).isEmpty()); // fuzzy over the whole dictionary
        QVERIFY(!index.search(u"s"_s).isEmpty());         // prefix of thousands of terms
        const QList<TaskRef> tasks = index.tasks();
        const QList<TagCount> tags = index.tags();
        const QList<PageSuggestion> suggestions = index.suggestPages(u"stro"_s, 20);
        const QList<BlockRef> blocks = index.suggestBlocks(u"tekst kawa"_s, 20);
        const QList<Backlink> backlinks = index.backlinks(u"Strony/Folder 7/Strona 7.md"_s);
        const QList<UnlinkedRef> unlinked = index.unlinkedReferences(u"Strony/Folder 7/Strona 7.md"_s);
        const GraphData graph = index.graph(true, true);
        const auto query = parseQuery(u"status:open due:<=2026-06-30 tag:kawa sort:due limit:50"_s);
        QVERIFY(std::holds_alternative<Query>(query));
        const QueryResult result = runQuery(std::get<Query>(query), index, QDate(2026, 6, 1));
        const QueryResult text =
                runQuery(std::get<Query>(parseQuery(u"type:blocks \"łódź\" group:page"_s)), index, QDate(2026, 6, 1));
        // Unfiltered, sorted and grouped: every task / block of the vault goes through the sort.
        const QueryResult broad = runQuery(std::get<Query>(parseQuery(u"type:tasks sort:due,-priority,page"_s)), index,
                                           QDate(2026, 6, 1));
        const QueryResult allBlocks = runQuery(std::get<Query>(parseQuery(u"type:blocks sort:page,-date group:tag"_s)),
                                               index, QDate(2026, 6, 1));
        const qint64 queries = timer.elapsed();
        QCOMPARE(broad.total, total * 8);
        QCOMPARE(broad.items.size(), qsizetype(DefaultQueryLimit));
        QVERIFY(allBlocks.total > total * 10);
        QVERIFY(!allBlocks.groups.isEmpty());

        QVERIFY(tasks.size() > total * 4);
        QVERIFY(tags.size() > 10);
        QCOMPARE(suggestions.size(), 20);
        QVERIFY(!blocks.isEmpty());
        QVERIFY(!backlinks.isEmpty());
        QVERIFY(unlinked.size() <= VaultIndex::MaxUnlinkedReferences);
        QVERIFY(graph.nodes.size() > total);
        QVERIFY(result.total > 0);
        QVERIFY(text.total > 0);
        QVERIFY2(queries < 10000, qPrintable(u"queries took %1 ms"_s.arg(queries)));

        // Incremental edits stay cheap.
        timer.restart();
        for (int i = 0; i < 50; ++i)
            index.update(notes[i].path, notes[i].content + u"\n- edycja %1"_s.arg(i));
        index.remove(notes[60].path);
        index.rename(notes[61].path, u"Nowe/Strona 61.md"_s, notes[61].content);
        QVERIFY(!index.search(u"edycja"_s).isEmpty());
        const qint64 edits = timer.elapsed();
        QCOMPARE(index.paths().size(), total - 1);
        QVERIFY2(edits < 5000, qPrintable(u"edits took %1 ms"_s.arg(edits)));

        qInfo("[perf] index %d notes: %lld ms; first search (builds the search index): %lld ms; "
              "other queries: %lld ms; 52 edits + search: %lld ms",
              total, indexing, firstSearch, queries, edits);
    }
};

QTEST_GUILESS_MAIN(TestIndexPerf)
#include "tst_index_perf.moc"

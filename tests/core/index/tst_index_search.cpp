// Full-text search (SearchIndex, VaultIndex::search) and suggestions (suggestPages,
// suggestBlocks).
#include "fixture.h"

#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

QStringList ids(const QList<SearchIndex::Hit> &hits)
{
    QStringList out;
    for (const SearchIndex::Hit &h : hits)
        out.append(h.id);
    return out;
}

QStringList hitPaths(const QList<SearchHit> &hits)
{
    QStringList out;
    for (const SearchHit &h : hits)
        out.append(h.path);
    return out;
}

QStringList suggestionPaths(const QList<PageSuggestion> &list)
{
    QStringList out;
    for (const PageSuggestion &s : list)
        out.append(s.path);
    return out;
}

} // namespace

class TestIndexSearch : public QObject {
    Q_OBJECT

private:
    VaultIndex m_index;

private Q_SLOTS:
    void initTestCase() { fixture::build(m_index); }

    // ---- SearchIndex --------------------------------------------------------------------------

    void tokenize_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("tokens");
        QTest::newRow("polish") << u"Kupić farbę (2 l)"_s << QStringList({u"kupic"_s, u"farbe"_s, u"2"_s, u"l"_s});
        QTest::newRow("upper diacritics") << u"ŻÓŁW Łódź"_s << QStringList({u"zolw"_s, u"lodz"_s});
        QTest::newRow("nfd") << u"z\u0307o\u0301\u0142w"_s << QStringList{u"zolw"_s};
        QTest::newRow("markdown") << u"- [ ] [[Strona 7]] #praca/raporty"_s
                                  << QStringList({u"strona"_s, u"7"_s, u"praca"_s, u"raporty"_s});
        QTest::newRow("dates") << u"📅 2026-10-15"_s << QStringList({u"2026"_s, u"10"_s, u"15"_s});
        QTest::newRow("emoji only") << u"🔁 ⏰"_s << QStringList();
        QTest::newRow("long token dropped") << (QString(70, u'a') + u" krótkie"_s) << QStringList{u"krotkie"_s};
        QTest::newRow("empty") << QString() << QStringList();
    }
    void tokenize()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, tokens);
        QCOMPARE(SearchIndex::tokenize(text), tokens);
    }

    void searchIndexBasics()
    {
        SearchIndex s;
        s.set(u"a"_s, u"Żółw"_s, {}, u"zielony żółw pływa"_s);
        s.set(u"b"_s, u"Kot"_s, {u"Mruczek"_s}, u"kot śpi na żółtym kocu"_s);
        QCOMPARE(s.size(), 2);
        QVERIFY(s.contains(u"a"_s));
        QCOMPARE(ids(s.search(u"zolw"_s, 10)), QStringList{u"a"_s});
        QCOMPARE(ids(s.search(u"MRUCZEK"_s, 10)), QStringList{u"b"_s});
        s.set(u"a"_s, u"Pies"_s, {}, u"pies"_s); // replace
        QVERIFY(s.search(u"zolw"_s, 10).isEmpty());
        QCOMPARE(s.size(), 2);
        s.remove(u"b"_s);
        s.remove(u"unknown"_s);
        QCOMPARE(s.size(), 1);
        QVERIFY(s.search(u"kot"_s, 10).isEmpty());
        QVERIFY(s.search(u"   "_s, 10).isEmpty());
        QVERIFY(s.search(u"pies"_s, 0).isEmpty());
        s.set(u"c"_s, u"Kot"_s, {}, u"nowy"_s); // reuses the freed slot
        QCOMPARE(ids(s.search(u"kot"_s, 10)), QStringList{u"c"_s});
        s.clear();
        QCOMPARE(s.size(), 0);
        QCOMPARE(s.termCount(), 0);
    }

    void searchIndexBoostsTitleThenAliases()
    {
        SearchIndex s;
        s.set(u"content"_s, u"Inna"_s, {}, u"ogród ogród"_s);
        s.set(u"alias"_s, u"Druga"_s, {u"Ogród"_s}, u"coś"_s);
        s.set(u"title"_s, u"Ogród"_s, {}, u"coś"_s);
        QCOMPARE(ids(s.search(u"ogrod"_s, 10)), QStringList({u"title"_s, u"alias"_s, u"content"_s}));
    }

    void searchIndexMatchKinds()
    {
        SearchIndex s;
        s.set(u"exact"_s, u"x"_s, {}, u"raport"_s);
        s.set(u"prefix"_s, u"x"_s, {}, u"raportowanie"_s);
        s.set(u"fuzzy"_s, u"x"_s, {}, u"rapotr"_s); // transposition: distance 1
        s.set(u"far"_s, u"x"_s, {}, u"rampa"_s);
        const QList<SearchIndex::Hit> hits = s.search(u"raport"_s, 10);
        QCOMPARE(ids(hits).first(), u"exact"_s);
        QVERIFY(ids(hits).contains(u"prefix"_s));
        QVERIFY(ids(hits).contains(u"fuzzy"_s));
        QVERIFY(!ids(hits).contains(u"far"_s));
        QCOMPARE(hits.first().terms, QStringList{u"raport"_s});
        // "rapor": prefix of raport / raportowanie, one edit from rapotr.
        QCOMPARE(ids(s.search(u"rapor"_s, 10)).size(), 3);
    }

    void searchIndexPrefixOnlyForLastTerm()
    {
        SearchIndex s;
        s.set(u"a"_s, u"x"_s, {}, u"raportowanie kawa"_s);
        s.set(u"b"_s, u"y"_s, {}, u"kawa"_s);
        // "rap" is the last term: prefix match, AND succeeds.
        QCOMPARE(ids(s.search(u"kawa rap"_s, 10)), QStringList{u"a"_s});
        // "rap" is not the last term: no prefix match, so AND fails and OR finds both.
        QCOMPARE(ids(s.search(u"rap kawa"_s, 10)).size(), 2);
    }

    void searchIndexFuzzyDistances_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("query");
        QTest::addColumn<bool>("found");
        QTest::newRow("3 chars: exact only") << u"kot"_s << u"kit"_s << false;
        QTest::newRow("prefix") << u"kawa"_s << u"kaw"_s << true;
        QTest::newRow("4 chars: substitution") << u"kawa"_s << u"kara"_s << true;
        QTest::newRow("4 chars: two typos") << u"kawa"_s << u"kora"_s << false;
        QTest::newRow("9 chars: two typos") << u"spotkanie"_s << u"spatkonie"_s << true;
        QTest::newRow("8 chars: two typos") << u"zakupowy"_s << u"zakopawy"_s << true;
        QTest::newRow("7 chars query: two typos") << u"raporty"_s << u"rapurto"_s << false;
        QTest::newRow("8 chars: three typos") << u"zakupowy"_s << u"zukopawi"_s << false;
        QTest::newRow("diacritics are no typos") << u"źdźbło"_s << u"zdzblo"_s << true;
    }
    void searchIndexFuzzyDistances()
    {
        QFETCH(QString, word);
        QFETCH(QString, query);
        QFETCH(bool, found);
        SearchIndex s;
        s.set(u"doc"_s, u"tytuł"_s, {}, word);
        QCOMPARE(!s.search(query, 10).isEmpty(), found);
    }

    void searchIndexAndThenOr()
    {
        SearchIndex s;
        s.set(u"both"_s, u"a"_s, {}, u"kawa mleko"_s);
        s.set(u"kawa"_s, u"b"_s, {}, u"kawa"_s);
        s.set(u"mleko"_s, u"c"_s, {}, u"mleko"_s);
        QCOMPARE(ids(s.search(u"kawa mleko"_s, 10)), QStringList{u"both"_s});
        // No document has both: any term may match.
        const QStringList either = ids(s.search(u"kawa herbata"_s, 10));
        QCOMPARE(either.size(), 2);
        QVERIFY(either.contains(u"both"_s) && either.contains(u"kawa"_s));
        QCOMPARE(ids(s.search(u"kawa mleko"_s, 1)).size(), 1);
    }

    void searchIndexIsDeterministic()
    {
        SearchIndex s;
        for (const QString &id : {u"c"_s, u"a"_s, u"b"_s})
            s.set(id, u"x"_s, {}, u"to samo"_s);
        QCOMPARE(ids(s.search(u"samo"_s, 10)), QStringList({u"a"_s, u"b"_s, u"c"_s}));
    }

    // ---- VaultIndex::search ---------------------------------------------------------------------

    void searchFindsDiacriticInsensitive()
    {
        const QList<SearchHit> hits = m_index.search(u"zolw"_s);
        QCOMPARE(hitPaths(hits), QStringList({u"Zakupy.md"_s, u"journals/2026-10-12.md"_s}));
        QCOMPARE(hits[0].title, u"Zakupy"_s);
        QCOMPARE(hits[0].line, 4);
        QCOMPARE(hits[0].excerpt, u"Żółw dla dzieci"_s);
        QCOMPARE(hits[1].line, 2);
        QCOMPARE(hits[1].excerpt, u"notatka o żółwiu i zółwiach"_s);
        QVERIFY(hits[0].score > hits[1].score);
        QCOMPARE(hitPaths(m_index.search(u"ŻÓŁW"_s)), hitPaths(hits));
    }

    void searchTitleBoostAndAliases()
    {
        QCOMPARE(hitPaths(m_index.search(u"budżet"_s)).first(), u"Projekty/Budżet.md"_s);
        const QList<SearchHit> remont = m_index.search(u"remont"_s);
        QCOMPARE(remont.first().path, u"Projekty/Dom.md"_s); // alias x2
        QVERIFY(hitPaths(remont).contains(u"journals/2026-10-12.md"_s));
        // Body match preferred over the frontmatter alias line.
        QCOMPARE(remont.first().line, 13);
        QCOMPARE(remont.first().excerpt, u"Sekcja #Remont/Łazienka"_s);
    }

    void searchAndOrFuzzy()
    {
        QCOMPARE(hitPaths(m_index.search(u"kupić farbę"_s)), QStringList{u"journals/2026-10-13.md"_s});
        QCOMPARE(hitPaths(m_index.search(u"hydraulka"_s)), QStringList{u"journals/2026-10-13.md"_s});
        QCOMPARE(hitPaths(m_index.search(u"hydraulika xyzzyq"_s)), QStringList{u"journals/2026-10-13.md"_s});
        QVERIFY(m_index.search(u"qqqqqq"_s).isEmpty());
        QVERIFY(m_index.search(QString()).isEmpty());
        QVERIFY(m_index.search(u"  \n "_s).isEmpty());
        QCOMPARE(m_index.search(u"dom"_s, 2).size(), 2);
        QVERIFY(m_index.search(u"dom"_s, 0).isEmpty());
    }

    void searchExcerptForTitleOnlyMatch()
    {
        VaultIndex index;
        index.update(u"Ogród zimowy.md"_s, u"---\ntags: [x]\n---\n\nPierwszy akapit tekstu.\n\n- drugi blok"_s);
        const QList<SearchHit> hits = index.search(u"zimowy"_s);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().line, -1);
        QCOMPARE(hits.first().excerpt, u"Pierwszy akapit tekstu."_s);
    }

    void searchExcerptAroundMatchInLongLine()
    {
        VaultIndex index;
        const QString filler = QString(u"słowo "_s).repeated(40);
        index.update(u"Długi.md"_s, u"- [ ] "_s + filler + u"Igła w stogu "_s + filler + u"^blok1"_s);
        const QList<SearchHit> hits = index.search(u"igla"_s);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().line, 0);
        const QString &excerpt = hits.first().excerpt;
        QVERIFY2(excerpt.contains(u"Igła w stogu"_s), qPrintable(excerpt));
        QVERIFY(excerpt.startsWith(u'…'));
        QVERIFY(excerpt.endsWith(u'…'));
        QVERIFY(excerpt.size() <= 122);
    }

    void searchIsUpdatedIncrementally()
    {
        VaultIndex index;
        index.update(u"a.md"_s, u"jabłko"_s);
        QCOMPARE(index.search(u"jablko"_s).size(), 1);
        index.update(u"a.md"_s, u"gruszka"_s);
        QVERIFY(index.search(u"jablko"_s).isEmpty());
        QCOMPARE(index.search(u"gruszka"_s).size(), 1);
        index.rename(u"a.md"_s, u"b/Owoce.md"_s, u"gruszka"_s);
        QCOMPARE(hitPaths(index.search(u"gruszka"_s)), QStringList{u"b/Owoce.md"_s});
        QCOMPARE(hitPaths(index.search(u"owoce"_s)), QStringList{u"b/Owoce.md"_s});
        index.remove(u"b/Owoce.md"_s);
        QVERIFY(index.search(u"gruszka"_s).isEmpty());
    }

    void searchCatchesUpInTheBackground()
    {
        VaultIndex index;
        for (int i = 0; i < 250; ++i)
            index.update(u"n%1.md"_s.arg(i), u"notatka numer %1 pomarańcza"_s.arg(i));
        // The background job indexes chunks once changes pause; wait for it, then search.
        QTest::qWait(1200);
        QCOMPARE(index.search(u"pomarancza"_s, 1000).size(), 250);
        index.flushSearch();
        QCOMPARE(index.search(u"numer"_s, 5).size(), 5);
    }

    // ---- suggestPages -------------------------------------------------------------------------

    void suggestPages_data()
    {
        QTest::addColumn<QString>("prefix");
        QTest::addColumn<QStringList>("expected");
        QTest::addColumn<QString>("alias");
        QTest::newRow("exact, duplicate names by path length")
                << u"dom"_s << QStringList({u"Projekty/Dom.md"_s, u"Archiwum/Stare/Dom.md"_s}) << QString();
        QTest::newRow("alias prefix") << u"rem"_s << QStringList{u"Projekty/Dom.md"_s} << u"Remont"_s;
        QTest::newRow("diacritics") << u"budzet"_s << QStringList{u"Projekty/Budżet.md"_s} << QString();
        QTest::newRow("title beats alias") << u"jan"_s << QStringList{u"Ludzie/Jan Kowalski.md"_s} << QString();
        QTest::newRow("logseq alias") << u"janek"_s << QStringList{u"Ludzie/Jan Kowalski.md"_s} << u"Janek"_s;
        QTest::newRow("word prefix") << u"kowal"_s << QStringList{u"Ludzie/Jan Kowalski.md"_s} << QString();
        QTest::newRow("multi-word") << u"jan kow"_s << QStringList{u"Ludzie/Jan Kowalski.md"_s} << QString();
        QTest::newRow("words out of order") << u"kow jan"_s << QStringList{u"Ludzie/Jan Kowalski.md"_s} << QString();
        QTest::newRow("path") << u"projekty/"_s << QStringList({u"Projekty/Dom.md"_s, u"Projekty/Budżet.md"_s})
                              << QString();
        QTest::newRow("typo") << u"notatkk"_s << QStringList{u"Notatki.md"_s} << QString();
        QTest::newRow("upper case + spaces") << u"  ZAKUPY "_s << QStringList{u"Zakupy.md"_s} << QString();
        QTest::newRow("no match") << u"xyzxyz"_s << QStringList() << QString();
    }
    void suggestPages()
    {
        QFETCH(QString, prefix);
        QFETCH(QStringList, expected);
        QFETCH(QString, alias);
        const QList<PageSuggestion> list = m_index.suggestPages(prefix);
        QCOMPARE(suggestionPaths(list).mid(0, expected.size()), expected);
        if (!expected.isEmpty())
            QCOMPARE(list.first().alias, alias);
        else
            QVERIFY(list.isEmpty());
    }

    void suggestPagesRanking()
    {
        VaultIndex index;
        index.update(u"Kawiarnie.md"_s, u"x"_s);  // starts with "kaw"
        index.update(u"Dobra kawa.md"_s, u"x"_s); // a word starts with it
        index.update(u"Sękawa.md"_s, u"x"_s);     // contains it
        index.update(u"Kawa.md"_s, u"x"_s);       // exact
        index.update(u"Karwasz.md"_s, u"x"_s);    // in-order characters
        index.update(u"Herbata.md"_s, u"x"_s);
        const QStringList kawa = suggestionPaths(index.suggestPages(u"kawa"_s));
        QCOMPARE(kawa.mid(0, 3), QStringList({u"Kawa.md"_s, u"Dobra kawa.md"_s, u"Sękawa.md"_s}));
        // Then the fuzzy ones: one typo in a word's beginning, characters in order.
        QCOMPARE(kawa.mid(3), QStringList({u"Karwasz.md"_s, u"Kawiarnie.md"_s}));
        const QStringList kaw = suggestionPaths(index.suggestPages(u"kaw"_s));
        QCOMPARE(kaw.mid(0, 4), QStringList({u"Kawa.md"_s, u"Kawiarnie.md"_s, u"Dobra kawa.md"_s, u"Sękawa.md"_s}));
        QVERIFY(!kaw.contains(u"Herbata.md"_s));
        QCOMPARE(index.suggestPages(u"ka"_s, 2).size(), 2);
        QVERIFY(index.suggestPages(u"ka"_s, 0).isEmpty());
    }

    void suggestPagesRecentFirstForEmptyPrefix()
    {
        VaultIndex index;
        index.update(u"a.md"_s, u"1"_s);
        index.update(u"b.md"_s, u"1"_s);
        index.update(u"c.md"_s, u"1"_s);
        index.update(u"a.md"_s, u"2"_s);
        QCOMPARE(suggestionPaths(index.suggestPages(QString())), QStringList({u"a.md"_s, u"c.md"_s, u"b.md"_s}));
        QCOMPARE(suggestionPaths(index.suggestPages(u"  "_s, 1)), QStringList{u"a.md"_s});
    }

    // ---- suggestBlocks ------------------------------------------------------------------------

    void suggestBlocks()
    {
        const QList<BlockRef> plan = m_index.suggestBlocks(u"plan"_s);
        QVERIFY(!plan.isEmpty());
        QCOMPARE(plan.first().path, u"Projekty/Dom.md"_s); // has an id and starts with the query
        QCOMPARE(fixture::blockText(m_index, plan.first().path, plan.first().blockIndex),
                 u"Plan remontu domu w Krakowie."_s);

        const QList<BlockRef> zolw = m_index.suggestBlocks(u"żółw"_s);
        QCOMPARE(zolw.size(), 2);
        QStringList texts;
        for (const BlockRef &r : zolw)
            texts.append(fixture::blockText(m_index, r.path, r.blockIndex));
        QVERIFY(texts.contains(u"Żółw dla dzieci"_s));
        QVERIFY(texts.contains(u"notatka o żółwiu i zółwiach"_s));

        // Every word must occur (any order).
        const QList<BlockRef> both = m_index.suggestBlocks(u"farbę kupić"_s);
        QCOMPARE(both.size(), 1);
        QCOMPARE(both.first().path, u"journals/2026-10-13.md"_s);
        QVERIFY(m_index.suggestBlocks(u"farbę xyz"_s).isEmpty());
        QCOMPARE(m_index.suggestBlocks(u"a"_s, 3).size(), 3);
    }

    void suggestBlocksEmptyQuery()
    {
        VaultIndex index;
        index.update(u"old.md"_s, u"- z id ^old1\n- bez id"_s);
        index.update(u"new.md"_s, u"---\ntags: [x]\n---\n- nowy bez id\n- nowy z id ^new1"_s);
        const QList<BlockRef> refs = index.suggestBlocks(QString(), 10);
        QStringList texts;
        for (const BlockRef &r : refs)
            texts.append(fixture::blockText(index, r.path, r.blockIndex));
        // Blocks with ids first (most recent note first), then others; frontmatter never.
        QCOMPARE(texts, QStringList({u"nowy z id"_s, u"z id"_s, u"nowy bez id"_s, u"bez id"_s}));
        QCOMPARE(index.suggestBlocks(QString(), 1).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestIndexSearch)
#include "tst_index_search.moc"

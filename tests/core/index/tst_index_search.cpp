// Full-text search (SearchIndex, VaultIndex::search) and suggestions (suggestPages,
// suggestBlocks).
#include "fixture.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include <type_traits>

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

    // Documents point into the dictionary (no own copies of their terms): random replacements
    // and removals, a squeeze and a move keep every search equal to that of a fresh index.
    void searchIndexChurnMatchesFresh()
    {
        static_assert(!std::is_copy_constructible_v<SearchIndex>);
        static_assert(std::is_nothrow_move_constructible_v<SearchIndex>);
        const QStringList words = {u"żółw"_s, u"zolw"_s,   u"spotkanie"_s, u"spot"_s,     u"łódź"_s,
                                   u"lodz"_s, u"raport"_s, u"kawa"_s,      u"ogród"_s,    u"źdźbło"_s,
                                   u"gęś"_s,  u"jaźń"_s,   u"zebranie"_s,  u"spotkanei"_s};
        QRandomGenerator rng(42);
        SearchIndex churned;
        QHash<QString, QStringList> docs; // id -> title, content
        for (int step = 0; step < 3000; ++step) {
            const QString id = u"Notatka %1.md"_s.arg(rng.bounded(40));
            if (rng.bounded(5) == 0) {
                churned.remove(id);
                docs.remove(id);
                continue;
            }
            QString content;
            for (int w = 0, n = int(rng.bounded(12)); w < n; ++w)
                content += words[rng.bounded(words.size())] + u' ';
            const QString title = words[rng.bounded(words.size())];
            churned.set(id, title, {}, content);
            docs.insert(id, {title, content});
            if (step % 500 == 0)
                churned.squeeze();
        }
        SearchIndex fresh;
        for (auto it = docs.cbegin(); it != docs.cend(); ++it)
            fresh.set(it.key(), it.value()[0], {}, it.value()[1]);
        const SearchIndex moved = std::move(churned);
        QCOMPARE(moved.size(), fresh.size());
        QCOMPARE(moved.termCount(), fresh.termCount());
        for (const QString &q : {u"zolw"_s, u"żółw"_s, u"spotkanie"_s, u"spot"_s, u"spotkanei"_s, u"lodz kawa"_s}) {
            const QList<SearchIndex::Hit> a = moved.search(q, 100);
            const QList<SearchIndex::Hit> b = fresh.search(q, 100);
            QCOMPARE(ids(a), ids(b));
            for (qsizetype i = 0; i < a.size(); ++i) {
                QCOMPARE(a[i].terms, b[i].terms);
                QVERIFY(qFuzzyCompare(a[i].score, b[i].score));
            }
        }
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

    void searchIndexLiteralMatchesBeforeTypos()
    {
        // "zolw" is a prefix of "żółwia" in a long note, one letter away from "Żółć" in the title of
        // a short one — which BM25 alone scores higher. What the user typed comes first.
        SearchIndex s;
        QString longDay;
        for (int i = 0; i < 40; ++i)
            longDay += u"Spotkanie z ekipą remontową, zakupy, farba do ścian, lista rzeczy nr "_s + QString::number(i)
                       + u'\n';
        longDay += u"Nakarmić żółwia Franka\n"_s;
        s.set(u"dzien"_s, u"2026-10-14"_s, {}, longDay);
        s.set(u"zolc"_s, u"Żółć"_s, {}, u"Żółć, ćma, źdźbło"_s);
        s.set(u"zolwie"_s, u"Notatki"_s, {}, u"żółwie lądowe"_s);
        const QList<SearchIndex::Hit> hits = s.search(u"zolw"_s, 10);
        QCOMPARE(ids(hits), QStringList({u"zolwie"_s, u"dzien"_s, u"zolc"_s}));
        QCOMPARE(hits.last().terms, QStringList{u"zolc"_s});
        // Exact still beats prefix (by score), both beat the typo.
        s.set(u"exact"_s, u"x"_s, {}, u"zolw"_s);
        QCOMPARE(ids(s.search(u"zolw"_s, 10)).first(), u"exact"_s);
        QCOMPARE(ids(s.search(u"zolw"_s, 10)).last(), u"zolc"_s);
        // Several terms: a document with every term as typed before one needing a typo for one.
        SearchIndex m;
        m.set(u"typo"_s, u"Kawa"_s, {}, u"kawa mleka"_s);
        m.set(u"literal"_s, u"x"_s, {}, u"lista: kawa, herbata, cukier, mleko, chleb, masło"_s);
        QCOMPARE(ids(m.search(u"kawa mleko"_s, 10)), QStringList({u"literal"_s, u"typo"_s}));
    }

    void searchIndexIsDeterministic()
    {
        SearchIndex s;
        for (const QString &id : {u"c"_s, u"a"_s, u"b"_s})
            s.set(id, u"x"_s, {}, u"to samo"_s);
        QCOMPARE(ids(s.search(u"samo"_s, 10)), QStringList({u"a"_s, u"b"_s, u"c"_s}));
    }

    // ---- VaultIndex::search ---------------------------------------------------------------------

    void searchIndexWarmsUpInTheBackground()
    {
        // After a vault is indexed the full-text documents are built on the event loop — a few
        // milliseconds per turn — without anyone searching, so the first search is fast.
        VaultIndex index;
        QString body;
        for (int i = 0; i < 120; ++i)
            body += u"- Zażółć gęślą jaźń, notatka o ogrodzie i remoncie numer %1\n"_s.arg(i);
        const int notes = 400;
        for (int i = 0; i < notes; ++i)
            index.update(u"Notatki/Strona %1.md"_s.arg(i), u"# Żółwie kod%1\n"_s.arg(i) + body);
        QCOMPARE(index.pendingSearchDocuments(), notes);
        QSet<qsizetype> seen;
        QElapsedTimer t;
        t.start();
        while (index.pendingSearchDocuments() > 0 && t.elapsed() < 20000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            seen.insert(index.pendingSearchDocuments());
        }
        QCOMPARE(index.pendingSearchDocuments(), 0);
        QVERIFY2(seen.size() > 2, qPrintable(u"built in %1 step(s)"_s.arg(seen.size()))); // in steps
        QCOMPARE(index.search(u"zolwie kod17"_s).first().path, u"Notatki/Strona 17.md"_s);
        // A change: pending again until the next quiet moment (or a search, which catches up).
        index.update(u"Notatki/Strona 3.md"_s, u"# Inny tytuł\n"_s);
        QCOMPARE(index.pendingSearchDocuments(), 1);
        QCOMPARE(index.search(u"inny tytul"_s).first().path, u"Notatki/Strona 3.md"_s);
        QCOMPARE(index.pendingSearchDocuments(), 0);
    }

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

    void suggestBlocksSkipsFrontmatter()
    {
        // Only blocks with every word are offered, never the frontmatter (the title and tags hold
        // the words here; YAML stays page-level, its block has no text).
        VaultIndex index;
        index.update(u"k.md"_s,
                     u"---\ntags: [kawa]\ntitle: Kawa ogród\n---\n- kawa z mlekiem w ogrodzie\n-   \n- herbata"_s);
        const QList<BlockRef> refs = index.suggestBlocks(u"kawa ogrod"_s, 10);
        QCOMPARE(refs.size(), 1);
        QCOMPARE(fixture::blockText(index, refs.first().path, refs.first().blockIndex), u"kawa z mlekiem w ogrodzie"_s);
        QVERIFY(index.note(u"k.md"_s)->blocks.first().kind == BlockKind::Frontmatter);
        QVERIFY(index.suggestBlocks(u"tags"_s, 10).isEmpty());
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

    // ---- suggestTags (`#` completion: tags in use, then pages a tag would name) -----------------

    void suggestTags_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QStringList>("entries"); // "tag kind count path"
        QTest::newRow("tags in use first, then the page")
                << u"pra"_s << QStringList{u"praca tag 3 "_s, u"Prawo jazdy page 0 Prawo jazdy.md"_s};
        QTest::newRow("'#' and diacritics ignored in what is typed")
                << u"#zolw"_s << QStringList{u"Żółw page 0 Zwierzęta/Żółw.md"_s};
        QTest::newRow("a tag in use names its page; the other Dom by its shortest tag")
                << u"dom"_s << QStringList{u"dom tag 2 Dom.md"_s, u"Projekty/Dom page 0 Projekty/Dom.md"_s};
        QTest::newRow("a sub-tag starting with the text") << u"proj"_s << QStringList{u"projekt/kuchnia tag 1 "_s};
        QTest::newRow("tags containing the text come last")
                << u"kuch"_s << QStringList{u"Kuchnia page 0 Kuchnia.md"_s, u"projekt/kuchnia tag 1 "_s};
        QTest::newRow("a page whose tag in use only contains the text: as that tag")
                << u"om"_s << QStringList{u"dom tag 2 Dom.md"_s, u"Projekty/Dom page 0 Projekty/Dom.md"_s};
        QTest::newRow("an alias") << u"janek"_s << QStringList{u"Janek page 0 Ludzie/Jan Kowalski.md"_s};
        QTest::newRow("nothing") << u"qqq"_s << QStringList();
    }
    void suggestTags()
    {
        QFETCH(QString, typed);
        QFETCH(QStringList, entries);
        VaultIndex index;
        index.update(u"Dom.md"_s, u"- strona\n"_s);
        index.update(u"Projekty/Dom.md"_s, u"- druga strona Dom\n"_s);
        index.update(u"Prawo jazdy.md"_s, u"- kurs\n"_s);
        index.update(u"Zwierzęta/Żółw.md"_s, u"- gad\n"_s);
        index.update(u"Kuchnia.md"_s, u"- przepisy\n"_s);
        index.update(u"Ludzie/Jan Kowalski.md"_s, u"---\naliases: [Janek]\n---\n- kontakt\n"_s);
        index.update(u"Notatki.md"_s, u"- #praca #dom\n- #praca\n- #Dom #projekt/kuchnia\n- #praca\n"_s);
        QStringList got;
        for (const TagSuggestion &s : index.suggestTags(typed))
            got.append(s.tag + (s.page ? u" page "_s : u" tag "_s) + QString::number(s.count) + u' ' + s.path);
        QCOMPARE(got, entries);
        QCOMPARE(index.suggestTags(typed, 0).size(), 0);
        QCOMPARE(index.suggestTags(typed, 1).size(), std::min<qsizetype>(1, entries.size()));
    }
};

QTEST_GUILESS_MAIN(TestIndexSearch)
#include "tst_index_search.moc"

// Three-way merge of notes (diff3 + union of concurrent insertions) and of JSON objects.
#include "sync/merge.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

namespace {

QJsonObject parsed(const std::optional<QString> &json)
{
    return json ? QJsonDocument::fromJson(json->toUtf8()).object() : QJsonObject();
}

} // namespace

class TestSyncMerge : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- splitText / joinLines -----------------------------------------------------------------
    void splitJoinRoundTrip_data()
    {
        QTest::addColumn<QString>("text");
        const QStringList samples = {QString(), u"a"_s, u"a\n"_s, u"a\nb"_s, u"a\r\nb\r\n"_s, u"\n"_s, u"a\n\n"_s,
                                     u"ząb\r\nżółw"_s, u"\r\n"_s, u"x\ry"_s};
        for (qsizetype i = 0; i < samples.size(); ++i)
            QTest::addRow("sample %d", int(i)) << samples.at(i);
    }
    void splitJoinRoundTrip()
    {
        QFETCH(QString, text);
        const SplitText t = splitText(text);
        QCOMPARE(joinLines(t.lines, t.eol.isEmpty() ? u"\n"_s : t.eol, t.finalNewline), text);
    }

    void dominantLineEnding()
    {
        QCOMPARE(splitText(u"a\r\nb\r\nc\n"_s).eol, u"\r\n"_s);
        QCOMPARE(splitText(u"a\nb\nc\r\n"_s).eol, u"\n"_s);
        QCOMPARE(splitText(u"a\r\nb\n"_s).eol, u"\r\n"_s);
        QVERIFY(splitText(u"abc"_s).eol.isEmpty());
        QVERIFY(splitText(QString()).lines.isEmpty());
        QCOMPARE(splitText(u"\n"_s).lines, QStringList{QString()});
        QVERIFY(splitText(u"\n"_s).finalNewline);
    }

    // --- merge3 --------------------------------------------------------------------------------
    void merge3Clean_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("local");
        QTest::addColumn<QString>("remote");
        QTest::addColumn<QString>("expected");

        QTest::newRow("one-sided changes")
            << u"# Lista\n- mleko\n- chleb\n- masło\n"_s << u"# Lista zakupów\n- mleko\n- chleb\n- masło\n"_s
            << u"# Lista\n- mleko\n- chleb\n- masło\n- jajka\n"_s << u"# Lista zakupów\n- mleko\n- chleb\n- masło\n- jajka\n"_s;
        QTest::newRow("remote only") << u"a\n"_s << u"a\n"_s << u"b\n"_s << u"b\n"_s;
        QTest::newRow("local only") << u"a\n"_s << u"b\n"_s << u"a\n"_s << u"b\n"_s;
        QTest::newRow("same change") << u"a\n"_s << u"c\n"_s << u"c\n"_s << u"c\n"_s;
        QTest::newRow("journal appends (union)")
            << u"- 08:00 kawa\n"_s << u"- 08:00 kawa\n- 09:00 spotkanie z Łucją\n"_s
            << u"- 08:00 kawa\n- 12:30 obiad — żurek\n"_s
            << u"- 08:00 kawa\n- 09:00 spotkanie z Łucją\n- 12:30 obiad — żurek\n"_s;
        QTest::newRow("union drops remote duplicates")
            << u"# Dziennik\n"_s << u"# Dziennik\n- [ ] kupić mleko\n- zadzwonić do mamy\n"_s
            << u"# Dziennik\n- [ ] kupić mleko\n- podlać kwiaty\n"_s
            << u"# Dziennik\n- [ ] kupić mleko\n- zadzwonić do mamy\n- podlać kwiaty\n"_s;
        QTest::newRow("same lines in another order") << u"x\n"_s << u"x\n- b\n- c\n"_s << u"x\n- c\n- b\n"_s
                                                      << u"x\n- b\n- c\n"_s;
        QTest::newRow("independent edits") << u"a\nb\nc\nd\ne\nf\n"_s << u"A\nb\nc\nd\ne\nf\n"_s
                                           << u"a\nb\nc\nd\ne\nF\n"_s << u"A\nb\nc\nd\ne\nF\n"_s;
        QTest::newRow("false conflict") << u"a\nb\nc\n"_s << u"a\nB\nc\n"_s << u"a\nB\nc\n"_s << u"a\nB\nc\n"_s;
        QTest::newRow("false conflict plus local edit") << u"a\nb\nc\nd\n"_s << u"a\nB\nc\nD\n"_s << u"a\nB\nc\nd\n"_s
                                                        << u"a\nB\nc\nD\n"_s;
        QTest::newRow("empty base: union") << QString() << u"# Dziennik\n- rano\n"_s << u"# Dziennik\n- wieczorem\n"_s
                                           << u"# Dziennik\n- rano\n- wieczorem\n"_s;
        QTest::newRow("empty base: superset") << QString() << u"a\nb\n"_s << u"a\nb\nc\n"_s << u"a\nb\nc\n"_s;
        QTest::newRow("CRLF vs LF, local style kept")
            << u"a\r\nb\r\nc\r\n"_s << u"a\r\nb\r\nc\r\nd\r\n"_s << u"A\nb\nc\n"_s << u"A\r\nb\r\nc\r\nd\r\n"_s;
        QTest::newRow("local removed final newline") << u"a\nb\n"_s << u"a\nb"_s << u"A\nb\n"_s << u"A\nb"_s;
        QTest::newRow("remote added final newline") << u"a\nb"_s << u"a\nb\nc"_s << u"a\nb\n"_s << u"a\nb\nc\n"_s;
        QTest::newRow("no final newlines") << u"a"_s << u"a\nb"_s << u"z\na"_s << u"z\na\nb"_s;
        QTest::newRow("empty local") << QString() << QString() << u"x\n"_s << u"x\n"_s;
        QTest::newRow("empty remote") << QString() << u"x\n"_s << QString() << u"x\n"_s;
        QTest::newRow("Polish lines") << u"Zażółć\ngęślą\njaźń\n"_s << u"Zażółć!\ngęślą\njaźń\n"_s
                                      << u"Zażółć\ngęślą\njaźń\nŹdźbło\n"_s << u"Zażółć!\ngęślą\njaźń\nŹdźbło\n"_s;
        QTest::newRow("prepend and append") << u"środek\n"_s << u"początek\nśrodek\n"_s << u"środek\nkoniec\n"_s
                                            << u"początek\nśrodek\nkoniec\n"_s;
    }
    void merge3Clean()
    {
        QFETCH(QString, base);
        QFETCH(QString, local);
        QFETCH(QString, remote);
        QFETCH(QString, expected);
        const MergeResult r = merge3(base, local, remote);
        QVERIFY(r.clean);
        QCOMPARE(r.text, expected);
        for (const MergeHunk &h : r.hunks)
            QCOMPARE(h.kind, MergeHunk::Kind::Ok);
    }

    void unionHunksAreJoined()
    {
        const MergeResult r = merge3(u"- 08:00 kawa\n"_s, u"- 08:00 kawa\n- 09:00 spotkanie z Łucją\n"_s,
                                     u"- 08:00 kawa\n- 12:30 obiad — żurek\n"_s);
        QCOMPARE(r.hunks, (QList<MergeHunk>{MergeHunk::ok(
                              {u"- 08:00 kawa"_s, u"- 09:00 spotkanie z Łucją"_s, u"- 12:30 obiad — żurek"_s})}));
    }

    void realConflictKeepsLocalSide()
    {
        const QString base = u"tytuł\nZadanie: kupić chleb\nkoniec\n"_s;
        const QString local = u"tytuł\nZadanie: kupić bułki\nkoniec\n"_s;
        const QString remote = u"tytuł\nZadanie: kupić rogale\nkoniec\n"_s;
        const MergeResult r = merge3(base, local, remote);
        QVERIFY(!r.clean);
        QCOMPARE(r.text, local);
        const QList<MergeHunk> expected = {
            MergeHunk::ok({u"tytuł"_s}),
            MergeHunk::conflict({u"Zadanie: kupić chleb"_s}, {u"Zadanie: kupić bułki"_s}, {u"Zadanie: kupić rogale"_s}),
            MergeHunk::ok({u"koniec"_s}),
        };
        QCOMPARE(r.hunks, expected);
    }

    void deleteVersusModifyConflicts()
    {
        const MergeResult r = merge3(u"a\nb\nc\n"_s, u"a\nc\n"_s, u"a\nB\nc\n"_s);
        QVERIFY(!r.clean);
        QCOMPARE(r.text, u"a\nc\n"_s);
    }

    void emptiedLocalVersusRemoteEditConflicts()
    {
        QVERIFY(!merge3(u"a\n"_s, QString(), u"a\nb\n"_s).clean);
    }

    void conflictingInsertionsInsideTextAreUnioned()
    {
        // Pure insertions at the same place (also mid-document) never conflict.
        const MergeResult r = merge3(u"a\nz\n"_s, u"a\nL1\nL2\nz\n"_s, u"a\nR1\nz\n"_s);
        QVERIFY(r.clean);
        QCOMPARE(r.text, u"a\nL1\nL2\nR1\nz\n"_s);
    }

    // --- unionLines ----------------------------------------------------------------------------
    void unionLines()
    {
        QCOMPARE(sync::unionLines({u"a"_s, u"b"_s}, {u"a"_s, u"c"_s}), (QStringList{u"a"_s, u"b"_s, u"c"_s}));
        QCOMPARE(sync::unionLines({}, {u"x"_s}), QStringList{u"x"_s});
        QCOMPARE(sync::unionLines({u"x"_s}, {}), QStringList{u"x"_s});
        QVERIFY(sync::unionLines({}, {}).isEmpty());
    }

    void unionLinesTreatsDuplicatesAsMultiset()
    {
        QCOMPARE(sync::unionLines({u"- a"_s}, {u"- a"_s, u"- a"_s}), (QStringList{u"- a"_s, u"- a"_s}));
    }

    // --- mergeJson3 ----------------------------------------------------------------------------
    void jsonKeysChangedOnDifferentSides()
    {
        const auto out = mergeJson3(u"{\"journalsFolder\":\"journals\",\"weekStartsOn\":1}"_s,
                                    u"{\"journalsFolder\":\"dziennik\",\"weekStartsOn\":1}"_s,
                                    u"{\"journalsFolder\":\"journals\",\"weekStartsOn\":0,\"extra\":\"ź\"}"_s);
        QVERIFY(out);
        QCOMPARE(parsed(out), (QJsonObject{{u"journalsFolder"_s, u"dziennik"_s}, {u"weekStartsOn"_s, 0}, {u"extra"_s, u"ź"_s}}));
    }

    void jsonLocalWinsAndDeletionsPropagate()
    {
        const auto out = mergeJson3(u"{\"a\":1,\"b\":2,\"c\":3}"_s, u"{\"a\":10,\"b\":2,\"c\":3}"_s, u"{\"a\":20,\"c\":3}"_s);
        QCOMPARE(parsed(out), (QJsonObject{{u"a"_s, 10}, {u"c"_s, 3}}));
        // Deleted locally, unchanged remotely: stays deleted.
        const auto deleted = mergeJson3(u"{\"a\":1,\"b\":2}"_s, u"{\"a\":1}"_s, u"{\"a\":1,\"b\":2}"_s);
        QCOMPARE(parsed(deleted), (QJsonObject{{u"a"_s, 1}}));
        // Nested values compare deeply.
        const auto nested = mergeJson3(u"{\"o\":{\"x\":[1,2]}}"_s, u"{\"o\":{\"x\":[1,2]}}"_s, u"{\"o\":{\"x\":[1,2,3]}}"_s);
        QCOMPARE(parsed(nested), (QJsonObject{{u"o"_s, QJsonObject{{u"x"_s, QJsonArray{1, 2, 3}}}}}));
    }

    void jsonRejectsInvalidSides()
    {
        QVERIFY(!mergeJson3(u"{}"_s, u"{oops"_s, u"{}"_s));
        QVERIFY(!mergeJson3(u"{}"_s, u"{}"_s, u"[1]"_s));
    }

    void jsonInvalidBaseCountsAsEmpty()
    {
        const auto out = mergeJson3(u"garbage"_s, u"{\"a\":1}"_s, u"{\"b\":2}"_s);
        QVERIFY(out);
        QCOMPARE(*out, u"{\n    \"a\": 1,\n    \"b\": 2\n}\n"_s);
    }

    void jsonEmptyTextAndBom()
    {
        const auto out = mergeJson3(QString(), QString(), u"\uFEFF{\"ą\":\"ę\"}"_s);
        QVERIFY(out);
        QCOMPARE(parsed(out), (QJsonObject{{u"ą"_s, u"ę"_s}}));
        QVERIFY(out->endsWith(u'\n'));
    }

    void jsonSpecialKeysAreData()
    {
        const auto out = mergeJson3(u"{}"_s, u"{\"__proto__\":{\"x\":1}}"_s, u"{}"_s);
        QVERIFY(out);
        QVERIFY(out->contains(u"\"__proto__\""_s));
    }

    // --- large notes ---------------------------------------------------------------------------
    void longNoteWithManyBlankLinesMergesQuickly()
    {
        QStringList lines;
        for (int i = 0; i < 4000; ++i)
            lines.append(i % 2 ? QString() : u"akapit numer %1 — zażółć"_s.arg(i));
        const QString base = lines.join(u'\n') + u'\n';
        QElapsedTimer timer;
        timer.start();
        const MergeResult r = merge3(base, u"nowy początek\n"_s + base, base + u"nowy koniec\n"_s);
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(r.clean);
        QCOMPARE(r.text, u"nowy początek\n"_s + base + u"nowy koniec\n"_s);
    }

    void hugeNotesDoNotOverflowTheStack()
    {
        QStringList lines;
        for (int i = 0; i < 200000; ++i)
            lines.append(u"w%1"_s.arg(i));
        const QString base = lines.join(u'\n');
        const MergeResult r = merge3(base, u"L\n"_s + base, base + u"\nR"_s);
        QVERIFY(r.clean);
        QVERIFY(r.text.startsWith(u"L\nw0\n"_s));
        QVERIFY(r.text.endsWith(u"w199999\nR"_s));
        // First sync of two big versions (empty base): a union, also without a stack overflow.
        const MergeResult u = merge3(QString(), base, base + u"\nnowa"_s);
        QVERIFY(u.clean);
        QCOMPARE(u.text, base + u"\nnowa"_s);
    }
};

QTEST_GUILESS_MAIN(TestSyncMerge)
#include "tst_sync_merge.moc"

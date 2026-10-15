// Tests for the QML helpers of the design system: TextHelper (folding, fuzzy matching,
// markup), DiffHelper (Myers line diff, rows, word segments, conflict hunks, resolution) and
// DatesHelper (natural language, formatting, rules, grids) — with Polish text.
#include "dateshelper.h"
#include "diffhelper.h"
#include "qttranslations.h"
#include "texthelper.h"

#include <QCoreApplication>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ui::DiffOp;

namespace {

QString kinds(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &r : rows)
        out.append(r.toMap().value(u"kind"_s).toString().left(1));
    return out.join(QString());
}

QList<int> ints(std::initializer_list<int> v)
{
    return QList<int>(v);
}

/// Apply an edit script to `a` and check that it produces `b`.
bool scriptTransforms(const QList<int> &a, const QList<int> &b, const QList<DiffOp> &ops)
{
    QList<int> out;
    int ai = 0;
    int bi = 0;
    for (const DiffOp &op : ops) {
        if (op.kind == DiffOp::Equal) {
            if (op.oldIndex != ai || op.newIndex != bi || a.value(ai) != b.value(bi))
                return false;
            out.append(a.at(ai));
            ++ai;
            ++bi;
        } else if (op.kind == DiffOp::Delete) {
            if (op.oldIndex != ai)
                return false;
            ++ai;
        } else {
            if (op.newIndex != bi)
                return false;
            out.append(b.at(bi));
            ++bi;
        }
    }
    return ai == a.size() && bi == b.size() && out == b;
}

int editCost(const QList<DiffOp> &ops)
{
    return int(std::count_if(ops.cbegin(), ops.cend(), [](const DiffOp &op) { return op.kind != DiffOp::Equal; }));
}

/// A text of one of Qt's own catalogs as the application translates it now (a helper, so that
/// lupdate does not take these texts into Brulion's translation).
QString qtText(const char *context, const char *source)
{
    return QCoreApplication::translate(context, source);
}

} // namespace

class TestUiHelpers : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- QtTranslations: Qt's own texts in Polish --------------------------------------------

    void qtTranslations()
    {
        // The catalogs are bundled (":/i18n/qt/"), so every platform has them.
        brulion::ui::QtTranslations qt;
        QCOMPARE(qtText("QPlatformTheme", "Cancel"), u"Cancel"_s);
        QCOMPARE(qt.setLanguage(u"pl"_s), int(brulion::ui::QtTranslations::catalogs().size()));
        QVERIFY2(qt.loadedFrom(u"qtbase"_s).startsWith(u":/i18n/qt/"_s), qPrintable(qt.loadedFrom(u"qtbase"_s)));
        QVERIFY2(qt.loadedFrom(u"qtdeclarative"_s).startsWith(u":/i18n/qt/"_s),
                 qPrintable(qt.loadedFrom(u"qtdeclarative"_s)));
        QCOMPARE(qtText("QPlatformTheme", "Cancel"), u"Anuluj"_s);
        QCOMPARE(qtText("QPlatformTheme", "Open"), u"Otwórz"_s);
        QCOMPARE(qtText("QFileDialog", "New Folder"), u"Nowy katalog"_s);
        // Switching back and forth replaces the catalogs (English: Qt's own texts).
        QCOMPARE(qt.setLanguage(u"en"_s), 0);
        QCOMPARE(qtText("QPlatformTheme", "Cancel"), u"Cancel"_s);
        QCOMPARE(qt.setLanguage(u"pl"_s), 2);
        QCOMPARE(qt.setLanguage(u"pl"_s), 2);
        QCOMPARE(qtText("QPlatformTheme", "Cancel"), u"Anuluj"_s);
        {
            brulion::ui::QtTranslations other; // gone with its object
        }
        QCOMPARE(qtText("QPlatformTheme", "Cancel"), u"Anuluj"_s);
    }
    void qtTranslationsAreRemovedWithTheirObject()
    {
        {
            brulion::ui::QtTranslations qt;
            qt.setLanguage(u"pl"_s);
            QCOMPARE(qtText("QPlatformTheme", "Save"), u"Zachowaj"_s);
        }
        QCOMPARE(qtText("QPlatformTheme", "Save"), u"Save"_s);
    }

    // ---- TextHelper ------------------------------------------------------------------------
    void fold()
    {
        brulion::ui::TextHelper h;
        QCOMPARE(h.fold(u"Zażółć GĘŚLĄ Jaźń"_s), u"zazolc gesla jazn"_s);
        QCOMPARE(h.fold(QString()), QString());
    }

    void fuzzyMatches_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("matches");
        QTest::newRow("prefix") << u"syn"_s << u"Synchronizacja"_s << true;
        QTest::newRow("diacritics in text") << u"zolw"_s << u"Żółw Fryderyk"_s << true;
        QTest::newRow("diacritics in query") << u"ŻÓŁW"_s << u"zolw"_s << true;
        QTest::newRow("subsequence") << u"plpd"_s << u"Plan podróży"_s << true;
        QTest::newRow("spaces ignored") << u"plan pod"_s << u"Plan podróży"_s << true;
        QTest::newRow("order matters") << u"dp"_s << u"pd"_s << false;
        QTest::newRow("missing char") << u"synx"_s << u"Synchronizacja"_s << false;
        QTest::newRow("longer than text") << u"abcdef"_s << u"abc"_s << false;
        QTest::newRow("empty text") << u"a"_s << QString() << false;
    }

    void fuzzyMatches()
    {
        QFETCH(QString, query);
        QFETCH(QString, text);
        QFETCH(bool, matches);
        QCOMPARE(brulion::ui::fuzzyMatch(query, text).score > 0, matches);
    }

    void fuzzyEmptyQuery()
    {
        const auto m = brulion::ui::fuzzyMatch(u"  "_s, u"Cokolwiek"_s);
        QCOMPARE(m.score, 1);
        QVERIFY(m.positions.isEmpty());
    }

    void fuzzyPositionsMapToOriginal()
    {
        // "ł" folds to "l"; positions refer to the original string.
        const auto m = brulion::ui::fuzzyMatch(u"zolw"_s, u"Mój żółw"_s);
        QCOMPARE(m.positions, (QList<int>{4, 5, 6, 7}));
        // Decomposed input: "o" + combining acute (U+0301) is one folded character.
        const QString decomposed = u"zólw"_s;
        const auto d = brulion::ui::fuzzyMatch(u"zolw"_s, decomposed);
        QVERIFY(d.score > 0);
        QCOMPARE(d.positions.first(), 0);
        QCOMPARE(d.positions.last(), 4);
    }

    void fuzzyRanking()
    {
        brulion::ui::TextHelper h;
        const QStringList texts{u"Ustawienia synchronizacji"_s, u"Synchronizacja"_s, u"Spis treści"_s,
                                u"Synchronizuj teraz"_s, u"Kosz"_s};
        const QVariantList ranked = h.fuzzyRank(u"syn"_s, texts);
        QCOMPARE(ranked.size(), 3);
        // Prefix matches beat a match inside the text; the shorter text wins a tie.
        QCOMPARE(ranked.at(0).toMap().value(u"index"_s).toInt(), 1);
        QCOMPARE(ranked.at(1).toMap().value(u"index"_s).toInt(), 3);
        QCOMPARE(ranked.at(2).toMap().value(u"index"_s).toInt(), 0);

        // Word starts beat scattered characters.
        QVERIFY(h.fuzzyScore(u"pp"_s, u"Plan podróży"_s) > h.fuzzyScore(u"pp"_s, u"Apple pie"_s));
        // Exact equality beats a longer title with the same prefix.
        QVERIFY(h.fuzzyScore(u"kosz"_s, u"Kosz"_s) > h.fuzzyScore(u"kosz"_s, u"Koszyk zakupów"_s));
        // Consecutive runs beat gaps.
        QVERIFY(h.fuzzyScore(u"graf"_s, u"Graf powiązań"_s) > h.fuzzyScore(u"graf"_s, u"Go read a file"_s));
    }

    void fuzzyRankEmptyQueryAndLimit()
    {
        brulion::ui::TextHelper h;
        const QStringList texts{u"a"_s, u"b"_s, u"c"_s};
        const QVariantList all = h.fuzzyRank(QString(), texts);
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.at(2).toMap().value(u"index"_s).toInt(), 2);
        QCOMPARE(h.fuzzyRank(QString(), texts, 2).size(), 2);
    }

    void fuzzyRankSecondary()
    {
        brulion::ui::TextHelper h;
        // The folder path matches, the title does not: still found (at half weight).
        const QVariantList ranked =
                h.fuzzyRank(u"podroze"_s, {u"Gdańsk"_s, u"Podróże"_s}, 0, {u"Projekty/Podróże"_s, QString()});
        QCOMPARE(ranked.size(), 2);
        QCOMPARE(ranked.at(0).toMap().value(u"index"_s).toInt(), 1);
        QVERIFY(ranked.at(1).toMap().value(u"positions"_s).toList().isEmpty());
    }

    void markMatchesEscapes()
    {
        brulion::ui::TextHelper h;
        QCOMPARE(h.markMatches(u"<b>&ż"_s, {0, 4}), u"<b>&lt;</b>b&gt;&amp;<b>ż</b>"_s);
        QCOMPARE(h.markMatches(u"abc"_s, {1}, u"#ff0000"_s), u"a<b><font color=\"#ff0000\">b</font></b>c"_s);
        QCOMPARE(h.markMatches(u"abc"_s, {-1, 7}), u"abc"_s);
        // A surrogate pair is never split.
        const QString emoji = u"a\U0001F600b"_s;
        QCOMPARE(h.markMatches(emoji, {1}), u"a<b>\U0001F600</b>b"_s);
        QCOMPARE(h.escapeHtml(u"<script>\"x\"</script>"_s), u"&lt;script&gt;&quot;x&quot;&lt;/script&gt;"_s);
    }

    // ---- DiffHelper ------------------------------------------------------------------------
    void diffSequencesMinimal_data()
    {
        QTest::addColumn<QList<int>>("a");
        QTest::addColumn<QList<int>>("b");
        QTest::addColumn<int>("cost");
        QTest::newRow("equal") << ints({1, 2, 3}) << ints({1, 2, 3}) << 0;
        QTest::newRow("both empty") << QList<int>() << QList<int>() << 0;
        QTest::newRow("insert all") << QList<int>() << ints({1, 2}) << 2;
        QTest::newRow("delete all") << ints({1, 2}) << QList<int>() << 2;
        QTest::newRow("replace middle") << ints({1, 2, 3}) << ints({1, 4, 3}) << 2;
        QTest::newRow("classic") << ints({1, 2, 3, 1, 2, 2, 1}) << ints({3, 2, 1, 2, 1, 3}) << 5;
        QTest::newRow("insert front") << ints({2, 3}) << ints({1, 2, 3}) << 1;
        QTest::newRow("delete end") << ints({1, 2, 3}) << ints({1, 2}) << 1;
        QTest::newRow("swap") << ints({1, 2}) << ints({2, 1}) << 2;
    }

    void diffSequencesMinimal()
    {
        QFETCH(QList<int>, a);
        QFETCH(QList<int>, b);
        QFETCH(int, cost);
        const QList<DiffOp> ops = brulion::ui::diffSequences(a, b);
        QVERIFY(scriptTransforms(a, b, ops));
        QCOMPARE(editCost(ops), cost);
    }

    void diffSequencesRandomRoundTrip()
    {
        // Deterministic pseudo-random sequences: every script must transform a into b.
        quint32 seed = 12345;
        auto next = [&seed] {
            seed = seed * 1103515245u + 12345u;
            return int((seed >> 16) % 6);
        };
        for (int round = 0; round < 200; ++round) {
            QList<int> a;
            QList<int> b;
            const int na = next() * 3;
            const int nb = next() * 3;
            for (int i = 0; i < na; ++i)
                a.append(next());
            for (int i = 0; i < nb; ++i)
                b.append(next());
            const QList<DiffOp> ops = brulion::ui::diffSequences(a, b);
            QVERIFY2(scriptTransforms(a, b, ops), qPrintable(u"round %1"_s.arg(round)));
        }
    }

    void diffSequencesCostLimit()
    {
        QList<int> a;
        QList<int> b;
        for (int i = 0; i < 300; ++i) {
            a.append(i);
            b.append(1000 + i);
        }
        const QList<DiffOp> ops = brulion::ui::diffSequences(a, b, 10);
        QVERIFY(scriptTransforms(a, b, ops));
        QCOMPARE(editCost(ops), 600);
    }

    void deletionsBeforeInsertions()
    {
        const QList<DiffOp> ops = brulion::ui::diffLines(u"a\nx\ny\nb\n"_s, u"a\nX\nY\nb\n"_s);
        QStringList k;
        for (const DiffOp &op : ops)
            k.append(op.kind == DiffOp::Equal ? u"="_s : op.kind == DiffOp::Delete ? u"-"_s : u"+"_s);
        QCOMPARE(k.join(QString()), u"=--++="_s);
    }

    void unifiedRowsDetail()
    {
        brulion::ui::DiffHelper h;
        const QVariantList rows = h.unifiedRows(u"Ala ma kota\nżółw\n"_s, u"Ala ma psa\nżółw\nnowy\n"_s, -1);
        QCOMPARE(kinds(rows), u"diei"_s);
        const QVariantMap del = rows.at(0).toMap();
        QCOMPARE(del.value(u"oldNo"_s).toInt(), 1);
        QCOMPARE(del.value(u"newNo"_s).toInt(), 0);
        // Word segments: "Ala ma " unchanged, "kota" changed.
        const QVariantList segs = del.value(u"segments"_s).toList();
        QCOMPARE(segs.size(), 2);
        QCOMPARE(segs.at(0).toMap().value(u"text"_s).toString(), u"Ala ma "_s);
        QCOMPARE(segs.at(1).toMap().value(u"text"_s).toString(), u"kota"_s);
        QVERIFY(segs.at(1).toMap().value(u"changed"_s).toBool());
        const QVariantMap ins = rows.at(3).toMap();
        QCOMPARE(ins.value(u"newNo"_s).toInt(), 3);
        QVERIFY(ins.value(u"segments"_s).toList().isEmpty()); // unpaired insertion
    }

    void lineEndingsIgnored()
    {
        brulion::ui::DiffHelper h;
        const QVariantMap s = h.stats(u"a\r\nb\r\n"_s, u"a\nb"_s);
        QCOMPARE(s.value(u"added"_s).toInt(), 0);
        QCOMPARE(s.value(u"removed"_s).toInt(), 0);
        QCOMPARE(s.value(u"unchanged"_s).toInt(), 2);
        QCOMPARE(h.stats(QString(), QString()).value(u"unchanged"_s).toInt(), 0);
    }

    void splitRowsPairs()
    {
        brulion::ui::DiffHelper h;
        const QVariantList rows = h.splitRows(u"a\nb\nc\n"_s, u"a\nB\nX\nc\n"_s, -1);
        QCOMPARE(kinds(rows), u"ecie"_s);
        const QVariantMap change = rows.at(1).toMap();
        QCOMPARE(change.value(u"left"_s).toString(), u"b"_s);
        QCOMPARE(change.value(u"right"_s).toString(), u"B"_s);
        const QVariantMap insert = rows.at(2).toMap();
        QCOMPARE(insert.value(u"left"_s).toString(), QString());
        QCOMPARE(insert.value(u"oldNo"_s).toInt(), 0);
        QCOMPARE(insert.value(u"newNo"_s).toInt(), 3);
    }

    void contextCollapse()
    {
        brulion::ui::DiffHelper h;
        QString a;
        for (int i = 1; i <= 20; ++i)
            a += u"wiersz %1\n"_s.arg(i);
        QString b = a;
        b.replace(u"wiersz 10\n"_s, u"wiersz dziesiąty\n"_s);
        const QVariantList rows = h.unifiedRows(a, b, 2);
        // skip(7) equal equal delete insert equal equal skip(8)
        QCOMPARE(kinds(rows), u"seediees"_s);
        QCOMPARE(rows.first().toMap().value(u"count"_s).toInt(), 7);
        QCOMPARE(rows.last().toMap().value(u"count"_s).toInt(), 8);
        QCOMPARE(rows.first().toMap().value(u"oldStart"_s).toInt(), 1);
        // A single hidden line is shown instead of a "1 unchanged line" row.
        const QVariantList small = h.unifiedRows(u"a\nb\nc\nd\n"_s, u"a\nb\nc\nX\n"_s, 2);
        QCOMPARE(kinds(small), u"eeedi"_s);
        // Identical texts collapse completely.
        QCOMPARE(kinds(h.unifiedRows(a, a, 3)), u"s"_s);
    }

    void wordSegmentsSkipRewrites()
    {
        brulion::ui::DiffHelper h;
        const QVariantList rows = h.splitRows(u"Zupełnie inny tekst\n"_s, u"Nic wspólnego tutaj\n"_s, -1);
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.at(0).toMap().value(u"leftSegments"_s).toList().isEmpty());
    }

    void segmentsHtml()
    {
        brulion::ui::DiffHelper h;
        const QVariantList segs{QVariantMap{{u"text"_s, u"  a<b"_s}, {u"changed"_s, false}},
                                QVariantMap{{u"text"_s, u"ż&"_s}, {u"changed"_s, true}}};
        QCOMPARE(
                h.segmentsHtml(segs, u"#123456"_s),
                u"<span style=\"white-space:pre-wrap\">  a&lt;b<span style=\"background-color:#123456\">ż&amp;</span></span>"_s);
    }

    void conflictHunksRoundTrip()
    {
        brulion::ui::DiffHelper h;
        const QString ours = u"# Zakupy\r\n- mleko\r\n- chleb\r\n- masło\r\n"_s;
        const QString theirs = u"# Zakupy\n- mleko\n- bułki\n- masło\n- ser\n"_s;
        const QVariantList hunks = h.conflictHunks(ours, theirs);
        QCOMPARE(kinds(hunks), u"scsc"_s);
        const QVariantMap first = hunks.at(1).toMap();
        QCOMPARE(first.value(u"ours"_s).toString(), u"- chleb\r\n"_s);
        QCOMPARE(first.value(u"theirs"_s).toString(), u"- bułki\n"_s);
        QCOMPARE(first.value(u"oursLine"_s).toInt(), 3);
        const QVariantMap appended = hunks.at(3).toMap();
        QCOMPARE(appended.value(u"ours"_s).toString(), QString());
        QCOMPARE(appended.value(u"oursLine"_s).toInt(), 0);

        // All "ours" reproduces ours byte for byte (CRLF kept).
        QCOMPARE(h.resolveHunks(hunks, {}), ours);
        const QVariantMap pickOurs{{u"choice"_s, u"ours"_s}};
        QCOMPARE(h.resolveHunks(hunks, {pickOurs, pickOurs}), ours);

        // Theirs for the first, both for the appended line.
        const QVariantList mixed{QVariantMap{{u"choice"_s, u"theirs"_s}}, QVariantMap{{u"choice"_s, u"both"_s}}};
        QCOMPARE(h.resolveHunks(hunks, mixed), u"# Zakupy\r\n- mleko\r\n- bułki\n- masło\r\n- ser\n"_s);

        // Manual text without a trailing newline gets one before the next piece.
        const QVariantList manual{QVariantMap{{u"choice"_s, u"manual"_s}, {u"text"_s, u"- chleb i bułki"_s}}, pickOurs};
        QCOMPARE(h.resolveHunks(hunks, manual), u"# Zakupy\r\n- mleko\r\n- chleb i bułki\r\n- masło\r\n"_s);
    }

    void conflictHunksManualKeepsLineEndings()
    {
        brulion::ui::DiffHelper h;
        // A QML TextArea hands back "\n" only: a CRLF note must stay CRLF after a manual merge.
        const QString ours = u"Zadania\r\n- zadzwonić do mamy\r\n- kupić żółty ser\r\nKoniec\r\n"_s;
        const QString theirs = u"Zadania\r\n- napisać do taty\r\nKoniec\r\n"_s;
        const QVariantList hunks = h.conflictHunks(ours, theirs);
        QCOMPARE(kinds(hunks), u"scs"_s);
        const QVariantList manual{
                QVariantMap{{u"choice"_s, u"manual"_s}, {u"text"_s, u"- zadzwonić do mamy\n- napisać do taty\n"_s}}};
        QCOMPARE(h.resolveHunks(hunks, manual), u"Zadania\r\n- zadzwonić do mamy\r\n- napisać do taty\r\nKoniec\r\n"_s);

        // LF notes stay LF, also when the manual text was pasted with CRLF.
        const QVariantList lfHunks = h.conflictHunks(u"a\nb\n"_s, u"a\nc\n"_s);
        const QVariantList pasted{QVariantMap{{u"choice"_s, u"manual"_s}, {u"text"_s, u"b\r\nc\r\n"_s}}};
        QCOMPARE(h.resolveHunks(lfHunks, pasted), u"a\nb\nc\n"_s);
    }

    void diffCacheFollowsInputs()
    {
        // stats() and the rows share a one-entry cache: it must never answer for other texts.
        brulion::ui::DiffHelper h;
        const QString base = u"jeden\ndwa\ntrzy\n"_s;
        QCOMPARE(h.stats(base, base).value(u"added"_s).toInt(), 0);
        QCOMPARE(h.stats(base, base + u"cztery\n"_s).value(u"added"_s).toInt(), 1);
        QCOMPARE(kinds(h.unifiedRows(base, base + u"cztery\n"_s, -1)), u"eeei"_s);
        QCOMPARE(h.stats(base, u"jeden\n"_s).value(u"removed"_s).toInt(), 2);
        QCOMPARE(kinds(h.splitRows(base, base, -1)), u"eee"_s);
        QCOMPARE(kinds(h.conflictHunks(base, u"jeden\nDWA\ntrzy\n"_s)), u"scs"_s);
        QCOMPARE(h.stats(base, base).value(u"unchanged"_s).toInt(), 3);
    }

    void conflictHunksBothJoinsLastLines()
    {
        brulion::ui::DiffHelper h;
        // Neither side ends with a newline: "both" must not glue the lines together.
        const QVariantList hunks = h.conflictHunks(u"a\nmoje"_s, u"a\nich"_s);
        QCOMPARE(kinds(hunks), u"sc"_s);
        QCOMPARE(h.resolveHunks(hunks, {QVariantMap{{u"choice"_s, u"both"_s}}}), u"a\nmoje\nich"_s);
    }

    void conflictHunksIdentical()
    {
        brulion::ui::DiffHelper h;
        const QVariantList hunks = h.conflictHunks(u"x\ny\n"_s, u"x\ny\n"_s);
        QCOMPARE(kinds(hunks), u"s"_s);
        QCOMPARE(hunks.first().toMap().value(u"lines"_s).toInt(), 2);
        QCOMPARE(h.conflictHunks(QString(), QString()).size(), 0);
    }

    // ---- DatesHelper -----------------------------------------------------------------------
    void datesNatural()
    {
        brulion::ui::DatesHelper d;
        d.setFixedNow(u"2026-10-14 10:00"_s); // a Wednesday
        QCOMPARE(d.today(), u"2026-10-14"_s);
        const QVariantMap r = d.parseNatural(u"Zadzwonić jutro o 9"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"date"_s).toString(), u"2026-10-15"_s);
        QCOMPARE(r.value(u"time"_s).toString(), u"09:00"_s);
        QCOMPARE(r.value(u"spec"_s).toString(), u"2026-10-15 09:00"_s);
        QCOMPARE(r.value(u"display"_s).toString(), u"czw, 15 paź 09:00"_s);
        QCOMPARE(r.value(u"human"_s).toString(), u"jutro 09:00"_s);
        QVERIFY(r.value(u"relative"_s).toBool());
        QCOMPARE(r.value(u"relativeWord"_s).toString(), u"jutro"_s);
        QVERIFY(!d.parseNatural(u"za tydzień"_s).value(u"relative"_s).toBool());
        QCOMPARE(r.value(u"matched"_s).toString(), u"jutro o 9"_s);
        QVERIFY(!d.parseNatural(u"nic tu nie ma"_s).value(u"ok"_s).toBool());

        d.setLocale(u"en"_s);
        QCOMPARE(d.parseNatural(u"next friday 6pm"_s).value(u"display"_s).toString(), u"Fri, Oct 23 18:00"_s);
        QCOMPARE(d.formatShort(u"2027-01-02"_s), u"Sat, Jan 2, 2027"_s);
        QCOMPARE(d.formatLong(u"2026-10-15"_s), u"Thursday, October 15, 2026"_s);
    }

    void datesFormatting()
    {
        brulion::ui::DatesHelper d;
        d.setFixedNow(u"2026-10-14 10:00"_s);
        d.setLocale(u"pl"_s);
        QCOMPARE(d.formatHuman(u"2026-10-14 09:30"_s), u"dziś 09:30"_s);
        QCOMPARE(d.formatShort(u"2027-01-02 07:05"_s), d.weekdayNames(u"short"_s).at(5) + u", 2 sty 2027 07:05"_s);
        QCOMPARE(d.formatShort(u"2026-10-15"_s), u"czw, 15 paź"_s);
        QCOMPARE(d.formatLong(u"2026-10-15"_s), u"czwartek, 15 października 2026"_s);
        QCOMPARE(d.monthTitle(2026, 10), u"październik 2026"_s);
        QCOMPARE(d.formatHuman(u"bzdura"_s), QString());
        QCOMPARE(d.weekdayNames(u"narrow"_s).first(), u"P"_s);
        QCOMPARE(d.weekdayNames(u"full"_s).at(2), u"środa"_s);
        QCOMPARE(d.monthNames(u"genitive"_s).at(9), u"października"_s);
        QCOMPARE(d.monthNames(u"full"_s, u"en"_s).at(0), u"January"_s);
    }

    void datesRules()
    {
        brulion::ui::DatesHelper d;
        d.setLocale(u"pl"_s);
        const QVariantMap weekly{{u"freq"_s, u"week"_s}, {u"interval"_s, 1}, {u"weekdays"_s, QVariantList{1}}};
        QCOMPARE(d.formatRule(weekly, u"en"_s), u"every week on Monday"_s);
        QCOMPARE(d.formatRule(weekly), u"co tydzień w poniedziałek"_s);
        QCOMPARE(d.formatRule({{u"freq"_s, u"day"_s}, {u"weekdaysOnly"_s, true}}, u"en"_s), u"every weekday"_s);
        QCOMPARE(d.formatRule({{u"freq"_s, u"hour"_s}}), QString());
        QCOMPARE(d.ruleLabel(u"every 2 weeks on Friday"_s), u"co 2 tygodnie w piątek"_s);
        QCOMPARE(d.canonicalRule(u"co tydzień w pn"_s), u"every week on Monday"_s);
        const QVariantMap parsed = d.parseRule(u"co miesiąc"_s);
        QVERIFY(parsed.value(u"ok"_s).toBool());
        QCOMPARE(parsed.value(u"freq"_s).toString(), u"month"_s);
        QCOMPARE(parsed.value(u"canonical"_s).toString(), u"every month"_s);
        QVERIFY(!d.parseRule(u"kiedyś"_s).value(u"ok"_s).toBool());
        QCOMPARE(d.nextOccurrences(u"every week on Monday"_s, u"2026-10-14 09:00"_s, 2),
                 (QStringList{u"2026-10-19 09:00"_s, u"2026-10-26 09:00"_s}));
        QCOMPARE(d.nextOccurrences(QVariant(weekly), u"zła data"_s, 2), QStringList());
    }

    void datesGridAndArithmetic()
    {
        brulion::ui::DatesHelper d;
        d.setFixedNow(u"2026-10-14 10:00"_s);
        const QVariantList cells = d.monthGrid(2026, 10, 1);
        QCOMPARE(cells.size(), 42);
        // October 2026 starts on a Thursday: the grid starts on Monday 28 September.
        QCOMPARE(cells.first().toMap().value(u"date"_s).toString(), u"2026-09-28"_s);
        QVERIFY(!cells.first().toMap().value(u"inMonth"_s).toBool());
        QVERIFY(cells.at(16).toMap().value(u"isToday"_s).toBool());
        // Sunday start.
        QCOMPARE(d.monthGrid(2026, 10, 7).first().toMap().value(u"date"_s).toString(), u"2026-09-27"_s);
        QVERIFY(d.monthGrid(2026, 13, 1).isEmpty());

        QCOMPARE(d.addDays(u"2026-12-31"_s, 1), u"2027-01-01"_s);
        QCOMPARE(d.addMonths(u"2026-01-31"_s, 1), u"2026-02-28"_s);
        QCOMPARE(d.addMinutes(u"2026-10-14 23:55"_s, 10), u"2026-10-15 00:05"_s);
        QCOMPARE(d.dayOfWeek(u"2026-10-14"_s), 3);
        QCOMPARE(d.dayOfWeek(u"2026-02-30"_s), 0);
        QCOMPARE(d.startOfWeek(u"2026-10-14"_s), u"2026-10-12"_s);
        QVERIFY(d.compare(u"2026-10-14"_s, u"2026-10-14 08:00"_s) < 0);
    }

    void datesNormalizeTime_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("hour") << u"9"_s << u"09:00"_s;
        QTest::newRow("colon") << u"9:30"_s << u"09:30"_s;
        QTest::newRow("dot") << u"18.30"_s << u"18:30"_s;
        QTest::newRow("compact") << u"930"_s << u"09:30"_s;
        QTest::newRow("compact4") << u"1845"_s << u"18:45"_s;
        QTest::newRow("spaces") << u" 07:05 "_s << u"07:05"_s;
        QTest::newRow("24") << u"24:00"_s << QString();
        QTest::newRow("minutes") << u"12:60"_s << QString();
        QTest::newRow("one minute digit") << u"9:5"_s << QString();
        QTest::newRow("text") << u"rano"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }

    void datesNormalizeTime()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        brulion::ui::DatesHelper d;
        QCOMPARE(d.normalizeTime(input), expected);
    }
};

QTEST_GUILESS_MAIN(TestUiHelpers)
#include "tst_uihelpers.moc"

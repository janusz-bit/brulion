// Tests of the semantic module: chunks, language guess, calibration, vector index, fusion.
#include "semantic/semantic.h"

#include <QJsonDocument>
#include <QTest>

#include <cmath>

using namespace brulion::semantic;
using namespace Qt::StringLiterals;

namespace {

Vector vec(std::initializer_list<float> xs)
{
    Vector v(xs);
    normalize(v);
    return v;
}

float norm(const Vector &v)
{
    return std::sqrt(dot(v, v));
}

QStringList ids(const QList<ScoredId> &hits)
{
    QStringList out;
    for (const ScoredId &h : hits)
        out.append(h.id);
    return out;
}

QStringList ids(const QList<VectorIndex::Hit> &hits)
{
    QStringList out;
    for (const VectorIndex::Hit &h : hits)
        out.append(h.id);
    return out;
}

} // namespace

class TestSemantic : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void chunkSmallNote()
    {
        const auto cs = chunkNote(u"Wymiana opon"_s, u"Opony zimowe założone.\n\n- koszt 180 zł\n- serwis na Woli"_s);
        QCOMPARE(cs.size(), 1);
        QCOMPARE(cs.first().text, u"Wymiana opon\nOpony zimowe założone. - koszt 180 zł - serwis na Woli"_s);
        QCOMPARE(cs.first().line, 0);
    }

    void chunkEmptyNote()
    {
        const auto cs = chunkNote(u"Pusta"_s, u""_s);
        QCOMPARE(cs.size(), 1);
        QCOMPARE(cs.first().text, u"Pusta\n"_s);
        QCOMPARE(chunkNote(u"Pusta"_s, u"\n\n  \n"_s).size(), 1);
    }

    void chunkSkipsFrontmatter()
    {
        const auto cs = chunkNote(u"Żółw"_s, u"---\ntags: [zwierzęta]\n---\nŻółw je sałatę.\r\n"_s);
        QCOMPARE(cs.size(), 1);
        QCOMPARE(cs.first().text, u"Żółw\nŻółw je sałatę."_s);
        QCOMPARE(cs.first().line, 3);
    }

    void chunkUnclosedFrontmatterIsText()
    {
        const auto cs = chunkNote(u"T"_s, u"---\nnie ma końca"_s);
        QCOMPARE(cs.first().text, u"T\n---\nnie ma końca"_s);
    }

    void chunkSplitsAtWordLimit()
    {
        QString text;
        for (int p = 0; p < 6; ++p) {
            for (int w = 0; w < 30; ++w)
                text += u"słowo%1 "_s.arg(p);
            text += u"\n\n"_s;
        }
        const auto cs = chunkNote(u"Długa"_s, text, 100);
        QCOMPARE(cs.size(), 2); // 3 paragraphs of 30 words fit in 100, then the next 3
        QCOMPARE(cs.at(0).line, 0);
        QCOMPARE(cs.at(1).line, 6);
        for (const Chunk &c : cs)
            QVERIFY(c.text.startsWith(u"Długa\n"_s));
    }

    void chunkSplitsLongParagraph()
    {
        QStringList words;
        for (int w = 0; w < 250; ++w)
            words.append(u"ąę%1"_s.arg(w));
        const auto cs = chunkNote(u"Akapit"_s, words.join(u' '), 100);
        QCOMPARE(cs.size(), 3);
        QVERIFY(cs.at(2).text.endsWith(u"ąę249"_s));
        QCOMPARE(cs.at(1).text.split(u' ').size(), 100); // "Akapit\nąę100" counts as one item
    }

    void chunkHeadingsAndBulletsStartParts()
    {
        const auto cs = chunkNote(u"N"_s, u"# Nagłówek\ntekst\n- punkt\n  - podpunkt\n## Drugi"_s, 4);
        QStringList texts;
        for (const Chunk &c : cs)
            texts.append(c.text.mid(2));
        QCOMPARE(texts, (QStringList{u"# Nagłówek\ntekst"_s, u"- punkt\n  - podpunkt"_s, u"## Drugi"_s}));
        QCOMPARE(cs.at(1).line, 2);
    }

    void language_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("english");
        QTest::newRow("pl") << u"Wymieniłem opony, bo już jest zimno"_s << false;
        QTest::newRow("pl-ascii") << u"to jest notatka dla mnie i nie tylko"_s << false;
        QTest::newRow("en") << u"Booked the flat for the trip to Lisbon and paid the deposit"_s << true;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("numbers") << u"2025-03-14 450 zł"_s << false;
    }

    void language()
    {
        QFETCH(QString, text);
        QFETCH(bool, english);
        QCOMPARE(looksEnglish(text), english);
    }

    void calibrationIdentityWhenEmpty()
    {
        const Calibration c;
        QVERIFY(c.isEmpty());
        const Vector v = vec({1, 2, 3});
        QCOMPARE(c.apply(v, true), v);
    }

    void calibrationRemovesMeanAndDirection()
    {
        Calibration c;
        c.queryMean = {0.5f, 0, 0};
        c.passageMean = {0, 0.5f, 0};
        c.direction = vec({0, 0, 1});
        const Vector q = c.apply(vec({1, 1, 1}), true);
        QVERIFY(std::abs(norm(q) - 1) < 1e-5f);
        QVERIFY(std::abs(q.at(2)) < 1e-6f); // direction projected out
        QVERIFY(q.at(0) < q.at(1));         // the query mean was subtracted from x
        const Vector p = c.apply(vec({1, 1, 1}), false);
        QVERIFY(p.at(0) > p.at(1)); // the passage mean from y
    }

    void calibrationIgnoresWrongSizes()
    {
        Calibration c;
        c.queryMean = {1, 2};
        c.direction = {1, 0};
        const Vector v = vec({3, 4, 0});
        const Vector out = c.apply(v, true);
        QCOMPARE(out.size(), 3);
        QVERIFY(std::abs(dot(out, v) - 1) < 1e-6f);
    }

    void calibrationAppliesToShortQueries()
    {
        Calibration c;
        QVERIFY(!c.appliesTo(u"opony"_s)); // empty: nothing to apply
        c.direction = vec({1, 0});
        QVERIFY(c.appliesTo(u"wymiana opon zimowych"_s));
        QVERIFY(c.appliesTo(u"  jak   wymieniałem opony w aucie  "_s)); // 5 words
        QVERIFY(!c.appliesTo(u"czy żołnierz który napadnie przełożonego idzie do więzienia"_s));
        c.maxQueryWords = 0;
        QVERIFY(!c.appliesTo(u"opony"_s));
        QVERIFY(c.appliesTo(QString())); // no words: within any limit
    }

    void vectorIndexRawSearchIgnoresCalibration()
    {
        VectorIndex index;
        index.set(u"pl.md"_s, {Chunk{u"T\nTo jest notatka"_s, 0}}, {vec({1, 1, 0})});
        index.set(u"en.md"_s, {Chunk{u"T\nThis is the note"_s, 0}}, {vec({0.3f, -1, 0.2f})});
        Calibration c;
        c.direction = vec({0, 1, 0});
        index.setCalibration(c);
        const Vector query = vec({1, -1, 0});
        QCOMPARE(index.search(query, 2, true).first().id, u"pl.md"_s);
        QCOMPARE(index.search(query, 2, false).first().id, u"en.md"_s);
    }

    void calibrationEstimate()
    {
        const QList<Vector> ps = {vec({1, 0.1f}), vec({1, 0.2f}), vec({0.1f, 1}), vec({0.2f, 1})};
        const Calibration c = Calibration::estimate(ps, {false, false, true, true}, {0.3f, 0.3f}, 2);
        QCOMPARE(c.queryMean, (Vector{0.3f, 0.3f}));
        QCOMPARE(c.passageMean.size(), 2);
        QVERIFY(std::abs(c.passageMean.at(0) - c.passageMean.at(1)) < 1e-6f);
        QCOMPARE(c.direction.size(), 2);
        QVERIFY(c.direction.at(0) > 0 && c.direction.at(1) < 0); // Polish minus English
        QVERIFY(std::abs(norm(c.direction) - 1) < 1e-5f);
        // Too few English chunks: no direction.
        QVERIFY(Calibration::estimate(ps, {false, false, false, true}, {}, 2).direction.isEmpty());
        QVERIFY(Calibration::estimate({}, {}, {}).passageMean.isEmpty());
    }

    void calibrationJsonRoundTrip()
    {
        Calibration c;
        c.queryMean = {0.25f, -1};
        c.direction = vec({3, 4});
        const QByteArray json = QJsonDocument(c.toJson()).toJson();
        const Calibration back = Calibration::fromJson(QJsonDocument::fromJson(json).object());
        QCOMPARE(back.queryMean, c.queryMean);
        QVERIFY(back.passageMean.isEmpty());
        QCOMPARE(back.maxQueryWords, 5);
        QCOMPARE(back.direction.size(), 2);
        QVERIFY(std::abs(back.direction.at(0) - 0.6f) < 1e-6f);
        QVERIFY(Calibration::fromJson({}).isEmpty());
    }

    void vectorIndexBestChunkWins()
    {
        VectorIndex index;
        const Chunk c0{u"a"_s, 0}, c1{u"b"_s, 7};
        index.set(u"Auto.md"_s, {c0, c1}, {vec({1, 0, 0}), vec({0, 1, 0})});
        index.set(u"Ogród.md"_s, {c0}, {vec({0.7f, 0.7f, 0})});
        QCOMPARE(index.dimension(), 3);
        QCOMPARE(index.chunkCount(), 3);
        const auto hits = index.search(vec({0, 1, 0}), 10);
        QCOMPARE(ids(hits), (QStringList{u"Auto.md"_s, u"Ogród.md"_s}));
        QCOMPARE(hits.first().line, 7);
        QVERIFY(std::abs(hits.first().score - 1) < 1e-5f);
        QCOMPARE(index.search(vec({0, 1, 0}), 1).size(), 1);
        QVERIFY(index.search(vec({0, 1}), 10).isEmpty()); // wrong dimension
        QVERIFY(index.search(vec({0, 1, 0}), 0).isEmpty());
    }

    void vectorIndexReplaceAndRemove()
    {
        VectorIndex index;
        index.set(u"a"_s, {Chunk{}}, {vec({1, 0})});
        index.set(u"b"_s, {Chunk{}, Chunk{}}, {vec({0, 1}), vec({1, 1})});
        index.set(u"a"_s, {Chunk{}}, {vec({0, 1})}); // replace
        QCOMPARE(index.size(), 2);
        QCOMPARE(index.chunkCount(), 3);
        index.remove(u"b"_s);
        index.remove(u"nieznany"_s);
        QCOMPARE(index.chunkCount(), 1);
        QCOMPARE(ids(index.search(vec({0, 1}), 5)), QStringList{u"a"_s});
        index.set(u"c"_s, {Chunk{}}, {vec({1, 0})}); // reuses b's slot
        QCOMPARE(ids(index.search(vec({1, 0}), 5)), (QStringList{u"c"_s, u"a"_s}));
        index.clear();
        QCOMPARE(index.size(), 0);
        QVERIFY(index.search(vec({1, 0}), 5).isEmpty());
    }

    void vectorIndexCalibrationChangesRanking()
    {
        // Two languages along y: the English query matches the English note only through y.
        VectorIndex index;
        index.set(u"pl.md"_s, {Chunk{u"T\nTo jest notatka i nie tylko"_s, 0}}, {vec({1, 1, 0})});
        index.set(u"en.md"_s, {Chunk{u"T\nThis is the note for the trip"_s, 0}}, {vec({0.3f, -1, 0.2f})});
        const Vector query = vec({1, -1, 0}); // about x, written in "English" (-y)
        QCOMPARE(index.search(query, 2).first().id, u"en.md"_s);
        Calibration c;
        c.direction = vec({0, 1, 0});
        index.setCalibration(c);
        QCOMPARE(index.search(query, 2).first().id, u"pl.md"_s);
        // Estimated from the stored chunks (languages from their text).
        const Calibration own = index.estimateCalibration({});
        QCOMPARE(own.passageMean.size(), 3);
        QVERIFY(own.direction.isEmpty()); // one chunk per language < 5
    }

    void fusion()
    {
        const QList<ScoredId> lexical = {{u"a"_s, 10}, {u"b"_s, 5}, {u"c"_s, 0}};
        const QList<ScoredId> semantic = {{u"c"_s, 0.9}, {u"b"_s, 0.8}, {u"d"_s, 0.4}};
        // Only the semantic order counts; "a" (lexical only) and "d" (the semantic minimum) tie at 0.
        QCOMPARE(ids(fuseConvex(lexical, semantic, 1.0, 10)), (QStringList{u"c"_s, u"b"_s, u"a"_s, u"d"_s}));
        QCOMPARE(ids(fuseConvex(lexical, semantic, 0.0, 10)).first(), u"a"_s);
        const auto mixed = fuseConvex(lexical, semantic, 0.5, 10);
        QCOMPARE(ids(mixed).first(), u"b"_s); // 0.5*0.5 + 0.5*0.8 = 0.65
        QCOMPARE(mixed.size(), 4);
        QCOMPARE(fuseConvex(lexical, semantic, 0.5, 2).size(), 2);
        QVERIFY(fuseConvex({}, {}, 0.5).isEmpty());
        // One hit, or equal scores: scaled to 1.
        QCOMPARE(fuseConvex({{u"x"_s, 3}}, {}, 0.2).first().score, 0.8);
        // Ties by id; alpha clamped.
        QCOMPARE(ids(fuseConvex({{u"z"_s, 1}, {u"y"_s, 1}}, {}, 7.0)), (QStringList{u"y"_s, u"z"_s}));
    }
};

QTEST_GUILESS_MAIN(TestSemantic)
#include "tst_semantic.moc"

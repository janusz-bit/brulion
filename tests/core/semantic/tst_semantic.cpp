// Tests of the semantic module: chunks, language guess, calibration, vector index (incremental
// changes, kept on the device), keeping it up to date (keys, diff), fusion and the hybrid search,
// the models the app downloads.
#include "semantic/semantic.h"

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
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

QStringList ids(const QList<HybridHit> &hits)
{
    QStringList out;
    for (const HybridHit &h : hits)
        out.append(h.id);
    return out;
}

/// A random unit vector of `dim` (the same for the same seed).
Vector randomVector(int dim, quint32 seed)
{
    QRandomGenerator g(seed);
    Vector v(dim);
    for (float &x : v)
        x = float(g.generateDouble() * 2 - 1);
    normalize(v);
    return v;
}

/// An index of `notes` notes with 1–3 chunks each in `dim` dimensions, keys 1000 + i.
VectorIndex randomIndex(int notes, int dim, quint32 seed)
{
    VectorIndex index;
    for (int i = 0; i < notes; ++i) {
        QList<Chunk> chunks;
        QList<Vector> vectors;
        for (int k = 0; k <= i % 3; ++k) {
            chunks.append(Chunk{k % 2 ? u"T\nThe note is in English and the text is for the test"_s
                                      : u"T\nNotatka jest po polsku i nie ma w niej nic więcej"_s,
                                k * 4});
            vectors.append(randomVector(dim, seed + quint32(i * 7 + k)));
        }
        index.set(u"notatka-%1.md"_s.arg(i), chunks, vectors, 1000 + quint64(i));
    }
    return index;
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

    void vectorIndexKeysAndIds()
    {
        VectorIndex index;
        index.set(u"b.md"_s, {Chunk{}}, {vec({1, 0})}, 42);
        index.set(u"a.md"_s, {Chunk{}}, {vec({0, 1})});
        QCOMPARE(index.key(u"b.md"_s), quint64(42));
        QCOMPARE(index.key(u"a.md"_s), quint64(0));
        QCOMPARE(index.key(u"nieznany.md"_s), quint64(0));
        QCOMPARE(index.ids(), (QStringList{u"a.md"_s, u"b.md"_s}));
        index.set(u"b.md"_s, {Chunk{}}, {vec({1, 1})}, 43);
        QCOMPARE(index.key(u"b.md"_s), quint64(43));
        index.remove(u"b.md"_s);
        QCOMPARE(index.key(u"b.md"_s), quint64(0));
        QCOMPARE(index.ids(), QStringList{u"a.md"_s});
    }

    void vectorIndexManyChangesCompact()
    {
        // Notes replaced and removed many times over (the arrays compacted on the way): what is
        // left searches exactly as an index built from it alone.
        VectorIndex index = randomIndex(300, 16, 1);
        for (int round = 0; round < 20; ++round) {
            for (int i = round; i < 300; i += 7) {
                const QString id = u"notatka-%1.md"_s.arg(i);
                if ((i + round) % 3 == 0)
                    index.remove(id);
                else
                    index.set(id, {Chunk{u"T\nx"_s, 1}, Chunk{u"T\ny"_s, 9}},
                              {randomVector(16, quint32(5000 + round * 300 + i)), randomVector(16, quint32(9000 + i))},
                              quint64(round));
            }
        }
        for (const QString &id : index.ids())
            QVERIFY(index.contains(id));
        qsizetype chunks = 0;
        for (int q = 0; q < 10; ++q) {
            const Vector query = randomVector(16, quint32(77 + q));
            const QList<VectorIndex::Hit> all = index.search(query, 1000, false);
            QCOMPARE(all.size(), index.size());
            const QList<VectorIndex::Hit> scored = index.score(index.ids(), query, false);
            QCOMPARE(scored.size(), index.size());
            QHash<QString, float> byId;
            for (const VectorIndex::Hit &h : scored)
                byId.insert(h.id, h.score);
            for (const VectorIndex::Hit &h : all)
                QCOMPARE(byId.value(h.id), h.score);
            chunks = index.chunkCount();
        }
        QVERIFY(chunks > 0);
        QVERIFY(chunks <= 3 * 300);
    }

    void vectorIndexCalibratedSearchIsCalibratedVectors()
    {
        // The calibrated search without a calibrated copy: the same scores as calibrating every
        // chunk (Calibration::apply) and comparing unit vectors.
        VectorIndex index = randomIndex(40, 24, 3);
        Calibration c;
        c.queryMean = randomVector(24, 900);
        for (float &x : c.queryMean)
            x *= 0.3f;
        c.passageMean = randomVector(24, 901);
        for (float &x : c.passageMean)
            x *= 0.4f;
        c.direction = randomVector(24, 902);
        index.setCalibration(c);
        for (int q = 0; q < 5; ++q) {
            const Vector query = randomVector(24, quint32(1000 + q));
            const Vector cq = c.apply(query, true);
            for (const VectorIndex::Hit &h : index.search(query, 10, true)) {
                // The note's chunks again: randomIndex() made them.
                const int i = h.id.mid(8, h.id.size() - 11).toInt();
                float best = -2;
                for (int k = 0; k <= i % 3; ++k)
                    best = std::max(best, dot(cq, c.apply(randomVector(24, quint32(3 + i * 7 + k)), false)));
                QVERIFY2(std::abs(best - h.score) < 1e-4f, qPrintable(u"%1: %2 vs %3"_s.arg(h.id).arg(best).arg(h.score)));
            }
        }
        // Calibrated only when asked; an empty calibration is the raw search.
        const Vector query = randomVector(24, 5);
        QCOMPARE(ids(index.search(query, 40, false)), ids(VectorIndex(randomIndex(40, 24, 3)).search(query, 40, false)));
        index.setCalibration({});
        QCOMPARE(ids(index.search(query, 40, true)), ids(index.search(query, 40, false)));
    }

    void vectorIndexSaveAndLoad()
    {
        VectorIndex index = randomIndex(25, 32, 11);
        index.set(u"Żółw/Ćma ąę.md"_s, {Chunk{u"T\nTo jest notatka"_s, 3}}, {randomVector(32, 4242)}, 7);
        const QByteArray bytes = index.save(u"multilingual-e5-small"_s);
        const std::optional<VectorIndex> loaded = VectorIndex::load(bytes, u"multilingual-e5-small"_s);
        QVERIFY(loaded);
        QCOMPARE(loaded->ids(), index.ids());
        QCOMPARE(loaded->chunkCount(), index.chunkCount());
        QCOMPARE(loaded->dimension(), 32);
        QCOMPARE(loaded->key(u"Żółw/Ćma ąę.md"_s), quint64(7));
        QCOMPARE(loaded->key(u"notatka-3.md"_s), quint64(1003));
        // Half floats: the same ranking, scores within a thousandth, the chunks' lines kept.
        for (int q = 0; q < 5; ++q) {
            const Vector query = randomVector(32, quint32(300 + q));
            const QList<VectorIndex::Hit> a = index.search(query, 30, false);
            const QList<VectorIndex::Hit> b = loaded->search(query, 30, false);
            QCOMPARE(b.size(), a.size());
            for (qsizetype i = 0; i < a.size(); ++i) {
                QVERIFY(std::abs(a.at(i).score - b.at(i).score) < 2e-3f);
                QCOMPARE(b.at(i).line, index.score({b.at(i).id}, query, false).first().line);
            }
        }
        // The languages of the chunks are kept (the calibration is estimated from them).
        const Calibration own = index.estimateCalibration({});
        const Calibration again = loaded->estimateCalibration({});
        QCOMPARE(again.direction.isEmpty(), own.direction.isEmpty());
        QVERIFY(!own.direction.isEmpty());
        QVERIFY(dot(own.direction, again.direction) > 0.999f);
        // Saved twice: the same bytes (sorted by id).
        QCOMPARE(loaded->save(u"multilingual-e5-small"_s).size(), bytes.size());
    }

    void vectorIndexLoadRefuses()
    {
        const VectorIndex index = randomIndex(5, 8, 21);
        const QByteArray bytes = index.save(u"e5"_s);
        QVERIFY(VectorIndex::load(bytes, u"e5"_s));
        QVERIFY(!VectorIndex::load(bytes, u"mmlw"_s)); // another model
        QVERIFY(!VectorIndex::load(QByteArray(), u"e5"_s));
        QVERIFY(!VectorIndex::load("BRSV", u"e5"_s));
        QVERIFY(!VectorIndex::load("not an index at all, just text", u"e5"_s));
        // Cut anywhere: refused, never a crash.
        for (qsizetype n = 0; n < bytes.size(); n += 7)
            QVERIFY(!VectorIndex::load(bytes.first(n), u"e5"_s));
        // Another version of the format.
        QByteArray other = bytes;
        other[4] = char(VectorIndex::FormatVersion + 1);
        QVERIFY(!VectorIndex::load(other, u"e5"_s));
        // Damaged counts or a vector that is not a number.
        QByteArray huge = bytes;
        for (int i = 0; i < 4; ++i)
            huge[huge.size() / 2 + i] = char(0xff);
        const std::optional<VectorIndex> damaged = VectorIndex::load(huge, u"e5"_s);
        Q_UNUSED(damaged) // (refused or read: either, but no crash and no huge allocation)
        // An empty index round-trips.
        const std::optional<VectorIndex> empty = VectorIndex::load(VectorIndex().save(u"e5"_s), u"e5"_s);
        QVERIFY(!empty); // (no dimension yet: nothing worth keeping)
    }

    void noteKeys_data()
    {
        QTest::addColumn<QString>("titleA");
        QTest::addColumn<QString>("contentA");
        QTest::addColumn<QString>("titleB");
        QTest::addColumn<QString>("contentB");
        QTest::addColumn<bool>("same");
        QTest::newRow("same") << u"Żółw"_s << u"- kupić karmę\n"_s << u"Żółw"_s << u"- kupić karmę\n"_s << true;
        QTest::newRow("edited") << u"Żółw"_s << u"- kupić karmę\n"_s << u"Żółw"_s << u"- kupić karmę!\n"_s << false;
        QTest::newRow("renamed") << u"Żółw"_s << u"tekst"_s << u"Zolw"_s << u"tekst"_s << false;
        QTest::newRow("boundary") << u"ab"_s << u"c"_s << u"a"_s << u"bc"_s << false;
        QTest::newRow("line ending") << u"T"_s << u"a\nb"_s << u"T"_s << u"a\r\nb"_s << false;
        QTest::newRow("empty") << QString() << QString() << QString() << QString() << true;
    }

    void noteKeys()
    {
        QFETCH(QString, titleA);
        QFETCH(QString, contentA);
        QFETCH(QString, titleB);
        QFETCH(QString, contentB);
        QFETCH(bool, same);
        QCOMPARE(noteKey(titleA, contentA) == noteKey(titleB, contentB), same);
        QVERIFY(noteKey(titleA, contentA) != 0);
        // Stable across runs (kept on the device): FNV-1a 64, fixed here.
        QCOMPARE(noteKey(QString(), QString()), noteKey(QString(), QString()));
    }

    void noteKeyIsFixed()
    {
        // The keys of vectors kept on a device must not change between versions of the app.
        QCOMPARE(noteKey(u"Szarlotka"_s, u"Jabłka, mąka, masło."_s), noteKey(u"Szarlotka"_s, u"Jabłka, mąka, masło."_s));
        const quint64 k = noteKey(u"a"_s, u"b"_s);
        quint64 h = 14695981039346656037ULL;
        for (char16_t u : {char16_t(u'a'), char16_t(u'\n'), char16_t(u'b')}) {
            h ^= quint64(u & 0xff);
            h *= 1099511628211ULL;
            h ^= quint64(u >> 8);
            h *= 1099511628211ULL;
        }
        QCOMPARE(k, h);
    }

    void noteChunksMaskEncrypted()
    {
        // (A v3 payload of Meld Encrypt, as tests/core/encryption/meldfixtures.h has it.)
        const QString secret = u"🔐γ AQNbYAWeS1Nspelbi7JOIG57XRSQf2Bct+v9VEmfhtn+Buq5WLJoArGo6rHtagI04U4wokv4RMgLg3kQ+B4yeweYu7mW7ntkhqP+"
                               u"yjYe1otitOmpU6U8NCBxhlZ+xTYWrVB6D7BBeRppNBFA43qY3p/77I+Rnrg5Jg== 🔐"_s;
        const QString content = u"Hasło do banku: "_s + secret + u"\n\nZwykły tekst o ogrodzie."_s;
        const QList<Chunk> chunks = noteChunks(u"Sejf"_s, content);
        QVERIFY(!chunks.isEmpty());
        for (const Chunk &c : chunks) {
            QVERIFY2(!c.text.contains(u"AQNbYAWeS1"_s), qPrintable(c.text));
            QVERIFY(!c.text.contains(u"🔐"_s));
        }
        QVERIFY(chunks.last().text.contains(u"ogrodzie"_s));
        // Without fragments: chunkNote() as it is.
        QCOMPARE(noteChunks(u"T"_s, u"Zwykły tekst."_s).first().text, chunkNote(u"T"_s, u"Zwykły tekst."_s).first().text);
    }

    void noteChunksCapped()
    {
        QString huge;
        for (int i = 0; i < MaxChunksPerNote * 2; ++i)
            huge += u"Akapit numer %1 o niczym szczególnym.\n\n"_s.arg(i).repeated(20);
        const QList<Chunk> chunks = noteChunks(u"Log"_s, huge);
        QCOMPARE(chunks.size(), MaxChunksPerNote);
        QCOMPARE(chunks.first().line, 0);
    }

    void excerpts()
    {
        const QString note = u"---\ntags: [x]\n---\n# Szarlotka\n\n- jabłka, kruszonka\n  - [ ] kupić cynamon\n> cytat babci\n"
                             u"```\nkod\n```\n1. piec 45 minut\n"_s;
        QCOMPARE(passageExcerpt(note, 3),
                 u"Szarlotka jabłka, kruszonka kupić cynamon cytat babci kod piec 45 minut"_s);
        QCOMPARE(passageExcerpt(note, 5, 5), u"jabłka, kruszonka"_s); // (whole lines, at least `chars`)
        QCOMPARE(passageExcerpt(note, 100), QString());
        QCOMPARE(passageExcerpt(QString(), 0), QString());
        // Encrypted text never shows.
        const QString secret = u"Hasło: 🔐γ AQNbYAWeS1Nspelbi7JOIG57XRSQf2Bct+v9VEmfhtn+Buq5WLJoArGo6rHtagI04U4wokv4RMgLg3kQ+B4yeweYu7mW7ntkhqP+"
                               u"yjYe1otitOmpU6U8NCBxhlZ+xTYWrVB6D7BBeRppNBFA43qY3p/77I+Rnrg5Jg== 🔐 koniec"_s;
        QCOMPARE(passageExcerpt(secret, 0).simplified(), u"Hasło: koniec"_s);
    }

    void diff()
    {
        VectorIndex index;
        index.set(u"stara.md"_s, {Chunk{}}, {vec({1, 0})}, 1);
        index.set(u"bez zmian.md"_s, {Chunk{}}, {vec({0, 1})}, 2);
        index.set(u"zmieniona.md"_s, {Chunk{}}, {vec({1, 1})}, 3);
        const IndexDiff d = diffIndex({{u"nowa.md"_s, 10}, {u"zmieniona.md"_s, 33}, {u"bez zmian.md"_s, 2}}, index);
        QCOMPARE(d.embed, (QStringList{u"nowa.md"_s, u"zmieniona.md"_s})); // in the order given
        QCOMPARE(d.remove, QStringList{u"stara.md"_s});
        // Everything current: nothing to do; an empty notebook: everything goes.
        const IndexDiff none = diffIndex({{u"stara.md"_s, 1}, {u"bez zmian.md"_s, 2}, {u"zmieniona.md"_s, 3}}, index);
        QVERIFY(none.embed.isEmpty());
        QVERIFY(none.remove.isEmpty());
        QCOMPARE(diffIndex({}, index).remove, (QStringList{u"bez zmian.md"_s, u"stara.md"_s, u"zmieniona.md"_s}));
        // A note set without vectors of the right dimension is there (nothing to search) and current.
        index.set(u"pusta.md"_s, {Chunk{}}, {vec({1, 0, 0})}, 5);
        QVERIFY(diffIndex({{u"pusta.md"_s, 5}}, index).embed.isEmpty());
    }

    void fusionKnown()
    {
        // An id the vector index does not know (an attachment, a note not embedded yet) scores by
        // its words alone.
        const QList<ScoredId> lexical = {{u"plik.pdf"_s, 10}, {u"a.md"_s, 10}, {u"b.md"_s, 0}};
        const QList<ScoredId> semantic = {{u"b.md"_s, 0.9}, {u"a.md"_s, 0.1}};
        const auto known = [](const QString &id) { return id.endsWith(u".md"_s); };
        const QList<ScoredId> fused = fuseConvex(lexical, semantic, known, 0.8, 10);
        QCOMPARE(ids(fused), (QStringList{u"plik.pdf"_s, u"b.md"_s, u"a.md"_s}));
        QCOMPARE(fused.at(0).score, 1.0);  // its words alone, as if its meaning matched as well
        QCOMPARE(fused.at(1).score, 0.8);  // meaning 1, words 0
        QVERIFY(std::abs(fused.at(2).score - 0.2) < 1e-9); // meaning 0, words 1
        // Without `known`: fuseConvex as before.
        QCOMPARE(ids(fuseConvex(lexical, semantic, {}, 0.8, 10)), (QStringList{u"b.md"_s, u"a.md"_s, u"plik.pdf"_s}));
    }

    void hybrid()
    {
        VectorIndex index;
        // "Szarlotka" holds no word of the query but its meaning; "Ciasto" both; the others neither.
        index.set(u"Szarlotka.md"_s, {Chunk{u"T\nx"_s, 4}}, {vec({1, 0.1f, 0})});
        index.set(u"Ciasto.md"_s, {Chunk{u"T\nx"_s, 0}, Chunk{u"T\ny"_s, 9}}, {vec({0, 1, 0}), vec({0.9f, 0.1f, 0})});
        for (int i = 0; i < 30; ++i)
            index.set(u"Inna %1.md"_s.arg(i, 2, 10, QChar(u'0')), {Chunk{}}, {vec({0.1f, 0.2f, 1.0f + float(i)})});
        const Vector query = vec({1, 0, 0});
        const QList<ScoredId> lexical = {{u"Ciasto.md"_s, 5}, {u"Wykład.pdf"_s, 4}, {u"Inna 29.md"_s, 1}};
        const QList<HybridHit> hits = hybridSearch(lexical, index, query, false, 100, 3);
        // Every lexical hit stays; three notes by meaning alone at most.
        QStringList found = ids(hits);
        for (const ScoredId &s : lexical)
            QVERIFY(found.contains(s.id));
        qsizetype alone = 0;
        for (const HybridHit &h : hits)
            alone += !h.lexical;
        QCOMPARE(alone, 3);
        QCOMPARE(hits.size(), 6);
        // The note with both first, the meaning-alone one next to it, where its chunk starts.
        QCOMPARE(hits.at(0).id, u"Ciasto.md"_s);
        QVERIFY(hits.at(0).lexical && hits.at(0).semantic);
        QCOMPARE(hits.at(0).line, 9);
        const auto szarlotka = std::ranges::find(hits, u"Szarlotka.md"_s, &HybridHit::id);
        QVERIFY(szarlotka != hits.cend());
        QVERIFY(!szarlotka->lexical && szarlotka->semantic);
        QCOMPARE(szarlotka->line, 4);
        // The PDF ranks by its words alone (no vectors), above weak notes.
        const auto pdf = std::ranges::find(hits, u"Wykład.pdf"_s, &HybridHit::id);
        QVERIFY(pdf != hits.cend());
        QCOMPARE(pdf->line, -1);
        QVERIFY(pdf->score > std::ranges::find(hits, u"Inna 29.md"_s, &HybridHit::id)->score);
        // An empty index or a query of another size: the words' order.
        QCOMPARE(ids(hybridSearch(lexical, VectorIndex(), query, false, 100)),
                 (QStringList{u"Ciasto.md"_s, u"Wykład.pdf"_s, u"Inna 29.md"_s}));
        QCOMPARE(ids(hybridSearch(lexical, index, vec({1, 0}), false, 100)),
                 (QStringList{u"Ciasto.md"_s, u"Wykład.pdf"_s, u"Inna 29.md"_s}));
        // No words found anywhere: a few notes close in meaning, not a page of them.
        QCOMPARE(hybridSearch({}, index, query, false, 100).size(), MeaningAloneHits);
    }

    void models()
    {
        const brulion::ai::LocalModel *m = semanticModel(QString());
        QVERIFY(m);
        QCOMPARE(m->id, u"multilingual-e5-small"_s);
        QCOMPARE(semanticModel(m->id), m);
        QVERIFY(!semanticModel(u"nieznany"_s));
        QCOMPARE(m->format, u"e5"_s);
        QVERIFY(m->file.url.isValid());
        QCOMPARE(m->file.url.scheme(), u"https"_s);
        QVERIFY(m->file.url.path().endsWith(m->file.name));
        QCOMPARE(m->file.sha256.size(), 64);
        QVERIFY(m->file.bytes > 100'000'000 && m->file.bytes < 200'000'000);
        QVERIFY(!m->adapted() && !m->sees());
        QCOMPARE(m->license, u"MIT"_s);
        const Vector mean = queryMeanOf(m->id);
        QCOMPARE(mean.size(), 384);
        QVERIFY(norm(mean) > 0.1f && norm(mean) < 1.0f); // a mean of unit vectors
        QVERIFY(queryMeanOf(u"nieznany"_s).isEmpty());
        QCOMPARE(indexFileName(u"abc123"_s, m->id), u"abc123-multilingual-e5-small.vectors"_s);
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

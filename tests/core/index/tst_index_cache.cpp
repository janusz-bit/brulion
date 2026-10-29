// The index cache's codec (index/indexcache.h): parsed notes and the full-text index round-trip
// exactly (the frozen corpus and the live guide notes), the encoding is frozen by a golden digest,
// every identity field is checked, and hostile input — truncation, flipped bytes, absurd lengths
// and counts, deep nesting, overlapping or copied records — is refused without crashing (CI runs
// this under ASan/UBSan); the writer aborts when the index changes and leaves the search section
// out while it is incomplete.
#include "index/indexcache.h"
#include "index/indexcache_p.h"

#include "index/index.h"
#include "indexcompare.h"
#include "markdown/parse.h"
#include "notecorpus.h"
#include "notedump.h"

#include <QCryptographicHash>
#include <QDir>
#include <QRandomGenerator>
#include <QTest>

#include <limits>
#include <tuple>

using namespace brulion;
using namespace brulion::testing;
using namespace Qt::StringLiterals;

namespace {

/// SHA-256 (first 16 bytes, hex) of the record bodies and the search section of the frozen corpus:
/// changes only with the encoding — then bump indexcache::FormatVersion (or
/// SearchIndex::FormatVersion for the search part) and record the new digest.
constexpr const char *kCacheDigest = "e127592f7160f78f03479a5ae7c88d8f";

QList<std::pair<VaultPath, QString>> guideNotes()
{
    QList<std::pair<VaultPath, QString>> out;
    const QDir dir(QStringLiteral(BRULION_GUIDE_DIR));
    for (const QString &name : dir.entryList({u"*.md"_s}, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(name));
        if (f.open(QIODevice::ReadOnly))
            out.append({name, QString::fromUtf8(f.readAll())});
    }
    return out;
}

/// An index of the corpus with its full-text index built.
std::unique_ptr<VaultIndex> corpusIndex()
{
    auto index = std::make_unique<VaultIndex>();
    for (const auto &[path, content] : noteCorpus())
        index->update(path, content);
    index->update(u"assets/zdjęcie.png"_s, QString());
    index->flushSearch();
    return index;
}

indexcache::StatFn fakeStat()
{
    return [](const VaultPath &path) -> std::optional<FileStat> {
        return FileStat{path, false, 1000 + path.size(), 1'700'000'000'000};
    };
}

indexcache::Identity testIdentity()
{
    return indexcache::Identity::current(VaultConfig(), u"vault-żółw"_s);
}

/// The corpus with the dumps of its parses (computed once).
const QList<std::tuple<VaultPath, QString, QString>> &corpusDumps()
{
    static const QList<std::tuple<VaultPath, QString, QString>> dumps = [] {
        QList<std::tuple<VaultPath, QString, QString>> out;
        for (const auto &[path, content] : noteCorpus())
            out.append({path, content, dumpNote(markdown::parseNote(path, content))});
        return out;
    }();
    return dumps;
}

/// The notes of `reader` (those whose record overlaps [from, to), all by default) decoded for
/// their text: -1 when one decodes to something else than its parse, else how many decoded.
int checkDecodedNotes(const indexcache::Reader &reader, qint64 from = 0,
                      qint64 to = std::numeric_limits<qint64>::max())
{
    int decoded = 0;
    for (const auto &[path, content, dump] : corpusDumps()) {
        const indexcache::Record *r = reader.find(path);
        if (!r || r->offset + r->length + 8 <= from || r->offset >= to)
            continue;
        const std::optional<ParsedNote> note = reader.decodeNote(*r, path, content, VaultConfig());
        if (!note)
            continue;
        ++decoded;
        if (dumpNote(*note) != dump)
            return -1;
    }
    return decoded;
}

} // namespace

class TestIndexCache : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void roundTrip_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("content");
        for (const auto &[path, content] : noteCorpus())
            QTest::newRow(qPrintable(u"corpus: "_s + path)) << path << content;
        for (const auto &[path, content] : guideNotes())
            QTest::newRow(qPrintable(u"guide: "_s + path)) << path << content;
    }
    void roundTrip()
    {
        QFETCH(QString, path);
        QFETCH(QString, content);
        const ParsedNote parsed = markdown::parseNote(path, content);
        const std::optional<QByteArray> body = indexcache::detail::encodeNote(parsed, content);
        QVERIFY(body.has_value());
        const std::optional<ParsedNote> decoded =
                indexcache::detail::decodeNote(*body, path, content, VaultConfig());
        QVERIFY(decoded.has_value());
        QCOMPARE(dumpNote(*decoded), dumpNote(parsed));
        // Shared as parseNote() shares them: reminder texts with their block's, slices with the note.
        for (const Block &b : decoded->blocks) {
            for (const Reminder &r : b.reminders) {
                if (r.text == b.text && !b.text.isEmpty())
                    QCOMPARE(r.text.constData(), b.text.constData());
            }
            if (b.raw.isSlice())
                QCOMPARE(b.raw.buffer().constData(), content.constData());
        }
        // Not one byte more is accepted.
        QVERIFY(!indexcache::detail::decodeNote(QByteArray(*body + '\0'), path, content, VaultConfig()));
        if (!body->isEmpty())
            QVERIFY(!indexcache::detail::decodeNote(body->chopped(1), path, content, VaultConfig()));
    }

    // A frontmatter value the format cannot hold: no record (the note is parsed on load).
    void unsupportedValuesAreNotEncoded()
    {
        ParsedNote note = markdown::parseNote(u"a.md"_s, u"---\nx: 1\n---\n- a\n"_s);
        note.frontmatter.insert(u"data"_s, QVariant(QDate(2026, 10, 29)));
        QVERIFY(!indexcache::detail::encodeNote(note, u"---\nx: 1\n---\n- a\n"_s));
        QVariant deep = QVariantList{};
        for (int i = 0; i < 40; ++i)
            deep = QVariantList{deep};
        note.frontmatter.insert(u"data"_s, deep);
        QVERIFY(!indexcache::detail::encodeNote(note, QString()));
    }

    void searchRoundTrip()
    {
        const std::unique_ptr<VaultIndex> index = corpusIndex();
        const QByteArray file = indexcache::encode(*index, testIdentity(), fakeStat());
        QString why;
        const auto reader = indexcache::Reader::open(file, testIdentity(), &why);
        QVERIFY2(reader, qPrintable(why));
        QVERIFY(reader->hasSearch());
        indexcache::Reader::SearchDecoder decoder = reader->searchDecoder();
        int steps = 0;
        while (!decoder.step(0)) // one term per step
            ++steps;
        QVERIFY(steps > 10);
        QVERIFY(!decoder.failed());
        VaultIndex adopted;
        for (const auto &[path, content] : noteCorpus())
            adopted.update(path, content);
        adopted.setSearchBuiltElsewhere(true);
        adopted.adoptSearch(decoder.take());
        QCOMPARE(adopted.pendingSearchDocuments(), 0);
        for (const QString &q : {u"zolw"_s, u"żółw"_s, u"spotkanie"_s, u"zadanie"_s, u"notatka 12"_s, u"lodz"_s, u"slowo"_s})
            QCOMPARE(describeSearch(adopted.search(q)), describeSearch(index->search(q)));
    }

    void cacheDigest()
    {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        SearchIndex search;
        for (const auto &[path, content] : noteCorpus()) {
            const ParsedNote note = markdown::parseNote(path, content);
            const std::optional<QByteArray> body = indexcache::detail::encodeNote(note, content);
            QVERIFY(body);
            hash.addData(*body);
            search.set(path, SearchIndex::prepare(note.title, note.aliases, content), VaultIndex::contentKey(content));
        }
        hash.addData(indexcache::detail::encodeSearch(search));
        const QByteArray digest = hash.result().toHex().left(32);
        QVERIFY2(digest == kCacheDigest,
                 qPrintable(u"the index cache's encoding changed (digest %1): bump indexcache::FormatVersion (or "
                            u"SearchIndex::FormatVersion) and update kCacheDigest"_s.arg(QString::fromLatin1(digest))));
    }

    void identity_data()
    {
        QTest::addColumn<QString>("field");
        for (const QString &f : {u"format"_s, u"parser"_s, u"search"_s, u"qt"_s, u"app"_s, u"journalsFolder"_s,
                                 u"journalFileFormat"_s, u"defaultReminderTime"_s, u"vault"_s})
            QTest::newRow(qPrintable(f)) << f;
    }
    void identity()
    {
        QFETCH(QString, field);
        const std::unique_ptr<VaultIndex> index = corpusIndex();
        const QByteArray file = indexcache::encode(*index, testIdentity(), fakeStat());
        QVERIFY(indexcache::Reader::open(file, testIdentity()));
        indexcache::Identity other = testIdentity();
        VaultConfig config;
        if (field == u"format")
            ++other.format;
        else if (field == u"parser")
            ++other.parser;
        else if (field == u"search")
            ++other.search;
        else if (field == u"qt")
            other.qt += u".1"_s;
        else if (field == u"app")
            other.app = u"9.9.9"_s;
        else if (field == u"vault")
            other.vaultId = u"inny"_s;
        else {
            if (field == u"journalsFolder")
                config.journalsFolder = u"Dziennik"_s;
            else if (field == u"journalFileFormat")
                config.journalFileFormat = u"dd.MM.yyyy"_s;
            else
                config.defaultReminderTime = QTime(7, 30);
            other = indexcache::Identity::current(config, testIdentity().vaultId);
        }
        QString why;
        QVERIFY(!indexcache::Reader::open(file, other, &why));
        QVERIFY(!why.isEmpty());
        // Byte order mark swapped.
        QByteArray swapped = file;
        std::swap(swapped[8], swapped[11]);
        QVERIFY(!indexcache::Reader::open(swapped, testIdentity(), &why));
        QCOMPARE(why, u"byte order"_s);
    }

    void corruption_data()
    {
        QTest::addColumn<QString>("kind");
        for (const QString &k : {u"empty"_s, u"truncated"_s, u"flipped bytes"_s, u"random ranges"_s, u"huge lengths"_s,
                                 u"overlapping records"_s, u"record of another path"_s, u"deep nesting"_s})
            QTest::newRow(qPrintable(k)) << k;
    }
    void corruption()
    {
        QFETCH(QString, kind);
        const std::unique_ptr<VaultIndex> index = corpusIndex();
        const QByteArray file = indexcache::encode(*index, testIdentity(), fakeStat());
        const auto good = indexcache::Reader::open(file, testIdentity());
        QVERIFY(good);
        QVERIFY(checkDecodedNotes(*good) > 80);
        QRandomGenerator rng(2026);
        // Only the records the change touched can decode differently (the others are intact).
        auto check = [&](const QByteArray &bytes, qint64 from = 0, qint64 to = std::numeric_limits<qint64>::max()) {
            const auto reader = indexcache::Reader::open(bytes, testIdentity());
            if (!reader)
                return true;
            if (checkDecodedNotes(*reader, from, to) < 0)
                return false; // a note decoded differently from its parse
            indexcache::Reader::SearchDecoder decoder = reader->searchDecoder();
            decoder.step(std::numeric_limits<int>::max());
            decoder.take();
            return true;
        };
        if (kind == u"empty") {
            QVERIFY(!indexcache::Reader::open(QByteArray(), testIdentity()));
            QVERIFY(!indexcache::Reader::open(QByteArray("BRULIDX1"), testIdentity()));
        } else if (kind == u"truncated") {
            for (qsizetype n = 0; n < file.size(); n += 97)
                QVERIFY2(check(file.first(n), n, n), qPrintable(QString::number(n)));
        } else if (kind == u"flipped bytes") {
            for (int i = 0; i < 2000; ++i) {
                QByteArray bytes = file;
                const int flips = 1 + int(rng.bounded(4));
                qint64 from = bytes.size();
                qint64 to = 0;
                for (int k = 0; k < flips; ++k) {
                    const int at = int(rng.bounded(int(bytes.size())));
                    bytes[at] ^= char(1 << rng.bounded(8));
                    from = std::min<qint64>(from, at);
                    to = std::max<qint64>(to, at + 1);
                }
                QVERIFY2(check(bytes, from, to), qPrintable(QString::number(i)));
            }
        } else if (kind == u"random ranges") {
            for (int i = 0; i < 300; ++i) {
                QByteArray bytes = file;
                const int at = int(rng.bounded(int(bytes.size())));
                const int n = std::min(int(bytes.size()) - at, 1 + int(rng.bounded(64)));
                for (int k = 0; k < n; ++k)
                    bytes[at + k] = char(rng.bounded(256));
                QVERIFY2(check(bytes, at, at + n), qPrintable(QString::number(i)));
            }
        } else if (kind == u"huge lengths") {
            // Varints of 0xFFFFFFFF / 2^31 / ten 0xFF bytes written over every 50th byte.
            const QList<QByteArray> values = {QByteArray("\xff\xff\xff\xff\x0f"), QByteArray("\x80\x80\x80\x80\x08"),
                                              QByteArray(10, '\xff')};
            for (qsizetype at = 0; at < file.size(); at += 211) {
                for (const QByteArray &v : values) {
                    QByteArray bytes = file;
                    bytes.replace(at, std::min<qsizetype>(v.size(), bytes.size() - at), v);
                    QVERIFY2(check(bytes.left(file.size()), at, at + v.size()), qPrintable(QString::number(at)));
                }
            }
        } else if (kind == u"overlapping records" || kind == u"record of another path") {
            // Rewrite the directory (with a valid checksum): records overlap, or a path names
            // another note's record.
            const QList<std::pair<VaultPath, QString>> corpus = noteCorpus();
            const indexcache::Record *a = good->find(corpus[3].first);
            const indexcache::Record *b = good->find(corpus[4].first);
            QVERIFY(a && b && a->length > 0 && b->length > 0);
            if (kind == u"overlapping records") {
                // A forged reader: the record of b starting inside a's.
                indexcache::Record forged = *b;
                forged.offset = a->offset + 1;
                QVERIFY(!good->decodeNote(forged, forged.path, corpus[4].second, VaultConfig()));
            } else {
                indexcache::Record forged = *a;
                forged.path = b->path;
                // The record checks out, but it is a's note: decodeNote() refuses another path.
                QVERIFY(!good->decodeNote(forged, b->path, corpus[4].second, VaultConfig()));
            }
        } else if (kind == u"deep nesting") {
            // A record body of a frontmatter value nested 10 000 lists deep.
            QByteArray body;
            body.append(char(1)); // map of one key
            body.append(char(3)); // str "x": (1 << 1) + 1
            body.append('x');
            for (int i = 0; i < 10000; ++i) {
                body.append(char(6)); // list
                body.append(char(1)); // of one
            }
            body.append(char(0));
            QVERIFY(!indexcache::detail::decodeNote(body, u"a.md"_s, QString(), VaultConfig()));
            QVERIFY(!indexcache::detail::decodeSearch(QByteArray("\xff\xff\xff\xff\x0f")));
        }
    }

    // The writer gives up when the index changes between its steps, and writes no search section
    // while the full-text index is incomplete.
    void writerAborts()
    {
        std::unique_ptr<VaultIndex> index = corpusIndex();
        {
            indexcache::Writer writer(*index, testIdentity(), fakeStat());
            QByteArray out;
            QCOMPARE(writer.step(0, &out), indexcache::Writer::State::More);
            index->update(u"Nowa.md"_s, u"- zmiana w trakcie"_s);
            QCOMPARE(writer.step(1000, &out), indexcache::Writer::State::Aborted);
        }
        QVERIFY(index->pendingSearchDocuments() > 0);
        indexcache::Writer writer(*index, testIdentity(), fakeStat());
        QByteArray out;
        while (writer.step(1, &out) == indexcache::Writer::State::More) { }
        QVERIFY(!writer.hasSearch());
        const auto reader = indexcache::Reader::open(out, testIdentity());
        QVERIFY(reader);
        QVERIFY(!reader->hasSearch());
        QCOMPARE(reader->noteCount(), index->paths().size());
        QCOMPARE(writer.notesWritten(), index->paths().size());
        // A note without a stat (not in the vault's listing) gets no record.
        indexcache::Writer partial(*index, testIdentity(), [](const VaultPath &p) -> std::optional<FileStat> {
            if (p == u"Nowa.md")
                return std::nullopt;
            return FileStat{p, false, 1, 1};
        });
        out.clear();
        while (partial.step(100, &out) == indexcache::Writer::State::More) { }
        const auto r2 = indexcache::Reader::open(out, testIdentity());
        QVERIFY(r2);
        QCOMPARE(r2->find(u"Nowa.md"_s)->length, 0);
    }
};

QTEST_GUILESS_MAIN(TestIndexCache)
#include "tst_index_cache.moc"

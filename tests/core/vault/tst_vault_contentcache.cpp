// ContentCache (vault_p.h): the content the vault keeps for History snapshots — as bytes, or as the
// very text a reader got (shared with it, no second copy) when that text gives the bytes back.
// Also the vault around it: a note read as text is kept as text, and History still gets the exact
// old bytes when another program replaces it.
#include "text/text.h"
#include "vault/history.h"
#include "vault/vault.h"
#include "vault/vault_p.h"
#include "vault/vaultutil_p.h"
#include "vaulttest.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

quint64 keyOf(const QByteArray &bytes)
{
    return text::fnv1a64(bytes);
}

} // namespace

class TestVaultContentCache : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void textForm_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("asText");
        QTest::newRow("ascii") << "# Note\n- one\n"_ba << true;
        QTest::newRow("polish") << u"Zażółć gęślą jaźń\n- ąćęłńóśźż\n"_s.toUtf8() << true;
        QTest::newRow("crlf") << u"- zadanie\r\n- łódź\r\n"_s.toUtf8() << true;
        QTest::newRow("lone cr") << "a\rb\r"_ba << true;
        QTest::newRow("bom") << "\xEF\xBB\xBF# Tytuł\n"_ba << true;
        // QString::fromUtf8() drops a BOM too: two of them decode to a text without either.
        QTest::newRow("two boms") << "\xEF\xBB\xBF\xEF\xBB\xBFx"_ba << false;
        QTest::newRow("bom later") << "x\xEF\xBB\xBFy"_ba << true;
        QTest::newRow("bom only") << "\xEF\xBB\xBF"_ba << true;
        QTest::newRow("empty") << QByteArray() << true;
        QTest::newRow("nul byte") << QByteArray("a\0b", 3) << true;
        QTest::newRow("emoji") << u"✅ 📅 2026-11-25 🔁 every week\n"_s.toUtf8() << true;
        QTest::newRow("noncharacter") << u"￾￿"_s.toUtf8() << true;
        // Not UTF-8: kept as the bytes (decoding would lose them).
        QTest::newRow("latin-2") << "Za\xBF\xF3\xB3\xE6\n"_ba << false;
        QTest::newRow("truncated sequence") << "abc\xC5"_ba << false;
        QTest::newRow("overlong") << "\xC0\xAF"_ba << false;
        QTest::newRow("surrogate") << "\xED\xA0\x80"_ba << false;
        QTest::newRow("bom then invalid") << "\xEF\xBB\xBF\xFF"_ba << false;
    }
    void textForm()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, asText);
        ContentCache cache;
        const QString text = vaultutil::decodeUtf8(bytes);
        cache.put(u"n.md"_s, bytes, text, keyOf(bytes));
        QVERIFY(cache.contains(u"n.md"_s));
        QCOMPARE(cache.holdsText(u"n.md"_s), asText);
        const std::optional<QByteArray> back = cache.get(u"n.md"_s);
        QVERIFY(back);
        QCOMPARE(*back, bytes);
    }

    void sharesTheText()
    {
        ContentCache cache;
        const QByteArray bytes = u"- Kupić farbę\n"_s.toUtf8();
        const QString text = vaultutil::decodeUtf8(bytes);
        cache.put(u"a.md"_s, bytes, text, keyOf(bytes));
        // The cache holds a reference to the reader's text, not a copy of its own.
        QVERIFY(!text.isDetached());
    }

    void wrongTextIsNeverGivenAsBytes()
    {
        // A text that is not what the bytes decode to (same length): the key check refuses it
        // rather than give other bytes than the file had.
        ContentCache cache;
        const QByteArray bytes = "abc"_ba;
        cache.put(u"a.md"_s, bytes, u"abd"_s, keyOf(bytes));
        QVERIFY(!cache.get(u"a.md"_s));
        // A text longer than the bytes cannot be theirs: kept as the bytes.
        cache.put(u"b.md"_s, bytes, u"abcd"_s, keyOf(bytes));
        QVERIFY(!cache.holdsText(u"b.md"_s));
        QCOMPARE(*cache.get(u"b.md"_s), bytes);
        // Bytes with an empty text (or the other way round): the bytes.
        cache.put(u"c.md"_s, bytes, QString(), keyOf(bytes));
        QVERIFY(!cache.holdsText(u"c.md"_s));
        QCOMPARE(*cache.get(u"c.md"_s), bytes);
    }

    void bytesForm()
    {
        ContentCache cache;
        cache.put(u"x.bin"_s, "\x00\x01\xFF"_ba);
        QVERIFY(!cache.holdsText(u"x.bin"_s));
        QCOMPARE(*cache.get(u"x.bin"_s), "\x00\x01\xFF"_ba);
        cache.put(u"x.bin"_s, "nowe"_ba); // replaced
        QCOMPARE(*cache.get(u"x.bin"_s), "nowe"_ba);
        cache.remove(u"x.bin"_s);
        QVERIFY(!cache.contains(u"x.bin"_s));
        QVERIFY(!cache.get(u"x.bin"_s));
    }

    void takeAndRestore()
    {
        ContentCache cache;
        const QByteArray bytes = "\xEF\xBB\xBF\xC5\xBC\xC3\xB3\xC5\x82w\r\n"_ba; // BOM "żółw" CRLF
        const QString text = vaultutil::decodeUtf8(bytes);
        cache.put(u"stara.md"_s, bytes, text, keyOf(bytes));
        std::optional<ContentCache::Item> item = cache.take(u"stara.md"_s);
        QVERIFY(item);
        QVERIFY(!cache.contains(u"stara.md"_s));
        QVERIFY(!cache.take(u"stara.md"_s));
        cache.restore(u"nowa.md"_s, std::move(*item));
        QVERIFY(cache.holdsText(u"nowa.md"_s)); // as it was
        QCOMPARE(*cache.get(u"nowa.md"_s), bytes);
    }

    void evictionCountsTheFilesBytes()
    {
        // Entries kept as text count as many bytes as the file has: the same entries stay as with
        // bytes. The limit is 64 MiB; items over a quarter of it are never kept.
        ContentCache cache;
        const QByteArray big(ContentCache::kMaxItemBytes, 'a');
        const QString bigText = QString::fromLatin1(big);
        cache.put(u"too-big.md"_s, big + 'b', bigText + u'b', keyOf(big + 'b'));
        QVERIFY(!cache.contains(u"too-big.md"_s));
        for (int i = 0; i < 4; ++i)
            cache.put(u"%1.md"_s.arg(i), big, bigText, keyOf(big));
        QVERIFY(cache.holdsText(u"3.md"_s));
        QVERIFY(cache.contains(u"0.md"_s)); // exactly at the limit: nothing evicted
        cache.put(u"4.md"_s, "x"_ba, u"x"_s, keyOf("x"_ba));
        // Over the limit: the oldest go until three quarters of it are left.
        QVERIFY(!cache.contains(u"0.md"_s));
        QVERIFY(!cache.contains(u"1.md"_s));
        QVERIFY(cache.contains(u"2.md"_s));
        QVERIFY(cache.contains(u"4.md"_s));
        cache.clear();
        QVERIFY(!cache.contains(u"2.md"_s));
    }

    // The vault: notes read as text are kept as that text, and History gets the exact bytes.
    void readTextKeepsTheText_data()
    {
        QTest::addColumn<QByteArray>("old");
        QTest::addColumn<bool>("detached");
        QTest::newRow("bom crlf") << "\xEF\xBB\xBF# Zażółć\r\n- gęślą jaźń\r\n"_ba << false;
        QTest::newRow("lf") << "- [ ] zadanie 📅 2026-11-25\n"_ba << false;
        QTest::newRow("windows-1250") << "Za\xBF\xF3\xB3\xE6\n"_ba << true;
    }
    void readTextKeepsTheText()
    {
        QFETCH(QByteArray, old);
        QFETCH(bool, detached);
        vaulttest::Fixture f;
        f.write(u"notatka.md"_s, old);
        QVERIFY(f.open());
        bool ok = false;
        const QString text = f.vault->readText(u"notatka.md"_s, &ok);
        QVERIFY(ok);
        QCOMPARE(text, vaultutil::decodeUtf8(old));
        QCOMPARE(text.isDetached(), detached); // shared with the vault's cache when it gives the bytes back
        // Another program replaces it (a sync): History keeps the old bytes as they were.
        f.write(u"notatka.md"_s, "nowa treść\n"_ba);
        f.vault->rescan();
        const QList<HistoryEntry> entries = f.vault->history()->list(u"notatka.md"_s);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(f.vault->history()->readBytes(entries.first().id), old);
    }

    void renameKeepsTheText()
    {
        vaulttest::Fixture f;
        const QByteArray old = "- łódź\n"_ba;
        f.write(u"a.md"_s, old);
        QVERIFY(f.open());
        const QString text = f.vault->readText(u"a.md"_s);
        QVERIFY(f.vault->rename(u"a.md"_s, u"b/ćma.md"_s, ChangeSource::User));
        QVERIFY(!text.isDetached()); // moved with the file, still shared
        QVERIFY(f.vault->writeText(u"b/ćma.md"_s, u"nowa\n"_s));
        const QList<HistoryEntry> entries = f.vault->history()->list(u"b/ćma.md"_s);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(f.vault->history()->readBytes(entries.first().id), old);
    }
};

QTEST_GUILESS_MAIN(TestVaultContentCache)
#include "tst_vault_contentcache.moc"

// The formats of the encrypted sync (src/core/sync/encryption.h, docs/SYNC-ENCRYPTION.md): the keys
// from a notebook key, names (AES-SIV, base32), file contents in 64 KiB chunks (STREAM over AES-GCM)
// whole and in pieces, the sizes, and what a server could do to a file — cut it, extend it, swap or
// change its chunks — refused. The fixed values were made by an independent implementation of the
// same formats over OpenSSL (Python's `cryptography`) before they were written here.
#include "sync/encryption.h"

#include "crypto/aead.h"
#include "crypto/hash.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::sync::encryption;

namespace {

constexpr qsizetype Chunk = ChunkSize;

QByteArray hex(const char *text)
{
    return QByteArray::fromHex(text);
}

/// The notebook key of the fixed values: 0, 1, …, 31.
QByteArray notebookKey()
{
    QByteArray key;
    for (int i = 0; i < 32; ++i)
        key.append(char(i));
    return key;
}

/// A RandomSource giving 100, 101, … (the fixed values' file nonce).
crypto::RandomSource counting(int from = 100)
{
    return [n = from](char *out, qsizetype size) mutable {
        for (qsizetype i = 0; i < size; ++i)
            out[i] = char(n++);
    };
}

QByteArray pattern(qsizetype size, int seed = 0)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = char((i * 7919 + 13 + seed * 31) & 0xff);
    return out;
}

/// `sealed` decrypted in pieces of the given sizes (repeated), nullopt when refused.
std::optional<QByteArray> openInPieces(const Keys &keys, const QByteArray &sealed, const QList<qsizetype> &sizes)
{
    Decryptor decryptor(keys);
    QByteArray plain;
    qsizetype at = 0;
    for (int i = 0; at < sealed.size(); ++i) {
        const qsizetype n = std::min(sizes.at(i % sizes.size()), sealed.size() - at);
        const std::optional<QByteArray> piece = decryptor.add(QByteArrayView(sealed).sliced(at, n));
        if (!piece)
            return std::nullopt;
        plain += *piece;
        at += n;
    }
    const std::optional<QByteArray> last = decryptor.finish();
    if (!last)
        return std::nullopt;
    return plain + *last;
}

} // namespace

class TestSyncEncryption : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void keysFromTheNotebookKey()
    {
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        QCOMPARE(keys.names.toByteArray(),
                 hex("5b97c39236c597560ab1969684bc08e2d4df30913f134f9f8ef3c1b680f9863525718a5f7a3591b27021090f0355b02fb6"
                     "826a07fc782270dc3a8a594a405cd8"));
        QCOMPARE(keys.content.toByteArray(), hex("9fafe679724bb23fdbb3922f08a54480cb49214116485bf1322126253977853f"));
        QVERIFY(!Keys::fromNotebookKey(QByteArray(31, 'k')).isValid()); // (a notebook key is 32 bytes)
        QVERIFY(keys.isValid());
    }

    void names_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("encrypted");
        QTest::newRow("folder") << u"Dziennik"_s << u"qoz4ejjv7avkpwehdxllghvodubmsxc5rec2dui"_s;
        QTest::newRow("journal day") << u"2026-11-28.md"_s << u"dwv4bzuasnmtwsa62iu767kaxe5po3v72xui2b6h5zw7kxi"_s;
        QTest::newRow("Polish") << u"Zażółć gęślą jaźń.md"_s
                                << u"mvkrcbws5mmo6xyoj62ka5j6ofs7rx4yjehon7nmhl4krtlvrwu2sckfbzwvyutrytxywl46"_s;
    }

    void names()
    {
        QFETCH(QString, name);
        QFETCH(QString, encrypted);
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        QCOMPARE(encryptName(keys, name), std::optional<QString>(encrypted));
        QCOMPARE(decryptName(keys, encrypted), std::optional<QString>(name));
    }

    void namesThatCannotBeEncrypted()
    {
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        QVERIFY(!encryptName(keys, QString()));
        // 143 bytes of UTF-8 fit 255 characters, 144 do not.
        const QString fits = QString(71, u'ż') + u"a"_s;
        QCOMPARE(fits.toUtf8().size(), 143);
        QCOMPARE(encryptName(keys, fits)->size(), MaxEncryptedName);
        QVERIFY(!encryptName(keys, fits + u"b"_s));
        QCOMPARE(decryptName(keys, *encryptName(keys, fits)), std::optional<QString>(fits));
    }

    void foreignNamesDoNotDecrypt_data()
    {
        QTest::addColumn<QString>("name");
        const QString real = *encryptName(Keys::fromNotebookKey(notebookKey()), u"Notatka.md"_s);
        QTest::newRow("plain") << u"Notatka.md"_s;
        QTest::newRow("key file folder") << u".brulion"_s;
        QTest::newRow("readme") << u"README.txt"_s;
        QTest::newRow("upper case") << real.toUpper();
        QTest::newRow("one letter changed") << QString(real).replace(3, 1, real.at(3) == u'a' ? u"b"_s : u"a"_s);
        QTest::newRow("cut") << real.chopped(1);
        QTest::newRow("too short for an iv") << u"aaaaaaaa"_s;
        QTest::newRow("padding") << real + u"="_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("other key") << *encryptName(Keys::fromNotebookKey(QByteArray(32, 'x')), u"Notatka.md"_s);
    }

    void foreignNamesDoNotDecrypt()
    {
        QFETCH(QString, name);
        QVERIFY(!decryptName(Keys::fromNotebookKey(notebookKey()), name));
    }

    void paths()
    {
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        const std::optional<QString> path = encryptPath(keys, u"Dziennik/2026-11-28.md"_s);
        QCOMPARE(path, std::optional<QString>(u"qoz4ejjv7avkpwehdxllghvodubmsxc5rec2dui/"
                                              "dwv4bzuasnmtwsa62iu767kaxe5po3v72xui2b6h5zw7kxi"_s));
        QCOMPARE(decryptPath(keys, *path), std::optional<QString>(u"Dziennik/2026-11-28.md"_s));
        QCOMPARE(encryptPath(keys, QString()), std::optional<QString>(QString())); // the folder itself
        QVERIFY(!encryptPath(keys, u"a//b"_s));
        QVERIFY(!encryptPath(keys, u"a/"_s + QString(200, u'x')));
        QVERIFY(!decryptPath(keys, *path + u"/README.txt"_s));
    }

    void sizes_data()
    {
        QTest::addColumn<qint64>("plain");
        QTest::addColumn<qint64>("encrypted");
        QTest::newRow("empty") << qint64(0) << qint64(48);
        QTest::newRow("one byte") << qint64(1) << qint64(49);
        QTest::newRow("a chunk less one") << qint64(Chunk - 1) << qint64(32 + Chunk - 1 + 16);
        QTest::newRow("a chunk") << qint64(Chunk) << qint64(32 + Chunk + 16);
        QTest::newRow("a chunk and one") << qint64(Chunk + 1) << qint64(32 + Chunk + 1 + 32);
        QTest::newRow("three chunks") << qint64(3 * Chunk) << qint64(32 + 3 * Chunk + 48);
        QTest::newRow("200 MB") << qint64(200'000'000) << qint64(32 + 200'000'000 + 16 * 3052);
    }

    void sizes()
    {
        QFETCH(qint64, plain);
        QFETCH(qint64, encrypted);
        QCOMPARE(encryptedSize(plain), encrypted);
        QCOMPARE(plainSize(encrypted), std::optional<qint64>(plain));
    }

    void sizesNoFileHas()
    {
        for (qint64 size : {qint64(0), qint64(31), qint64(47), qint64(32 + Chunk + 16 + 1), qint64(32 + Chunk + 16 + 16),
                            qint64(-1)})
            QVERIFY2(!plainSize(size), qPrintable(QString::number(size)));
    }

    void fixedFiles_data()
    {
        QTest::addColumn<QByteArray>("plain");
        QTest::addColumn<QByteArray>("sealed");
        QTest::newRow("empty") << QByteArray()
                               << hex("42524c4e011000006465666768696a6b6c6d6e6f707172737475767778797a7b5ce643bd8570d048eb9c"
                                      "ea627566e804");
        QTest::newRow("a note") << "Zażółć gęślą jaźń\n"_ba
                                << hex("42524c4e011000006465666768696a6b6c6d6e6f707172737475767778797a7b3e465f7ac2268c09c2f8"
                                       "8854cdb8183f85b1e6fff218c93deea6eb16f1f0668963245e89f5d411a048a3b5");
    }

    void fixedFiles()
    {
        QFETCH(QByteArray, plain);
        QFETCH(QByteArray, sealed);
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        QCOMPARE(seal(keys, plain, counting()), sealed);
        const std::optional<crypto::SecureBytes> opened = open(keys, sealed);
        QVERIFY(opened);
        QCOMPARE(opened->toByteArray(), plain);
    }

    void aFileOfTwoChunks()
    {
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        const QByteArray plain = pattern(Chunk + 5);
        const QByteArray sealed = seal(keys, plain, counting());
        QCOMPARE(sealed.size(), 65605);
        QCOMPARE(QCryptographicHash::hash(sealed, QCryptographicHash::Sha256).toHex(),
                 "d7798271e43c24d54f241237a5a81b4f0b88ea9e8b73be678405c7c9b2d2cbce"_ba);
    }

    void everyUploadHasItsOwnNonce()
    {
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        const QByteArray a = seal(keys, "ta sama treść", {});
        const QByteArray b = seal(keys, "ta sama treść", {});
        QVERIFY(a != b);
        QCOMPARE(open(keys, a)->toByteArray(), open(keys, b)->toByteArray());
    }

    void inPieces_data()
    {
        QTest::addColumn<qsizetype>("size");
        QTest::addColumn<QList<qsizetype>>("encryptPieces");
        QTest::addColumn<QList<qsizetype>>("decryptPieces");
        QTest::newRow("empty") << qsizetype(0) << QList<qsizetype>{1} << QList<qsizetype>{7};
        QTest::newRow("small, byte by byte") << qsizetype(100) << QList<qsizetype>{1} << QList<qsizetype>{1};
        QTest::newRow("a chunk exactly") << Chunk << QList<qsizetype>{Chunk} << QList<qsizetype>{Chunk + 16};
        QTest::newRow("two chunks exactly, odd pieces") << 2 * Chunk << QList<qsizetype>{1000, 77777}
                                                        << QList<qsizetype>{3, 65552, 9999};
        QTest::newRow("200 KiB and 5") << qsizetype(200 * 1024 + 5) << QList<qsizetype>{256 * 1024} << QList<qsizetype>{256 * 1024};
        QTest::newRow("1 MiB in small pieces") << qsizetype(1024 * 1024) << QList<qsizetype>{4096, 13}
                                               << QList<qsizetype>{16384, 5};
    }

    void inPieces()
    {
        QFETCH(qsizetype, size);
        QFETCH(QList<qsizetype>, encryptPieces);
        QFETCH(QList<qsizetype>, decryptPieces);
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        const QByteArray plain = pattern(size, 1);
        Encryptor encryptor(keys, counting());
        QByteArray sealed = encryptor.header();
        qsizetype largest = 0;
        for (qsizetype at = 0, i = 0; at < plain.size(); ++i) {
            const qsizetype n = std::min(encryptPieces.at(i % encryptPieces.size()), plain.size() - at);
            const QByteArray piece = encryptor.add(QByteArrayView(plain).sliced(at, n));
            largest = std::max(largest, piece.size());
            sealed += piece;
            at += n;
        }
        sealed += encryptor.finish();
        // The same bytes as sealed whole; no piece larger than what was added and a chunk.
        QCOMPARE(sealed, seal(keys, plain, counting()));
        QCOMPARE(sealed.size(), encryptedSize(size));
        QVERIFY(largest <= encryptPieces.first() + 2 * (Chunk + TagSize));
        QCOMPARE(openInPieces(keys, sealed, decryptPieces), std::optional<QByteArray>(plain));
    }

    void whatAServerCouldDoIsRefused_data()
    {
        QTest::addColumn<int>("what");
        QTest::newRow("cut at the last chunk") << 0;
        QTest::newRow("cut inside a chunk") << 1;
        QTest::newRow("a byte added") << 2;
        QTest::newRow("two chunks swapped") << 3;
        QTest::newRow("a byte of the header changed") << 4;
        QTest::newRow("a byte of a chunk changed") << 5;
        QTest::newRow("another magic") << 6;
        QTest::newRow("another version") << 7;
        QTest::newRow("only the header") << 8;
        QTest::newRow("the first chunk of another file") << 9;
    }

    void whatAServerCouldDoIsRefused()
    {
        QFETCH(int, what);
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        QByteArray sealed = seal(keys, pattern(3 * Chunk + 100, 2), {});
        const qsizetype sealedChunk = Chunk + TagSize;
        switch (what) {
        case 0: sealed.chop(100 + TagSize); break; // the last chunk gone: the third is not the last
        case 1: sealed.chop(50); break;
        case 2: sealed.append('\0'); break;
        case 3: {
            const QByteArray first = sealed.mid(HeaderSize, sealedChunk);
            sealed.replace(HeaderSize, sealedChunk, sealed.mid(HeaderSize + sealedChunk, sealedChunk));
            sealed.replace(HeaderSize + sealedChunk, sealedChunk, first);
            break;
        }
        case 4: sealed[10] = char(sealed[10] ^ 1); break;
        case 5: sealed[HeaderSize + sealedChunk + 7] = char(sealed[HeaderSize + sealedChunk + 7] ^ 1); break;
        case 6: sealed[0] = 'b'; break;
        case 7: sealed[4] = 2; break;
        case 8: sealed.truncate(HeaderSize); break;
        case 9: {
            const QByteArray other = seal(keys, pattern(3 * Chunk + 100, 3), {});
            sealed.replace(HeaderSize, sealedChunk, other.mid(HeaderSize, sealedChunk));
            break;
        }
        }
        QVERIFY(!open(keys, sealed));
        QVERIFY(!openInPieces(keys, sealed, {4096}));
    }

    void anotherKeyOpensNothing()
    {
        const QByteArray sealed = seal(Keys::fromNotebookKey(notebookKey()), "sekret", {});
        QVERIFY(!open(Keys::fromNotebookKey(QByteArray(32, 'y')), sealed));
    }

    void aRefusedDecryptorStaysRefused()
    {
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        QByteArray sealed = seal(keys, pattern(3 * Chunk, 4), {});
        sealed[HeaderSize + 3] = char(sealed[HeaderSize + 3] ^ 1);
        Decryptor decryptor(keys);
        QVERIFY(decryptor.add(QByteArrayView(sealed).first(HeaderSize + Chunk + TagSize + 1)) == std::nullopt);
        QVERIFY(!decryptor.add(QByteArrayView(sealed).sliced(HeaderSize + Chunk + TagSize + 1)));
        QVERIFY(!decryptor.finish());
    }

    void anEmptyChunkAfterAFullOneIsRefused()
    {
        // Made by hand from the format (docs/SYNC-ENCRYPTION.md): a full first chunk that is not
        // the last, then an empty last one — what an encoder that does not hold a full chunk back
        // would write; only an empty file may end in an empty chunk.
        const Keys keys = Keys::fromNotebookKey(notebookKey());
        const QByteArray header = QByteArray("BRLN\x01\x10\x00\x00", 8) + QByteArray(24, '\x07');
        const crypto::SecureBytes fileKey =
                crypto::hkdf(crypto::Hash::Sha256, keys.content.view(), header.sliced(8), "brulion-sync file v1", 32);
        auto nonce = [](int index, bool last) {
            QByteArray n(12, '\0');
            n[10] = char(index);
            n[11] = last ? 1 : 0;
            return n;
        };
        const QByteArray plain = pattern(Chunk, 5);
        const QByteArray file = header + crypto::aesgcm::seal(fileKey.view(), nonce(0, false), header, plain)
                                + crypto::aesgcm::seal(fileKey.view(), nonce(1, true), header, {});
        QVERIFY(!open(keys, file));
        // The same chunk as the last one is the file, as Encryptor writes it.
        const QByteArray one = header + crypto::aesgcm::seal(fileKey.view(), nonce(0, true), header, plain);
        QCOMPARE(open(keys, one)->toByteArray(), plain);
    }
};

QTEST_GUILESS_MAIN(TestSyncEncryption)
#include "tst_sync_encryption.moc"

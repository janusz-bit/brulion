// The core module crypto (src/core/crypto): SHA-256 / SHA-512 (FIPS 180-4 examples), HMAC (RFC 4231),
// PBKDF2 (RFC 7914 §11 for SHA-256, the published SHA-512 vectors), AES-GCM (the test cases of the
// GCM specification — McGrew and Viega, also NIST's CAVS — for 128, 192 and 256-bit keys and IVs of
// 8, 12 and 60 bytes), refused tags and inputs, Base64 strict and forgiving, wiped memory, the
// constant-time comparison and the stored hashes of a PIN. Every expected value was checked against
// another implementation (OpenSSL through Node's crypto) before it was written here.
#include "crypto/crypto.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::crypto;

namespace {

QByteArray hex(const char *text)
{
    return QByteArray::fromHex(text);
}

QByteArray repeatedByte(char c, qsizetype n)
{
    return QByteArray(n, c);
}

/// A RandomSource giving 0, 1, 2, … (tests that need fixed salts).
RandomSource counting()
{
    return [n = 0](char *out, qsizetype size) mutable {
        for (qsizetype i = 0; i < size; ++i)
            out[i] = char(n++);
    };
}

} // namespace

class TestCrypto : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void sha_data();
    void sha();
    void shaInPieces();
    void hmacVectors_data();
    void hmacVectors();
    void pbkdf2Vectors_data();
    void pbkdf2Vectors();
    void pbkdf2Edges();
    void gcmVectors_data();
    void gcmVectors();
    void gcmRefusesChanges();
    void gcmRefusesBadInput();
    void gcmRoundTripPolish();
    void base64_data();
    void base64();
    void base64Encode();
    void constantTime();
    void secureBytes();
    void wipeStrings();
    void secretHash();
    void secretHashMalformed_data();
    void secretHashMalformed();
    void randomSource();
};

void TestCrypto::sha_data()
{
    QTest::addColumn<int>("hash");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QByteArray>("expected");
    const int s256 = int(Hash::Sha256);
    const int s512 = int(Hash::Sha512);
    QTest::newRow("sha256 empty") << s256 << QByteArray()
                                  << hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    QTest::newRow("sha256 abc") << s256 << "abc"_ba
                                << hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    QTest::newRow("sha256 448 bits") << s256 << "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"_ba
                                     << hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    QTest::newRow("sha256 million a") << s256 << repeatedByte('a', 1000000)
                                      << hex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    QTest::newRow("sha512 empty") << s512 << QByteArray()
                                  << hex("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f"
                                         "2b0ff8318d2877eec2f63b931bd"
                                         "47417a81a538327af927da3e");
    QTest::newRow("sha512 abc") << s512 << "abc"_ba
                                << hex("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a"
                                       "836ba3c23a3feebbd454d44"
                                       "23643ce80e2a9ac94fa54ca49f");
    QTest::newRow("sha512 896 bits") << s512
                                     << "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmno"
                                        "pqklmnopqrlmnopqrsmnopqrstnop"
                                        "qrstu"_ba
                                     << hex("8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e49"
                                            "00f7e4331b99dec4b5433ac7d329"
                                            "eeb6dd26545e96e55b874be909");
    QTest::newRow("sha512 million a") << s512 << repeatedByte('a', 1000000)
                                      << hex("e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff2448"
                                             "77ea60a4cb0432ce577c31beb009c"
                                             "5c2c49aa2e4eadb217ad8cc09b");
}

void TestCrypto::sha()
{
    QFETCH(int, hash);
    QFETCH(QByteArray, data);
    QFETCH(QByteArray, expected);
    QCOMPARE(digest(Hash(hash), data), expected);
    QCOMPARE(digestSize(Hash(hash)), expected.size());
}

void TestCrypto::shaInPieces()
{
    // Pieces across block boundaries give the one-shot digest; result() can be read twice.
    const QByteArray data = repeatedByte('a', 1000000);
    for (const Hash hash : {Hash::Sha256, Hash::Sha512}) {
        Hasher h(hash);
        qsizetype at = 0;
        qsizetype step = 1;
        while (at < data.size()) {
            const qsizetype n = qMin(step, data.size() - at);
            h.add(QByteArrayView(data).sliced(at, n));
            at += n;
            step = step * 3 + 1;
        }
        QCOMPARE(h.result(), digest(hash, data));
        QCOMPARE(h.result(), digest(hash, data));
    }
}

void TestCrypto::hmacVectors_data()
{
    QTest::addColumn<QByteArray>("key");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QByteArray>("sha256");
    QTest::addColumn<QByteArray>("sha512");
    // RFC 4231, test cases 1, 2, 3 and 6 (a key longer than the block).
    QTest::newRow("case 1")
            << repeatedByte('\x0b', 20) << "Hi There"_ba
            << hex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")
            << hex("87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274e"
                   "aea3f4e4be9d914eeb61f1702e696c203a126854");
    QTest::newRow("case 2")
            << "Jefe"_ba << "what do ya want for nothing?"_ba
            << hex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")
            << hex("164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65"
                   "f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
    QTest::newRow("case 3")
            << repeatedByte('\xaa', 20) << repeatedByte('\xdd', 50)
            << hex("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe")
            << hex("fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39bf3e848279a722c806b485a4"
                   "7e67c807b946a337bee8942674278859e13292fb");
    QTest::newRow("case 6")
            << repeatedByte('\xaa', 131) << "Test Using Larger Than Block-Size Key - Hash Key First"_ba
            << hex("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54")
            << hex("80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd221"
                   "5d6a1e5295e64f73f63f0aec8b915a985d786598");
}

void TestCrypto::hmacVectors()
{
    QFETCH(QByteArray, key);
    QFETCH(QByteArray, data);
    QFETCH(QByteArray, sha256);
    QFETCH(QByteArray, sha512);
    QCOMPARE(hmac(Hash::Sha256, key, data), sha256);
    QCOMPARE(hmac(Hash::Sha512, key, data), sha512);
}

void TestCrypto::pbkdf2Vectors_data()
{
    QTest::addColumn<int>("hash");
    QTest::addColumn<QByteArray>("password");
    QTest::addColumn<QByteArray>("salt");
    QTest::addColumn<quint32>("iterations");
    QTest::addColumn<QByteArray>("expected");
    const int s256 = int(Hash::Sha256);
    const int s512 = int(Hash::Sha512);
    // RFC 7914 §11.
    QTest::newRow("sha256 passwd") << s256 << "passwd"_ba << "salt"_ba << 1u
                                   << hex("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179"
                                          "b645991664b39d77ef317c71b8"
                                          "45b1e30bd509112041d3a19783");
    QTest::newRow("sha256 Password NaCl 80000") << s256 << "Password"_ba << "NaCl"_ba << 80000u
                                                << hex("4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab5"
                                                       "6a1d425a1225833549adb841b51c9b3176a272b"
                                                       "debba1d078478f62b397f33c8d");
    // The SHA-512 vectors published with RFC 6070's inputs.
    QTest::newRow("sha512 c=1") << s512 << "password"_ba << "salt"_ba << 1u
                                << hex("867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252c02d470a285a050"
                                       "1bad999bfe943c08f050235"
                                       "d7d68b1da55e63f73b60a57fce");
    QTest::newRow("sha512 c=2") << s512 << "password"_ba << "salt"_ba << 2u
                                << hex("e1d9c16aa681708a45f5c7c4e215ceb66e011a2e9f0040713f18aefdb866d53cf76cab2868a39b9"
                                       "f7840edce4fef5a82be6733"
                                       "5c77a6068e04112754f27ccf4e");
    QTest::newRow("sha512 c=4096") << s512 << "password"_ba << "salt"_ba << 4096u
                                   << hex("d197b1b33db0143e018b12f3d1d1479e6cdebdcc97c5c0f87f6902e072f457b5143f30602641"
                                          "b3d55cd335988cb36b843760"
                                          "60ecd532e039b742a239434af2d5");
    QTest::newRow("sha512 long password and salt")
            << s512 << "passwordPASSWORDpassword"_ba << "saltSALTsaltSALTsaltSALTsaltSALTsalt"_ba << 4096u
            << hex("8c0511f4c6e597c6ac6315d8f0362e225f3c501495ba23b868c005174dc4ee71115b59f9e60cd9532fa33e0f75aefe30225"
                   "c58"
                   "3a186cd82bd4daea9724a3d3b8");
    // Two blocks (100 bytes) and Polish UTF-8 (checked against OpenSSL).
    QTest::newRow("sha512 100 bytes") << s512 << "password"_ba << "salt"_ba << 2u
                                      << hex("e1d9c16aa681708a45f5c7c4e215ceb66e011a2e9f0040713f18aefdb866d53cf76cab286"
                                             "8a39b9f7840edce4fef5a82be6733"
                                             "5c77a6068e04112754f27ccf4e473e311ad827b68945f4e2dddb204c78e40e2495141e411"
                                             "cd272d020640d673cd34aa29f");
    QTest::newRow("sha512 polish") << s512 << u"hasło-żółw 🔑"_s.toUtf8() << u"sól morska"_s.toUtf8() << 1000u
                                   << hex("6c3e43700e0ac3fc9c090978dbce21724f107f4f082bded6d7f50a8012d1f22c");
}

void TestCrypto::pbkdf2Vectors()
{
    QFETCH(int, hash);
    QFETCH(QByteArray, password);
    QFETCH(QByteArray, salt);
    QFETCH(quint32, iterations);
    QFETCH(QByteArray, expected);
    const SecureBytes key = pbkdf2(Hash(hash), password, salt, iterations, expected.size());
    QCOMPARE(key.toByteArray(), expected);
}

void TestCrypto::pbkdf2Edges()
{
    QVERIFY(pbkdf2(Hash::Sha512, "p", "s", 0, 32).isEmpty());
    QVERIFY(pbkdf2(Hash::Sha512, "p", "s", 1, 0).isEmpty());
    // An empty password and an empty salt are allowed (WebCrypto takes them too).
    QCOMPARE(pbkdf2(Hash::Sha256, {}, {}, 1, 32).size(), 32);
    // A shorter key is the longer one's start.
    const SecureBytes longer = pbkdf2(Hash::Sha512, "pw", "salt", 3, 64);
    const SecureBytes shorter = pbkdf2(Hash::Sha512, "pw", "salt", 3, 20);
    QCOMPARE(shorter.toByteArray(), longer.toByteArray().left(20));
}

void TestCrypto::gcmVectors_data()
{
    QTest::addColumn<QByteArray>("key");
    QTest::addColumn<QByteArray>("iv");
    QTest::addColumn<QByteArray>("aad");
    QTest::addColumn<QByteArray>("plaintext");
    QTest::addColumn<QByteArray>("ciphertext");
    QTest::addColumn<QByteArray>("tag");
    const QByteArray k256 = hex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const QByteArray p =
            hex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525"
                "b16aedf5aa0de657ba637b391aafd255");
    const QByteArray p60 = p.left(60);
    const QByteArray a = hex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    QTest::newRow("case 13: empty") << QByteArray(32, '\0') << QByteArray(12, '\0') << QByteArray() << QByteArray()
                                    << QByteArray() << hex("530f8afbc74536b9a963b4f1c4cb738b");
    QTest::newRow("case 14: zero block") << QByteArray(32, '\0') << QByteArray(12, '\0') << QByteArray()
                                         << QByteArray(16, '\0') << hex("cea7403d4d606b6e074ec5d3baf39d18")
                                         << hex("d0d1c8a799996bf0265b98b5d48ab919");
    QTest::newRow("case 15: 64 bytes") << k256 << hex("cafebabefacedbaddecaf888") << QByteArray() << p
                                       << hex("522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48"
                                              "590dbb3da7b08b1056828838c5f61e"
                                              "6393ba7a0abcc9f662898015ad")
                                       << hex("b094dac5d93471bdec1a502270e3cc6c");
    QTest::newRow("case 16: aad") << k256 << hex("cafebabefacedbaddecaf888") << a << p60
                                  << hex("522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590db"
                                         "b3da7b08b1056828838c5f61e"
                                         "6393ba7a0abcc9f662")
                                  << hex("76fc6ece0f4e1768cddf8853bb2d551b");
    QTest::newRow("case 17: 8-byte iv") << k256 << hex("cafebabefacedbad") << a << p60
                                        << hex("c3762df1ca787d32ae47c13bf19844cbaf1ae14d0b976afac52ff7d79bba9de0feb582d"
                                               "33934a4f0954cc2363bc73f7862ac43"
                                               "0e64abe499f47c9b1f")
                                        << hex("3a337dbf46a792c45e454913fe2ea8f2");
    QTest::newRow("case 18: 60-byte iv") << k256
                                         << hex("9313225df88406e555909c5aff5269aa6a7a9538534f7da1e4c303d2a318a728c3c0c9"
                                                "5156809539fcf0e2429a6b525416aedbf5"
                                                "a0de6a57a637b39b")
                                         << a << p60
                                         << hex("5a8def2f0c9e53f1f75d7853659e2a20eeb2b22aafde6419a058ab4f6f746bf40fc0c3"
                                                "b780f244452da3ebf1c5d82cdea24189"
                                                "97200ef82e44ae7e3f")
                                         << hex("a44a8266ee1c8eb0c8b5d4cf5ae9f19a");
    QTest::newRow("case 4: aes-128") << hex("feffe9928665731c6d6a8f9467308308") << hex("cafebabefacedbaddecaf888") << a
                                     << p60
                                     << hex("42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b254"
                                            "66931c7d8f6a5aac84aa051ba30b"
                                            "396a0aac973d58e091")
                                     << hex("5bc94fbc3221a5db94fae95ae7121a47");
    QTest::newRow("case 10: aes-192") << hex("feffe9928665731c6d6a8f9467308308feffe9928665731c")
                                      << hex("cafebabefacedbaddecaf888") << a << p60
                                      << hex("3980ca0b3c00e841eb06fac4872a2757859e1ceaa6efd984628593b40ca1e19c7d773d00c"
                                             "144c525ac619d18c84a3f4718e244"
                                             "8b2fe324d9ccda2710")
                                      << hex("2519498e80f1478f37ba55bd6d27618c");
}

void TestCrypto::gcmVectors()
{
    QFETCH(QByteArray, key);
    QFETCH(QByteArray, iv);
    QFETCH(QByteArray, aad);
    QFETCH(QByteArray, plaintext);
    QFETCH(QByteArray, ciphertext);
    QFETCH(QByteArray, tag);
    const QByteArray sealed = aesgcm::seal(key, iv, aad, plaintext);
    QCOMPARE(sealed, ciphertext + tag);
    const auto opened = aesgcm::open(key, iv, aad, sealed);
    QVERIFY(opened);
    QCOMPARE(opened->toByteArray(), plaintext);
}

void TestCrypto::gcmRefusesChanges()
{
    const QByteArray key = hex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const QByteArray iv = hex("cafebabefacedbaddecaf888");
    const QByteArray aad = "nagłówek"_ba;
    const QByteArray sealed = aesgcm::seal(key, iv, aad, u"Zażółć gęślą jaźń"_s.toUtf8());
    QVERIFY(aesgcm::open(key, iv, aad, sealed));
    // Every flipped bit of ciphertext or tag, another key, IV or AAD, a cut tag: refused.
    for (qsizetype i = 0; i < sealed.size(); ++i) {
        QByteArray changed = sealed;
        changed[i] = char(changed[i] ^ 0x01);
        QVERIFY2(!aesgcm::open(key, iv, aad, changed), qPrintable(u"byte %1"_s.arg(i)));
    }
    QByteArray otherKey = key;
    otherKey[31] = char(otherKey[31] ^ 0x80);
    QVERIFY(!aesgcm::open(otherKey, iv, aad, sealed));
    QVERIFY(!aesgcm::open(key, hex("cafebabefacedbaddecaf889"), aad, sealed));
    QVERIFY(!aesgcm::open(key, iv, "naglowek"_ba, sealed));
    QVERIFY(!aesgcm::open(key, iv, {}, sealed));
    QVERIFY(!aesgcm::open(key, iv, aad, sealed.left(sealed.size() - 1)));
    QVERIFY(!aesgcm::open(key, iv, aad, sealed.left(aesgcm::TagSize - 1)));
}

void TestCrypto::gcmRefusesBadInput()
{
    QVERIFY(aesgcm::seal(QByteArray(31, 'k'), "iv", {}, "x").isEmpty());
    QVERIFY(aesgcm::seal(QByteArray(32, 'k'), {}, {}, "x").isEmpty());
    QVERIFY(!aesgcm::open(QByteArray(33, 'k'), "iv", {}, QByteArray(20, 'x')));
    QVERIFY(!aesgcm::open(QByteArray(32, 'k'), {}, {}, QByteArray(20, 'x')));
    QVERIFY(!aesgcm::open(QByteArray(32, 'k'), "iv", {}, {}));
    QVERIFY(aesgcm::isKeySize(16) && aesgcm::isKeySize(24) && aesgcm::isKeySize(32));
    QVERIFY(!aesgcm::isKeySize(0) && !aesgcm::isKeySize(15) && !aesgcm::isKeySize(64));
}

void TestCrypto::gcmRoundTripPolish()
{
    const QByteArray key = randomBytes(32);
    const QByteArray iv = randomBytes(16); // Meld Encrypt's older 16-byte IVs
    const QString text = u"Tajny przepis: 2 łyżki miodu, ćwierć szklanki żółtego sera 🧀\nDruga linia"_s;
    const QByteArray sealed = aesgcm::seal(key, iv, {}, text.toUtf8());
    QCOMPARE(sealed.size(), text.toUtf8().size() + aesgcm::TagSize);
    QVERIFY(!sealed.contains(text.toUtf8()));
    const auto opened = aesgcm::open(key, iv, {}, sealed);
    QVERIFY(opened);
    QCOMPARE(opened->toUtf8String(), text);
    // Long text across many blocks.
    const QByteArray big = QByteArray(100000, 'z') + "koniec";
    const auto bigOpened = aesgcm::open(key, iv, "a", aesgcm::seal(key, iv, "a", big));
    QVERIFY(bigOpened);
    QCOMPARE(bigOpened->toByteArray(), big);
}

void TestCrypto::base64_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("strictOk");
    QTest::addColumn<bool>("forgivingOk");
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("empty") << QString() << true << true << QByteArray();
    QTest::newRow("one byte") << u"YQ=="_s << true << true << "a"_ba;
    QTest::newRow("two bytes") << u"YWI="_s << true << true << "ab"_ba;
    QTest::newRow("three bytes") << u"YWJj"_s << true << true << "abc"_ba;
    QTest::newRow("unpadded") << u"YQ"_s << false << true << "a"_ba;
    QTest::newRow("white space") << u" YW\nJj\t"_s << false << true << "abc"_ba;
    QTest::newRow("stray bits") << u"YR=="_s << false << true << "a"_ba;
    QTest::newRow("url alphabet") << u"-_8="_s << false << false << QByteArray();
    QTest::newRow("one char") << u"Y"_s << false << false << QByteArray();
    QTest::newRow("five chars") << u"YWJjZ"_s << false << false << QByteArray();
    QTest::newRow("too much padding") << u"YQ==="_s << false << false << QByteArray();
    QTest::newRow("padding inside") << u"YQ==YQ=="_s << false << false << QByteArray();
    QTest::newRow("polish letter") << u"YWłj"_s << false << false << QByteArray();
    QTest::newRow("all values") << u"+/+/"_s << true << true << hex("fbffbf");
}

void TestCrypto::base64()
{
    QFETCH(QString, text);
    QFETCH(bool, strictOk);
    QFETCH(bool, forgivingOk);
    QFETCH(QByteArray, bytes);
    const auto strict = decodeBase64(text, Base64Mode::Strict);
    QCOMPARE(strict.has_value(), strictOk);
    if (strict)
        QCOMPARE(*strict, bytes);
    const auto forgiving = decodeBase64(text, Base64Mode::Forgiving);
    QCOMPARE(forgiving.has_value(), forgivingOk);
    if (forgiving)
        QCOMPARE(*forgiving, bytes);
}

void TestCrypto::base64Encode()
{
    for (int n = 0; n < 70; ++n) {
        const QByteArray bytes = randomBytes(n);
        const QString text = encodeBase64(bytes);
        QCOMPARE(text, QString::fromLatin1(bytes.toBase64()));
        QCOMPARE(decodeBase64(text).value_or("x"_ba), bytes);
    }
}

void TestCrypto::constantTime()
{
    QVERIFY(constantTimeEquals({}, {}));
    QVERIFY(constantTimeEquals("abc", "abc"));
    QVERIFY(!constantTimeEquals("abc", "abd"));
    QVERIFY(!constantTimeEquals("abc", "ab"));
    QVERIFY(!constantTimeEquals("\x80", "\x00"));
}

void TestCrypto::secureBytes()
{
    SecureBytes a = SecureBytes::fromUtf8(u"hasło"_s);
    QCOMPARE(a.toByteArray(), u"hasło"_s.toUtf8());
    QCOMPARE(a.toUtf8String(), u"hasło"_s);
    SecureBytes b = a; // a deep copy
    QVERIFY(b.data() != a.data());
    QVERIFY(a == b);
    b.append("!");
    QVERIFY(!(a == b));
    QCOMPARE(b.toByteArray(), u"hasło!"_s.toUtf8());
    b.resize(2);
    QCOMPARE(b.toByteArray(), "ha"_ba);
    b.resize(4);
    QCOMPARE(b.toByteArray(), QByteArray("ha\0\0", 4));
    SecureBytes c = std::move(b);
    QVERIFY(b.isEmpty());
    QCOMPARE(c.size(), 4);
    c.clear();
    QVERIFY(c.isEmpty());
    QVERIFY(SecureBytes(qsizetype(0)).isEmpty());
    // A lone surrogate is written as U+FFFD, as TextEncoder does.
    const QString broken = QString(QChar(0xD800)) + u'a';
    QCOMPARE(SecureBytes::fromUtf8(broken).toByteArray(), "\xEF\xBF\xBD"
                                                          "a"_ba);
    // wipe() zeroes in place.
    char buffer[8] = {'s', 'e', 'k', 'r', 'e', 't', '!', '?'};
    wipe(buffer, sizeof buffer);
    for (const char ch : buffer)
        QCOMPARE(ch, '\0');
}

void TestCrypto::wipeStrings()
{
    QString text = u"tajne żółwie"_s;
    text.detach();
    const QChar *data = text.constData();
    const qsizetype size = text.size();
    // (Read through a copy of the buffer's address: the string is cleared afterwards.)
    QString shared = u"inne"_s;
    QString copy = shared;
    wipe(shared); // shared: only cleared, the copy keeps its text
    QVERIFY(shared.isEmpty());
    QCOMPARE(copy, u"inne"_s);
    Q_UNUSED(data);
    Q_UNUSED(size);
    wipe(text);
    QVERIFY(text.isEmpty());
    QByteArray bytes = "sekret"_ba;
    bytes.detach();
    wipe(bytes);
    QVERIFY(bytes.isEmpty());
}

void TestCrypto::secretHash()
{
    const QString stored = hashSecret(u"1234"_s, counting(), MinSecretIterations);
    QVERIFY(stored.startsWith(u"pbkdf2-sha512$10000$"_s));
    // The salt is the source's 16 bytes 0 … 15.
    const auto parsed = parseSecretHash(stored);
    QVERIFY(parsed);
    QCOMPARE(parsed->iterations, MinSecretIterations);
    QCOMPARE(parsed->salt, hex("000102030405060708090a0b0c0d0e0f"));
    QCOMPARE(parsed->hash, pbkdf2(Hash::Sha512, "1234", parsed->salt, MinSecretIterations, 32).toByteArray());
    QVERIFY(verifySecret(stored, u"1234"_s));
    QVERIFY(!verifySecret(stored, u"1235"_s));
    QVERIFY(!verifySecret(stored, u"12345"_s));
    QVERIFY(!verifySecret(stored, QString()));
    // Polish letters (a passphrase): UTF-8.
    const QString polish = hashSecret(u"żółć"_s, {}, MinSecretIterations);
    QVERIFY(verifySecret(polish, u"żółć"_s));
    QVERIFY(!verifySecret(polish, u"zolc"_s));
    // Two hashes of one secret differ (random salts).
    QVERIFY(hashSecret(u"1234"_s, {}, MinSecretIterations) != hashSecret(u"1234"_s, {}, MinSecretIterations));
    // Refused: empty secrets, rounds out of bounds.
    QVERIFY(hashSecret(QString()).isEmpty());
    QVERIFY(hashSecret(u"1"_s, {}, MinSecretIterations - 1).isEmpty());
    QVERIFY(hashSecret(u"1"_s, {}, MaxSecretIterations + 1).isEmpty());
}

void TestCrypto::secretHashMalformed_data()
{
    QTest::addColumn<QString>("stored");
    const QString salt = encodeBase64(QByteArray(16, 's'));
    const QString hash = encodeBase64(QByteArray(32, 'h'));
    QTest::newRow("empty") << QString();
    QTest::newRow("other scheme") << u"pbkdf2-sha256$10000$%1$%2"_s.arg(salt, hash);
    QTest::newRow("too few rounds") << u"pbkdf2-sha512$9999$%1$%2"_s.arg(salt, hash);
    QTest::newRow("too many rounds") << u"pbkdf2-sha512$10000001$%1$%2"_s.arg(salt, hash);
    QTest::newRow("leading zero") << u"pbkdf2-sha512$010000$%1$%2"_s.arg(salt, hash);
    QTest::newRow("sign") << u"pbkdf2-sha512$+10000$%1$%2"_s.arg(salt, hash);
    QTest::newRow("short salt") << u"pbkdf2-sha512$10000$%1$%2"_s.arg(encodeBase64("abc"), hash);
    QTest::newRow("short hash") << u"pbkdf2-sha512$10000$%1$%2"_s.arg(salt, encodeBase64("abc"));
    QTest::newRow("bad base64") << u"pbkdf2-sha512$10000$%1$%2"_s.arg(u"!!!!"_s, hash);
    QTest::newRow("extra part") << u"pbkdf2-sha512$10000$%1$%2$x"_s.arg(salt, hash);
    QTest::newRow("huge number") << u"pbkdf2-sha512$99999999999999$%1$%2"_s.arg(salt, hash);
}

void TestCrypto::secretHashMalformed()
{
    QFETCH(QString, stored);
    QVERIFY(!parseSecretHash(stored));
    QVERIFY(!verifySecret(stored, u"1234"_s));
}

void TestCrypto::randomSource()
{
    const QByteArray a = randomBytes(32);
    const QByteArray b = randomBytes(32);
    QCOMPARE(a.size(), 32);
    QVERIFY(a != b);
    QCOMPARE(randomBytes(7, counting()), hex("00010203040506"));
    QVERIFY(randomBytes(0).isEmpty());
    QVERIFY(randomBytes(-3).isEmpty());
    // Odd sizes from the system source.
    for (qsizetype n = 1; n < 10; ++n)
        QCOMPARE(randomBytes(n).size(), n);
}

QTEST_GUILESS_MAIN(TestCrypto)
#include "tst_crypto.moc"

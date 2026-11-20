// The core module encryption (src/core/encryption) and the syntax of encrypted fragments
// (markdown/encrypted.h): every format Meld Encrypt wrote, read from fixtures the plugin itself made
// (meldfixtures.h); what Brulion writes equal byte for byte to the plugin's own output for the same
// random bytes; wrong passwords; malformed, tampered and hostile payloads and files; the fragments'
// markers in text; the edits (encrypt, save again, change the password, decrypt in place) and the
// session's keyring. Polish text and passwords throughout.
#include "encryption/encryption.h"
#include "markdown/encrypted.h"

#include "crypto/base64.h"

#include "meldfixtures.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::encryption;
using crypto::SecureBytes;
using markdown::EncryptedSpan;
using markdown::EncryptedVersion;

Q_DECLARE_METATYPE(brulion::markdown::EncryptedVersion)
Q_DECLARE_METATYPE(brulion::encryption::Failure)

namespace {

SecureBytes pw(QStringView text)
{
    return SecureBytes::fromUtf8(text);
}

/// 0, 1, 2, … as Meld Encrypt's fixtures were made with (meldfixtures::Deterministic*).
crypto::RandomSource counting()
{
    return [n = 0](char *out, qsizetype size) mutable {
        for (qsizetype i = 0; i < size; ++i)
            out[i] = char(n++);
    };
}

/// A v3 payload whose bytes were changed by `edit`, Base64 again.
QString editedV3(QStringView payload, const std::function<void(QByteArray &)> &edit)
{
    QByteArray bytes = crypto::decodeBase64(payload).value();
    edit(bytes);
    return crypto::encodeBase64(bytes);
}

/// The file `json` with the member at `path` ("kdf.iterations") replaced by `value`.
QString editedFile(QStringView json, const QString &path, const QJsonValue &value)
{
    QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    const QStringList parts = path.split(u'.');
    if (parts.size() == 1) {
        if (value.isUndefined())
            root.remove(parts[0]);
        else
            root.insert(parts[0], value);
    } else {
        QJsonObject inner = root.value(parts[0]).toObject();
        if (value.isUndefined())
            inner.remove(parts[1]);
        else
            inner.insert(parts[1], value);
        root.insert(parts[0], inner);
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace

class TestEncryption : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // The plugin's formats.
    void meldInline_data();
    void meldInline();
    void meldInlineWrongPassword_data();
    void meldInlineWrongPassword();
    void meldNotes_data();
    void meldNotes();
    void legacyFixtures();
    void writesWhatThePluginWrites();
    void roundTrips_data();
    void roundTrips();
    void malformedPayloads_data();
    void malformedPayloads();
    void malformedFiles_data();
    void malformedFiles();
    void hintIsPublicButParametersAreNot();
    void blankFiles();
    void openNotesKeepTheirKey();
    void iterationsOfPayloads();
    void failureMessages();
    // Fragments in text.
    void findFragments_data();
    void findFragments();
    void fragmentTextAndHints();
    void maskForSearch();
    void notePaths();
    // Edits.
    void encryptAndDecryptFragments();
    void reencryptUpgradesAndKeepsLook();
    void changePassword();
    void decryptInPlace();
    void ciphertextHidesThePlaintext();
    // Keyring.
    void keyringRemembersPerPage();
    void keyringForgetsAfterInactivity();
    void keyringFollowsRenames();
};

void TestEncryption::meldInline_data()
{
    QTest::addColumn<EncryptedVersion>("version");
    QTest::addColumn<QString>("payload");
    QTest::addColumn<QString>("text");
    QTest::newRow("v0 obsolete") << EncryptedVersion::Obsolete << meldfixtures::InlineV0.toString()
                                 << meldfixtures::Text.toString();
    QTest::newRow("v1 alpha") << EncryptedVersion::Alpha << meldfixtures::InlineV1.toString()
                              << meldfixtures::Text.toString();
    QTest::newRow("v2 beta") << EncryptedVersion::Beta << meldfixtures::InlineV2.toString()
                             << meldfixtures::Text.toString();
    QTest::newRow("v3 gamma") << EncryptedVersion::Gamma << meldfixtures::InlineV3.toString()
                              << meldfixtures::Text.toString();
    QTest::newRow("v3 empty text") << EncryptedVersion::Gamma << meldfixtures::InlineV3Empty.toString() << QString();
    QTest::newRow("v3 300000 rounds") << EncryptedVersion::Gamma << meldfixtures::InlineV3Iter300k.toString()
                                      << meldfixtures::Text.toString();
}

void TestEncryption::meldInline()
{
    QFETCH(EncryptedVersion, version);
    QFETCH(QString, payload);
    QFETCH(QString, text);
    const Decrypted d = decryptPayload(version, payload, pw(meldfixtures::Password));
    QCOMPARE(d.failure, Failure::None);
    QCOMPARE(d.text, text);
}

void TestEncryption::meldInlineWrongPassword_data()
{
    meldInline_data();
}

void TestEncryption::meldInlineWrongPassword()
{
    QFETCH(EncryptedVersion, version);
    QFETCH(QString, payload);
    for (const QString &wrong : {u"hasło-zółw 🔑"_s, QString()}) {
        const Decrypted d = decryptPayload(version, payload, pw(wrong));
        QCOMPARE(d.failure, Failure::WrongPassword);
        QVERIFY(d.text.isEmpty());
    }
    // A payload read as another version: never the text.
    const EncryptedVersion other =
            version == EncryptedVersion::Gamma ? EncryptedVersion::Beta : EncryptedVersion::Gamma;
    QVERIFY(!decryptPayload(other, payload, pw(meldfixtures::Password)).ok());
}

void TestEncryption::meldNotes_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("password");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("version");
    QTest::addColumn<QString>("hint");
    QTest::addColumn<quint32>("iterations");
    const QString p = meldfixtures::Password.toString();
    const QString t = meldfixtures::Text.toString();
    QTest::newRow("1.0") << meldfixtures::FileV1.toString() << p << t << u"1.0"_s << QString() << 1000u;
    QTest::newRow("2.0") << meldfixtures::FileV2.toString() << p << t << u"2.0"_s << u"stara"_s << 210000u;
    QTest::newRow("3.0") << meldfixtures::FileV3.toString() << p << t << u"3.0"_s << u"Podpowiedź: zwierzę"_s
                         << 220000u;
    QTest::newRow("3.0 compact, other order") << meldfixtures::FileV3Compact.toString() << u"p"_s << u"Krótki"_s
                                              << u"3.0"_s << u"podpowiedź"_s << 220000u;
    QTest::newRow("plugin's v2 note") << meldfixtures::LegacyWhole.toString() << meldfixtures::LegacyPassword.toString()
                                      << meldfixtures::LegacyText.toString() << u"2.0"_s << u"legacy test"_s << 210000u;
}

void TestEncryption::meldNotes()
{
    QFETCH(QString, file);
    QFETCH(QString, password);
    QFETCH(QString, text);
    QFETCH(QString, version);
    QFETCH(QString, hint);
    QFETCH(quint32, iterations);
    Failure failure = Failure::Malformed;
    const auto read = readNoteFile(file, &failure);
    QVERIFY(read);
    QCOMPARE(failure, Failure::None);
    QCOMPARE(read->version, version);
    QCOMPARE(read->hint, hint);
    QCOMPARE(read->iterations, iterations);
    QVERIFY(!read->blank);
    const Decrypted d = decryptNote(file, pw(password));
    QCOMPARE(d.failure, Failure::None);
    QCOMPARE(d.text, text);
    QCOMPARE(decryptNote(file, pw(QString(password + u'x'))).failure, Failure::WrongPassword);
}

void TestEncryption::legacyFixtures()
{
    // The plugin's beta marker in a note, found and opened.
    const QString note = u"Przed tekstem "_s + meldfixtures::LegacyInline + u" i po nim."_s;
    const QList<EncryptedSpan> spans = markdown::findEncryptedFragments(note);
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].version, EncryptedVersion::Beta);
    QVERIFY(spans[0].hidden);
    QVERIFY(spans[0].hint.isEmpty());
    QCOMPARE(note.sliced(spans[0].from, spans[0].to - spans[0].from), meldfixtures::LegacyInline.toString());
    const Decrypted d = decryptFragment(note, spans[0], pw(meldfixtures::LegacyPassword));
    QCOMPARE(d.failure, Failure::None);
    QCOMPARE(d.text, meldfixtures::LegacyText.toString());
}

void TestEncryption::writesWhatThePluginWrites()
{
    // The same random bytes give the plugin's bytes exactly: payload and file.
    QCOMPARE(encryptPayload(meldfixtures::Text, pw(meldfixtures::Password), counting()),
             meldfixtures::DeterministicInline.toString());
    QCOMPARE(writeNoteFile(meldfixtures::Text, pw(meldfixtures::Password), meldfixtures::DeterministicHint, counting()),
             meldfixtures::DeterministicFile.toString());
    // The plugin's FileV3 has exactly our layout (only the random parts differ).
    const QString ours = writeNoteFile(u"x"_s, pw(u"p"_s), u"Podpowiedź: zwierzę"_s);
    const auto skeleton = [](QString s) {
        static const QRegularExpression quoted(uR"re("(encodedData|salt|iv)": "[^"]*")re"_s);
        return s.replace(quoted, u"\"\\1\": \"…\""_s);
    };
    QCOMPARE(skeleton(ours), skeleton(meldfixtures::FileV3.toString()));
}

void TestEncryption::roundTrips_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("password");
    QTest::newRow("polish") << u"Zażółć gęślą jaźń — ĄĆĘŁŃÓŚŹŻ"_s << u"żółw"_s;
    QTest::newRow("empty text") << QString() << u"hasło"_s;
    QTest::newRow("empty password") << u"tekst"_s << QString();
    QTest::newRow("lines and markdown") << u"- [ ] zadanie 📅 2026-11-20\n  - [[Strona]] #tag\r\n```\nkod\n```"_s
                                        << u"długie hasło z odstępami i znakami !@#$%^&*()"_s;
    QTest::newRow("emoji and markers") << u"🔐γ nie fragment 🔐 💡 %% ` *"_s << u"🔑"_s;
    QTest::newRow("long") << QString(20000, u'ż') << u"p"_s;
}

void TestEncryption::roundTrips()
{
    QFETCH(QString, text);
    QFETCH(QString, password);
    const QString payload = encryptPayload(text, pw(password));
    const Decrypted inlineBack = decryptPayload(EncryptedVersion::Gamma, payload, pw(password));
    QCOMPARE(inlineBack.failure, Failure::None);
    QCOMPARE(inlineBack.text, text);
    const QString file = writeNoteFile(text, pw(password), u"podpowiedź"_s);
    const auto read = readNoteFile(file);
    QVERIFY(read);
    QCOMPARE(read->version, u"3.0"_s);
    QCOMPARE(read->hint, u"podpowiedź"_s);
    const Decrypted fileBack = decryptNote(file, pw(password));
    QCOMPARE(fileBack.failure, Failure::None);
    QCOMPARE(fileBack.text, text);
    // Fresh salt and IV each time.
    QVERIFY(encryptPayload(text, pw(password)) != payload);
}

void TestEncryption::malformedPayloads_data()
{
    QTest::addColumn<EncryptedVersion>("version");
    QTest::addColumn<QString>("payload");
    QTest::addColumn<Failure>("failure");
    const QString v3 = meldfixtures::InlineV3.toString();
    QTest::newRow("v3 not base64") << EncryptedVersion::Gamma << u"!!!!"_s << Failure::Malformed;
    QTest::newRow("v3 unpadded") << EncryptedVersion::Gamma << QString(v3).remove(u'=') << Failure::Malformed;
    QTest::newRow("v3 white space") << EncryptedVersion::Gamma << v3.left(20) + u' ' + v3.sliced(20)
                                    << Failure::Malformed;
    QTest::newRow("v3 header only") << EncryptedVersion::Gamma << crypto::encodeBase64(QByteArray(47, '\x01'))
                                    << Failure::Malformed;
    QTest::newRow("v3 other suite") << EncryptedVersion::Gamma << editedV3(v3, [](QByteArray &b) { b[0] = 2; })
                                    << Failure::Unsupported;
    QTest::newRow("v3 too few rounds") << EncryptedVersion::Gamma << editedV3(v3, [](QByteArray &b) {
        b[1] = 0;
        b[2] = 0;
        b[3] = 1;
    }) << Failure::Unsupported;
    QTest::newRow("v3 too many rounds") << EncryptedVersion::Gamma << editedV3(v3, [](QByteArray &b) {
        b[1] = char(0xFF);
        b[2] = char(0xFF);
        b[3] = char(0xFF);
    }) << Failure::Unsupported;
    // Rounds changed within the bounds: authenticated (another key and another AAD).
    QTest::newRow("v3 other rounds") << EncryptedVersion::Gamma
                                     << editedV3(v3, [](QByteArray &b) { b[3] = char(b[3] + 1); })
                                     << Failure::WrongPassword;
    QTest::newRow("v3 salt changed") << EncryptedVersion::Gamma
                                     << editedV3(v3, [](QByteArray &b) { b[5] = char(b[5] ^ 1); })
                                     << Failure::WrongPassword;
    QTest::newRow("v3 iv changed") << EncryptedVersion::Gamma
                                   << editedV3(v3, [](QByteArray &b) { b[21] = char(b[21] ^ 1); })
                                   << Failure::WrongPassword;
    QTest::newRow("v3 tag cut") << EncryptedVersion::Gamma << editedV3(v3, [](QByteArray &b) { b.chop(1); })
                                << Failure::WrongPassword;
    QTest::newRow("v2 short") << EncryptedVersion::Beta << crypto::encodeBase64(QByteArray(47, 'x'))
                              << Failure::Malformed;
    QTest::newRow("v1 short") << EncryptedVersion::Alpha << crypto::encodeBase64(QByteArray(31, 'x'))
                              << Failure::Malformed;
    QTest::newRow("v0 short") << EncryptedVersion::Obsolete << crypto::encodeBase64(QByteArray(15, 'x'))
                              << Failure::Malformed;
    QTest::newRow("v2 not base64") << EncryptedVersion::Beta << u"żółw"_s << Failure::Malformed;
    QTest::newRow("v2 changed") << EncryptedVersion::Beta
                                << editedV3(meldfixtures::InlineV2, [](QByteArray &b) { b[40] = char(b[40] ^ 4); })
                                << Failure::WrongPassword;
    QTest::newRow("empty") << EncryptedVersion::Gamma << QString() << Failure::Malformed;
    QTest::newRow("too large") << EncryptedVersion::Gamma << QString(MaxEncodedSize + 4, u'A') << Failure::TooLarge;
}

void TestEncryption::malformedPayloads()
{
    QFETCH(EncryptedVersion, version);
    QFETCH(QString, payload);
    QFETCH(Failure, failure);
    const Decrypted d = decryptPayload(version, payload, pw(meldfixtures::Password));
    QCOMPARE(d.failure, failure);
    QVERIFY(d.text.isEmpty());
}

void TestEncryption::malformedFiles_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<Failure>("failure");
    const QString v3 = meldfixtures::FileV3.toString();
    QTest::newRow("not json") << u"to nie jest JSON"_s << Failure::Malformed;
    QTest::newRow("markdown") << u"# Tytuł\n\nTreść"_s << Failure::Malformed;
    QTest::newRow("array") << u"[1, 2]"_s << Failure::Malformed;
    QTest::newRow("white space only") << u"  \n"_s << Failure::Malformed;
    QTest::newRow("no hint") << editedFile(v3, u"hint"_s, QJsonValue::Undefined) << Failure::Malformed;
    QTest::newRow("hint not a string") << editedFile(v3, u"hint"_s, 5) << Failure::Malformed;
    QTest::newRow("no data") << editedFile(v3, u"encodedData"_s, QJsonValue::Undefined) << Failure::Malformed;
    QTest::newRow("version 4.0") << editedFile(v3, u"version"_s, u"4.0"_s) << Failure::Unsupported;
    QTest::newRow("version number") << editedFile(v3, u"version"_s, 3) << Failure::Malformed;
    QTest::newRow("unknown key") << editedFile(v3, u"extra"_s, u"x"_s) << Failure::Unsupported;
    QTest::newRow("unknown kdf key") << editedFile(v3, u"kdf.pepper"_s, u"x"_s) << Failure::Unsupported;
    QTest::newRow("sha-256") << editedFile(v3, u"kdf.hash"_s, u"SHA-256"_s) << Failure::Unsupported;
    QTest::newRow("aes-128") << editedFile(v3, u"cipher.keyLength"_s, 128) << Failure::Unsupported;
    QTest::newRow("short tag") << editedFile(v3, u"cipher.tagLength"_s, 96) << Failure::Unsupported;
    QTest::newRow("too few rounds") << editedFile(v3, u"kdf.iterations"_s, 219999) << Failure::Unsupported;
    QTest::newRow("fractional rounds") << editedFile(v3, u"kdf.iterations"_s, 220000.5) << Failure::Unsupported;
    QTest::newRow("rounds as text") << editedFile(v3, u"kdf.iterations"_s, u"220000"_s) << Failure::Unsupported;
    QTest::newRow("too many rounds") << editedFile(v3, u"kdf.iterations"_s, 2000001) << Failure::Unsupported;
    QTest::newRow("no kdf") << editedFile(v3, u"kdf"_s, QJsonValue::Undefined) << Failure::Malformed;
    QTest::newRow("short salt") << editedFile(v3, u"kdf.salt"_s, crypto::encodeBase64(QByteArray(15, 's')))
                                << Failure::Malformed;
    QTest::newRow("long iv") << editedFile(v3, u"cipher.iv"_s, crypto::encodeBase64(QByteArray(16, 'i')))
                             << Failure::Malformed;
    QTest::newRow("short data") << editedFile(v3, u"encodedData"_s, crypto::encodeBase64(QByteArray(15, 'd')))
                                << Failure::Malformed;
    QTest::newRow("data not canonical") << editedFile(v3, u"encodedData"_s, u"YR=="_s) << Failure::Malformed;
    // Changed parameters within the bounds: the AAD and the key differ — never decrypted.
    QTest::newRow("other rounds") << editedFile(v3, u"kdf.iterations"_s, 220001) << Failure::WrongPassword;
    QTest::newRow("other salt") << editedFile(v3, u"kdf.salt"_s, crypto::encodeBase64(QByteArray(16, 's')))
                                << Failure::WrongPassword;
}

void TestEncryption::malformedFiles()
{
    QFETCH(QString, file);
    QFETCH(Failure, failure);
    const Decrypted d = decryptNote(file, pw(meldfixtures::Password));
    QCOMPARE(d.failure, failure);
    QVERIFY(d.text.isEmpty());
    if (failure != Failure::WrongPassword) {
        Failure why = Failure::None;
        QVERIFY(!readNoteFile(file, &why));
        QCOMPARE(why, failure);
    }
}

void TestEncryption::hintIsPublicButParametersAreNot()
{
    // The hint is not authenticated (the plugin's design): changing it keeps the note readable.
    const QString changed = editedFile(meldfixtures::FileV3, u"hint"_s, u"inna podpowiedź"_s);
    QCOMPARE(readNoteFile(changed)->hint, u"inna podpowiedź"_s);
    QCOMPARE(decryptNote(changed, pw(meldfixtures::Password)).text, meldfixtures::Text.toString());
}

void TestEncryption::blankFiles()
{
    const auto read = readNoteFile(QString());
    QVERIFY(read);
    QVERIFY(read->blank);
    QCOMPARE(decryptNote(QString(), pw(u"cokolwiek"_s)).failure, Failure::None);
    // Older files with no data are empty notes too (the plugin's reader).
    const QString emptyV2 = uR"({"version":"2.0","hint":"","encodedData":""})"_s;
    QCOMPARE(decryptNote(emptyV2, pw(u"x"_s)).failure, Failure::None);
    QVERIFY(decryptNote(emptyV2, pw(u"x"_s)).text.isEmpty());
}

void TestEncryption::openNotesKeepTheirKey()
{
    // A v3 note: its own salt and rounds, the plugin's note read again with the key alone.
    const OpenedNote opened = openNote(meldfixtures::FileV3, pw(meldfixtures::Password));
    QCOMPARE(opened.failure, Failure::None);
    QCOMPARE(opened.text, meldfixtures::Text.toString());
    QCOMPARE(opened.hint, u"Podpowiedź: zwierzę"_s);
    QCOMPARE(opened.key.iterations, 220000u);
    QCOMPARE(crypto::encodeBase64(opened.key.salt), u"40ShtIjS+ZA7VMyYOnQgdw=="_s);
    QCOMPARE(openNoteWithKey(meldfixtures::FileV3, opened.key).text, meldfixtures::Text.toString());
    // A save with the key: no derivation, the same salt, a fresh IV, readable with the password.
    const QString saved = writeNoteFile(u"Zmieniona treść"_s, opened.key, opened.hint);
    QVERIFY(saved.contains(u"\"salt\": \"40ShtIjS+ZA7VMyYOnQgdw==\""_s));
    QVERIFY(saved != writeNoteFile(u"Zmieniona treść"_s, opened.key, opened.hint));
    QCOMPARE(decryptNote(saved, pw(meldfixtures::Password)).text, u"Zmieniona treść"_s);
    QCOMPARE(openNoteWithKey(saved, opened.key).text, u"Zmieniona treść"_s);
    // Another salt (the plugin saved it again elsewhere): the key alone says no, the password opens it.
    const QString elsewhere = writeNoteFile(u"Z innego urządzenia"_s, pw(meldfixtures::Password), u"h"_s);
    QCOMPARE(openNoteWithKey(elsewhere, opened.key).failure, Failure::WrongPassword);
    QCOMPARE(openNote(elsewhere, pw(meldfixtures::Password)).text, u"Z innego urządzenia"_s);
    // A wrong password: no key handed out.
    const OpenedNote wrong = openNote(meldfixtures::FileV3, pw(u"nie to"_s));
    QCOMPARE(wrong.failure, Failure::WrongPassword);
    QVERIFY(wrong.key.isNull());
    // An older note: a fresh v3 key, so the next save is v3.
    const OpenedNote old = openNote(meldfixtures::FileV2, pw(meldfixtures::Password));
    QCOMPARE(old.failure, Failure::None);
    QCOMPARE(old.text, meldfixtures::Text.toString());
    QCOMPARE(old.key.iterations, DefaultIterations);
    QCOMPARE(readNoteFile(writeNoteFile(old.text, old.key, old.hint))->version, u"3.0"_s);
    QCOMPARE(openNoteWithKey(meldfixtures::FileV2, old.key).failure, Failure::WrongPassword);
    // A blank file: an empty note and a key for its first save.
    const OpenedNote blank = openNote(QString(), pw(u"nowe"_s));
    QCOMPARE(blank.failure, Failure::None);
    QVERIFY(!blank.key.isNull());
    QCOMPARE(decryptNote(writeNoteFile(u"Pierwsza"_s, blank.key, {}), pw(u"nowe"_s)).text, u"Pierwsza"_s);
    // Malformed files are refused before any derivation.
    QCOMPARE(openNote(u"{"_s, pw(u"p"_s)).failure, Failure::Malformed);
    QVERIFY(writeNoteFile(u"x"_s, NoteKey(), {}).isEmpty());
}

void TestEncryption::iterationsOfPayloads()
{
    QCOMPARE(payloadIterations(EncryptedVersion::Gamma, meldfixtures::InlineV3), 220000u);
    QCOMPARE(payloadIterations(EncryptedVersion::Gamma, meldfixtures::InlineV3Iter300k), 300000u);
    QCOMPARE(payloadIterations(EncryptedVersion::Beta, meldfixtures::InlineV2), 210000u);
    QCOMPARE(payloadIterations(EncryptedVersion::Alpha, meldfixtures::InlineV1), 1000u);
    QCOMPARE(payloadIterations(EncryptedVersion::Gamma, u"!!"_s), 0u);
    QCOMPARE(payloadIterations(EncryptedVersion::Gamma,
                               editedV3(meldfixtures::InlineV3, [](QByteArray &b) { b[1] = char(0x7F); })),
             0u);
}

void TestEncryption::failureMessages()
{
    QVERIFY(failureMessage(Failure::None).isEmpty());
    for (const Failure f : {Failure::WrongPassword, Failure::Malformed, Failure::Unsupported, Failure::TooLarge})
        QVERIFY(!failureMessage(f).isEmpty());
}

void TestEncryption::findFragments_data()
{
    const QString v3 = meldfixtures::InlineV3.toString();
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("version");
    QTest::addColumn<bool>("hidden");
    QTest::addColumn<QString>("hint");
    QTest::newRow("hidden gamma") << u"Hasło: %%🔐γ "_s + v3 + u" 🔐%% koniec"_s << 1 << 3 << true << QString();
    QTest::newRow("visible gamma") << u"🔐γ "_s + v3 + u" 🔐"_s << 1 << 3 << false << QString();
    QTest::newRow("hint") << u"%%🔐γ 💡imię psa💡"_s + v3 + u" 🔐%%"_s << 1 << 3 << true << u"imię psa"_s;
    QTest::newRow("beta") << u"%%🔐β "_s + meldfixtures::InlineV2 + u" 🔐%%"_s << 1 << 2 << true << QString();
    QTest::newRow("alpha visible") << u"🔐α "_s + meldfixtures::InlineV1 + u" 🔐"_s << 1 << 1 << false << QString();
    QTest::newRow("obsolete") << u"%%🔐 "_s + meldfixtures::InlineV0 + u" 🔐%%"_s << 1 << 0 << true << QString();
    QTest::newRow("two in a line") << u"%%🔐γ "_s + v3 + u" 🔐%% i 🔐γ "_s + v3 + u" 🔐"_s << 2 << 3 << true
                                   << QString();
    QTest::newRow("prose with locks") << u"🔐 hello 🔐 i 🔐γ krótko 🔐"_s << 0 << 0 << false << QString();
    QTest::newRow("unclosed") << u"%%🔐γ "_s + v3 << 0 << 0 << false << QString();
    QTest::newRow("across lines") << u"%%🔐γ "_s + v3.left(30) + u"\n"_s + v3.sliced(30) + u" 🔐%%"_s << 0 << 0 << false
                                  << QString();
    QTest::newRow("unclosed hint") << u"%%🔐γ 💡bez końca"_s + v3 + u" 🔐%%"_s << 0 << 0 << false << QString();
    QTest::newRow("not base64") << u"%%🔐γ "_s + v3.left(40) + u"ż"_s + v3.sliced(41) + u" 🔐%%"_s << 0 << 0 << false
                                << QString();
    QTest::newRow("no space before suffix") << u"%%🔐γ "_s + v3 + u"🔐%%"_s << 0 << 0 << false << QString();
    QTest::newRow("hidden prefix, visible suffix") << u"%%🔐γ "_s + v3 + u" 🔐 dalej"_s << 1 << 3 << false << QString();
    QTest::newRow("empty") << QString() << 0 << 0 << false << QString();
    QTest::newRow("lonely icon") << u"🔐"_s << 0 << 0 << false << QString();
    QTest::newRow("percent noise") << u"%%%🔐γ "_s + v3 + u" 🔐%%%"_s << 1 << 3 << true << QString();
}

void TestEncryption::findFragments()
{
    QFETCH(QString, text);
    QFETCH(int, count);
    QFETCH(int, version);
    QFETCH(bool, hidden);
    QFETCH(QString, hint);
    const QList<EncryptedSpan> spans = markdown::findEncryptedFragments(text);
    QCOMPARE(spans.size(), count);
    if (count == 0)
        return;
    const EncryptedSpan &s = spans[0];
    QCOMPARE(int(s.version), version);
    QCOMPARE(s.hidden, hidden);
    QCOMPARE(s.hint, hint);
    // The ranges: the payload inside the fragment, the fragment found again exactly there.
    QVERIFY(s.from <= s.payloadFrom && s.payloadTo <= s.to);
    const QStringView payload = QStringView(text).sliced(s.payloadFrom, s.payloadTo - s.payloadFrom);
    QVERIFY(crypto::decodeBase64(payload, crypto::Base64Mode::Forgiving));
    const auto again = markdown::encryptedFragmentAt(text, s.from, s.to);
    QVERIFY(again);
    QCOMPARE(*again, s);
    QVERIFY(!markdown::encryptedFragmentAt(text, s.from + 1, s.to));
    QVERIFY(markdown::mayContainEncrypted(text));
}

void TestEncryption::fragmentTextAndHints()
{
    const QString payload = meldfixtures::InlineV3.toString();
    QCOMPARE(markdown::encryptedFragmentText(EncryptedVersion::Gamma, true, {}, payload),
             u"%%🔐γ "_s + payload + u" 🔐%%"_s);
    QCOMPARE(markdown::encryptedFragmentText(EncryptedVersion::Gamma, false, u"pies"_s, payload),
             u"🔐γ 💡pies💡"_s + payload + u" 🔐"_s);
    QCOMPARE(markdown::encryptedFragmentText(EncryptedVersion::Beta, true, {}, payload),
             u"%%🔐β "_s + payload + u" 🔐%%"_s);
    QVERIFY(markdown::isValidEncryptedHint(u"Imię pierwszego psa"_s));
    QVERIFY(markdown::isValidEncryptedHint(QString()));
    QVERIFY(!markdown::isValidEncryptedHint(u"z 🔐 w środku"_s));
    QVERIFY(!markdown::isValidEncryptedHint(u"💡"_s));
    QVERIFY(!markdown::isValidEncryptedHint(u"dwie\nlinie"_s));
    // Every written fragment is found again with its hint and look.
    for (const bool hidden : {true, false}) {
        const QString text =
                u"a "_s + markdown::encryptedFragmentText(EncryptedVersion::Gamma, hidden, u"żółw"_s, payload);
        const QList<EncryptedSpan> spans = markdown::findEncryptedFragments(text);
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans[0].hidden, hidden);
        QCOMPARE(spans[0].hint, u"żółw"_s);
        QCOMPARE(spans[0].from, 2);
        QCOMPARE(spans[0].to, text.size());
    }
}

void TestEncryption::maskForSearch()
{
    const QString fragment = u"%%🔐γ 💡pies💡"_s + meldfixtures::InlineV3 + u" 🔐%%"_s;
    const QString text = u"Początek\n"_s + fragment + u" koniec\nDruga linia"_s;
    const QString masked = markdown::maskEncryptedFragments(text);
    QCOMPARE(masked.size(), text.size());
    QCOMPARE(masked, u"Początek\n"_s + QString(fragment.size(), u' ') + u" koniec\nDruga linia"_s);
    QCOMPARE(markdown::maskEncryptedFragments(u"bez szyfru"_s), u"bez szyfru"_s);
}

void TestEncryption::notePaths()
{
    QVERIFY(markdown::isEncryptedNotePath(u"Sekrety/Hasła.mdenc"_s));
    QVERIFY(markdown::isEncryptedNotePath(u"a.MDENC"_s));
    QVERIFY(markdown::isEncryptedNotePath(u"stare.encrypted"_s));
    QVERIFY(!markdown::isEncryptedNotePath(u"notatka.md"_s));
    QVERIFY(!markdown::isEncryptedNotePath(u"mdenc"_s));
    QVERIFY(!markdown::isEncryptedNotePath(u"x.mdenc.md"_s));
}

void TestEncryption::encryptAndDecryptFragments()
{
    const QString secret = u"PIN do sejfu: 4711, kod: żółć"_s;
    const QString fragment = encryptFragment(secret, pw(u"hasło"_s), u"zwykłe"_s, true);
    QVERIFY(fragment.startsWith(u"%%🔐γ 💡zwykłe💡"_s));
    QVERIFY(fragment.endsWith(u" 🔐%%"_s));
    const QString note = u"- Bank: "_s + fragment + u"\n- Inne"_s;
    const QList<EncryptedSpan> spans = markdown::findEncryptedFragments(note);
    QCOMPARE(spans.size(), 1);
    QCOMPARE(decryptFragment(note, spans[0], pw(u"hasło"_s)).text, secret);
    QCOMPARE(decryptFragment(note, spans[0], pw(u"haslo"_s)).failure, Failure::WrongPassword);
    // Visible look, no hint.
    const QString visible = encryptFragment(secret, pw(u"hasło"_s), {}, false);
    QVERIFY(visible.startsWith(u"🔐γ "_s) && visible.endsWith(u" 🔐"_s));
    // A hint the markers forbid: nothing.
    QVERIFY(encryptFragment(secret, pw(u"hasło"_s), u"zła 💡"_s).isEmpty());
    // A span outside the text is malformed, never a crash.
    EncryptedSpan bogus;
    bogus.payloadFrom = 5;
    bogus.payloadTo = 500;
    QCOMPARE(decryptFragment(u"krótki"_s, bogus, pw(u"x"_s)).failure, Failure::Malformed);
}

void TestEncryption::reencryptUpgradesAndKeepsLook()
{
    const QString beta = u"🔐β 💡stara💡"_s + meldfixtures::InlineV2 + u" 🔐"_s;
    const EncryptedSpan span = markdown::findEncryptedFragments(beta).value(0);
    QCOMPARE(span.version, EncryptedVersion::Beta);
    const QString again = reencryptFragment(span, u"Nowa treść"_s, pw(meldfixtures::Password));
    const EncryptedSpan upgraded = markdown::findEncryptedFragments(again).value(0);
    QCOMPARE(upgraded.version, EncryptedVersion::Gamma);
    QCOMPARE(upgraded.hidden, false);
    QCOMPARE(upgraded.hint, u"stara"_s);
    QCOMPARE(decryptFragment(again, upgraded, pw(meldfixtures::Password)).text, u"Nowa treść"_s);
}

void TestEncryption::changePassword()
{
    const QString note = u"A "_s + encryptFragment(u"tajemnica"_s, pw(u"stare"_s), u"stara"_s) + u" B"_s;
    const EncryptedSpan span = markdown::findEncryptedFragments(note).value(0);
    Failure failure = Failure::None;
    QVERIFY(!changeFragmentPassword(note, span, pw(u"złe"_s), pw(u"nowe"_s), u"nowa"_s, &failure));
    QCOMPARE(failure, Failure::WrongPassword);
    const auto changed = changeFragmentPassword(note, span, pw(u"stare"_s), pw(u"nowe"_s), u"nowa"_s, &failure);
    QVERIFY(changed);
    QCOMPARE(failure, Failure::None);
    QVERIFY(changed->startsWith(u"A "_s) && changed->endsWith(u" B"_s));
    const EncryptedSpan now = markdown::findEncryptedFragments(*changed).value(0);
    QCOMPARE(now.hint, u"nowa"_s);
    QCOMPARE(decryptFragment(*changed, now, pw(u"nowe"_s)).text, u"tajemnica"_s);
    QCOMPARE(decryptFragment(*changed, now, pw(u"stare"_s)).failure, Failure::WrongPassword);
}

void TestEncryption::decryptInPlace()
{
    const QString note = u"Przed\r\nKod: "_s + encryptFragment(u"1234\nlinia"_s, pw(u"p"_s)) + u" po\r\n"_s;
    const EncryptedSpan span = markdown::findEncryptedFragments(note).value(0);
    QVERIFY(!encryption::decryptInPlace(note, span, pw(u"q"_s)));
    QCOMPARE(encryption::decryptInPlace(note, span, pw(u"p"_s)).value_or(QString()),
             u"Przed\r\nKod: 1234\nlinia po\r\n"_s);
}

void TestEncryption::ciphertextHidesThePlaintext()
{
    const QString secret = u"żółwie tajne słowo"_s;
    const QString fragment = encryptFragment(secret, pw(u"p"_s));
    const QString file = writeNoteFile(secret, pw(u"p"_s), u"hint"_s);
    for (const QString &word : secret.split(u' ')) {
        QVERIFY(!fragment.contains(word));
        QVERIFY(!file.contains(word));
    }
    QVERIFY(!file.toUtf8().contains(secret.toUtf8()));
}

void TestEncryption::keyringRemembersPerPage()
{
    Keyring ring(60000);
    QVERIFY(ring.candidates(u"a.md"_s, 0).isEmpty());
    ring.remember(u"a.md"_s, pw(u"jeden"_s), 1000);
    ring.remember(u"b.md"_s, pw(u"dwa"_s), 2000);
    ring.remember(u"c.md"_s, pw(u"jeden"_s), 3000);
    QCOMPARE(ring.size(), 3);
    // The page's own first, then by last use; no password twice; at most `limit`.
    QList<SecureBytes> got = ring.candidates(u"b.md"_s, 4000);
    QCOMPARE(got.size(), 2);
    QCOMPARE(got[0].toUtf8String(), u"dwa"_s);
    QCOMPARE(got[1].toUtf8String(), u"jeden"_s);
    got = ring.candidates(u"nowa.md"_s, 4000, 5);
    QCOMPARE(got.size(), 2); // "jeden" once
    QCOMPARE(got[0].toUtf8String(), u"jeden"_s);
    QCOMPARE(ring.passwordFor(u"b.md"_s, 4000)->toUtf8String(), u"dwa"_s);
    QVERIFY(!ring.passwordFor(u"nowa.md"_s, 4000));
    // A page's password replaced.
    ring.remember(u"b.md"_s, pw(u"trzy"_s), 5000);
    QCOMPARE(ring.passwordFor(u"b.md"_s, 5000)->toUtf8String(), u"trzy"_s);
    ring.forget(u"b.md"_s);
    QVERIFY(!ring.passwordFor(u"b.md"_s, 5000));
    ring.clear();
    QVERIFY(ring.isEmpty());
    QCOMPARE(ring.expiresAt(), -1);
    // Empty passwords are not remembered.
    ring.remember(u"a.md"_s, pw(QString()), 6000);
    QVERIFY(ring.isEmpty());
}

void TestEncryption::keyringForgetsAfterInactivity()
{
    Keyring ring(60000);
    ring.remember(u"a.md"_s, pw(u"p"_s), 0);
    QCOMPARE(ring.expiresAt(), 60000);
    QVERIFY(!ring.expire(59999));
    ring.touch(50000); // used again: the timeout starts anew
    QCOMPARE(ring.expiresAt(), 110000);
    QVERIFY(!ring.expire(100000));
    QCOMPARE(ring.candidates(u"a.md"_s, 100000).size(), 1);
    // Past the timeout nothing is handed out even before expire().
    QVERIFY(ring.candidates(u"a.md"_s, 110000).isEmpty());
    QVERIFY(!ring.passwordFor(u"a.md"_s, 110000));
    ring.touch(110000); // too late: no revival
    QVERIFY(ring.expire(110001));
    QVERIFY(ring.isEmpty());
    // A timeout of 0 remembers nothing; lowering it to 0 forgets at once.
    Keyring none(0);
    none.remember(u"a.md"_s, pw(u"p"_s), 0);
    QVERIFY(none.isEmpty());
    ring.remember(u"a.md"_s, pw(u"p"_s), 200000);
    ring.setTimeout(0);
    QVERIFY(ring.isEmpty());
    // Remembering after expiry starts a fresh ring (the old passwords are not revived).
    Keyring later(1000);
    later.remember(u"a.md"_s, pw(u"stare"_s), 0);
    later.remember(u"b.md"_s, pw(u"nowe"_s), 5000);
    QCOMPARE(later.size(), 1);
    QCOMPARE(later.candidates(u"a.md"_s, 5000).value(0).toUtf8String(), u"nowe"_s);
}

void TestEncryption::keyringFollowsRenames()
{
    Keyring ring(60000);
    ring.remember(u"Sekrety/a.mdenc"_s, pw(u"p"_s), 0);
    ring.remember(u"Sekrety/b/c.md"_s, pw(u"q"_s), 0);
    ring.remember(u"Inne.md"_s, pw(u"r"_s), 0);
    ring.renamed(u"Sekrety"_s, u"Tajne"_s);
    QVERIFY(ring.passwordFor(u"Tajne/a.mdenc"_s, 1));
    QVERIFY(ring.passwordFor(u"Tajne/b/c.md"_s, 1));
    QVERIFY(!ring.passwordFor(u"Sekrety/a.mdenc"_s, 1));
    ring.renamed(u"Inne.md"_s, u"Nowe.md"_s);
    QCOMPARE(ring.passwordFor(u"Nowe.md"_s, 1)->toUtf8String(), u"r"_s);
    // A folder prefix that is no folder ("Sekret" vs "Sekrety/…") moves nothing.
    ring.renamed(u"Tajn"_s, u"X"_s);
    QVERIFY(ring.passwordFor(u"Tajne/a.mdenc"_s, 1));
}

QTEST_GUILESS_MAIN(TestEncryption)
#include "tst_encryption.moc"

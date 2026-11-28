// The key file of an encrypted sync (src/core/sync/encryptionkeys.h, docs/SYNC-ENCRYPTION.md
// "Keys", "Recovery"): a notebook key sealed with the password and a recovery key, opened by
// either, the password changed, and key files that are malformed, changed or ask too much refused.
#include "sync/encryptionkeys.h"

#include "sync/encryption.h"

#include "crypto/base64.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::sync::encryption;

namespace {

constexpr quint32 Fast = 1000; // rounds for the tests (the real default is checked once)
const QString kPassword = u"Zażółć gęślą jaźń 2026"_s;

QJsonObject parse(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

QByteArray write(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

class TestSyncEncryptionKeys : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void createdAndUnlocked()
    {
        const std::optional<NewNotebook> made = createKeyFile(kPassword, {}, Fast);
        QVERIFY(made);
        QCOMPARE(made->notebookKey.size(), NotebookKeySize);
        const Unlocked byPassword = unlockWithPassword(made->keyFile, kPassword);
        QCOMPARE(byPassword.error, UnlockError::None);
        QCOMPARE(byPassword.notebookKey.toByteArray(), made->notebookKey.toByteArray());
        const Unlocked byRecovery = unlockWithRecoveryKey(made->keyFile, made->recoveryKey);
        QCOMPARE(byRecovery.error, UnlockError::None);
        QCOMPARE(byRecovery.notebookKey.toByteArray(), made->notebookKey.toByteArray());
        QCOMPARE(keyIdOf(made->keyFile), keyIdFor(made->notebookKey));
        QCOMPARE(keyIdOf(made->keyFile).size(), 16);
    }

    void theFileSaysNothingWithoutASecret()
    {
        const std::optional<NewNotebook> made = createKeyFile(kPassword, {}, Fast);
        QVERIFY(made);
        const QByteArray json = made->keyFile;
        QVERIFY(!json.contains(made->notebookKey.toByteArray()));
        QVERIFY(!json.contains(crypto::encodeBase64(made->notebookKey.view()).toUtf8()));
        QVERIFY(!json.contains(kPassword.toUtf8()));
        const QJsonObject o = parse(json);
        QCOMPARE(o.value(u"format"_s).toString(), u"brulion-sync-encryption"_s);
        QCOMPARE(o.value(u"version"_s).toInt(), 1);
        QCOMPARE(o.value(u"password"_s).toObject().value(u"kdf"_s).toString(), u"pbkdf2-sha512"_s);
        QCOMPARE(o.value(u"password"_s).toObject().value(u"iterations"_s).toInteger(), qint64(Fast));
    }

    void theDefaultRounds()
    {
        const std::optional<NewNotebook> made = createKeyFile(kPassword);
        QVERIFY(made);
        QCOMPARE(parse(made->keyFile).value(u"password"_s).toObject().value(u"iterations"_s).toInteger(),
                 qint64(DefaultIterations));
        QCOMPARE(DefaultIterations, 600'000u);
    }

    void wrongSecrets()
    {
        const std::optional<NewNotebook> made = createKeyFile(kPassword, {}, Fast);
        QVERIFY(made);
        QCOMPARE(unlockWithPassword(made->keyFile, u"zażółć gęślą jaźń 2026"_s).error, UnlockError::WrongSecret);
        QCOMPARE(unlockWithPassword(made->keyFile, QString()).error, UnlockError::WrongSecret);
        QVERIFY(unlockWithPassword(made->keyFile, u"x"_s).notebookKey.isEmpty());
        QString other = made->recoveryKey;
        other[0] = other.at(0) == u'a' ? u'b' : u'a';
        QCOMPARE(unlockWithRecoveryKey(made->keyFile, other).error, UnlockError::WrongSecret);
        QCOMPARE(unlockWithRecoveryKey(made->keyFile, u"to nie jest klucz"_s).error, UnlockError::WrongSecret);
    }

    void recoveryKeysAsPeopleTypeThem()
    {
        const std::optional<NewNotebook> made = createKeyFile(kPassword, {}, Fast);
        QVERIFY(made);
        // 52 letters and digits of base32 in groups of four.
        QVERIFY2(QRegularExpression(u"^([a-z2-7]{4}-){12}[a-z2-7]{4}$"_s).match(made->recoveryKey).hasMatch(),
                 qPrintable(made->recoveryKey));
        QString typed = made->recoveryKey.toUpper();
        typed.replace(u'-', u"  "_s);
        typed = u"  "_s + typed + u"\n"_s;
        QCOMPARE(unlockWithRecoveryKey(made->keyFile, typed).error, UnlockError::None);
        QCOMPARE(parseRecoveryKey(typed)->size(), 32);
        QVERIFY(!parseRecoveryKey(made->recoveryKey.chopped(1)));
        QVERIFY(!parseRecoveryKey(QString(made->recoveryKey + u"a"_s)));
        QVERIFY(!parseRecoveryKey(QString(made->recoveryKey).replace(0, 1, u"1"_s))); // no 0, 1, 8, 9 in base32
    }

    void twoNotebooksHaveTwoKeys()
    {
        const std::optional<NewNotebook> a = createKeyFile(kPassword, {}, Fast);
        const std::optional<NewNotebook> b = createKeyFile(kPassword, {}, Fast);
        QVERIFY(a && b);
        QVERIFY(a->notebookKey.toByteArray() != b->notebookKey.toByteArray());
        QVERIFY(a->recoveryKey != b->recoveryKey);
        QVERIFY(keyIdOf(a->keyFile) != keyIdOf(b->keyFile));
        QCOMPARE(unlockWithRecoveryKey(a->keyFile, b->recoveryKey).error, UnlockError::WrongSecret);
    }

    void passwordChanged()
    {
        const std::optional<NewNotebook> made = createKeyFile(kPassword, {}, Fast);
        QVERIFY(made);
        const std::optional<QByteArray> changed = changePassword(made->keyFile, made->notebookKey, u"nowe hasło łąka 42"_s, {}, Fast);
        QVERIFY(changed);
        QCOMPARE(unlockWithPassword(*changed, kPassword).error, UnlockError::WrongSecret);
        QCOMPARE(unlockWithPassword(*changed, u"nowe hasło łąka 42"_s).notebookKey.toByteArray(),
                 made->notebookKey.toByteArray());
        // The recovery key goes on working; the key id stays.
        QCOMPARE(unlockWithRecoveryKey(*changed, made->recoveryKey).error, UnlockError::None);
        QCOMPARE(keyIdOf(*changed), keyIdOf(made->keyFile));
        // Not with a key the file is not for.
        QVERIFY(!changePassword(made->keyFile, crypto::SecureBytes(QByteArray(32, 'z')), u"cokolwiek długiego"_s, {}, Fast));
    }

    void passwordsTooShort()
    {
        QVERIFY(!isPasswordAcceptable(u"krótkie"_s));
        QVERIFY(!isPasswordAcceptable(u"           x"_s)); // (spaces around do not count)
        QVERIFY(isPasswordAcceptable(u"dwanaście zn"_s));
        QCOMPARE(MinPasswordLength, 12);
        QVERIFY(!createKeyFile(u"krótkie"_s, {}, Fast));
    }

    void malformedFiles_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<int>("error");
        const std::optional<NewNotebook> made = createKeyFile(kPassword, {}, Fast);
        const QJsonObject good = parse(made->keyFile);
        auto changed = [&good](const QString &section, const QString &key, const QJsonValue &value) {
            QJsonObject o = good;
            if (section.isEmpty()) {
                o.insert(key, value);
            } else {
                QJsonObject inner = o.value(section).toObject();
                inner.insert(key, value);
                o.insert(section, inner);
            }
            return write(o);
        };
        const int malformed = int(UnlockError::Malformed);
        QTest::newRow("not JSON") << "{"_ba << malformed;
        QTest::newRow("an array") << "[]"_ba << malformed;
        QTest::newRow("empty") << QByteArray() << malformed;
        QTest::newRow("another format") << changed({}, u"format"_s, u"joplin"_s) << malformed;
        QTest::newRow("a later version") << changed({}, u"version"_s, 2) << int(UnlockError::UnknownVersion);
        QTest::newRow("another kdf") << changed(u"password"_s, u"kdf"_s, u"md5"_s) << malformed;
        QTest::newRow("no rounds") << changed(u"password"_s, u"iterations"_s, 0) << malformed;
        QTest::newRow("too many rounds") << changed(u"password"_s, u"iterations"_s, 20'000'001) << malformed;
        QTest::newRow("salt not base64") << changed(u"password"_s, u"salt"_s, u"***"_s) << malformed;
        QTest::newRow("no sealed key") << changed(u"password"_s, u"sealed"_s, QString()) << malformed;
        QTest::newRow("short iv") << changed(u"password"_s, u"iv"_s, u"AAAA"_s) << malformed;
        QTest::newRow("sealed key changed")
                << changed(u"password"_s, u"sealed"_s,
                           crypto::encodeBase64(QByteArray(48, 'q')))
                << int(UnlockError::WrongSecret);
        QTest::newRow("fewer rounds than sealed with") << changed(u"password"_s, u"iterations"_s, int(Fast - 1))
                                                       << int(UnlockError::WrongSecret);
    }

    void malformedFiles()
    {
        QFETCH(QByteArray, json);
        QFETCH(int, error);
        QCOMPARE(int(unlockWithPassword(json, kPassword).error), error);
    }
};

QTEST_GUILESS_MAIN(TestSyncEncryptionKeys)
#include "tst_sync_encryption_keys.moc"

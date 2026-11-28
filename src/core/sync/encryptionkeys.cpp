#include "sync/encryptionkeys.h"

#include "crypto/aead.h"
#include "crypto/base64.h"
#include "crypto/hash.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::sync::encryption {

namespace {

const QString Format = u"brulion-sync-encryption"_s;
constexpr int Version = 1;
constexpr qsizetype KeySize = 32;
constexpr qsizetype SaltSize = 16;
constexpr qsizetype IvSize = 12;
constexpr char PasswordAad[] = "brulion-sync-encryption v1 password";
constexpr char RecoveryAad[] = "brulion-sync-encryption v1 recovery";

QString base64(QByteArrayView bytes)
{
    return crypto::encodeBase64(bytes);
}

/// A base64 field of `section` of exactly `size` bytes (0: any but none).
std::optional<QByteArray> bytesOf(const QJsonObject &section, const QString &key, qsizetype size)
{
    const std::optional<QByteArray> bytes = crypto::decodeBase64(section.value(key).toString());
    if (!bytes || bytes->isEmpty() || (size > 0 && bytes->size() != size))
        return std::nullopt;
    return bytes;
}

crypto::SecureBytes passwordKey(QStringView password, QByteArrayView salt, quint32 iterations)
{
    const crypto::SecureBytes utf8 = crypto::SecureBytes::fromUtf8(password);
    return crypto::pbkdf2(crypto::Hash::Sha512, utf8.view(), salt, iterations, KeySize);
}

crypto::SecureBytes recoveryWrapKey(const crypto::SecureBytes &recovery)
{
    return crypto::hkdf(crypto::Hash::Sha256, recovery.view(), {}, "brulion-sync recovery v1", KeySize);
}

/// The notebook key sealed under `key`: {iv, sealed}.
QJsonObject sealKey(const crypto::SecureBytes &key, const crypto::SecureBytes &notebookKey, const char *aad,
                    const crypto::RandomSource &random)
{
    const QByteArray iv = crypto::randomBytes(IvSize, random);
    return {{u"iv"_s, base64(iv)},
            {u"sealed"_s, base64(crypto::aesgcm::seal(key.view(), iv, aad, notebookKey.view()))}};
}

Unlocked openKey(const crypto::SecureBytes &key, const QJsonObject &section, const char *aad)
{
    const std::optional<QByteArray> iv = bytesOf(section, u"iv"_s, IvSize);
    const std::optional<QByteArray> sealed = bytesOf(section, u"sealed"_s, KeySize + crypto::aesgcm::TagSize);
    if (!iv || !sealed)
        return {{}, UnlockError::Malformed};
    std::optional<crypto::SecureBytes> notebookKey = crypto::aesgcm::open(key.view(), *iv, aad, *sealed);
    if (!notebookKey)
        return {{}, UnlockError::WrongSecret};
    return {std::move(*notebookKey), UnlockError::None};
}

/// The key file's object, or the error that it is not one.
std::optional<QJsonObject> parseKeyFile(QByteArrayView keyFile, UnlockError *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(keyFile.toByteArray());
    const QJsonObject o = document.object();
    *error = UnlockError::Malformed;
    if (!document.isObject() || o.value(u"format"_s).toString() != Format)
        return std::nullopt;
    if (o.value(u"version"_s).toInt() != Version) {
        *error = o.value(u"version"_s).toInt() > Version ? UnlockError::UnknownVersion : UnlockError::Malformed;
        return std::nullopt;
    }
    *error = UnlockError::None;
    return o;
}

/// The recovery key as it is shown: base32 in groups of four letters ("abcd-efgh-…").
QString recoveryText(const crypto::SecureBytes &recovery)
{
    const QString letters = crypto::encodeBase32(recovery.view());
    QStringList groups;
    for (qsizetype i = 0; i < letters.size(); i += 4)
        groups.append(letters.mid(i, 4));
    return groups.join(u'-');
}

QJsonObject keyFileObject(const crypto::SecureBytes &notebookKey, const QJsonObject &passwordSection,
                          const QJsonObject &recoverySection)
{
    return {{u"format"_s, Format},
            {u"version"_s, Version},
            {u"keyId"_s, keyIdFor(notebookKey)},
            {u"password"_s, passwordSection},
            {u"recovery"_s, recoverySection}};
}

QJsonObject passwordSection(QStringView password, const crypto::SecureBytes &notebookKey,
                            const crypto::RandomSource &random, quint32 iterations)
{
    const QByteArray salt = crypto::randomBytes(SaltSize, random);
    QJsonObject section = sealKey(passwordKey(password, salt, iterations), notebookKey, PasswordAad, random);
    section.insert(u"kdf"_s, u"pbkdf2-sha512"_s);
    section.insert(u"iterations"_s, qint64(iterations));
    section.insert(u"salt"_s, base64(salt));
    return section;
}

} // namespace

bool isPasswordAcceptable(QStringView password)
{
    return password.trimmed().size() >= MinPasswordLength;
}

std::optional<NewNotebook> createKeyFile(QStringView password, const crypto::RandomSource &random, quint32 iterations)
{
    if (!isPasswordAcceptable(password) || iterations == 0 || iterations > MaxIterations)
        return std::nullopt;
    crypto::SecureBytes notebookKey(crypto::randomBytes(KeySize, random));
    const crypto::SecureBytes recovery(crypto::randomBytes(KeySize, random));
    QJsonObject recoverySection = sealKey(recoveryWrapKey(recovery), notebookKey, RecoveryAad, random);
    recoverySection.insert(u"kdf"_s, u"hkdf-sha256"_s);
    const QJsonObject o = keyFileObject(notebookKey, passwordSection(password, notebookKey, random, iterations),
                                        recoverySection);
    return NewNotebook{QJsonDocument(o).toJson(QJsonDocument::Indented), std::move(notebookKey), recoveryText(recovery)};
}

Unlocked unlockWithPassword(QByteArrayView keyFile, QStringView password)
{
    UnlockError error = UnlockError::None;
    const std::optional<QJsonObject> o = parseKeyFile(keyFile, &error);
    if (!o)
        return {{}, error};
    const QJsonObject section = o->value(u"password"_s).toObject();
    const qint64 iterations = section.value(u"iterations"_s).toInteger();
    const std::optional<QByteArray> salt = bytesOf(section, u"salt"_s, 0);
    if (section.value(u"kdf"_s).toString() != u"pbkdf2-sha512" || iterations < 1 || iterations > MaxIterations || !salt)
        return {{}, UnlockError::Malformed};
    return openKey(passwordKey(password, *salt, quint32(iterations)), section, PasswordAad);
}

Unlocked unlockWithRecoveryKey(QByteArrayView keyFile, QStringView recoveryKey)
{
    UnlockError error = UnlockError::None;
    const std::optional<QJsonObject> o = parseKeyFile(keyFile, &error);
    if (!o)
        return {{}, error};
    const QJsonObject section = o->value(u"recovery"_s).toObject();
    if (section.value(u"kdf"_s).toString() != u"hkdf-sha256")
        return {{}, UnlockError::Malformed};
    const std::optional<crypto::SecureBytes> recovery = parseRecoveryKey(recoveryKey);
    if (!recovery)
        return {{}, UnlockError::WrongSecret};
    return openKey(recoveryWrapKey(*recovery), section, RecoveryAad);
}

std::optional<QByteArray> changePassword(QByteArrayView keyFile, const crypto::SecureBytes &notebookKey,
                                         QStringView newPassword, const crypto::RandomSource &random,
                                         quint32 iterations)
{
    UnlockError error = UnlockError::None;
    std::optional<QJsonObject> o = parseKeyFile(keyFile, &error);
    if (!o || !isPasswordAcceptable(newPassword) || iterations == 0 || iterations > MaxIterations
        || o->value(u"keyId"_s).toString() != keyIdFor(notebookKey))
        return std::nullopt;
    o->insert(u"password"_s, passwordSection(newPassword, notebookKey, random, iterations));
    return QJsonDocument(*o).toJson(QJsonDocument::Indented);
}

std::optional<crypto::SecureBytes> parseRecoveryKey(QStringView typed)
{
    QString letters;
    for (const QChar c : typed) {
        if (!c.isSpace() && c != u'-')
            letters.append(c.toLower());
    }
    std::optional<QByteArray> bytes = crypto::decodeBase32(letters);
    crypto::wipe(letters);
    if (!bytes || bytes->size() != KeySize) {
        if (bytes)
            crypto::wipe(*bytes);
        return std::nullopt;
    }
    crypto::SecureBytes key(*bytes);
    crypto::wipe(*bytes);
    return key;
}

QString keyIdFor(const crypto::SecureBytes &notebookKey)
{
    return QString::fromLatin1(
            crypto::hkdf(crypto::Hash::Sha256, notebookKey.view(), {}, "brulion-sync key id v1", 8).toByteArray().toHex());
}

QString keyIdOf(QByteArrayView keyFile)
{
    UnlockError error = UnlockError::None;
    const std::optional<QJsonObject> o = parseKeyFile(keyFile, &error);
    return o ? o->value(u"keyId"_s).toString() : QString();
}

} // namespace brulion::sync::encryption

#include "encryption/meld.h"

#include "crypto/aead.h"
#include "crypto/base64.h"
#include "crypto/hash.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringDecoder>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::encryption {

using crypto::SecureBytes;
using markdown::EncryptedVersion;

namespace {

constexpr qsizetype KeySize = 32;
constexpr qsizetype TagSize = crypto::aesgcm::TagSize;

// v0: the plugin's fixed IV (CryptoHelperObsolete.algorithmObsolete).
constexpr unsigned char ObsoleteIv[12] = {196, 190, 240, 190, 188, 78, 41, 132, 15, 220, 84, 211};
// v1: the plugin's fixed salt (CryptoHelper).
constexpr QByteArrayView AlphaSalt = "XHWnDAT6ehMVY2zD";
constexpr quint32 AlphaIterations = 1000;
// v2: CryptoHelper2304(16, 16, 210000).
constexpr qsizetype BetaIvSize = 16;
constexpr qsizetype BetaSaltSize = 16;
constexpr quint32 BetaIterations = 210000;
// v3: suite 1.
constexpr char V3Suite = 1;
constexpr qsizetype V3SaltSize = 16;
constexpr qsizetype V3IvSize = 12;
constexpr qsizetype V3HeaderSize = 4 + V3SaltSize + V3IvSize; // suite, rounds, salt, IV

/// The plaintext's bytes as the browser's TextDecoder reads them: UTF-8, U+FFFD for invalid
/// sequences, a leading BOM dropped.
QString decodeUtf8(const SecureBytes &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Default);
    return decoder.decode(bytes.view());
}

/// The text a decryption gives, or WrongPassword.
Decrypted opened(const std::optional<SecureBytes> &plain)
{
    if (!plain)
        return {Failure::WrongPassword, {}};
    return {Failure::None, decodeUtf8(*plain)};
}

/// The v3 header as JSON.stringify writes {version, kdf, cipher} (the AAD; also the file's
/// members, same order). `indent` "" is compact.
QString v3Kdf(quint32 iterations, QStringView salt)
{
    return uR"({"name":"PBKDF2","hash":"SHA-512","iterations":%1,"salt":"%2"})"_s.arg(iterations).arg(salt);
}
QString v3Cipher(QStringView iv)
{
    return uR"({"name":"AES-GCM","keyLength":256,"iv":"%1","tagLength":128})"_s.arg(iv);
}
QByteArray v3Aad(quint32 iterations, QStringView salt, QStringView iv)
{
    return (uR"({"version":"3.0","kdf":)"_s + v3Kdf(iterations, salt) + uR"(,"cipher":)"_s + v3Cipher(iv) + u'}')
            .toUtf8();
}

SecureBytes deriveKey(crypto::Hash hash, const SecureBytes &password, QByteArrayView salt, quint32 iterations)
{
    return crypto::pbkdf2(hash, password.view(), salt, iterations, KeySize);
}

/// v3: open the ciphertext with the header's parameters.
Decrypted openV3(quint32 iterations, QByteArrayView salt, QByteArrayView iv, QByteArrayView sealed,
                 const SecureBytes &password)
{
    const SecureBytes key = deriveKey(crypto::Hash::Sha512, password, salt, iterations);
    const QByteArray aad = v3Aad(iterations, crypto::encodeBase64(salt), crypto::encodeBase64(iv));
    return opened(crypto::aesgcm::open(key.view(), iv, aad, sealed));
}

/// A v3 payload taken apart.
struct V3Payload {
    quint32 iterations = 0;
    QByteArray bytes; ///< the whole payload
};
std::optional<V3Payload> v3Payload(QStringView payload, Failure *failure)
{
    const auto bytes = crypto::decodeBase64(payload, crypto::Base64Mode::Strict);
    if (!bytes || bytes->size() < V3HeaderSize + TagSize) {
        *failure = Failure::Malformed;
        return std::nullopt;
    }
    if ((*bytes)[0] != V3Suite) {
        *failure = Failure::Unsupported;
        return std::nullopt;
    }
    const auto byte = [&](int i) { return quint32(static_cast<unsigned char>((*bytes)[i])); };
    const quint32 iterations = byte(1) << 16 | byte(2) << 8 | byte(3);
    if (iterations < MinIterations || iterations > MaxIterations) {
        *failure = Failure::Unsupported;
        return std::nullopt;
    }
    return V3Payload{iterations, *bytes};
}

/// A JSON string as JSON.stringify writes it: `"`, `\` and the control characters escaped (\b \f
/// \n \r \t, else \u00XX), lone surrogates as \uXXXX, everything else as is.
QString jsonString(QStringView s)
{
    QString out = u"\""_s;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const char16_t c = s[i].unicode();
        if (c == u'"')
            out += u"\\\""_s;
        else if (c == u'\\')
            out += u"\\\\"_s;
        else if (c == u'\b')
            out += u"\\b"_s;
        else if (c == u'\f')
            out += u"\\f"_s;
        else if (c == u'\n')
            out += u"\\n"_s;
        else if (c == u'\r')
            out += u"\\r"_s;
        else if (c == u'\t')
            out += u"\\t"_s;
        else if (c < 0x20)
            out += u"\\u%1"_s.arg(int(c), 4, 16, u'0');
        else if (QChar::isHighSurrogate(c) && i + 1 < s.size() && QChar::isLowSurrogate(s[i + 1].unicode())) {
            out += s[i];
            out += s[++i];
        } else if (QChar::isSurrogate(c))
            out += u"\\u%1"_s.arg(int(c), 4, 16, u'0');
        else
            out += s[i];
    }
    out += u'"';
    return out;
}

/// The members of `object` are all in `allowed`.
bool onlyKeys(const QJsonObject &object, std::initializer_list<QStringView> allowed)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const QStringView key : allowed)
            known = known || it.key() == key;
        if (!known)
            return false;
    }
    return true;
}

/// A whole number of JSON (a double) in [min, max], else nullopt.
std::optional<quint32> integerIn(const QJsonValue &value, quint32 min, quint32 max)
{
    if (!value.isDouble())
        return std::nullopt;
    const double d = value.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d || d < double(min) || d > double(max))
        return std::nullopt;
    return quint32(d);
}

/// A v3 file's parameters, checked as validateEnvelopeV3() checks them.
struct V3File {
    quint32 iterations = 0;
    QByteArray salt;
    QByteArray iv;
    QByteArray sealed;
};
std::optional<V3File> v3File(const QJsonObject &root, Failure *failure)
{
    *failure = Failure::Unsupported;
    if (!onlyKeys(root, {u"version", u"hint", u"kdf", u"cipher", u"encodedData"}))
        return std::nullopt;
    const QJsonValue kdfValue = root.value(u"kdf"_s);
    const QJsonValue cipherValue = root.value(u"cipher"_s);
    if (!kdfValue.isObject() || !cipherValue.isObject()) {
        *failure = Failure::Malformed;
        return std::nullopt;
    }
    const QJsonObject kdf = kdfValue.toObject();
    const QJsonObject cipher = cipherValue.toObject();
    if (!onlyKeys(kdf, {u"name", u"hash", u"iterations", u"salt"})
        || !onlyKeys(cipher, {u"name", u"keyLength", u"iv", u"tagLength"}))
        return std::nullopt;
    if (kdf.value(u"name"_s) != u"PBKDF2"_s || kdf.value(u"hash"_s) != u"SHA-512"_s
        || cipher.value(u"name"_s) != u"AES-GCM"_s || integerIn(cipher.value(u"keyLength"_s), 256, 256) != 256u
        || integerIn(cipher.value(u"tagLength"_s), 128, 128) != 128u)
        return std::nullopt;
    const auto iterations = integerIn(kdf.value(u"iterations"_s), MinIterations, MaxIterations);
    if (!iterations)
        return std::nullopt;
    *failure = Failure::Malformed;
    const QJsonValue saltValue = kdf.value(u"salt"_s);
    const QJsonValue ivValue = cipher.value(u"iv"_s);
    const QJsonValue dataValue = root.value(u"encodedData"_s);
    if (!saltValue.isString() || !ivValue.isString() || !dataValue.isString())
        return std::nullopt;
    if (dataValue.toString().size() > MaxEncodedSize) {
        *failure = Failure::TooLarge;
        return std::nullopt;
    }
    const auto salt = crypto::decodeBase64(saltValue.toString());
    const auto iv = crypto::decodeBase64(ivValue.toString());
    const auto sealed = crypto::decodeBase64(dataValue.toString());
    if (!salt || !iv || !sealed || salt->size() != V3SaltSize || iv->size() != V3IvSize || sealed->size() < TagSize)
        return std::nullopt;
    *failure = Failure::None;
    return V3File{*iterations, *salt, *iv, *sealed};
}

/// The file's JSON object, checked as JsonFileEncoding.decode() checks it.
std::optional<QJsonObject> fileObject(QStringView fileText, Failure *failure)
{
    if (fileText.size() > MaxEncodedSize) {
        *failure = Failure::TooLarge;
        return std::nullopt;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(fileText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        *failure = Failure::Malformed;
        return std::nullopt;
    }
    const QJsonObject root = doc.object();
    if (!root.value(u"version"_s).isString() || !root.value(u"hint"_s).isString()
        || !root.value(u"encodedData"_s).isString()) {
        *failure = Failure::Malformed;
        return std::nullopt;
    }
    return root;
}

} // namespace

Decrypted decryptPayload(EncryptedVersion version, QStringView payload, const SecureBytes &password)
{
    if (payload.size() > MaxEncodedSize)
        return {Failure::TooLarge, {}};
    if (version == EncryptedVersion::Gamma) {
        Failure failure = Failure::None;
        const auto v3 = v3Payload(payload, &failure);
        if (!v3)
            return {failure, {}};
        const QByteArrayView bytes(v3->bytes);
        return openV3(v3->iterations, bytes.sliced(4, V3SaltSize), bytes.sliced(4 + V3SaltSize, V3IvSize),
                      bytes.sliced(V3HeaderSize), password);
    }
    // The older readers decode as atob() does.
    const auto decoded = crypto::decodeBase64(payload, crypto::Base64Mode::Forgiving);
    if (!decoded)
        return {Failure::Malformed, {}};
    const QByteArrayView bytes(*decoded);
    switch (version) {
    case EncryptedVersion::Obsolete: {
        if (bytes.size() < TagSize)
            return {Failure::Malformed, {}};
        QByteArray digest = crypto::digest(crypto::Hash::Sha256, password.view());
        const SecureBytes key(digest);
        crypto::wipe(digest);
        return opened(crypto::aesgcm::open(
                key.view(), QByteArrayView(reinterpret_cast<const char *>(ObsoleteIv), sizeof ObsoleteIv), {}, bytes));
    }
    case EncryptedVersion::Alpha: {
        if (bytes.size() < BetaIvSize + TagSize)
            return {Failure::Malformed, {}};
        const SecureBytes key = deriveKey(crypto::Hash::Sha256, password, AlphaSalt, AlphaIterations);
        return opened(crypto::aesgcm::open(key.view(), bytes.first(BetaIvSize), {}, bytes.sliced(BetaIvSize)));
    }
    case EncryptedVersion::Beta: {
        if (bytes.size() < BetaIvSize + BetaSaltSize + TagSize)
            return {Failure::Malformed, {}};
        const SecureBytes key =
                deriveKey(crypto::Hash::Sha512, password, bytes.sliced(BetaIvSize, BetaSaltSize), BetaIterations);
        return opened(
                crypto::aesgcm::open(key.view(), bytes.first(BetaIvSize), {}, bytes.sliced(BetaIvSize + BetaSaltSize)));
    }
    case EncryptedVersion::Gamma: break;
    }
    return {Failure::Unsupported, {}};
}

QString encryptPayload(QStringView plaintext, const SecureBytes &password, const crypto::RandomSource &random,
                       quint32 iterations)
{
    if (iterations < MinIterations || iterations > MaxIterations)
        iterations = DefaultIterations;
    const QByteArray salt = crypto::randomBytes(V3SaltSize, random);
    const QByteArray iv = crypto::randomBytes(V3IvSize, random);
    const SecureBytes key = deriveKey(crypto::Hash::Sha512, password, salt, iterations);
    const SecureBytes plain = SecureBytes::fromUtf8(plaintext);
    const QByteArray aad = v3Aad(iterations, crypto::encodeBase64(salt), crypto::encodeBase64(iv));
    QByteArray bytes;
    bytes.reserve(V3HeaderSize + plain.size() + TagSize);
    bytes.append(V3Suite);
    bytes.append(char(iterations >> 16 & 0xFF));
    bytes.append(char(iterations >> 8 & 0xFF));
    bytes.append(char(iterations & 0xFF));
    bytes.append(salt);
    bytes.append(iv);
    bytes.append(crypto::aesgcm::seal(key.view(), iv, aad, plain.view()));
    return crypto::encodeBase64(bytes);
}

quint32 payloadIterations(EncryptedVersion version, QStringView payload)
{
    switch (version) {
    case EncryptedVersion::Obsolete: return 1;
    case EncryptedVersion::Alpha: return AlphaIterations;
    case EncryptedVersion::Beta: return BetaIterations;
    case EncryptedVersion::Gamma: {
        // The header is in the first 8 Base64 characters (6 bytes).
        const auto head =
                crypto::decodeBase64(payload.first(qMin<qsizetype>(8, payload.size())), crypto::Base64Mode::Forgiving);
        if (!head || head->size() < 4 || (*head)[0] != V3Suite)
            return 0;
        const auto byte = [&](int i) { return quint32(static_cast<unsigned char>((*head)[i])); };
        const quint32 iterations = byte(1) << 16 | byte(2) << 8 | byte(3);
        return iterations >= MinIterations && iterations <= MaxIterations ? iterations : 0;
    }
    }
    return 0;
}

std::optional<NoteFile> readNoteFile(QStringView fileText, Failure *failure)
{
    Failure local = Failure::None;
    Failure &why = failure ? *failure : local;
    why = Failure::None;
    NoteFile out;
    if (fileText.isEmpty()) {
        out.blank = true;
        return out;
    }
    const auto root = fileObject(fileText, &why);
    if (!root)
        return std::nullopt;
    out.version = root->value(u"version"_s).toString();
    out.hint = root->value(u"hint"_s).toString();
    if (out.version == u"1.0") {
        out.iterations = AlphaIterations;
    } else if (out.version == u"2.0") {
        out.iterations = BetaIterations;
    } else if (out.version == u"3.0") {
        const auto v3 = v3File(*root, &why);
        if (!v3)
            return std::nullopt;
        out.iterations = v3->iterations;
    } else {
        why = Failure::Unsupported;
        return std::nullopt;
    }
    return out;
}

Decrypted decryptNote(QStringView fileText, const SecureBytes &password)
{
    if (fileText.isEmpty())
        return {Failure::None, {}};
    Failure failure = Failure::None;
    const auto root = fileObject(fileText, &failure);
    if (!root)
        return {failure, {}};
    const QString version = root->value(u"version"_s).toString();
    const QString encoded = root->value(u"encodedData"_s).toString();
    if (version == u"3.0") {
        const auto v3 = v3File(*root, &failure);
        if (!v3)
            return {failure, {}};
        return openV3(v3->iterations, v3->salt, v3->iv, v3->sealed, password);
    }
    if (version == u"1.0" || version == u"2.0") {
        if (encoded.isEmpty())
            return {Failure::None, {}}; // (the plugin's FileDataHelper.decrypt)
        return decryptPayload(version == u"1.0" ? EncryptedVersion::Alpha : EncryptedVersion::Beta, encoded, password);
    }
    return {Failure::Unsupported, {}};
}

QString writeNoteFile(QStringView markdown, const SecureBytes &password, QStringView hint,
                      const crypto::RandomSource &random, quint32 iterations)
{
    // (The salt is drawn before the IV, as the plugin draws them.)
    const NoteKey key = deriveNoteKey(password, random, iterations);
    return writeNoteFile(markdown, key, hint, random);
}

NoteKey deriveNoteKey(const SecureBytes &password, const crypto::RandomSource &random, quint32 iterations)
{
    if (iterations < MinIterations || iterations > MaxIterations)
        iterations = DefaultIterations;
    NoteKey out;
    out.salt = crypto::randomBytes(V3SaltSize, random);
    out.iterations = iterations;
    out.key = deriveKey(crypto::Hash::Sha512, password, out.salt, iterations);
    return out;
}

QString writeNoteFile(QStringView markdown, const NoteKey &key, QStringView hint, const crypto::RandomSource &random)
{
    if (key.isNull() || key.salt.size() != V3SaltSize)
        return {};
    const QByteArray iv = crypto::randomBytes(V3IvSize, random);
    const QString saltText = crypto::encodeBase64(key.salt);
    const QString ivText = crypto::encodeBase64(iv);
    const SecureBytes plain = SecureBytes::fromUtf8(markdown);
    const QByteArray sealed =
            crypto::aesgcm::seal(key.key.view(), iv, v3Aad(key.iterations, saltText, ivText), plain.view());
    // JSON.stringify(fileData, null, 2): FileData's own fields first (version, hint, encodedData),
    // then kdf and cipher as Object.assign added them.
    return u"{\n  \"version\": \"3.0\",\n  \"hint\": "_s + jsonString(hint) + u",\n  \"encodedData\": \""_s
           + crypto::encodeBase64(sealed)
           + u"\",\n  \"kdf\": {\n    \"name\": \"PBKDF2\",\n    \"hash\": \"SHA-512\",\n    \"iterations\": "_s
           + QString::number(key.iterations) + u",\n    \"salt\": \""_s + saltText
           + u"\"\n  },\n  \"cipher\": {\n    \"name\": \"AES-GCM\",\n    \"keyLength\": 256,\n    \"iv\": \""_s
           + ivText + u"\",\n    \"tagLength\": 128\n  }\n}"_s;
}

OpenedNote openNote(QStringView fileText, const SecureBytes &password, const crypto::RandomSource &random)
{
    OpenedNote out;
    Failure failure = Failure::None;
    if (fileText.isEmpty()) {
        out.key = deriveNoteKey(password, random);
        return out;
    }
    const auto root = fileObject(fileText, &failure);
    if (!root) {
        out.failure = failure;
        return out;
    }
    out.hint = root->value(u"hint"_s).toString();
    const QString version = root->value(u"version"_s).toString();
    if (version == u"3.0") {
        const auto v3 = v3File(*root, &failure);
        if (!v3) {
            out.failure = failure;
            return out;
        }
        NoteKey key;
        key.salt = v3->salt;
        key.iterations = v3->iterations;
        key.key = deriveKey(crypto::Hash::Sha512, password, v3->salt, v3->iterations);
        const Decrypted d = opened(crypto::aesgcm::open(
                key.key.view(), v3->iv,
                v3Aad(v3->iterations, crypto::encodeBase64(v3->salt), crypto::encodeBase64(v3->iv)), v3->sealed));
        out.failure = d.failure;
        out.text = d.text;
        if (d.ok())
            out.key = std::move(key);
        return out;
    }
    const Decrypted d = decryptNote(fileText, password);
    out.failure = d.failure;
    out.text = d.text;
    if (d.ok())
        out.key = deriveNoteKey(password, random);
    return out;
}

OpenedNote openNoteWithKey(QStringView fileText, const NoteKey &key)
{
    OpenedNote out;
    out.failure = Failure::WrongPassword;
    if (key.isNull())
        return out;
    Failure failure = Failure::None;
    if (fileText.isEmpty()) {
        out.failure = Failure::None;
        out.key = key;
        return out;
    }
    const auto root = fileObject(fileText, &failure);
    if (!root) {
        out.failure = failure;
        return out;
    }
    out.hint = root->value(u"hint"_s).toString();
    if (root->value(u"version"_s).toString() != u"3.0")
        return out; // an older file: derived from the password
    const auto v3 = v3File(*root, &failure);
    if (!v3) {
        out.failure = failure;
        return out;
    }
    if (v3->salt != key.salt || v3->iterations != key.iterations)
        return out; // another key: derived from the password
    const Decrypted d = opened(crypto::aesgcm::open(
            key.key.view(), v3->iv, v3Aad(v3->iterations, crypto::encodeBase64(v3->salt), crypto::encodeBase64(v3->iv)),
            v3->sealed));
    out.failure = d.failure;
    out.text = d.text;
    if (d.ok())
        out.key = key;
    return out;
}

QString failureMessage(Failure failure)
{
    switch (failure) {
    case Failure::None: return {};
    case Failure::WrongPassword:
        return QCoreApplication::translate("brulion::encryption",
                                           "Wrong password (or the encrypted text was changed).");
    case Failure::Malformed:
        return QCoreApplication::translate("brulion::encryption", "The encrypted text is damaged and cannot be read.");
    case Failure::Unsupported:
        return QCoreApplication::translate("brulion::encryption",
                                           "This encrypted text uses a format this version of Brulion does not know.");
    case Failure::TooLarge:
        return QCoreApplication::translate("brulion::encryption", "The encrypted text is too large to open.");
    }
    return {};
}

} // namespace brulion::encryption

#include "crypto/secrethash.h"

#include "crypto/base64.h"
#include "crypto/hash.h"
#include "crypto/secure.h"

namespace brulion::crypto {

namespace {

constexpr QStringView Scheme = u"pbkdf2-sha512";
constexpr qsizetype SaltSize = 16;
constexpr qsizetype HashSize = 32;

} // namespace

QString hashSecret(QStringView secret, const RandomSource &random, quint32 iterations)
{
    if (secret.isEmpty() || iterations < MinSecretIterations || iterations > MaxSecretIterations)
        return {};
    const QByteArray salt = randomBytes(SaltSize, random);
    const SecureBytes password = SecureBytes::fromUtf8(secret);
    const SecureBytes key = pbkdf2(Hash::Sha512, password.view(), salt, iterations, HashSize);
    return Scheme.toString() + u'$' + QString::number(iterations) + u'$' + encodeBase64(salt) + u'$'
           + encodeBase64(key.view());
}

std::optional<SecretHash> parseSecretHash(QStringView stored)
{
    const QList<QStringView> parts = stored.split(u'$');
    if (parts.size() != 4 || parts[0] != Scheme)
        return std::nullopt;
    // Digits only: no sign, no spaces, no leading zero.
    const QStringView rounds = parts[1];
    if (rounds.isEmpty() || rounds.size() > 9 || rounds.front() == u'0')
        return std::nullopt;
    for (const QChar c : rounds) {
        if (c < u'0' || c > u'9')
            return std::nullopt;
    }
    SecretHash out;
    out.iterations = rounds.toUInt();
    if (out.iterations < MinSecretIterations || out.iterations > MaxSecretIterations)
        return std::nullopt;
    const auto salt = decodeBase64(parts[2]);
    const auto hash = decodeBase64(parts[3]);
    if (!salt || !hash || salt->size() != SaltSize || hash->size() != HashSize)
        return std::nullopt;
    out.salt = *salt;
    out.hash = *hash;
    return out;
}

bool verifySecret(QStringView stored, QStringView secret)
{
    const auto parsed = parseSecretHash(stored);
    if (!parsed || secret.isEmpty())
        return false;
    const SecureBytes password = SecureBytes::fromUtf8(secret);
    const SecureBytes key = pbkdf2(Hash::Sha512, password.view(), parsed->salt, parsed->iterations, HashSize);
    return constantTimeEquals(key.view(), parsed->hash);
}

} // namespace brulion::crypto

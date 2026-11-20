// brulion::crypto — stored hashes of short secrets: the app lock's PIN (see crypto/crypto.h).
#pragma once

#include "crypto/random.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::crypto {

/// Rounds of PBKDF2-HMAC-SHA512 for a new hash: about 0.1 s on a laptop, 0.3–0.5 s on a phone.
inline constexpr quint32 DefaultSecretIterations = 210000;
/// The fewest and the most rounds a stored hash may name (verifySecret() refuses others: a forged
/// setting must neither weaken the hash nor stall the app).
inline constexpr quint32 MinSecretIterations = 10000;
inline constexpr quint32 MaxSecretIterations = 10000000;

/// A stored hash taken apart.
struct SecretHash {
    quint32 iterations = 0;
    QByteArray salt; ///< 16 bytes
    QByteArray hash; ///< 32 bytes
};

/// A salted slow hash of `secret` (its UTF-8; NFC is the caller's business) as one printable
/// string: `pbkdf2-sha512$<iterations>$<salt, Base64>$<hash, Base64>` — 16 random bytes of salt
/// (`random`, empty: the system's), 32 bytes of PBKDF2-HMAC-SHA512. Empty for an empty secret or
/// iterations outside Min…MaxSecretIterations.
QString hashSecret(QStringView secret, const RandomSource &random = {}, quint32 iterations = DefaultSecretIterations);

/// The parts of a string hashSecret() made; nullopt for anything else (another scheme, Base64
/// that is not canonical, other lengths, rounds out of bounds).
std::optional<SecretHash> parseSecretHash(QStringView stored);

/// Does `secret` match `stored` (hashSecret()'s string)? The hashes are compared in constant time;
/// a malformed `stored` matches nothing.
bool verifySecret(QStringView stored, QStringView secret);

} // namespace brulion::crypto

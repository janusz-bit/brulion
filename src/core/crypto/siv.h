// brulion::crypto — AES-CMAC and AES-SIV (see crypto/crypto.h).
//
// AES-SIV (RFC 5297) is deterministic authenticated encryption: the same key, associated data and
// plaintext always give the same ciphertext, and any change is refused. For what must be found
// again by its encrypted form — a file's name on a sync server (docs/SYNC-ENCRYPTION.md) —, as
// Cryptomator and Syncthing's untrusted devices encrypt names. It hides everything but equality
// (and the length); data that may repeat without that leaking anything goes through AES-GCM with
// a random IV instead (aead.h).
#pragma once

#include "crypto/secure.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>

#include <optional>

namespace brulion::crypto {

namespace aescmac {

/// Bytes of a CMAC: one AES block.
inline constexpr qsizetype MacSize = 16;

/// AES-CMAC (RFC 4493, NIST SP 800-38B) of `message` under `key` (16, 24 or 32 bytes). Empty
/// for another key size.
QByteArray mac(QByteArrayView key, QByteArrayView message);

} // namespace aescmac

namespace aessiv {

/// Bytes of the synthetic IV in front of the ciphertext.
inline constexpr qsizetype IvSize = 16;
/// The most associated data RFC 5297 takes (its S2V ends after 126 strings and the plaintext).
inline constexpr qsizetype MaxAssociatedData = 126;

/// Is `size` a key size of AES-SIV: 32, 48 or 64 bytes (two AES keys: CMAC's, then CTR's).
bool isKeySize(qsizetype size);

/// AES-SIV encryption (RFC 5297) of `plaintext` with `key`, authenticating each of `ad` (in
/// order): the 16-byte synthetic IV followed by the ciphertext, as long as the plaintext. Empty
/// for a wrong key size or more than MaxAssociatedData.
QByteArray seal(QByteArrayView key, const QList<QByteArrayView> &ad, QByteArrayView plaintext);

/// The plaintext of `sealed` (as seal() makes it), nullopt when it was not sealed with this key
/// and these associated data — any changed byte —, when it is shorter than the IV, or for a wrong
/// key size. The IV is compared in constant time.
std::optional<SecureBytes> open(QByteArrayView key, const QList<QByteArrayView> &ad, QByteArrayView sealed);

} // namespace aessiv

} // namespace brulion::crypto

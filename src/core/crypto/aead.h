// brulion::crypto — authenticated encryption: AES-GCM (see crypto/crypto.h).
#pragma once

#include "crypto/secure.h"

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

namespace brulion::crypto::aesgcm {

/// Bytes of the authentication tag sealed after the ciphertext (128 bits, the full tag).
inline constexpr qsizetype TagSize = 16;
/// The IV length GCM is made for (96 bits): any other is hashed into one first (NIST SP 800-38D),
/// which Meld Encrypt's older formats use (16 bytes).
inline constexpr qsizetype StandardIvSize = 12;

/// Is `size` a key size of AES: 16, 24 or 32 bytes.
bool isKeySize(qsizetype size);

/// AES-GCM encryption (NIST SP 800-38D) of `plaintext` with `key` (16, 24 or 32 bytes: AES-128,
/// -192, -256) and `iv` (not empty; never use one twice with a key), authenticating `aad` too:
/// the ciphertext followed by the 16-byte tag — WebCrypto's and Meld Encrypt's layout. Empty for
/// a wrong key size or an empty IV (an empty plaintext still gives the 16-byte tag).
QByteArray seal(QByteArrayView key, QByteArrayView iv, QByteArrayView aad, QByteArrayView plaintext);

/// The plaintext of `sealed` (ciphertext + tag, as seal() makes it), nullopt when the tag does not
/// match — a wrong key, another IV or AAD, any changed byte — (checked in constant time before any
/// plaintext is handed out), when `sealed` is shorter than a tag, or for a wrong key size or IV.
std::optional<SecureBytes> open(QByteArrayView key, QByteArrayView iv, QByteArrayView aad, QByteArrayView sealed);

} // namespace brulion::crypto::aesgcm

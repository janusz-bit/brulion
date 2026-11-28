// brulion::crypto — Base64 and base32 as formats with secrets write them (see crypto/crypto.h).
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::crypto {

/// How decodeBase64() reads.
enum class Base64Mode {
    /// The standard alphabet (RFC 4648 §4), padded to a multiple of 4, nothing else, and canonical:
    /// the bytes encode back to exactly the text (no stray bits in the last character). What a
    /// format that authenticates its metadata needs (Meld Encrypt's v3).
    Strict,
    /// As a browser's atob() (the "forgiving-base64 decode" of the HTML standard): ASCII white
    /// space ignored, padding optional, the last character's unused bits ignored; still only the
    /// standard alphabet.
    Forgiving,
};

/// `bytes` in the standard alphabet, padded.
QString encodeBase64(QByteArrayView bytes);
/// The bytes `text` encodes, nullopt when it is not Base64 by `mode`'s rules.
std::optional<QByteArray> decodeBase64(QStringView text, Base64Mode mode = Base64Mode::Strict);

/// `bytes` in RFC 4648's base32 alphabet in lower case, without padding: names that survive file
/// systems which ignore case (docs/SYNC-ENCRYPTION.md "Names"), keys people type.
QString encodeBase32(QByteArrayView bytes);
/// The bytes of canonical lowercase unpadded base32 (as encodeBase32() writes it: no stray bits in
/// the last letter); nullopt for anything else.
std::optional<QByteArray> decodeBase32(QStringView text);

} // namespace brulion::crypto

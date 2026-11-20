// brulion::encryption — Meld Encrypt's formats (see encryption/encryption.h).
//
// Inline fragments (markdown/encrypted.h finds them) carry a Base64 payload:
//   v0  obsolete  AES-256-GCM, key SHA-256(password), the plugin's fixed 12-byte IV; the ciphertext
//   v1  α         AES-256-GCM, key PBKDF2-SHA256(password, fixed salt "XHWnDAT6ehMVY2zD", 1000);
//                 16-byte IV ‖ ciphertext
//   v2  β         AES-256-GCM, key PBKDF2-SHA512(password, salt, 210 000); 16-byte IV ‖ 16-byte
//                 salt ‖ ciphertext
//   v3  γ         suite 1 (PBKDF2-SHA512 / AES-256-GCM) ‖ rounds (3 bytes, big-endian, 220 000 …
//                 2 000 000) ‖ 16-byte salt ‖ 12-byte IV ‖ ciphertext; the AAD is the compact JSON
//                 header {"version":"3.0","kdf":{…},"cipher":{…}}, so the parameters are
//                 authenticated; strict, canonical Base64
// (the ciphertext always ends with the 16-byte GCM tag). v0, v1 and v2 are read only — a fixed IV
// or salt is too weak to write again —; every write is v3, as the plugin's own.
//
// A whole note (`.mdenc`) is JSON: {"version":"1.0"|"2.0", "hint", "encodedData": v1 | v2 payload}
// or {"version":"3.0", "hint", "encodedData": ciphertext, "kdf": {name, hash, iterations, salt},
// "cipher": {name, keyLength, iv, tagLength}} (unknown keys refused, the AAD rebuilt from the
// values); an empty file is a blank note (its password not chosen yet).
#pragma once

#include "crypto/random.h"
#include "crypto/secure.h"
#include "markdown/encrypted.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::encryption {

/// Rounds of PBKDF2-SHA512 Brulion writes (v3: Meld Encrypt's default, OWASP's figure).
inline constexpr quint32 DefaultIterations = 220000;
/// The rounds a v3 payload may name (Meld Encrypt's reader bounds).
inline constexpr quint32 MinIterations = 220000;
inline constexpr quint32 MaxIterations = 2000000;
/// The largest ciphertext read (Base64 characters of a payload or a note): bigger is refused
/// before anything is decoded.
inline constexpr qsizetype MaxEncodedSize = 96 * 1024 * 1024;

/// Why something could not be decrypted or read.
enum class Failure {
    None,
    WrongPassword, ///< the tag did not match: another password — or a changed payload
    Malformed,     ///< not the format: bad Base64, wrong lengths, broken JSON, missing fields
    Unsupported,   ///< a format this version does not know: another suite or version, rounds out of bounds
    TooLarge,      ///< more than MaxEncodedSize
};

/// A decryption's result: the text, or why there is none. `text` is the caller's to wipe
/// (crypto::wipe) once it is not needed.
struct Decrypted {
    Failure failure = Failure::None;
    QString text;
    bool ok() const { return failure == Failure::None; }
};

/// The plaintext of an inline payload (EncryptedSpan's Base64) of `version` under `password`
/// (its UTF-8). Invalid UTF-8 in the plaintext becomes U+FFFD and a leading BOM is dropped, as the
/// browser's TextDecoder reads it.
Decrypted decryptPayload(markdown::EncryptedVersion version, QStringView payload, const crypto::SecureBytes &password);

/// A v3 (γ) payload of `plaintext` (its UTF-8) under `password`: fresh salt and IV from `random`
/// (empty: the system's), `iterations` rounds (MinIterations … MaxIterations; DefaultIterations
/// otherwise). Always succeeds.
QString encryptPayload(QStringView plaintext, const crypto::SecureBytes &password,
                       const crypto::RandomSource &random = {}, quint32 iterations = DefaultIterations);

/// The rounds a payload costs to try (0 when it is not even well-formed): v3's from its header,
/// 210 000 for v2, 1000 for v1, 1 for v0. What a caller weighs before trying passwords.
quint32 payloadIterations(markdown::EncryptedVersion version, QStringView payload);

/// A whole encrypted note's file, read (readNoteFile()).
struct NoteFile {
    QString version;        ///< "1.0", "2.0", "3.0"; "" for a blank file
    QString hint;           ///< public ("" none)
    bool blank = false;     ///< an empty file: no password chosen yet, its text is ""
    quint32 iterations = 0; ///< the rounds a try costs
};

/// The file text `fileText` of a `.mdenc` note read (not decrypted): nullopt with `failure` set for
/// one that is not such a note (Malformed, Unsupported, TooLarge).
std::optional<NoteFile> readNoteFile(QStringView fileText, Failure *failure = nullptr);

/// The note's Markdown, decrypted. A blank file gives "" with any password.
Decrypted decryptNote(QStringView fileText, const crypto::SecureBytes &password);

/// The file text of a `.mdenc` note holding `markdown` under `password`, v3, with the public `hint`
/// — exactly what Meld Encrypt writes (JSON.stringify of {version, hint, encodedData, kdf, cipher}
/// indented by two spaces, no final line break).
QString writeNoteFile(QStringView markdown, const crypto::SecureBytes &password, QStringView hint = {},
                      const crypto::RandomSource &random = {}, quint32 iterations = DefaultIterations);

/// The key of an open note: derived once when it is unlocked (PBKDF2, the slow part), then used for
/// every save while it is open — a save costs AES-GCM only, with a fresh IV each time (the salt and
/// rounds stay those of the key; GCM's security rests on the IV never repeating under a key, and 96
/// random bits do not in any number of saves a note sees).
struct NoteKey {
    crypto::SecureBytes key; ///< 32 bytes
    QByteArray salt;         ///< 16 bytes
    quint32 iterations = 0;
    bool isNull() const { return key.isEmpty(); }
};

/// A key with a fresh salt from `random` (a new note, a new password, an older note's first v3 save).
NoteKey deriveNoteKey(const crypto::SecureBytes &password, const crypto::RandomSource &random = {},
                      quint32 iterations = DefaultIterations);

/// An encrypted note opened: its Markdown and the key to save it again with.
struct OpenedNote {
    Failure failure = Failure::None;
    QString text; ///< the caller's to wipe
    QString hint;
    /// A v3 file: its own salt and rounds (saves keep them); an older or blank one: a fresh v3 key
    /// (its next save writes v3, which the plugin reads too).
    NoteKey key;
    bool ok() const { return failure == Failure::None; }
};

/// decryptNote() keeping the key: one derivation for the file (two for a 1.0 / 2.0 file).
OpenedNote openNote(QStringView fileText, const crypto::SecureBytes &password, const crypto::RandomSource &random = {});
/// The same with a key the caller holds already (a file changed elsewhere and read again): no
/// derivation when the file has the key's salt and rounds, else Failure::WrongPassword — the caller
/// derives from the password then (openNote()).
OpenedNote openNoteWithKey(QStringView fileText, const NoteKey &key);

/// writeNoteFile() with a key held already (the note's own: no derivation).
QString writeNoteFile(QStringView markdown, const NoteKey &key, QStringView hint,
                      const crypto::RandomSource &random = {});

/// A user-facing English sentence for a failure (translated by the app in context
/// "brulion::encryption").
QString failureMessage(Failure failure);

} // namespace brulion::encryption

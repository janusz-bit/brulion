// The formats of the end-to-end encrypted sync (docs/SYNC-ENCRYPTION.md): the keys made from a
// notebook key, names of files and folders (AES-256-SIV, lowercase base32 — the same name always
// gives the same encrypted name), and file contents (a 32-byte header, then AES-256-GCM in chunks of
// 64 KiB with the STREAM construction's counter and last-chunk flag in the nonce, under a key of the
// file's own). Pure functions and two small state machines for contents in pieces; nothing here
// knows about WebDAV (EncryptingTransport does). Any thread.
#pragma once

#include "crypto/random.h"
#include "crypto/secure.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::sync::encryption {

/// Bytes of a notebook key (random: systemRandom()).
inline constexpr qsizetype NotebookKeySize = 32;
/// Bytes of an encrypted file's header: "BRLN", version, chunk size's log2, two zeros, the nonce.
inline constexpr qsizetype HeaderSize = 32;
/// Bytes of plaintext per chunk (every chunk but the last is full).
inline constexpr qsizetype ChunkSize = 64 * 1024;
/// Bytes a chunk grows by: GCM's tag.
inline constexpr qsizetype TagSize = 16;
/// The longest encrypted name: what servers allow for a name (an original name of up to 143 bytes
/// of UTF-8 fits).
inline constexpr qsizetype MaxEncryptedName = 255;

/// The keys of a notebook's encrypted sync, made from its notebook key (HKDF-SHA-256).
struct Keys {
    crypto::SecureBytes names;   ///< 64 bytes: AES-256-SIV
    crypto::SecureBytes content; ///< 32 bytes: the files' keys are made from it

    /// Empty keys (not isValid()) for a notebook key that is not NotebookKeySize bytes.
    static Keys fromNotebookKey(QByteArrayView notebookKey);
    bool isValid() const { return names.size() == 64 && content.size() == 32; }
};

/// The encrypted form of one name (a segment of a path, not empty): nullopt when it is empty or
/// its encrypted form would be longer than MaxEncryptedName.
std::optional<QString> encryptName(const Keys &keys, const QString &name);
/// The name `encrypted` stands for; nullopt for a name not encrypted with these keys (a plain
/// file's, one changed or made up on the server) — only canonical lowercase base32 is taken.
std::optional<QString> decryptName(const Keys &keys, QStringView encrypted);
/// Every segment of a '/'-separated path encrypted ("" stays ""): nullopt when one cannot be.
std::optional<QString> encryptPath(const Keys &keys, const QString &path);
/// Every segment decrypted: nullopt when one does not decrypt.
std::optional<QString> decryptPath(const Keys &keys, const QString &path);

/// The size of a file of `plainSize` bytes once encrypted.
qint64 encryptedSize(qint64 plainSize);
/// The plain size of an encrypted file of `encryptedSize` bytes; nullopt for a size no encrypted
/// file has.
std::optional<qint64> plainSize(qint64 encryptedSize);

/// A file's contents encrypted as they come: header() first, then what add() and finish() give, in
/// order. Holds at most a chunk of plaintext (wiped memory).
class Encryptor {
public:
    /// `random` makes the file's nonce (empty: the system's generator).
    explicit Encryptor(const Keys &keys, const crypto::RandomSource &random = {});
    QByteArray header() const { return m_header; }
    /// The chunks completed by `plain` (a full chunk waits until it is known not to be the last).
    QByteArray add(QByteArrayView plain);
    /// The last chunk (possibly empty: an empty file). Call once.
    QByteArray finish();

private:
    QByteArray sealChunk(QByteArrayView plain, bool last);
    QByteArray m_header;
    crypto::SecureBytes m_key;
    crypto::SecureBytes m_pending; ///< plaintext not sealed yet (≤ ChunkSize)
    qsizetype m_pendingSize = 0;
    quint64 m_index = 0;
};

/// An encrypted file's contents decrypted as they come, each chunk checked before its plaintext is
/// handed out. Holds at most two chunks. After a refusal every call is refused.
class Decryptor {
public:
    explicit Decryptor(const Keys &keys);
    /// The plaintext of the chunks `sealed` completes (a full chunk waits until it is known not to
    /// be the last); nullopt when the file is not one of these keys' or was changed.
    std::optional<QByteArray> add(QByteArrayView sealed);
    /// The last chunk's plaintext; nullopt when the file ended early (no last chunk), was changed
    /// or extended.
    std::optional<QByteArray> finish();

private:
    bool readHeader();
    std::optional<QByteArray> openChunk(QByteArrayView sealed, bool last);
    crypto::SecureBytes m_content;
    QByteArray m_buffer;
    QByteArray m_header;
    crypto::SecureBytes m_key;
    quint64 m_index = 0;
    bool m_failed = false;
};

/// A whole file encrypted (header and chunks).
QByteArray seal(const Keys &keys, QByteArrayView plain, const crypto::RandomSource &random = {});
/// A whole file decrypted; nullopt as Decryptor refuses.
std::optional<crypto::SecureBytes> open(const Keys &keys, QByteArrayView sealed);

} // namespace brulion::sync::encryption

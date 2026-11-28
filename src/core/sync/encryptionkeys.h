// The key file of an end-to-end encrypted sync (docs/SYNC-ENCRYPTION.md "Keys", "Recovery"):
// `.brulion/encryption.json` in the notebook's server folder, plain JSON holding the notebook key
// sealed twice with AES-256-GCM — once under a key from the password (PBKDF2-HMAC-SHA-512), once
// under a key from the recovery key (HKDF-SHA-256: it is random already). Either opens it; the
// password changes without anything else being encrypted again. Pure functions; any thread
// (unlocking with the password costs its rounds: ~0.3 s on a laptop — off the GUI thread).
#pragma once

#include "crypto/random.h"
#include "crypto/secure.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QLatin1StringView>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::sync::encryption {

/// The key file's path in the server folder: under `.brulion/`, which no sync lists (an
/// unencrypted device never downloads it), and plain (encrypted names are not for it).
inline constexpr QLatin1StringView KeyFilePath{".brulion/encryption.json"};
/// PBKDF2's rounds for a new key file (an attacker who has the file pays them per guess).
inline constexpr quint32 DefaultIterations = 600'000;
/// The most rounds a key file may ask for (a hostile server could otherwise ask for hours).
inline constexpr quint32 MaxIterations = 20'000'000;
/// The shortest password a key file is made with (characters, spaces around not counted).
inline constexpr int MinPasswordLength = 12;

struct NewNotebook {
    QByteArray keyFile;              ///< the JSON for the server
    crypto::SecureBytes notebookKey; ///< 32 random bytes (encryption.h: Keys::fromNotebookKey)
    QString recoveryKey;             ///< to be shown once: 52 base32 characters in groups of four
};

enum class UnlockError {
    None,
    Malformed,      ///< not a key file, or one with values no Brulion writes
    UnknownVersion, ///< a key file of a later format
    WrongSecret,    ///< the password or recovery key does not open it (or it was changed)
};

struct Unlocked {
    crypto::SecureBytes notebookKey; ///< empty unless error is None
    UnlockError error = UnlockError::None;
};

/// PBKDF2's rounds a key file may have: 1 to MaxIterations.
bool isIterationCountAcceptable(qint64 iterations);
/// The password is long enough for a key file (MinPasswordLength).
bool isPasswordAcceptable(QStringView password);
/// A new notebook key and its key file; nullopt for a password that is not acceptable.
std::optional<NewNotebook> createKeyFile(QStringView password, const crypto::RandomSource &random = {},
                                         quint32 iterations = DefaultIterations);
Unlocked unlockWithPassword(QByteArrayView keyFile, QStringView password);
Unlocked unlockWithRecoveryKey(QByteArrayView keyFile, QStringView recoveryKey);
/// The key file with the notebook key sealed under `newPassword` (the recovery key and the key id
/// unchanged); nullopt when `notebookKey` is not the file's or the password is not acceptable.
std::optional<QByteArray> changePassword(QByteArrayView keyFile, const crypto::SecureBytes &notebookKey,
                                         QStringView newPassword, const crypto::RandomSource &random = {},
                                         quint32 iterations = DefaultIterations);

/// The 32 bytes of a recovery key as people type it: any case, spaces and dashes anywhere.
std::optional<crypto::SecureBytes> parseRecoveryKey(QStringView typed);
/// A notebook key's id (16 hex digits, no secret: HKDF of the key): devices tell by it that they
/// share one notebook key.
QString keyIdFor(const crypto::SecureBytes &notebookKey);
/// The id a key file names ("" for a malformed one).
QString keyIdOf(QByteArrayView keyFile);

} // namespace brulion::sync::encryption

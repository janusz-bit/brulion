// brulion::encryption — the passwords of this session (see encryption/encryption.h).
#pragma once

#include "crypto/secure.h"

#include <QList>
#include <QString>

namespace brulion::encryption {

/// The passwords the user typed in this session, so that opening the next fragment or page of a
/// notebook does not ask again: each kept in wiped memory with the page (vault path) it opened,
/// all forgotten together once the keyring has not been used for `timeout` (an inactivity
/// timeout: every unlock, save or touch() restarts it), on clear() (the app locked, the notebook
/// closed, "Lock encrypted notes") and never written anywhere. A timeout of 0 remembers nothing.
/// Time is the caller's (milliseconds of a monotonic clock).
class Keyring {
public:
    explicit Keyring(qint64 timeoutMs = 5 * 60 * 1000);

    void setTimeout(qint64 timeoutMs);
    qint64 timeout() const { return m_timeout; }

    /// Keep `password` as the one that opened something of `path` (replacing the page's previous).
    void remember(const QString &path, const crypto::SecureBytes &password, qint64 now);
    /// The passwords worth trying for something of `path`, best first: the one remembered for that
    /// page, then the most recently used other ones — at most `limit`, no password twice. Expired
    /// ones are never given (expire() first: nothing is dropped here).
    QList<crypto::SecureBytes> candidates(const QString &path, qint64 now, int limit = 2) const;
    /// The password remembered for `path` itself, if any (nothing else tried).
    std::optional<crypto::SecureBytes> passwordFor(const QString &path, qint64 now) const;
    /// Something was done with a remembered password: the timeout starts again.
    void touch(qint64 now);
    /// A page was renamed or moved: its password follows it (folders too: `oldPath` a prefix with '/').
    void renamed(const QString &oldPath, const QString &newPath);
    /// Forget the password of `path`.
    void forget(const QString &path);
    /// Forget every password (wiped).
    void clear();
    /// Forget everything when `now` is past the last use + the timeout; true when that happened.
    bool expire(qint64 now);
    /// When everything will be forgotten without further use: -1 when nothing is remembered.
    qint64 expiresAt() const;
    bool isEmpty() const { return m_entries.isEmpty(); }
    qsizetype size() const { return m_entries.size(); }

private:
    struct Entry {
        QString path;
        crypto::SecureBytes password;
        qint64 used = 0;
    };
    bool expired(qint64 now) const;
    QList<Entry> m_entries;
    qint64 m_timeout;
    qint64 m_lastUse = 0;
};

} // namespace brulion::encryption

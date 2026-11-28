// brulion::crypto — hashes, HMAC and PBKDF2 (see crypto/crypto.h).
#pragma once

#include "crypto/secure.h"

#include <QByteArray>
#include <QByteArrayView>

#include <memory>

namespace brulion::crypto {

/// The hash functions offered: SHA-256 and SHA-512 (FIPS 180-4).
enum class Hash { Sha256, Sha512 };

/// Bytes of a digest: 32 or 64.
qsizetype digestSize(Hash hash);

/// The digest of `data`.
QByteArray digest(Hash hash, QByteArrayView data);

/// A digest over data given in pieces. Its state (which holds the data's last block) is wiped when
/// it goes.
class Hasher {
public:
    explicit Hasher(Hash hash);
    ~Hasher();
    Hasher(const Hasher &) = delete;
    Hasher &operator=(const Hasher &) = delete;

    void add(QByteArrayView data);
    /// The digest of everything added so far (adding may go on).
    QByteArray result() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// HMAC (RFC 2104) of `data` under `key` (any length).
QByteArray hmac(Hash hash, QByteArrayView key, QByteArrayView data);

/// PBKDF2 (RFC 8018 §5.2) with HMAC-`hash`: `length` bytes of key derived from `password` and
/// `salt` with `iterations` rounds (at least 1). Empty for iterations 0 or length ≤ 0. The
/// password, the rounds' values and the key stay in wiped memory. Costs `iterations` × the blocks
/// of the key: 220 000 rounds of SHA-512 take about 0.1 s on a laptop, a few times that on a phone
/// and in the browser — run it off the GUI thread where a thread is at hand.
SecureBytes pbkdf2(Hash hash, QByteArrayView password, QByteArrayView salt, quint32 iterations, qsizetype length);

/// HKDF (RFC 5869) with HMAC-`hash`: `length` bytes of key from the input key material `ikm`,
/// `salt` (none: a block of zeros) and `info`, which tells the keys of one secret apart. Empty
/// for length ≤ 0 or more than 255 digests. For keys from a key that is random already (a
/// file's key from the notebook's: docs/SYNC-ENCRYPTION.md) — not from a password (pbkdf2()).
SecureBytes hkdf(Hash hash, QByteArrayView ikm, QByteArrayView salt, QByteArrayView info, qsizetype length);

} // namespace brulion::crypto

// brulion::crypto — the cryptographic primitives of Brulion (docs/ARCHITECTURE.md "Security"):
// small, GUI-free and pure, for any module that needs them (encrypted notes: core `encryption`;
// the app lock's PIN: core `applock`; whatever else signs or seals data later). Nothing here keeps
// state between calls or reads the clock; randomness comes from a RandomSource the caller may give
// (tests pass a fixed one), else from the system's generator. Any thread.
//
// The algorithms are BearSSL's (3rdparty/bearssl, MIT; vendored unmodified, only the parts below
// compiled): AES in its constant-time bitsliced implementation (aes_ct64 — no table is indexed by
// key or data, so the cache tells nothing), GHASH by constant-time carryless multiplication
// (ghash_ctmul64), GCM over them, SHA-256 / SHA-512, HMAC. PBKDF2, HKDF, CMAC, SIV and the rest
// are Brulion's, after their RFCs, tested against the RFC and NIST vectors (tests/core/crypto).
//
//   secure.h      SecureBytes (a byte buffer wiped when it goes), wipe() for buffers and strings
//                 Brulion owns, constantTimeEquals()
//   random.h      RandomSource, systemRandom(), randomBytes()
//   base64.h      encodeBase64(), decodeBase64() — strict (canonical, padded) or as a browser's
//                 atob() reads it
//   hash.h        Hash (SHA-256, SHA-512): digest(), Hasher, hmac(), pbkdf2(), hkdf()
//   aead.h        aesgcm::seal() / open(): AES-128/192/256-GCM, any IV length, 16-byte tags
//   siv.h         aescmac::mac(), aessiv::seal() / open(): deterministic encryption (RFC 5297)
//   secrethash.h  hashSecret() / verifySecret(): a salted slow hash of a short secret (a PIN) as one
//                 printable string, "pbkdf2-sha512$<iterations>$<salt>$<hash>"
//
// What a caller must know:
//  * Secrets travel as SecureBytes where Brulion owns the memory: keys, passwords as UTF-8,
//    decrypted bytes. A QString that came from QML (a password field) cannot be wiped reliably —
//    Qt may hold copies —; wipe(QString &) clears the caller's own unshared copy, the rest is best
//    effort (docs/ARCHITECTURE.md "Security").
//  * Comparisons of secrets (tags, hashes) go through constantTimeEquals() or BearSSL's own
//    constant-time tag check; only lengths, which are public, end a comparison early.
//  * Nothing here logs. Callers must never put a secret, a key or decrypted text into a log
//    message (logging::ScopedSecret registers a password for redaction while it is in use).
#pragma once

#include "crypto/aead.h"
#include "crypto/base64.h"
#include "crypto/hash.h"
#include "crypto/random.h"
#include "crypto/secrethash.h"
#include "crypto/secure.h"
#include "crypto/siv.h"

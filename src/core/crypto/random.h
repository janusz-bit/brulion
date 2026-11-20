// brulion::crypto — randomness (see crypto/crypto.h).
#pragma once

#include <QByteArray>

#include <functional>

namespace brulion::crypto {

/// Fills `size` bytes at `out` with random bytes. Functions that need randomness take one, so
/// tests can pass a fixed sequence; an empty RandomSource means systemRandom().
using RandomSource = std::function<void(char *out, qsizetype size)>;

/// The system's cryptographically secure generator (QRandomGenerator::system(): getrandom /
/// arc4random / the CryptoAPI, crypto.getRandomValues() in the browser).
RandomSource systemRandom();

/// `size` random bytes from `source` (empty: systemRandom()).
QByteArray randomBytes(qsizetype size, const RandomSource &source = {});

} // namespace brulion::crypto

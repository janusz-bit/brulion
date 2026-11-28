#include "crypto/siv.h"

#include <bearssl_block.h>

#include <array>
#include <cstring>

namespace brulion::crypto {

namespace {

using Block = std::array<unsigned char, 16>;

/// AES under one key (BearSSL's constant-time ct64 in counter mode), its schedule wiped when it goes.
class Aes {
public:
    explicit Aes(QByteArrayView key) { br_aes_ct64_ctr_init(&m_keys, key.data(), std::size_t(key.size())); }
    ~Aes() { wipe(&m_keys, sizeof m_keys); }
    Aes(const Aes &) = delete;
    Aes &operator=(const Aes &) = delete;

    /// The block cipher itself on one block: counter mode's key stream for the counter `x`,
    /// over zeros, is E(x).
    Block encrypt(const Block &x)
    {
        Block out{};
        br_aes_ct64_ctr_run(&m_keys, x.data(), counterOf(x), out.data(), out.size());
        return out;
    }
    /// `data` XORed in place with the key stream from the counter block `q` (RFC 5297's CTR: the
    /// 128-bit counter's low 32 bits never carry, as SIV clears their top bit).
    void ctr(const Block &q, unsigned char *data, qsizetype size)
    {
        if (size > 0)
            br_aes_ct64_ctr_run(&m_keys, q.data(), counterOf(q), data, std::size_t(size));
    }

private:
    static quint32 counterOf(const Block &x)
    {
        return quint32(x[12]) << 24 | quint32(x[13]) << 16 | quint32(x[14]) << 8 | quint32(x[15]);
    }
    br_aes_ct64_ctr_keys m_keys;
};

/// Doubling in GF(2^128) (RFC 5297's dbl, RFC 4493's subkey step).
Block dbl(const Block &in)
{
    Block out{};
    for (int i = 0; i < 15; ++i)
        out[i] = static_cast<unsigned char>(in[i] << 1 | in[i + 1] >> 7);
    out[15] = static_cast<unsigned char>(in[15] << 1);
    if (in[0] & 0x80)
        out[15] ^= 0x87;
    return out;
}

void xorInto(Block &into, const unsigned char *data, qsizetype size)
{
    for (qsizetype i = 0; i < size; ++i)
        into[std::size_t(i)] ^= data[i];
}

/// CMAC (RFC 4493 §2.4) of `message` with `aes`.
Block cmac(Aes &aes, QByteArrayView message)
{
    const Block k1 = dbl(aes.encrypt(Block{}));
    const auto *m = reinterpret_cast<const unsigned char *>(message.data());
    const qsizetype size = message.size();
    // The last block: whole (XOR K1), or padded with 10…0 (XOR K2) — also for an empty message.
    const bool whole = size > 0 && size % 16 == 0;
    const qsizetype lastAt = whole ? size - 16 : size - size % 16;
    Block x{};
    for (qsizetype at = 0; at < lastAt; at += 16) {
        xorInto(x, m + at, 16);
        x = aes.encrypt(x);
    }
    Block last = whole ? k1 : dbl(k1);
    xorInto(last, m + lastAt, size - lastAt);
    if (!whole)
        last[std::size_t(size - lastAt)] ^= 0x80;
    xorInto(x, last.data(), 16);
    return aes.encrypt(x);
}

/// S2V (RFC 5297 §2.4) of the associated data and the plaintext.
Block s2v(Aes &aes, const QList<QByteArrayView> &ad, QByteArrayView plaintext)
{
    Block d = cmac(aes, QByteArrayView(QByteArray(16, '\0')));
    for (const QByteArrayView &string : ad) {
        const Block mac = cmac(aes, string);
        d = dbl(d);
        xorInto(d, mac.data(), 16);
    }
    if (plaintext.size() >= 16) {
        // The plaintext with D XORed into its last 16 bytes ("xorend").
        SecureBytes t(plaintext.data(), plaintext.size());
        auto *tail = reinterpret_cast<unsigned char *>(t.data()) + t.size() - 16;
        for (int i = 0; i < 16; ++i)
            tail[i] ^= d[std::size_t(i)];
        return cmac(aes, QByteArrayView(t.data(), t.size()));
    }
    Block t = dbl(d);
    xorInto(t, reinterpret_cast<const unsigned char *>(plaintext.data()), plaintext.size());
    t[std::size_t(plaintext.size())] ^= 0x80;
    return cmac(aes, QByteArrayView(reinterpret_cast<const char *>(t.data()), 16));
}

/// The counter block from the synthetic IV: two bits cleared (RFC 5297 §2.6).
Block counterOf(const Block &v)
{
    Block q = v;
    q[8] &= 0x7f;
    q[12] &= 0x7f;
    return q;
}

} // namespace

namespace aescmac {

QByteArray mac(QByteArrayView key, QByteArrayView message)
{
    if (key.size() != 16 && key.size() != 24 && key.size() != 32)
        return {};
    Aes aes(key);
    const Block t = cmac(aes, message);
    return QByteArray(reinterpret_cast<const char *>(t.data()), MacSize);
}

} // namespace aescmac

namespace aessiv {

bool isKeySize(qsizetype size)
{
    return size == 32 || size == 48 || size == 64;
}

QByteArray seal(QByteArrayView key, const QList<QByteArrayView> &ad, QByteArrayView plaintext)
{
    if (!isKeySize(key.size()) || ad.size() > MaxAssociatedData)
        return {};
    const qsizetype half = key.size() / 2;
    Aes macKey(key.first(half));
    const Block v = s2v(macKey, ad, plaintext);
    QByteArray out(IvSize + plaintext.size(), Qt::Uninitialized);
    std::memcpy(out.data(), v.data(), IvSize);
    if (!plaintext.isEmpty())
        std::memcpy(out.data() + IvSize, plaintext.data(), std::size_t(plaintext.size()));
    Aes ctrKey(key.sliced(half));
    ctrKey.ctr(counterOf(v), reinterpret_cast<unsigned char *>(out.data()) + IvSize, plaintext.size());
    return out;
}

std::optional<SecureBytes> open(QByteArrayView key, const QList<QByteArrayView> &ad, QByteArrayView sealed)
{
    if (!isKeySize(key.size()) || ad.size() > MaxAssociatedData || sealed.size() < IvSize)
        return std::nullopt;
    const qsizetype half = key.size() / 2;
    Block v{};
    std::memcpy(v.data(), sealed.data(), IvSize);
    SecureBytes plain(sealed.data() + IvSize, sealed.size() - IvSize);
    Aes ctrKey(key.sliced(half));
    ctrKey.ctr(counterOf(v), reinterpret_cast<unsigned char *>(plain.data()), plain.size());
    Aes macKey(key.first(half));
    const Block t = s2v(macKey, ad, QByteArrayView(plain.data(), plain.size()));
    if (!constantTimeEquals(QByteArrayView(reinterpret_cast<const char *>(t.data()), IvSize),
                            QByteArrayView(reinterpret_cast<const char *>(v.data()), IvSize)))
        return std::nullopt; // (plain is wiped as it goes)
    return plain;
}

} // namespace aessiv

} // namespace brulion::crypto

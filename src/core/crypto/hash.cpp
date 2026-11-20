#include "crypto/hash.h"

#include <bearssl_hash.h>
#include <bearssl_hmac.h>

#include <cstring>

namespace brulion::crypto {

namespace {

const br_hash_class *vtableOf(Hash hash)
{
    return hash == Hash::Sha512 ? &br_sha512_vtable : &br_sha256_vtable;
}

/// Wipes a BearSSL context when it goes out of scope.
template<typename T>
struct WipedOnExit {
    T &value;
    ~WipedOnExit() { wipe(&value, sizeof value); }
};

} // namespace

qsizetype digestSize(Hash hash)
{
    return hash == Hash::Sha512 ? br_sha512_SIZE : br_sha256_SIZE;
}

QByteArray digest(Hash hash, QByteArrayView data)
{
    Hasher h(hash);
    h.add(data);
    return h.result();
}

struct Hasher::State {
    Hash hash;
    br_hash_compat_context context;
};

Hasher::Hasher(Hash hash) : m_state(std::make_unique<State>())
{
    m_state->hash = hash;
    const br_hash_class *vtable = vtableOf(hash);
    vtable->init(&m_state->context.vtable);
}

Hasher::~Hasher()
{
    wipe(&m_state->context, sizeof m_state->context);
}

void Hasher::add(QByteArrayView data)
{
    if (!data.isEmpty())
        m_state->context.vtable->update(&m_state->context.vtable, data.data(), std::size_t(data.size()));
}

QByteArray Hasher::result() const
{
    QByteArray out(digestSize(m_state->hash), Qt::Uninitialized);
    m_state->context.vtable->out(&m_state->context.vtable, out.data());
    return out;
}

QByteArray hmac(Hash hash, QByteArrayView key, QByteArrayView data)
{
    br_hmac_key_context keyContext;
    WipedOnExit<br_hmac_key_context> keyWiped{keyContext};
    br_hmac_key_init(&keyContext, vtableOf(hash), key.data(), std::size_t(key.size()));
    br_hmac_context context;
    WipedOnExit<br_hmac_context> contextWiped{context};
    br_hmac_init(&context, &keyContext, 0);
    if (!data.isEmpty())
        br_hmac_update(&context, data.data(), std::size_t(data.size()));
    QByteArray out(digestSize(hash), Qt::Uninitialized);
    br_hmac_out(&context, out.data());
    return out;
}

SecureBytes pbkdf2(Hash hash, QByteArrayView password, QByteArrayView salt, quint32 iterations, qsizetype length)
{
    if (iterations == 0 || length <= 0)
        return {};
    const qsizetype hashLength = digestSize(hash);
    // The key's inner and outer pads are hashed once (br_hmac_key_init keeps both states); every
    // round then costs two compressions.
    br_hmac_key_context keyContext;
    WipedOnExit<br_hmac_key_context> keyWiped{keyContext};
    br_hmac_key_init(&keyContext, vtableOf(hash), password.data(), std::size_t(password.size()));
    br_hmac_context context;
    WipedOnExit<br_hmac_context> contextWiped{context};

    SecureBytes out(length);
    // U and T of RFC 8018 as 64-bit words (8 of them for SHA-512): the round's XOR is a few
    // operations even in an unoptimised build, where the per-byte loop cost more than the hashing.
    constexpr qsizetype Words = br_sha512_SIZE / sizeof(quint64);
    quint64 u[Words] = {};
    quint64 t[Words] = {};
    const qsizetype words = hashLength / qsizetype(sizeof(quint64));
    const std::size_t hashBytes = std::size_t(hashLength);
    const qsizetype blocks = (length + hashLength - 1) / hashLength;
    for (qsizetype block = 1; block <= blocks; ++block) {
        const unsigned char counter[4] = {static_cast<unsigned char>(block >> 24),
                                          static_cast<unsigned char>(block >> 16),
                                          static_cast<unsigned char>(block >> 8), static_cast<unsigned char>(block)};
        br_hmac_init(&context, &keyContext, 0);
        if (!salt.isEmpty())
            br_hmac_update(&context, salt.data(), std::size_t(salt.size()));
        br_hmac_update(&context, counter, sizeof counter);
        br_hmac_out(&context, u);
        std::memcpy(t, u, hashBytes);
        for (quint32 i = 1; i < iterations; ++i) {
            br_hmac_init(&context, &keyContext, 0);
            br_hmac_update(&context, u, hashBytes);
            br_hmac_out(&context, u);
            for (qsizetype k = 0; k < words; ++k)
                t[k] ^= u[k];
        }
        const qsizetype offset = (block - 1) * hashLength;
        std::memcpy(out.data() + offset, t, std::size_t(qMin(hashLength, length - offset)));
    }
    wipe(u, sizeof u);
    wipe(t, sizeof t);
    return out;
}

} // namespace brulion::crypto

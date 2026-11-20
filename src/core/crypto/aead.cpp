#include "crypto/aead.h"

#include <bearssl_aead.h>
#include <bearssl_block.h>
#include <bearssl_hash.h>

#include <cstring>

namespace brulion::crypto::aesgcm {

namespace {

/// A GCM run over AES (ct64) whose key schedule and state are wiped when it goes.
struct Gcm {
    br_aes_ct64_ctr_keys keys;
    br_gcm_context gcm;

    Gcm(QByteArrayView key, QByteArrayView iv, QByteArrayView aad)
    {
        br_aes_ct64_ctr_init(&keys, key.data(), std::size_t(key.size()));
        br_gcm_init(&gcm, &keys.vtable, br_ghash_ctmul64);
        br_gcm_reset(&gcm, iv.data(), std::size_t(iv.size()));
        if (!aad.isEmpty())
            br_gcm_aad_inject(&gcm, aad.data(), std::size_t(aad.size()));
        br_gcm_flip(&gcm);
    }
    ~Gcm()
    {
        wipe(&gcm, sizeof gcm);
        wipe(&keys, sizeof keys);
    }
    Gcm(const Gcm &) = delete;
    Gcm &operator=(const Gcm &) = delete;
};

} // namespace

bool isKeySize(qsizetype size)
{
    return size == 16 || size == 24 || size == 32;
}

QByteArray seal(QByteArrayView key, QByteArrayView iv, QByteArrayView aad, QByteArrayView plaintext)
{
    if (!isKeySize(key.size()) || iv.isEmpty())
        return {};
    Gcm run(key, iv, aad);
    QByteArray out(plaintext.size() + TagSize, Qt::Uninitialized);
    if (!plaintext.isEmpty()) {
        std::memcpy(out.data(), plaintext.data(), std::size_t(plaintext.size()));
        br_gcm_run(&run.gcm, 1, out.data(), std::size_t(plaintext.size()));
    }
    br_gcm_get_tag(&run.gcm, out.data() + plaintext.size());
    return out;
}

std::optional<SecureBytes> open(QByteArrayView key, QByteArrayView iv, QByteArrayView aad, QByteArrayView sealed)
{
    if (!isKeySize(key.size()) || iv.isEmpty() || sealed.size() < TagSize)
        return std::nullopt;
    const qsizetype length = sealed.size() - TagSize;
    Gcm run(key, iv, aad);
    SecureBytes plain(sealed.data(), length);
    if (length > 0)
        br_gcm_run(&run.gcm, 0, plain.data(), std::size_t(length));
    if (br_gcm_check_tag(&run.gcm, sealed.data() + length) != 1)
        return std::nullopt; // (plain is wiped as it goes)
    return plain;
}

} // namespace brulion::crypto::aesgcm

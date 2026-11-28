#include "sync/encryption.h"

#include "crypto/aead.h"
#include "crypto/base64.h"
#include "crypto/hash.h"
#include "crypto/siv.h"

#include <QStringList>

#include <algorithm>
#include <cstring>

using namespace Qt::StringLiterals;

namespace brulion::sync::encryption {

namespace {

constexpr char Magic[4] = {'B', 'R', 'L', 'N'};
constexpr char Version = 1;
constexpr char ChunkLog2 = 16; // ChunkSize = 2^16
constexpr qsizetype NonceSize = HeaderSize - 8;
constexpr qsizetype SealedChunk = ChunkSize + TagSize;

/// The nonce of chunk `index`: the index as 11 bytes big-endian, then 1 for the last chunk.
QByteArray chunkNonce(quint64 index, bool last)
{
    QByteArray nonce(12, '\0');
    for (int i = 10; i >= 3; --i, index >>= 8)
        nonce[i] = char(index & 0xff);
    nonce[11] = last ? 1 : 0;
    return nonce;
}

crypto::SecureBytes fileKey(const crypto::SecureBytes &content, QByteArrayView header)
{
    return crypto::hkdf(crypto::Hash::Sha256, content.view(), header.sliced(8, NonceSize), "brulion-sync file v1",
                        32);
}

} // namespace

Keys Keys::fromNotebookKey(QByteArrayView notebookKey)
{
    if (notebookKey.size() != NotebookKeySize)
        return {};
    return {crypto::hkdf(crypto::Hash::Sha256, notebookKey, {}, "brulion-sync names v1", 64),
            crypto::hkdf(crypto::Hash::Sha256, notebookKey, {}, "brulion-sync content v1", 32)};
}

std::optional<QString> encryptName(const Keys &keys, const QString &name)
{
    if (name.isEmpty())
        return std::nullopt;
    const QString encrypted = crypto::encodeBase32(crypto::aessiv::seal(keys.names.view(), {}, name.toUtf8()));
    if (encrypted.isEmpty() || encrypted.size() > MaxEncryptedName)
        return std::nullopt;
    return encrypted;
}

std::optional<QString> decryptName(const Keys &keys, QStringView encrypted)
{
    if (encrypted.isEmpty() || encrypted.size() > MaxEncryptedName)
        return std::nullopt;
    const std::optional<QByteArray> sealed = crypto::decodeBase32(encrypted);
    if (!sealed)
        return std::nullopt;
    const std::optional<crypto::SecureBytes> name = crypto::aessiv::open(keys.names.view(), {}, *sealed);
    if (!name || name->isEmpty())
        return std::nullopt;
    return name->toUtf8String();
}

namespace {

/// Every segment of `path` through `map` ("" stays ""), nullopt when one does not go.
template<typename Map>
std::optional<QString> mapSegments(const QString &path, Map map)
{
    if (path.isEmpty())
        return QString();
    QStringList out;
    for (const QString &segment : path.split(u'/')) {
        std::optional<QString> mapped = map(segment);
        if (!mapped)
            return std::nullopt;
        out.append(*mapped);
    }
    return out.join(u'/');
}

} // namespace

std::optional<QString> encryptPath(const Keys &keys, const QString &path)
{
    return mapSegments(path, [&keys](const QString &segment) { return encryptName(keys, segment); });
}

std::optional<QString> decryptPath(const Keys &keys, const QString &path)
{
    return mapSegments(path, [&keys](const QString &segment) { return decryptName(keys, segment); });
}

qint64 encryptedSize(qint64 plainSize)
{
    const qint64 chunks = std::max<qint64>(1, (plainSize + ChunkSize - 1) / ChunkSize);
    return HeaderSize + plainSize + chunks * TagSize;
}

std::optional<qint64> plainSize(qint64 encryptedSize)
{
    if (encryptedSize < HeaderSize + TagSize)
        return std::nullopt;
    const qint64 body = encryptedSize - HeaderSize;
    const qint64 chunks = (body + SealedChunk - 1) / SealedChunk;
    const qint64 plain = body - chunks * TagSize;
    if (plain < 0 || encryption::encryptedSize(plain) != encryptedSize)
        return std::nullopt;
    return plain;
}

Encryptor::Encryptor(const Keys &keys, const crypto::RandomSource &random)
    : m_pending(ChunkSize)
{
    const char fields[4] = {Version, ChunkLog2, 0, 0};
    m_header = QByteArray(Magic, 4) + QByteArray(fields, 4) + crypto::randomBytes(NonceSize, random);
    m_key = fileKey(keys.content, m_header);
}

QByteArray Encryptor::sealChunk(QByteArrayView plain, bool last)
{
    const QByteArray sealed = crypto::aesgcm::seal(m_key.view(), chunkNonce(m_index, last), m_header, plain);
    ++m_index;
    return sealed;
}

QByteArray Encryptor::add(QByteArrayView plain)
{
    QByteArray out;
    while (!plain.isEmpty()) {
        if (m_pendingSize == ChunkSize) {
            // More is coming: the full chunk waiting is not the last.
            out += sealChunk(m_pending.view(), false);
            m_pendingSize = 0;
        }
        const qsizetype n = std::min(plain.size(), ChunkSize - m_pendingSize);
        std::memcpy(m_pending.data() + m_pendingSize, plain.data(), std::size_t(n));
        m_pendingSize += n;
        plain = plain.sliced(n);
    }
    return out;
}

QByteArray Encryptor::finish()
{
    const QByteArray last = sealChunk(m_pending.view().first(m_pendingSize), true);
    m_pending.clear(); // (wiped: an Encryptor is finished once)
    return last;
}

Decryptor::Decryptor(const Keys &keys)
    : m_content(keys.content)
{
}

bool Decryptor::readHeader()
{
    const QByteArrayView header = QByteArrayView(m_buffer).first(HeaderSize);
    // (The two reserved bytes need no check: the header is every chunk's associated data.)
    if (header.first(4) != QByteArrayView(Magic, 4) || header.at(4) != Version || header.at(5) != ChunkLog2)
        return false;
    m_header = header.toByteArray();
    m_key = fileKey(m_content, m_header);
    m_buffer.remove(0, HeaderSize);
    return true;
}

std::optional<QByteArray> Decryptor::openChunk(QByteArrayView sealed, bool last)
{
    const std::optional<crypto::SecureBytes> plain =
            crypto::aesgcm::open(m_key.view(), chunkNonce(m_index, last), m_header, sealed);
    // (Only the first chunk may be empty: an empty file. A later empty one is a file extended.)
    if (!plain || (last && plain->isEmpty() && m_index > 0)) {
        m_failed = true;
        return std::nullopt;
    }
    ++m_index;
    return plain->toByteArray();
}

std::optional<QByteArray> Decryptor::add(QByteArrayView sealed)
{
    if (m_failed)
        return std::nullopt;
    m_buffer.append(sealed);
    if (m_header.isEmpty()) {
        if (m_buffer.size() < HeaderSize)
            return QByteArray();
        if (!readHeader()) {
            m_failed = true;
            return std::nullopt;
        }
    }
    QByteArray out;
    qsizetype used = 0;
    // A full chunk with more after it is not the last.
    while (m_buffer.size() - used > SealedChunk) {
        const std::optional<QByteArray> plain = openChunk(QByteArrayView(m_buffer).sliced(used, SealedChunk), false);
        if (!plain)
            return std::nullopt;
        out += *plain;
        used += SealedChunk;
    }
    m_buffer.remove(0, used);
    return out;
}

std::optional<QByteArray> Decryptor::finish()
{
    if (m_failed || m_header.isEmpty() || m_buffer.size() < TagSize) {
        m_failed = true;
        return std::nullopt;
    }
    const std::optional<QByteArray> last = openChunk(m_buffer, true);
    m_failed = true; // (nothing may follow the last chunk)
    return last;
}

QByteArray seal(const Keys &keys, QByteArrayView plain, const crypto::RandomSource &random)
{
    Encryptor encryptor(keys, random);
    return encryptor.header() + encryptor.add(plain) + encryptor.finish();
}

std::optional<crypto::SecureBytes> open(const Keys &keys, QByteArrayView sealed)
{
    Decryptor decryptor(keys);
    const std::optional<QByteArray> body = decryptor.add(sealed);
    if (!body)
        return std::nullopt;
    const std::optional<QByteArray> last = decryptor.finish();
    if (!last)
        return std::nullopt;
    crypto::SecureBytes plain(body->size() + last->size());
    std::memcpy(plain.data(), body->constData(), std::size_t(body->size()));
    std::memcpy(plain.data() + body->size(), last->constData(), std::size_t(last->size()));
    return plain;
}

} // namespace brulion::sync::encryption

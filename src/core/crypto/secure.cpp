#include "crypto/secure.h"

#include <QStringDecoder>
#include <QStringEncoder>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace brulion::crypto {

void wipe(void *data, std::size_t size)
{
    if (!data || size == 0)
        return;
    // Volatile stores the optimiser must keep, then a fence so nothing is moved past them.
    volatile unsigned char *p = static_cast<volatile unsigned char *>(data);
    for (std::size_t i = 0; i < size; ++i)
        p[i] = 0;
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

void wipe(QByteArray &bytes)
{
    if (bytes.isDetached() && !bytes.isEmpty())
        wipe(bytes.data(), std::size_t(bytes.size()));
    bytes.clear();
}

void wipe(QString &text)
{
    if (text.isDetached() && !text.isEmpty())
        wipe(text.data(), std::size_t(text.size()) * sizeof(QChar));
    text.clear();
}

bool constantTimeEquals(QByteArrayView a, QByteArrayView b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

SecureBytes::SecureBytes(qsizetype size)
{
    resize(size);
}

SecureBytes::SecureBytes(QByteArrayView bytes) : SecureBytes(bytes.data(), bytes.size()) { }

SecureBytes::SecureBytes(const char *data, qsizetype size)
{
    if (size <= 0)
        return;
    m_data = std::make_unique<char[]>(std::size_t(size));
    m_size = size;
    if (data)
        std::memcpy(m_data.get(), data, std::size_t(size));
}

SecureBytes::~SecureBytes()
{
    clear();
}

SecureBytes::SecureBytes(const SecureBytes &other) : SecureBytes(other.m_data.get(), other.m_size) { }

SecureBytes &SecureBytes::operator=(const SecureBytes &other)
{
    if (this != &other) {
        SecureBytes copy(other);
        *this = std::move(copy);
    }
    return *this;
}

SecureBytes::SecureBytes(SecureBytes &&other) noexcept
    : m_data(std::move(other.m_data)), m_size(std::exchange(other.m_size, 0))
{ }

SecureBytes &SecureBytes::operator=(SecureBytes &&other) noexcept
{
    if (this != &other) {
        clear();
        m_data = std::move(other.m_data);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

SecureBytes SecureBytes::fromUtf8(QStringView text)
{
    QStringEncoder encoder(QStringEncoder::Utf8, QStringConverter::Flag::Default);
    SecureBytes out(encoder.requiredSpace(text.size()));
    char *end = encoder.appendToBuffer(out.data(), text);
    out.resize(end - out.data());
    return out;
}

QString SecureBytes::toUtf8String() const
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Default);
    return decoder.decode(view());
}

void SecureBytes::clear()
{
    if (m_data)
        wipe(m_data.get(), std::size_t(m_size));
    m_data.reset();
    m_size = 0;
}

void SecureBytes::resize(qsizetype size)
{
    if (size == m_size)
        return;
    if (size <= 0) {
        clear();
        return;
    }
    auto fresh = std::make_unique<char[]>(std::size_t(size));
    std::memset(fresh.get(), 0, std::size_t(size));
    if (m_data)
        std::memcpy(fresh.get(), m_data.get(), std::size_t(std::min(size, m_size)));
    clear();
    m_data = std::move(fresh);
    m_size = size;
}

void SecureBytes::append(QByteArrayView bytes)
{
    if (bytes.isEmpty())
        return;
    const qsizetype old = m_size;
    resize(old + bytes.size());
    std::memcpy(m_data.get() + old, bytes.data(), std::size_t(bytes.size()));
}

} // namespace brulion::crypto

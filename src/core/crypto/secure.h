// brulion::crypto — memory for secrets (see crypto/crypto.h).
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <cstddef>
#include <memory>

namespace brulion::crypto {

/// Overwrite `size` bytes at `data` with zeros in a way the compiler does not drop as a dead store.
void wipe(void *data, std::size_t size);
/// Zero a QByteArray's bytes and clear it — when this is its only copy (an unshared buffer); a
/// shared one is only cleared (another holder keeps its bytes: Qt's implicit sharing).
void wipe(QByteArray &bytes);
/// The same for a QString's UTF-16 text.
void wipe(QString &text);

/// Equal bytes, in time that depends on the lengths only (which are public): for MACs, tags and
/// hashes of secrets. Different lengths are unequal at once.
bool constantTimeEquals(QByteArrayView a, QByteArrayView b);

/// An owned byte buffer whose bytes are zeroed when it is destroyed, assigned over, resized or
/// cleared: keys, passwords as UTF-8, decrypted text. Copies are deep (each copy wipes its own).
/// Never shares its memory, unlike QByteArray; view() lends it out without a copy.
class SecureBytes {
public:
    SecureBytes() = default;
    /// `size` zero bytes.
    explicit SecureBytes(qsizetype size);
    explicit SecureBytes(QByteArrayView bytes);
    SecureBytes(const char *data, qsizetype size);
    ~SecureBytes();
    SecureBytes(const SecureBytes &other);
    SecureBytes &operator=(const SecureBytes &other);
    SecureBytes(SecureBytes &&other) noexcept;
    SecureBytes &operator=(SecureBytes &&other) noexcept;

    /// `text` as UTF-8, encoded straight into the buffer (no QByteArray copy in between); a lone
    /// surrogate becomes U+FFFD, as the browser's TextEncoder writes it.
    static SecureBytes fromUtf8(QStringView text);
    /// The bytes read as UTF-8 (invalid sequences as U+FFFD). The QString is the caller's to wipe.
    QString toUtf8String() const;

    char *data() { return m_data.get(); }
    const char *data() const { return m_data.get(); }
    const char *constData() const { return m_data.get(); }
    qsizetype size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }
    QByteArrayView view() const { return {m_data.get(), m_size}; }
    /// A QByteArray copy — which is NOT wiped by this class: only for bytes that are no secret any
    /// more (ciphertext) or that the caller wipes itself.
    QByteArray toByteArray() const { return QByteArray(m_data.get(), m_size); }

    /// Zero and drop the bytes.
    void clear();
    /// New size: kept bytes stay, new ones are zero; the old buffer is wiped.
    void resize(qsizetype size);
    /// Append bytes (the old buffer is wiped).
    void append(QByteArrayView bytes);

    /// Constant-time comparison of the bytes (see constantTimeEquals()).
    friend bool operator==(const SecureBytes &a, const SecureBytes &b)
    {
        return constantTimeEquals(a.view(), b.view());
    }

private:
    std::unique_ptr<char[]> m_data;
    qsizetype m_size = 0;
};

} // namespace brulion::crypto

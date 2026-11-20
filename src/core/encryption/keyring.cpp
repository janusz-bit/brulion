#include "encryption/keyring.h"

#include <algorithm>

namespace brulion::encryption {

Keyring::Keyring(qint64 timeoutMs) : m_timeout(qMax<qint64>(0, timeoutMs)) { }

void Keyring::setTimeout(qint64 timeoutMs)
{
    m_timeout = qMax<qint64>(0, timeoutMs);
    if (m_timeout == 0)
        clear();
}

bool Keyring::expired(qint64 now) const
{
    return m_timeout == 0 || now - m_lastUse >= m_timeout;
}

void Keyring::remember(const QString &path, const crypto::SecureBytes &password, qint64 now)
{
    if (m_timeout == 0 || password.isEmpty())
        return;
    if (!m_entries.isEmpty() && expired(now))
        clear();
    forget(path);
    m_entries.append({path, password, now});
    m_lastUse = now;
}

QList<crypto::SecureBytes> Keyring::candidates(const QString &path, qint64 now, int limit) const
{
    QList<crypto::SecureBytes> out;
    if (m_entries.isEmpty() || expired(now) || limit <= 0)
        return out;
    QList<const Entry *> order;
    for (const Entry &e : m_entries)
        order.append(&e);
    // The page's own first, then by last use.
    std::ranges::stable_sort(order, [&](const Entry *a, const Entry *b) {
        const bool aOwn = a->path == path;
        const bool bOwn = b->path == path;
        if (aOwn != bOwn)
            return aOwn;
        return a->used > b->used;
    });
    for (const Entry *e : std::as_const(order)) {
        if (out.size() >= limit)
            break;
        if (std::ranges::none_of(out, [&](const crypto::SecureBytes &p) { return p == e->password; }))
            out.append(e->password);
    }
    return out;
}

std::optional<crypto::SecureBytes> Keyring::passwordFor(const QString &path, qint64 now) const
{
    if (expired(now))
        return std::nullopt;
    for (const Entry &e : m_entries) {
        if (e.path == path)
            return e.password;
    }
    return std::nullopt;
}

void Keyring::touch(qint64 now)
{
    if (!m_entries.isEmpty() && !expired(now))
        m_lastUse = now;
}

void Keyring::renamed(const QString &oldPath, const QString &newPath)
{
    const QString folder = oldPath + u'/';
    for (Entry &e : m_entries) {
        if (e.path == oldPath)
            e.path = newPath;
        else if (e.path.startsWith(folder))
            e.path = newPath + u'/' + e.path.sliced(folder.size());
    }
}

void Keyring::forget(const QString &path)
{
    m_entries.removeIf([&](const Entry &e) { return e.path == path; });
}

void Keyring::clear()
{
    m_entries.clear(); // (each SecureBytes wipes itself)
}

bool Keyring::expire(qint64 now)
{
    if (m_entries.isEmpty() || !expired(now))
        return false;
    clear();
    return true;
}

qint64 Keyring::expiresAt() const
{
    return m_entries.isEmpty() ? -1 : m_lastUse + m_timeout;
}

} // namespace brulion::encryption

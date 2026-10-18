#include "vault/localstore.h"

#include "vault/vaultutil_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace brulion {

LocalStore::LocalStore(const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_path(filePath.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(filePath).absoluteFilePath()))
    , m_timer(this)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { flush(); });
    reload();
}

LocalStore::~LocalStore()
{
    if (hasPendingChanges() && !flush())
        qCWarning(lcVault).noquote() << "LocalStore: unsaved changes lost:" << m_error;
}

QString LocalStore::filePath() const
{
    return m_path;
}

bool LocalStore::isPersistent() const
{
    return !m_path.isEmpty();
}

QJsonValue LocalStore::get(const QString &key, const QJsonValue &defaultValue) const
{
    return m_data.value(key, defaultValue);
}

bool LocalStore::contains(const QString &key) const
{
    return m_data.contains(key);
}

void LocalStore::set(const QString &key, const QJsonValue &value)
{
    if (value.isUndefined()) {
        remove(key);
        return;
    }
    const auto it = m_data.constFind(key);
    if (it != m_data.cend() && *it == value)
        return;
    m_data.insert(key, value);
    touch(key, value);
}

bool LocalStore::remove(const QString &key)
{
    if (m_data.remove(key) == 0)
        return false;
    touch(key, std::nullopt);
    return true;
}

int LocalStore::removePrefix(const QString &prefix)
{
    const QStringList doomed = keys(prefix);
    for (const QString &key : doomed)
        remove(key);
    return static_cast<int>(doomed.size());
}

void LocalStore::clear()
{
    if (m_data.isEmpty() && m_pending.isEmpty() && m_lastBytes.isEmpty())
        return;
    m_data.clear();
    m_pending.clear();
    m_cleared = true;
    scheduleFlush();
}

QStringList LocalStore::keys(const QString &prefix) const
{
    QStringList out;
    for (auto it = m_data.lowerBound(prefix); it != m_data.cend() && it.key().startsWith(prefix); ++it)
        out.append(it.key());
    return out;
}

QJsonObject LocalStore::values(const QString &prefix) const
{
    QJsonObject out;
    for (auto it = m_data.lowerBound(prefix); it != m_data.cend() && it.key().startsWith(prefix); ++it)
        out.insert(it.key(), it.value());
    return out;
}

qsizetype LocalStore::size() const
{
    return m_data.size();
}

bool LocalStore::hasPendingChanges() const
{
    return m_cleared || !m_pending.isEmpty();
}

void LocalStore::setFlushDelay(int msec)
{
    m_flushDelay = qMax(0, msec);
}

int LocalStore::flushDelay() const
{
    return m_flushDelay;
}

QString LocalStore::errorString() const
{
    return m_error;
}

bool LocalStore::flush()
{
    m_timer.stop();
    if (!hasPendingChanges())
        return true;
    if (m_path.isEmpty()) {
        m_pending.clear();
        m_cleared = false;
        return true;
    }
    Map disk;
    QByteArray raw;
    // The file as we last read or wrote it (the common case: nobody else writes it) needs no
    // parsing: m_data already is its content with our changes applied. With the sync merge
    // bases in the store, that parse is most of a flush.
    if (!readFile(disk, raw, &m_lastBytes)) {
        Q_EMIT saveFailed(m_error);
        return false;
    }
    // Somebody else saved the file since we last looked: keep their keys, apply ours on top.
    // (A missing or unreadable file is no newer version: then our view is written as it is.)
    if (!raw.isEmpty() && raw != m_lastBytes)
        m_data = withPendingApplied(disk);
    QJsonObject object;
    for (auto it = m_data.cbegin(); it != m_data.cend(); ++it)
        object.insert(it.key(), it.value());
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QSaveFile file(m_path);
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = tr("Cannot save %1: %2").arg(m_path, file.errorString());
        Q_EMIT saveFailed(m_error);
        return false;
    }
    m_lastBytes = bytes;
    m_pending.clear();
    m_cleared = false;
    return true;
}

bool LocalStore::reload()
{
    Map disk;
    QByteArray raw;
    if (!readFile(disk, raw))
        return false;
    m_lastBytes = raw;
    m_data = withPendingApplied(disk);
    return true;
}

bool LocalStore::readFile(Map &out, QByteArray &raw, const QByteArray *known)
{
    out.clear();
    raw.clear();
    if (m_path.isEmpty() || !QFileInfo::exists(m_path))
        return true;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = tr("Cannot read %1: %2").arg(m_path, file.errorString());
        return false;
    }
    raw = file.readAll();
    // `known` bytes were parsed (or written by us) before: valid, and the caller has their keys.
    if (known && !raw.isEmpty() && raw == *known)
        return true;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcVault).noquote() << "LocalStore:" << m_path << "is not a JSON object (" << error.errorString()
                                     << "); starting empty";
        file.close();
        keepCorruptCopy();
        raw.clear();
        return true;
    }
    const QJsonObject object = doc.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        out.insert(it.key(), it.value());
    return true;
}

void LocalStore::keepCorruptCopy()
{
    const QString copy = m_path + u".corrupt"_s;
    QFile::remove(copy);
    if (!QFile::rename(m_path, copy))
        qCWarning(lcVault).noquote() << "LocalStore: cannot move the unreadable file aside:" << m_path;
}

LocalStore::Map LocalStore::withPendingApplied(const Map &disk) const
{
    Map out = m_cleared ? Map() : disk;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it.value())
            out.insert(it.key(), *it.value());
        else
            out.remove(it.key());
    }
    return out;
}

void LocalStore::touch(const QString &key, const std::optional<QJsonValue> &value)
{
    m_pending.insert(key, value);
    scheduleFlush();
}

void LocalStore::scheduleFlush()
{
    if (m_flushDelay <= 0)
        flush();
    else if (!m_timer.isActive())
        m_timer.start(m_flushDelay);
}

} // namespace brulion

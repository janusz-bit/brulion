#include "vault/history.h"

#include "text/text.h"
#include "util/paths.h"
#include "vault/vaultutil_p.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <atomic>

using namespace Qt::StringLiterals;

namespace brulion {
namespace {

constexpr qint64 kMsPerDay = 24LL * 60 * 60 * 1000;
/// Content without an index entry younger than this is left alone by prune (in-flight snapshot).
constexpr qint64 kOrphanGraceMs = 10LL * 60 * 1000;
constexpr quint32 kIdSpace = 36U * 36 * 36 * 36;

QString tr(const char *text)
{
    return QCoreApplication::translate("brulion::History", text);
}

bool isAlwaysKept(const QString &reason)
{
    return reason == "sync"_L1 || reason == "import"_L1 || reason == "external"_L1;
}

const QRegularExpression &idPattern()
{
    static const QRegularExpression re(u"^[0-9a-z]{1,13}-[0-9a-z]{8}$"_s);
    return re;
}

/// Time encoded in a snapshot id (base-36 prefix).
std::optional<qint64> idTime(const QString &id)
{
    bool ok = false;
    const qint64 t = id.section(u'-', 0, 0).toLongLong(&ok, 36);
    return ok ? std::optional<qint64>(t) : std::nullopt;
}

} // namespace

History::History(const QString &dir)
    : m_dir(QDir::cleanPath(QFileInfo(dir).absoluteFilePath())), m_clock([] { return QDateTime::currentDateTime(); })
{ }

QString History::dir() const
{
    return m_dir;
}

void History::setClock(std::function<QDateTime()> clock)
{
    if (clock)
        m_clock = std::move(clock);
    else
        m_clock = [] { return QDateTime::currentDateTime(); };
}

void History::setPolicy(const HistoryPolicy &policy)
{
    m_policy = policy;
}

HistoryPolicy History::policy() const
{
    return m_policy;
}

QString History::errorString() const
{
    return m_error;
}

bool History::snapshot(const VaultPath &path, const QString &content, const QString &reason)
{
    return snapshotBytes(path, content.toUtf8(), reason);
}

bool History::snapshotBytes(const VaultPath &path, QByteArrayView bytes, const QString &reason)
{
    const VaultPath p = paths::normalize(path);
    if (p.isEmpty())
        return fail(tr("History: empty path"));
    QList<Stored> entries = load(p);
    const QString hash = text::fnv1a64Hex(bytes);
    const qint64 time = now();
    if (!entries.isEmpty()) {
        const Stored &newest = entries.constLast();
        if (newest.hash == hash || isThrottled(newest, reason, time))
            return true;
    }
    Stored entry{newId(time), time, bytes.size(), reason, hash};
    // Content first: an interrupted snapshot leaves only an orphan, never a dangling entry.
    if (!writeFile(objectFile(entry.id), bytes))
        return false;
    entries.append(entry);
    const QList<Stored> kept = retain(entries, time, m_policy.maxPerPath, m_policy.maxAgeDays, m_policy.keepAtLeast);
    if (!save(p, kept)) {
        QFile::remove(objectFile(entry.id));
        return false;
    }
    removeObjects(entries, kept);
    return true;
}

QList<HistoryEntry> History::list(const VaultPath &path) const
{
    const QList<Stored> entries = load(paths::normalize(path));
    QList<HistoryEntry> out;
    out.reserve(entries.size());
    for (auto it = entries.crbegin(); it != entries.crend(); ++it)
        out.append(HistoryEntry{it->id, QDateTime::fromMSecsSinceEpoch(it->time), it->size, it->reason});
    return out;
}

QString History::read(const QString &id, bool *ok) const
{
    bool good = false;
    const QByteArray bytes = readBytes(id, &good);
    if (ok)
        *ok = good;
    return good ? vaultutil::decodeUtf8(bytes) : QString();
}

QByteArray History::readBytes(const QString &id, bool *ok) const
{
    if (ok)
        *ok = false;
    if (!idPattern().match(id).hasMatch())
        return {};
    QFile file(objectFile(id));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return {};
    if (ok)
        *ok = true;
    return bytes;
}

QStringList History::paths() const
{
    QStringList out;
    QDirIterator it(m_dir + u"/index"_s, {u"*.json"_s}, QDir::Files);
    while (it.hasNext()) {
        if (const auto index = loadFile(it.next()); index && !index->entries.isEmpty())
            out.append(index->path);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool History::rename(const VaultPath &from, const VaultPath &to)
{
    const VaultPath a = paths::normalize(from);
    const VaultPath b = paths::normalize(to);
    if (a == b || a.isEmpty() || b.isEmpty())
        return true;
    const QList<Stored> moving = load(a);
    if (moving.isEmpty())
        return true;
    QList<Stored> merged = load(b) + moving;
    std::stable_sort(merged.begin(), merged.end(), [](const Stored &x, const Stored &y) { return x.time < y.time; });
    return save(b, merged) && save(a, {});
}

bool History::remove(const VaultPath &path)
{
    const VaultPath p = paths::normalize(path);
    const QList<Stored> entries = load(p);
    if (!save(p, {}))
        return false;
    removeObjects(entries, {});
    return true;
}

int History::prune(int maxPerPath, int maxAgeDays, int keepAtLeast)
{
    const qint64 time = now();
    int removed = 0;
    QSet<QString> live;
    QDirIterator it(m_dir + u"/index"_s, {u"*.json"_s}, QDir::Files);
    while (it.hasNext()) {
        const QString file = it.next();
        const std::optional<Index> index = loadFile(file);
        if (!index) {
            // Unreadable index: its snapshots become orphans and are removed below.
            QFile::remove(file);
            continue;
        }
        const QList<Stored> kept = retain(index->entries, time, maxPerPath, maxAgeDays, keepAtLeast);
        if (kept.size() != index->entries.size()) {
            if (!save(index->path, kept))
                continue;
            removeObjects(index->entries, kept);
            removed += static_cast<int>(index->entries.size() - kept.size());
        }
        for (const Stored &e : kept)
            live.insert(e.id);
    }
    QDirIterator objects(m_dir + u"/objects"_s, {u"*.txt"_s}, QDir::Files);
    while (objects.hasNext()) {
        const QFileInfo fi = objects.nextFileInfo();
        const QString id = fi.completeBaseName();
        if (live.contains(id))
            continue;
        const std::optional<qint64> created = idTime(id);
        if (created && time - *created < kOrphanGraceMs)
            continue;
        QFile::remove(fi.filePath());
    }
    return removed;
}

QString History::indexFile(const VaultPath &path) const
{
    const QByteArray key = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_dir + u"/index/"_s + QString::fromLatin1(key) + u".json"_s;
}

QString History::objectFile(const QString &id) const
{
    return m_dir + u"/objects/"_s + id + u".txt"_s;
}

std::optional<History::Index> History::loadFile(const QString &file) const
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject object = doc.object();
    Index index;
    index.path = object.value("path"_L1).toString();
    if (index.path.isEmpty())
        return std::nullopt;
    const QJsonArray entries = object.value("entries"_L1).toArray();
    for (const QJsonValue &v : entries) {
        const QJsonObject e = v.toObject();
        const QString id = e.value("id"_L1).toString();
        if (!idPattern().match(id).hasMatch() || !e.value("time"_L1).isDouble() || !e.value("size"_L1).isDouble()
            || !e.value("reason"_L1).isString() || !e.value("hash"_L1).isString())
            continue; // defensive: skip malformed entries instead of failing the whole index
        index.entries.append(Stored{id, e.value("time"_L1).toInteger(), e.value("size"_L1).toInteger(),
                                    e.value("reason"_L1).toString(), e.value("hash"_L1).toString()});
    }
    return index;
}

QList<History::Stored> History::load(const VaultPath &path) const
{
    if (path.isEmpty())
        return {};
    const std::optional<Index> index = loadFile(indexFile(path));
    // The stored path guards against hash collisions (and files copied from elsewhere).
    if (!index || index->path != path)
        return {};
    return index->entries;
}

bool History::save(const VaultPath &path, const QList<Stored> &entries)
{
    const QString file = indexFile(path);
    if (entries.isEmpty()) {
        if (QFileInfo::exists(file) && !QFile::remove(file))
            return fail(tr("Cannot remove %1").arg(file));
        return true;
    }
    QJsonArray array;
    for (const Stored &e : entries) {
        array.append(QJsonObject{{u"id"_s, e.id},
                                 {u"time"_s, e.time},
                                 {u"size"_s, e.size},
                                 {u"reason"_s, e.reason},
                                 {u"hash"_s, e.hash}});
    }
    const QJsonObject object{{u"path"_s, path}, {u"entries"_s, array}};
    return writeFile(file, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool History::writeFile(const QString &file, QByteArrayView bytes)
{
    QString error;
    if (!QDir().mkpath(QFileInfo(file).absolutePath()) || !vaultutil::saveAtomically(file, bytes, &error))
        return fail(tr("Cannot write %1: %2").arg(file, error));
    return true;
}

void History::removeObjects(const QList<Stored> &all, const QList<Stored> &kept)
{
    QSet<QString> keep;
    for (const Stored &e : kept)
        keep.insert(e.id);
    for (const Stored &e : all) {
        if (!keep.contains(e.id))
            QFile::remove(objectFile(e.id));
    }
}

QList<History::Stored> History::retain(const QList<Stored> &entries, qint64 now, int maxPerPath, int maxAgeDays,
                                       int keepAtLeast)
{
    const qint64 maxAgeMs = static_cast<qint64>(maxAgeDays) * kMsPerDay;
    const qsizetype n = entries.size();
    QList<Stored> kept;
    kept.reserve(n);
    for (qsizetype i = 0; i < n; ++i) {
        const qsizetype rank = n - 1 - i; // 0 = newest
        const Stored &e = entries.at(i);
        if (rank < keepAtLeast || (rank < maxPerPath && now - e.time <= maxAgeMs))
            kept.append(e);
    }
    return kept;
}

bool History::isThrottled(const Stored &newest, const QString &reason, qint64 now) const
{
    if (isAlwaysKept(reason) || isAlwaysKept(newest.reason))
        return false;
    // A negative age means the clock was set back: do not throttle (it could last for days).
    const qint64 age = now - newest.time;
    return age >= 0 && age < static_cast<qint64>(m_policy.throttleSeconds) * 1000;
}

QString History::newId(qint64 time) const
{
    static std::atomic<quint32> counter{0};
    for (;;) {
        const quint32 seq = counter.fetch_add(1, std::memory_order_relaxed) % kIdSpace;
        const quint32 random = QRandomGenerator::global()->bounded(kIdSpace);
        const QString id = QString::number(qMax<qint64>(time, 0), 36) + u'-' + u"%1"_s.arg(seq, 4, 36, QChar(u'0'))
                           + u"%1"_s.arg(random, 4, 36, QChar(u'0'));
        if (!QFileInfo::exists(objectFile(id)))
            return id;
    }
}

qint64 History::now() const
{
    return m_clock().toMSecsSinceEpoch();
}

bool History::fail(const QString &message)
{
    m_error = message;
    return false;
}

} // namespace brulion

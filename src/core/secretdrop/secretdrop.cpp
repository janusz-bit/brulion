#include "secretdrop.h"

#include "log/redact.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRandomGenerator>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::secretdrop {

namespace {

constexpr QFileDevice::Permissions OwnerFolder =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
constexpr QFileDevice::Permissions OwnerFile = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

QString randomHex(int bytes)
{
    QString out;
    for (int i = 0; i < bytes; ++i)
        out += u"%1"_s.arg(QRandomGenerator::system()->bounded(256), 2, 16, u'0');
    return out;
}

/// `path` is a real folder (no link) only its owner may enter.
bool privateFolder(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir() || info.isSymLink())
        return false;
    // (Where permissions are no POSIX bits — Windows — the folder of the user's profile decides.)
    return QFile::setPermissions(path, OwnerFolder) || !(info.permissions() & (QFileDevice::ReadOther));
}

} // namespace

struct Drop::Entry {
    Entry(const QString &o, const QString &p, const QString &value, qint64 lifetimeMs)
        : owner(o), path(p), secret(value), deadline(lifetimeMs)
    { }
    QString owner;
    QString path; ///< "" a held value
    logging::ScopedSecret secret;
    QDeadlineTimer deadline;
};

Drop::Drop(const QString &base, QObject *parent)
    : QObject(parent), m_base(base.isEmpty() ? QString() : QDir::cleanPath(base))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Drop::tick);
}

Drop::~Drop()
{
    discardAll();
    if (!m_folder.isEmpty())
        QDir(m_folder).removeRecursively();
    m_lock.reset(); // (unlocks and removes the lock file)
}

bool Drop::prepare(QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!m_folder.isEmpty() && QFileInfo(m_folder).isDir())
        return true;
    if (m_base.isEmpty())
        return fail(u"no private folder for secrets"_s);
    if (QFileInfo(m_base).isSymLink())
        return fail(u"%1 is a link: secrets are kept only in a real folder"_s.arg(m_base));
    if (!QDir().mkpath(m_base) || !privateFolder(m_base))
        return fail(u"cannot make %1 private"_s.arg(m_base));
    const QString folder = m_base + u"/run-"_s + randomHex(8);
    auto lock = std::make_unique<QLockFile>(folder + u".lock"_s);
    lock->setStaleLockTime(0); // (stale only when its process is gone: a run lives for days)
    if (!lock->tryLock(0))
        return fail(u"cannot lock %1"_s.arg(folder));
    if (!QDir().mkdir(folder) || !privateFolder(folder)) {
        lock->unlock();
        return fail(u"cannot make %1 private"_s.arg(folder));
    }
    m_lock = std::move(lock);
    m_folder = folder;
    return true;
}

QString Drop::write(const QString &owner, const QString &value, qint64 lifetimeMs, QString *error)
{
    if (!prepare(error))
        return {};
    const QString path = m_folder + u"/secret-"_s + randomHex(12);
    QFile file(path);
    // Created owner-only (never readable by others for a moment), never over an existing file.
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly, OwnerFile)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    const QByteArray bytes = value.toUtf8();
    const bool written = file.write(bytes) == bytes.size() && file.flush();
    file.close();
    if (!written) {
        QFile::remove(path);
        if (error)
            *error = u"cannot write %1"_s.arg(path);
        return {};
    }
    QFile::setPermissions(path, OwnerFile);
    add(owner, path, value, lifetimeMs);
    if (error)
        error->clear();
    return path;
}

void Drop::hold(const QString &owner, const QString &value, qint64 lifetimeMs)
{
    add(owner, QString(), value, lifetimeMs);
}

void Drop::add(const QString &owner, const QString &path, const QString &value, qint64 lifetimeMs)
{
    m_entries.append(std::make_shared<Entry>(owner, path, value, std::clamp<qint64>(lifetimeMs, 0, MaxLifetimeMs)));
    tick();
}

void Drop::remove(qsizetype index)
{
    const std::shared_ptr<Entry> entry = m_entries.takeAt(index);
    if (!entry->path.isEmpty())
        QFile::remove(entry->path);
    // (Its redaction ends with it.)
}

void Drop::tick()
{
    QStringList ownersDue;
    QStringList pathsDue;
    for (qsizetype i = m_entries.size() - 1; i >= 0; --i) {
        if (!m_entries.at(i)->deadline.hasExpired())
            continue;
        ownersDue.prepend(m_entries.at(i)->owner);
        pathsDue.prepend(m_entries.at(i)->path);
        remove(i);
    }
    qint64 next = -1;
    for (const std::shared_ptr<Entry> &e : std::as_const(m_entries)) {
        const qint64 left = e->deadline.remainingTime();
        next = next < 0 ? left : std::min(next, left);
    }
    if (next < 0)
        m_timer.stop();
    else
        m_timer.start(int(std::max<qint64>(next, 0)));
    for (qsizetype i = 0; i < ownersDue.size(); ++i)
        Q_EMIT expired(ownersDue.at(i), pathsDue.at(i));
}

int Drop::discard(const QString &owner)
{
    int gone = 0;
    for (qsizetype i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries.at(i)->owner == owner) {
            remove(i);
            ++gone;
        }
    }
    if (m_entries.isEmpty())
        m_timer.stop();
    return gone;
}

void Drop::discardAll()
{
    while (!m_entries.isEmpty())
        remove(m_entries.size() - 1);
    m_timer.stop();
}

QStringList Drop::files(const QString &owner) const
{
    QStringList out;
    for (const std::shared_ptr<Entry> &e : m_entries) {
        if (e->owner == owner && !e->path.isEmpty())
            out.append(e->path);
    }
    return out;
}

bool Drop::holds(const QString &owner) const
{
    return std::ranges::any_of(m_entries, [&owner](const std::shared_ptr<Entry> &e) { return e->owner == owner; });
}

QStringList Drop::owners() const
{
    QStringList out;
    for (const std::shared_ptr<Entry> &e : m_entries) {
        if (!out.contains(e->owner))
            out.append(e->owner);
    }
    return out;
}

int Drop::sweep(const QString &base)
{
    if (base.isEmpty() || QFileInfo(base).isSymLink() || !QFileInfo(base).isDir())
        return 0;
    int removed = 0;
    const QDir dir(base);
    for (const QString &name : dir.entryList({u"run-*"_s}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString folder = dir.filePath(name);
        if (QFileInfo(folder).isSymLink())
            continue;
        QLockFile lock(folder + u".lock"_s);
        lock.setStaleLockTime(0);
        // Held by a live run (this one too): kept. Free, or left by a run that ended: removed.
        if (!lock.tryLock(0))
            continue;
        if (QDir(folder).removeRecursively())
            ++removed;
        lock.unlock();
    }
    // A lock whose folder is gone (a run that ended between the two): removed when it is free.
    for (const QString &name : dir.entryList({u"run-*.lock"_s}, QDir::Files | QDir::Hidden)) {
        if (QFileInfo::exists(dir.filePath(name.chopped(5))))
            continue;
        QLockFile lock(dir.filePath(name));
        lock.setStaleLockTime(0);
        if (lock.tryLock(0))
            lock.unlock();
    }
    return removed;
}

} // namespace brulion::secretdrop

// Helpers shared by the vault tests: compact change strings, files written "behind the
// vault's back" (with distinct, explicit modification times so tests never depend on the
// file system's timestamp granularity), a controllable clock and a vault fixture.
#pragma once

#include "vault/vault.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <functional>
#include <memory>

namespace vaulttest {

using namespace brulion;
using namespace Qt::StringLiterals;

inline QString typeName(ChangeType type)
{
    switch (type) {
    case ChangeType::Created: return u"create"_s;
    case ChangeType::Modified: return u"modify"_s;
    case ChangeType::Deleted: return u"delete"_s;
    case ChangeType::Renamed: return u"rename"_s;
    }
    return {};
}

/// "rename:old.md->new.md@user" — compact form of a change.
inline QString shortChange(const VaultChange &change)
{
    QString out = typeName(change.type) + u':';
    if (!change.oldPath.isEmpty())
        out += change.oldPath + u"->"_s;
    out += change.path + u'@' + changeSourceName(change.source);
    return out;
}

inline QStringList shortChanges(const QList<VaultChange> &changes)
{
    QStringList out;
    for (const VaultChange &change : changes)
        out.append(shortChange(change));
    return out;
}

inline QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

/// Every change delivered to `spy` (a QSignalSpy on Vault::changed), in order.
inline QList<VaultChange> collected(const QSignalSpy &spy)
{
    QList<VaultChange> out;
    for (const QList<QVariant> &args : spy)
        out += args.at(0).value<QList<VaultChange>>();
    return out;
}

inline QStringList collectedShort(const QSignalSpy &spy)
{
    return shortChanges(collected(spy));
}

inline QStringList statPaths(const QList<FileStat> &stats)
{
    QStringList out;
    for (const FileStat &st : stats)
        out.append(st.path);
    return out;
}

/// Distinct modification times far from "now" for files written behind the vault's back.
inline qint64 nextFakeMtime()
{
    static qint64 t = 1'700'000'000'000;
    t += 1000;
    return t;
}

/// Write `bytes` to `root/rel` like another program would (folders are created). The file
/// gets `mtimeMs` as modification time, or a fresh distinct one.
inline void writeBehind(const QString &root, const QString &rel, const QByteArray &bytes, qint64 mtimeMs = 0)
{
    const QString abs = root + u'/' + rel;
    QVERIFY(QDir().mkpath(QFileInfo(abs).absolutePath()));
    QFile file(abs);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
    QVERIFY(file.flush());
    QVERIFY(file.setFileTime(QDateTime::fromMSecsSinceEpoch(mtimeMs > 0 ? mtimeMs : nextFakeMtime()),
                             QFileDevice::FileModificationTime));
}

/// Set the modification time of an existing file without changing its content ("touch").
inline void touchBehind(const QString &root, const QString &rel)
{
    QFile file(root + u'/' + rel);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::fromMSecsSinceEpoch(nextFakeMtime()), QFileDevice::FileModificationTime));
}

inline QByteArray readBehind(const QString &root, const QString &rel)
{
    QFile file(root + u'/' + rel);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

inline bool removeBehind(const QString &root, const QString &rel)
{
    const QString abs = root + u'/' + rel;
    return QFileInfo(abs).isDir() ? QDir(abs).removeRecursively() : QFile::remove(abs);
}

/// rename(2) like another program would (keeps the modification time).
inline bool renameBehind(const QString &root, const QString &from, const QString &to)
{
    const QString target = root + u'/' + to;
    return QDir().mkpath(QFileInfo(target).absolutePath()) && QDir().rename(root + u'/' + from, target);
}

/// Every file under `root` (ignored ones too) relative to it, sorted.
inline QStringList diskFiles(const QString &root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext())
        out.append(it.next().sliced(root.size() + 1));
    out.sort();
    return out;
}

/// A controllable local wall clock, starting at 2026-10-14 10:11:12.
struct TestClock {
    QDateTime now{QDate(2026, 10, 14), QTime(10, 11, 12)};
    std::function<QDateTime()> fn()
    {
        return [this] { return now; };
    }
    void advanceMs(qint64 ms) { now = now.addMSecs(ms); }
};

/// A vault in a temporary folder ("<tmp>/Notatki"), device state in "<tmp>/state".
struct Fixture {
    QTemporaryDir tmp;
    TestClock clock;
    std::unique_ptr<Vault> vault;
    std::unique_ptr<QSignalSpy> spy;
    /// Storage identity probe given to the vault before open() (unset: the system's).
    std::function<QString(const QString &)> storageProbe;

    QString root() const { return tmp.path() + u"/Notatki"_s; }
    QString state() const { return tmp.path() + u"/state"_s; }

    void write(const QString &rel, const QByteArray &bytes, qint64 mtimeMs = 0)
    {
        writeBehind(root(), rel, bytes, mtimeMs);
    }
    QByteArray read(const QString &rel) const { return readBehind(root(), rel); }

    /// Create and open the vault (with History unless `withHistory` is false).
    bool open(bool withHistory = true)
    {
        spy.reset();
        vault = std::make_unique<Vault>(root(), withHistory ? state() : QString());
        vault->setClock(clock.fn());
        if (storageProbe)
            vault->setStorageProbe(storageProbe);
        QString error;
        if (!vault->open(&error)) {
            qWarning().noquote() << "open failed:" << error;
            return false;
        }
        spy = std::make_unique<QSignalSpy>(vault.get(), &Vault::changed);
        return true;
    }

    QStringList changes() const { return collectedShort(*spy); }
    QStringList files() const { return statPaths(vault->files()); }
    QStringList folders() const { return statPaths(vault->folders()); }
    QString text(const QString &path) const { return vault->readText(path); }

    /// Contents of the snapshots of `path`, newest first.
    QStringList historyContents(const QString &path) const
    {
        QStringList out;
        for (const HistoryEntry &e : vault->history()->list(path))
            out.append(vault->history()->read(e.id));
        return out;
    }
    QStringList historyReasons(const QString &path) const
    {
        QStringList out;
        for (const HistoryEntry &e : vault->history()->list(path))
            out.append(e.reason);
        return out;
    }
};

} // namespace vaulttest

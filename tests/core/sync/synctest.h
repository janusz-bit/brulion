// Helpers shared by the sync tests: a controllable clock, devices (a vault in a temporary folder
// + WebDAV client + sync engine against a FakeWebDavServer), running a sync to completion and
// compact report counts.
#pragma once

#include "fakewebdavserver.h"
#include "filesystemtraits.h"

#include "sync/sync.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <memory>
#include <optional>

namespace synctest {

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

/// Changes made by one sync, in the shape of the archive tests.
struct Counts {
    int up = 0;
    int down = 0;
    int delL = 0;
    int delR = 0;
    int merged = 0;
    int conflicts = 0;
    int errors = 0;

    friend bool operator==(const Counts &, const Counts &) = default;
};

inline Counts counts(const SyncReport &r)
{
    return Counts{r.uploaded, r.downloaded, r.deletedLocal, r.deletedRemote, r.merged,
                  static_cast<int>(r.conflicts.size()), static_cast<int>(r.errors.size())};
}

inline char *toString(const Counts &c)
{
    return QTest::toString(u"up=%1 down=%2 delL=%3 delR=%4 merged=%5 conflicts=%6 errors=%7"_s.arg(c.up)
                               .arg(c.down)
                               .arg(c.delL)
                               .arg(c.delR)
                               .arg(c.merged)
                               .arg(c.conflicts)
                               .arg(c.errors));
}

/// A controllable local wall clock, starting at 2026-10-14 10:00:00.
struct TestClock {
    QDateTime now{QDate(2026, 10, 14), QTime(10, 0, 0)};

    std::function<QDateTime()> fn()
    {
        return [this] { return now; };
    }
    std::function<qint64()> msecs()
    {
        return [this] { return now.toMSecsSinceEpoch(); };
    }
    void advance(qint64 ms) { now = now.addMSecs(ms); }
};

inline const QString kFolder = u"Notatki"_s;

/// UTF-8 bytes of a QString (shorthand for file maps).
inline QByteArray u8(const QString &text)
{
    return text.toUtf8();
}

/// Run `fn` until `done` holds or `timeoutMs` passes, processing events.
inline bool waitFor(const std::function<bool()> &done, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 20);
    return done();
}

/// Write `bytes` to root/rel like another program would (folders are created). With `mtimeMs`
/// the file gets that modification time.
inline bool writeFileBehind(const QString &root, const QString &rel, const QByteArray &bytes, qint64 mtimeMs = 0)
{
    const QString abs = root + u'/' + rel;
    if (!QDir().mkpath(QFileInfo(abs).absolutePath()))
        return false;
    QFile file(abs);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
        return false;
    file.flush();
    return mtimeMs <= 0
        || file.setFileTime(QDateTime::fromMSecsSinceEpoch(mtimeMs), QFileDevice::FileModificationTime);
}

/// Every file below `root` (hidden ones too) relative to it, sorted.
inline QStringList filesBelow(const QString &root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext())
        out.append(it.next().mid(root.size() + 1));
    out.sort();
    return out;
}

struct DeviceOptions {
    TestClock *clock = nullptr;
    QString folder = kFolder;
    int concurrency = 0;
    qint64 maxFileSize = 0;
    bool open = true;
};

/// One device: a vault in a temporary folder, its WebDAV client and sync engine.
class Device {
public:
    Device(FakeWebDavServer &server, const QString &deviceName, const QMap<QString, QByteArray> &files = {},
           const DeviceOptions &options = {})
        : name(deviceName)
        , root(tmp.path() + u"/vault"_s)
        , m_ownClock(options.clock ? nullptr : std::make_unique<TestClock>())
        , clock(options.clock ? options.clock : m_ownClock.get())
    {
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeFileBehind(root, it.key(), it.value());
        QDir().mkpath(root);
        vault = std::make_unique<Vault>(root, QString());
        vault->setClock(clock->fn());
        if (options.open)
            vault->open();
        QObject::connect(vault.get(), &Vault::changed, vault.get(),
                         [this](const QList<VaultChange> &c) { changes += c; });
        store = std::make_unique<LocalStore>(QString());
        client = std::make_unique<WebDavClient>(server.url(options.folder), server.options().username,
                                                server.options().password, &server);
        engine = std::make_unique<SyncEngine>(vault.get(), client.get(), store.get(), deviceName);
        engine->setClock(clock->fn());
        if (options.concurrency > 0)
            engine->setConcurrency(options.concurrency);
        if (options.maxFileSize > 0)
            engine->setMaxFileSize(options.maxFileSize);
    }

    /// Run one sync to completion.
    SyncReport sync(const SyncRunOptions &options = {}) { return runSync(*engine, options); }

    static SyncReport runSync(SyncEngine &engine, const SyncRunOptions &options = {})
    {
        auto result = std::make_shared<std::optional<SyncReport>>();
        engine.sync(options, [result](const SyncReport &r) { *result = r; });
        if (!waitFor([result] { return result->has_value(); })) {
            qWarning("sync did not finish in time");
            SyncReport failed;
            failed.remoteError = WebDavError::make(WebDavErrorCode::Timeout, u"test: sync did not finish"_s);
            return failed;
        }
        return **result;
    }

    QString read(const QString &path) const { return vault->readText(path); }
    QByteArray readBytes(const QString &path) const { return vault->readBinary(path); }
    bool write(const QString &path, const QString &text) { return vault->writeText(path, text); }
    bool writeBytes(const QString &path, const QByteArray &data) { return vault->writeBinary(path, data); }
    bool remove(const QString &path) { return vault->remove(path); }

    /// Text of every file on disk outside the trash.
    QMap<QString, QString> files() const
    {
        QMap<QString, QString> out;
        for (const QString &rel : filesBelow(root)) {
            if (rel.startsWith(u".trash/"_s))
                continue;
            QFile file(root + u'/' + rel);
            if (file.open(QIODevice::ReadOnly))
                out.insert(rel, QString::fromUtf8(file.readAll()));
        }
        return out;
    }

    QStringList trash() const
    {
        QStringList out;
        for (const QString &rel : filesBelow(root)) {
            if (rel.startsWith(u".trash/"_s))
                out.append(rel);
        }
        return out;
    }

    QString name;
    QTemporaryDir tmp;
    QString root;

private:
    std::unique_ptr<TestClock> m_ownClock;

public:
    TestClock *clock;
    std::unique_ptr<Vault> vault;
    std::unique_ptr<LocalStore> store;
    std::unique_ptr<WebDavClient> client;
    std::unique_ptr<SyncEngine> engine;
    QList<VaultChange> changes;
};

/// A server whose clock is `clock`.
inline FakeWebDavServer::Options serverOptions(TestClock &clock, FakeWebDavServer::Options options = {})
{
    options.now = clock.msecs();
    return options;
}

} // namespace synctest

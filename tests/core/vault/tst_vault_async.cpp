// Vault::writeTextIfUnchangedAsync(): an editor's save with its work on a writer thread. Results
// (written, nothing to write, changed by another program, failed), another program's write
// between the start and the writer's read, every other vault operation during a write in flight
// (no resurrection after a rename or a deletion, the trash holds the newest text, a later write
// over the text in flight), order, closing with a write in flight, bytes kept exactly (BOM, CRLF,
// Polish), the watcher's echo (never External), History's throttle, the same results without a
// writer thread, and another program's change of the folder while a save syncs its data (it is
// reported: the echo of the save is not trusted then).
#include "vaulttest.h"

#include "vault/vaultutil_p.h"

#include <QAtomicInt>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace vaulttest;

namespace {

/// The writer of a test: one thread, as the app's (Workspace), or none (inline, WebAssembly).
struct Writer {
    QThreadPool pool;
    Writer() { pool.setMaxThreadCount(1); }
    ~Writer() { pool.waitForDone(); }
    void attach(Vault &vault, bool threaded)
    {
        if (threaded)
            vault.setWriteExecutor([this](std::function<void()> job) { pool.start(std::move(job)); });
        else
            vault.setWriteExecutor({});
    }
};

/// Outcome of an async write, filled in by its `done`.
struct Result {
    bool done = false;
    WriteOutcome outcome = WriteOutcome::Failed;
    bool doneBeforeChange = false; ///< `done` ran before changed() reported the write
};

quint64 startWrite(Vault &vault, const QString &path, const QString &content, const QString &expected,
                   std::shared_ptr<Result> result, std::function<void(const QString &)> afterWrite = {})
{
    AsyncTextWrite write{.path = path, .content = content, .expected = expected, .source = ChangeSource::User};
    write.afterWrite = std::move(afterWrite);
    write.done = [result](WriteOutcome outcome) {
        result->done = true;
        result->outcome = outcome;
    };
    return vault.writeTextIfUnchangedAsync(std::move(write));
}

bool createPlain(const QString &root, const QString &rel, const QByteArray &bytes)
{
    const QString abs = root + u'/' + rel;
    QFile file(abs);
    return QDir().mkpath(QFileInfo(abs).absolutePath()) && file.open(QIODevice::WriteOnly)
           && file.write(bytes) == bytes.size();
}

/// Make the writer's rename wait `ms` (the GUI thread's next call surely meets the write in flight).
void slowWrites(Vault &vault, int ms)
{
    vault.setBeforeRenameHook([ms](const VaultPath &) { QThread::msleep(ms); });
}

} // namespace

class TestVaultAsync : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void modes_data()
    {
        QTest::addColumn<bool>("threaded");
        QTest::newRow("writer thread") << true;
        QTest::newRow("inline") << false;
    }

    void results_data() { modes_data(); }
    void results()
    {
        QFETCH(bool, threaded);
        Fixture f;
        Writer writer;
        f.write(u"Dziennik/2026-11-18.md"_s, "- rano: kawa\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, threaded);
        const QString path = u"Dziennik/2026-11-18.md"_s;
        const QString before = f.text(path);

        // Written: the change after `done`, Modified by the user.
        auto written = std::make_shared<Result>();
        QObject::connect(f.vault.get(), &Vault::changed, f.vault.get(), [written] {
            if (written->done)
                written->doneBeforeChange = true;
        });
        const QString next = before + u"- południe: żurek ąćęłńóśźż\n"_s;
        const quint64 ticket = startWrite(*f.vault, path, next, before, written);
        QVERIFY(ticket > 0);
        QVERIFY(threaded ? f.vault->hasWritesInFlight() || written->done : written->done);
        f.vault->waitForWrites();
        QVERIFY(written->done);
        QCOMPARE(written->outcome, WriteOutcome::Written);
        QVERIFY(written->doneBeforeChange);
        QVERIFY(!f.vault->hasWritesInFlight());
        QCOMPARE(f.read(path), next.toUtf8());
        QCOMPARE(f.changes(), QStringList{u"modify:Dziennik/2026-11-18.md@user"_s});
        QCOMPARE(f.vault->stat(path)->size, next.toUtf8().size());
        QVERIFY(f.vault->holdsText(path, next));
        QVERIFY(!f.vault->holdsText(path,
                                    QString(next.constData(), next.size()))); // an equal text is not the same string
        QCOMPARE(f.historyContents(path), QStringList{before});

        // Nothing to write: Written, no change.
        auto same = std::make_shared<Result>();
        startWrite(*f.vault, path, next, next, same);
        f.vault->waitForWrites();
        QCOMPARE(same->outcome, WriteOutcome::Written);
        QCOMPARE(f.spy->size(), 1);

        // Changed: another program's text is kept and reported (External, its predecessor in History).
        writeBehind(f.root(), path, "- tekst innego programu\n");
        auto changed = std::make_shared<Result>();
        startWrite(*f.vault, path, next + u"- wieczór\n"_s, next, changed);
        f.vault->waitForWrites();
        QCOMPARE(changed->outcome, WriteOutcome::Changed);
        QCOMPARE(f.read(path), QByteArray("- tekst innego programu\n"));
        QCOMPARE(f.changes().last(), u"modify:Dziennik/2026-11-18.md@external"_s);
        QVERIFY(!f.vault->holdsText(path, next));
        QCOMPARE(f.historyReasons(path).first(), u"external"_s);
        QCOMPARE(f.historyContents(path).first(), next);
    }

    // Another program writes between the start and the writer's read: Changed, its text kept —
    // also behind the same size and modification time (decided by the writer's comparison).
    void otherProgramBeforeTheWritersRead_data()
    {
        QTest::addColumn<bool>("sameStat");
        QTest::newRow("new stat") << false;
        QTest::newRow("same stat") << true;
    }
    void otherProgramBeforeTheWritersRead()
    {
        QFETCH(bool, sameStat);
        Fixture f;
        f.write(u"a.md"_s, "- moje\n");
        QVERIFY(f.open());
        QSemaphore gate;
        QThreadPool pool;
        pool.setMaxThreadCount(1);
        f.vault->setWriteExecutor([&](std::function<void()> job) {
            pool.start([&gate, job = std::move(job)] {
                gate.acquire();
                job();
            });
        });
        QCOMPARE(f.text(u"a.md"_s), u"- moje\n"_s); // (as an editor read it: the vault knows its content)
        // (Before the write: stat() of a path being written waits for that write.)
        const qint64 known = f.vault->stat(u"a.md"_s)->mtime;
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, u"a.md"_s, u"- moje i nowe\n"_s, u"- moje\n"_s, result);
        const QByteArray other = sameStat ? QByteArray("- obce\n") : QByteArray("- obce zdanie źdźbła\n");
        writeBehind(f.root(), u"a.md"_s, other, sameStat ? known : 0);
        gate.release();
        f.vault->waitForWrites();
        QCOMPARE(result->outcome, WriteOutcome::Changed);
        QCOMPARE(f.read(u"a.md"_s), other);
        QCOMPARE(f.changes(), QStringList{u"modify:a.md@external"_s});
        QCOMPARE(f.text(u"a.md"_s), QString::fromUtf8(other));
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"- moje\n"_s});
        pool.waitForDone();
    }

    // Every other operation waits for the write in flight and sees its result.
    void operationsDuringAWrite_data()
    {
        QTest::addColumn<QString>("operation");
        for (const char *op : {"rename", "remove", "removePermanently", "restore", "write", "read", "rescan", "create",
                               "stat", "exists"})
            QTest::newRow(op) << QString::fromLatin1(op);
    }
    void operationsDuringAWrite()
    {
        QFETCH(QString, operation);
        Fixture f;
        Writer writer;
        f.write(u"Projekty/łódź.md"_s, "- plan\n");
        f.write(u".trash/Projekty/łódź.md"_s, "- stara wersja\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        slowWrites(*f.vault, 150);
        const QString path = u"Projekty/łódź.md"_s;
        const QString newest = u"- plan\n- nowy punkt ąę\n"_s;
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, path, newest, u"- plan\n"_s, result);
        QVERIFY(f.vault->hasWritesInFlight());
        if (operation == "rename"_L1) {
            QVERIFY(f.vault->rename(path, u"Projekty/Gdańsk.md"_s));
            QVERIFY(!QFileInfo::exists(f.root() + u'/' + path)); // never written again under the old name
            QCOMPARE(f.read(u"Projekty/Gdańsk.md"_s), newest.toUtf8());
            QCOMPARE(f.changes(), (QStringList{u"modify:Projekty/łódź.md@user"_s,
                                               u"rename:Projekty/łódź.md->Projekty/Gdańsk.md@user"_s}));
        } else if (operation == "remove"_L1) {
            QVERIFY(f.vault->remove(path));
            QVERIFY(!QFileInfo::exists(f.root() + u'/' + path));
            // The trash holds the newest text (the older trashed copy keeps its own name).
            bool found = false;
            for (const FileStat &st : f.vault->trash())
                found = found || readBehind(f.root(), st.path) == newest.toUtf8();
            QVERIFY(found);
        } else if (operation == "removePermanently"_L1) {
            QVERIFY(f.vault->removePermanently(path));
            QVERIFY(!QFileInfo::exists(f.root() + u'/' + path));
            QCOMPARE(f.changes().last(), u"delete:Projekty/łódź.md@user"_s);
        } else if (operation == "restore"_L1) {
            const VaultPath restored = f.vault->restore(u".trash/Projekty/łódź.md"_s);
            QCOMPARE(restored, u"Projekty/łódź 2.md"_s); // the note is there, with the newest text
            QCOMPARE(f.read(path), newest.toUtf8());
            QCOMPARE(f.read(restored), QByteArray("- stara wersja\n"));
        } else if (operation == "write"_L1) {
            // A save over the text in flight: the write in flight landed first.
            const QString third = newest + u"- trzeci\n"_s;
            QCOMPARE(f.vault->writeTextIfUnchanged(path, third, newest), WriteOutcome::Written);
            QCOMPARE(f.read(path), third.toUtf8());
        } else if (operation == "read"_L1) {
            QCOMPARE(f.text(path), newest);
        } else if (operation == "rescan"_L1) {
            QVERIFY(f.vault->rescan().isEmpty()); // its own write, not another program's
            QCOMPARE(f.changes(), QStringList{u"modify:Projekty/łódź.md@user"_s});
        } else if (operation == "create"_L1) {
            QCOMPARE(f.vault->create(path, u"- inna\n"_s), u"Projekty/łódź 2.md"_s);
            QCOMPARE(f.read(path), newest.toUtf8());
        } else if (operation == "stat"_L1) {
            // The stat after the write (a sync's guard before it writes compares it).
            const std::optional<FileStat> st = f.vault->stat(path);
            QVERIFY(st);
            QCOMPARE(st->size, qint64(newest.toUtf8().size()));
        } else if (operation == "exists"_L1) {
            QVERIFY(f.vault->exists(path));
        }
        if (operation == "read"_L1 || operation == "stat"_L1 || operation == "exists"_L1) {
            // A read or a query merged the write but reports it from the event loop, never inside
            // its caller (a dataview query holding the index's notes, a model building a row).
            QVERIFY(!result->done);
            QVERIFY(f.changes().isEmpty());
            QVERIFY(f.vault->hasWritesInFlight());
            QCOMPARE(f.read(path), newest.toUtf8());
            QTRY_VERIFY(result->done);
            QCOMPARE(f.changes(), QStringList{u"modify:Projekty/łódź.md@user"_s});
        }
        QVERIFY(result->done);
        QCOMPARE(result->outcome, WriteOutcome::Written);
        QVERIFY(!f.vault->hasWritesInFlight());
    }

    // What a read merged is reported before anything newer: the next call that changes files
    // reports it first, in order (its `done` before its changed(), as always).
    void readsReportBeforeTheNextChange()
    {
        Fixture f;
        Writer writer;
        f.write(u"a.md"_s, "- ą\n");
        f.write(u"b.md"_s, "- b\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        slowWrites(*f.vault, 100);
        auto result = std::make_shared<Result>();
        QStringList seen;
        QObject::connect(f.vault.get(), &Vault::changed, f.vault.get(), [&](const QList<VaultChange> &changes) {
            for (const VaultChange &c : changes)
                seen.append(c.path);
        });
        AsyncTextWrite write{.path = u"a.md"_s, .content = u"- ąę\n"_s, .expected = u"- ą\n"_s};
        write.done = [&, result](WriteOutcome outcome) {
            result->done = true;
            result->outcome = outcome;
            result->doneBeforeChange = seen.isEmpty();
        };
        f.vault->writeTextIfUnchangedAsync(std::move(write));
        QCOMPARE(f.text(u"b.md"_s), u"- b\n"_s); // (merges a.md's write, reports nothing)
        QVERIFY(!result->done);
        QVERIFY(seen.isEmpty());
        QVERIFY(f.vault->writeText(u"b.md"_s, u"- bę\n"_s));
        QVERIFY(result->done);
        QVERIFY(result->doneBeforeChange);
        QCOMPARE(result->outcome, WriteOutcome::Written);
        QCOMPARE(seen, (QStringList{u"a.md"_s, u"b.md"_s}));
        QVERIFY(!f.vault->hasWritesInFlight());
        QTest::qWait(20); // (the queued report finds nothing left)
        QCOMPARE(seen.size(), 2);
    }

    // The writer's fallback (a stat that changed behind the vault: decided on the vault's thread)
    // merged by a read: written there and then, reported from the event loop.
    void fallbackMergedByARead()
    {
        Fixture f;
        f.write(u"a.md"_s, "- moje\n");
        QVERIFY(f.open());
        QSemaphore gate;
        QThreadPool pool;
        pool.setMaxThreadCount(1);
        f.vault->setWriteExecutor([&](std::function<void()> job) {
            pool.start([&gate, job = std::move(job)] {
                gate.acquire();
                job();
            });
        });
        QCOMPARE(f.text(u"a.md"_s), u"- moje\n"_s);
        const qint64 known = f.vault->stat(u"a.md"_s)->mtime;
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, u"a.md"_s, u"- moje i nowe\n"_s, u"- moje\n"_s, result);
        writeBehind(f.root(), u"a.md"_s, "- moje\n", known + 5000); // (the same text, another stat)
        gate.release();
        QVERIFY(f.vault->exists(u"a.md"_s));
        QCOMPARE(f.read(u"a.md"_s), QByteArray("- moje i nowe\n"));
        QVERIFY(!result->done);
        QVERIFY(f.changes().isEmpty());
        QTRY_VERIFY(result->done);
        QCOMPARE(result->outcome, WriteOutcome::Written);
        QVERIFY(!f.changes().isEmpty());
        QVERIFY(f.changes().contains(u"modify:a.md@user"_s));
        pool.waitForDone();
    }

    void twoQuickSavesInOrder_data() { modes_data(); }
    void twoQuickSavesInOrder()
    {
        QFETCH(bool, threaded);
        Fixture f;
        Writer writer;
        f.write(u"a.md"_s, "- 0\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, threaded);
        auto first = std::make_shared<Result>();
        auto second = std::make_shared<Result>();
        startWrite(*f.vault, u"a.md"_s, u"- 0\n- 1\n"_s, u"- 0\n"_s, first);
        startWrite(*f.vault, u"a.md"_s, u"- 0\n- 1\n- 2\n"_s, u"- 0\n- 1\n"_s, second);
        f.vault->waitForWrites();
        QCOMPARE(first->outcome, WriteOutcome::Written);
        QCOMPARE(second->outcome, WriteOutcome::Written);
        QCOMPARE(f.read(u"a.md"_s), QByteArray("- 0\n- 1\n- 2\n"));
        QCOMPARE(f.changes(), (QStringList{u"modify:a.md@user"_s, u"modify:a.md@user"_s}));
    }

    // The queued merge arrives through the event loop by itself.
    void mergedFromTheEventLoop()
    {
        Fixture f;
        Writer writer;
        f.write(u"a.md"_s, "- 0\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, u"a.md"_s, u"- 0\n- 1\n"_s, u"- 0\n"_s, result);
        QTRY_VERIFY(result->done);
        QCOMPARE(result->outcome, WriteOutcome::Written);
        QCOMPARE(f.spy->size(), 1);
    }

    // The ticket of the write while changed() reports it, the parse's text on the writer.
    void reportingAndAfterWrite()
    {
        Fixture f;
        Writer writer;
        f.write(u"a.md"_s, "\xEF\xBB\xBF- z BOM\r\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        auto result = std::make_shared<Result>();
        QString parsed;
        QAtomicInt otherThread = 0;
        QThread *const gui = QThread::currentThread();
        const quint64 ticket = startWrite(*f.vault, u"a.md"_s, u"- z BOM\r\n- dalej\r\n"_s, u"- z BOM\r\n"_s, result,
                                          [&](const QString &text) {
                                              parsed = text;
                                              otherThread.storeRelaxed(QThread::currentThread() != gui);
                                          });
        quint64 reported = 0;
        QObject::connect(f.vault.get(), &Vault::changed, f.vault.get(), [&] { reported = f.vault->reportingWrite(); });
        f.vault->waitForWrites();
        QCOMPARE(reported, ticket);
        QCOMPARE(f.vault->reportingWrite(), quint64(0));
        QCOMPARE(parsed, u"- z BOM\r\n- dalej\r\n"_s);
        QVERIFY(otherThread.loadRelaxed());
        // A sync write reports no ticket.
        QCOMPARE(f.vault->writeTextIfUnchanged(u"a.md"_s, u"- x\r\n"_s, parsed), WriteOutcome::Written);
        QCOMPARE(reported, quint64(0));
    }

    void bytesKept_data() { modes_data(); }
    void bytesKept()
    {
        QFETCH(bool, threaded);
        Fixture f;
        Writer writer;
        const QByteArray bom = vaultutil::utf8Bom();
        f.write(u"Zażółć.md"_s, bom + "- gęślą jaźń\r\n- druga\r\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, threaded);
        auto result = std::make_shared<Result>();
        const QString next = u"- gęślą jaźń\r\n- druga\r\n- trzecia ćma\r\n"_s;
        startWrite(*f.vault, u"Zażółć.md"_s, next, f.text(u"Zażółć.md"_s), result);
        f.vault->waitForWrites();
        QCOMPARE(result->outcome, WriteOutcome::Written);
        QCOMPARE(f.read(u"Zażółć.md"_s), bom + next.toUtf8());
        // History keeps the bytes as they were, BOM and CRLF included.
        const QList<HistoryEntry> versions = f.vault->history()->list(u"Zażółć.md"_s);
        QCOMPARE(versions.size(), 1);
        QCOMPARE(f.vault->history()->readBytes(versions.first().id), bom + "- gęślą jaźń\r\n- druga\r\n");
    }

    // The watcher's echo of a write in flight (and after) is the vault's own: no External change,
    // no "external" snapshot.
    void echoIsNotExternal()
    {
        Fixture f;
        Writer writer;
        f.write(u"Dziennik/a.md"_s, "- 0\n");
        for (int i = 0; i < 10; ++i)
            f.write(u"Dziennik/inna %1.md"_s.arg(i), "- inna\n");
        QVERIFY(f.open());
        f.vault->setWatchDelay(10);
        f.vault->startWatching();
        f.vault->watchFile(u"Dziennik/a.md"_s);
        writer.attach(*f.vault, true);
        slowWrites(*f.vault, 80); // the echo of the temporary file arrives while the write is in flight
        QString text = u"- 0\n"_s;
        for (int i = 1; i <= 3; ++i) {
            QTest::qWait(20);
            auto result = std::make_shared<Result>();
            const QString next = text + u"- %1 źdźbło\n"_s.arg(i);
            startWrite(*f.vault, u"Dziennik/a.md"_s, next, text, result);
            QTRY_VERIFY(result->done);
            QCOMPARE(result->outcome, WriteOutcome::Written);
            text = next;
            QTest::qWait(150);
        }
        f.vault->processPendingEvents();
        QCOMPARE(f.changes(), QStringList(3, u"modify:Dziennik/a.md@user"_s));
        QVERIFY(!f.historyReasons(u"Dziennik/a.md"_s).contains(u"external"_s));
    }

    // History throttles the writer's snapshots as before: one per 5 minutes of ordinary saves.
    void historyThrottle()
    {
        Fixture f;
        Writer writer;
        f.write(u"a.md"_s, "- 0\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        QString text = u"- 0\n"_s;
        const auto save = [&](const QString &line) {
            auto result = std::make_shared<Result>();
            const QString next = text + line;
            startWrite(*f.vault, u"a.md"_s, next, text, result);
            f.vault->waitForWrites();
            QCOMPARE(result->outcome, WriteOutcome::Written);
            text = next;
        };
        save(u"- 1\n"_s);
        f.clock.advanceMs(60 * 1000);
        save(u"- 2\n"_s);
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"- 0\n"_s});
        f.clock.advanceMs(5 * 60 * 1000);
        save(u"- 3\n"_s);
        QCOMPARE(f.historyContents(u"a.md"_s), (QStringList{u"- 0\n- 1\n- 2\n"_s, u"- 0\n"_s}));
    }

    // Closing with a write in flight: the destructor waits for it (the file holds the text) and
    // reports nothing; a vault opened again drops nothing either.
    void closeWithAWriteInFlight()
    {
        Fixture f;
        Writer writer;
        f.write(u"a.md"_s, "- 0\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        slowWrites(*f.vault, 100);
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, u"a.md"_s, u"- 0\n- ostatnie słowo\n"_s, u"- 0\n"_s, result);
        f.spy.reset();
        f.vault.reset();
        QVERIFY(!result->done);
        QCOMPARE(f.read(u"a.md"_s), QByteArray("- 0\n- ostatnie słowo\n"));

        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        slowWrites(*f.vault, 100);
        auto again = std::make_shared<Result>();
        startWrite(*f.vault, u"a.md"_s, u"- po otwarciu\n"_s, u"- 0\n- ostatnie słowo\n"_s, again);
        QVERIFY(f.vault->open()); // open() again: waits, merges, then reads the folder anew
        QVERIFY(again->done);
        QCOMPARE(again->outcome, WriteOutcome::Written);
        QCOMPARE(f.text(u"a.md"_s), u"- po otwarciu\n"_s);
    }

    // A failed write (a read-only folder) is reported with the result; nothing changes.
    void failedWrite()
    {
#ifdef Q_OS_UNIX
        if (::getuid() == 0)
            QSKIP("root writes into read-only folders");
        Fixture f;
        Writer writer;
        f.write(u"Zamknięte/a.md"_s, "- 0\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        const QString dir = f.root() + u"/Zamknięte"_s;
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, u"Zamknięte/a.md"_s, u"- 1\n"_s, u"- 0\n"_s, result);
        f.vault->waitForWrites();
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QCOMPARE(result->outcome, WriteOutcome::Failed);
        QVERIFY(f.vault->errorString().contains(u"Zamknięte/a.md"_s));
        QCOMPARE(f.read(u"Zamknięte/a.md"_s), QByteArray("- 0\n"));
        QVERIFY(f.changes().isEmpty());
        QVERIFY(!QDir(dir).entryList(QDir::Files | QDir::Hidden).join(u' ').contains(u".brulion-tmp-"_s));
        // Writable again: the next write goes through.
        auto retry = std::make_shared<Result>();
        startWrite(*f.vault, u"Zamknięte/a.md"_s, u"- 1\n"_s, u"- 0\n"_s, retry);
        f.vault->waitForWrites();
        QCOMPARE(retry->outcome, WriteOutcome::Written);
#else
        QSKIP("a folder's read-only attribute does not stop new files in it on Windows");
#endif
    }

    // A note made read-only is not written over (QSaveFile refused it; the own write's rename would
    // not), by an async save or a plain one, and no temporary file is left.
    void readOnlyNoteNotOverwritten_data() { modes_data(); }
    void readOnlyNoteNotOverwritten()
    {
#ifdef Q_OS_UNIX
        if (::getuid() == 0)
            QSKIP("root writes over read-only files");
#endif
        QFETCH(bool, threaded);
        Fixture f;
        Writer writer;
        f.write(u"Archiwum/zamknięta.md"_s, "- stała treść\n");
        QVERIFY(f.open());
        writer.attach(*f.vault, threaded);
        const QString abs = f.root() + u"/Archiwum/zamknięta.md"_s;
        QVERIFY(QFile::setPermissions(abs, QFileDevice::ReadOwner | QFileDevice::ReadGroup));
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, u"Archiwum/zamknięta.md"_s, u"- zmieniona\n"_s, u"- stała treść\n"_s, result);
        f.vault->waitForWrites();
        QCOMPARE(result->outcome, WriteOutcome::Failed);
        QVERIFY(!f.vault->writeText(u"Archiwum/zamknięta.md"_s, u"- zmieniona\n"_s));
        QVERIFY(QFile::setPermissions(abs, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(f.read(u"Archiwum/zamknięta.md"_s), QByteArray("- stała treść\n"));
        const QStringList left = QDir(f.root() + u"/Archiwum"_s).entryList(QDir::Files | QDir::Hidden);
        QCOMPARE(left, QStringList{u"zamknięta.md"_s});
    }

    // Not the plain case (a file the vault does not know yet): written at once, same outcome.
    void newFileWrittenAtOnce()
    {
        Fixture f;
        Writer writer;
        QVERIFY(f.open());
        writer.attach(*f.vault, true);
        auto result = std::make_shared<Result>();
        startWrite(*f.vault, u"Nowy folder/nowa ść.md"_s, u"- pierwsza\n"_s, QString(), result);
        QVERIFY(result->done); // before writeTextIfUnchangedAsync() returned
        QCOMPARE(result->outcome, WriteOutcome::Written);
        QCOMPARE(f.changes(), QStringList{u"create:Nowy folder/nowa ść.md@user"_s});
    }

    // Without a writer thread the same sequence gives the same files, changes and History.
    void inlineEqualsThreaded()
    {
        const auto run = [](bool threaded) {
            Fixture f;
            Writer writer;
            f.write(u"a.md"_s, "- 0\n");
            f.write(u"b/c.md"_s, "\xEF\xBB\xBF- c\r\n");
            if (!f.open())
                return QStringList{u"open failed"_s};
            writer.attach(*f.vault, threaded);
            QStringList outcomes;
            const auto save = [&](const QString &path, const QString &next, const QString &expected) {
                auto result = std::make_shared<Result>();
                startWrite(*f.vault, path, next, expected, result);
                f.vault->waitForWrites();
                outcomes.append(QString::number(int(result->outcome)));
            };
            save(u"a.md"_s, u"- 0\n- 1\n"_s, u"- 0\n"_s);
            save(u"b/c.md"_s, u"- c\r\n- d\r\n"_s, u"- c\r\n"_s);
            save(u"a.md"_s, u"- 0\n- 1\n"_s, u"- 0\n- 1\n"_s);
            writeBehind(f.root(), u"a.md"_s, "- obcy\n");
            save(u"a.md"_s, u"- 0\n- 1\n- 2\n"_s, u"- 0\n- 1\n"_s);
            return outcomes + f.changes() + diskFiles(f.root())
                   + QStringList{QString::fromUtf8(f.read(u"a.md"_s)), QString::fromUtf8(f.read(u"b/c.md"_s))}
                   + f.historyContents(u"a.md"_s) + f.historyContents(u"b/c.md"_s);
        };
        QCOMPARE(run(true), run(false));
    }

    // Another program changes the folder while a save writes and syncs its data (Syncthing
    // renaming its new version of a note over it): the folder's stamp moved by more than the
    // save's own steps, so its echo is not trusted and the other program's change is reported.
    void foreignChangeWhileSaving_data()
    {
        QTest::addColumn<bool>("async");
        QTest::addColumn<QString>("kind");
        for (const bool async : {false, true}) {
            QTest::newRow(async ? "async, created" : "sync, created") << async << u"created"_s;
#ifndef Q_OS_WIN
            QTest::newRow(async ? "async, replaced" : "sync, replaced") << async << u"replaced"_s;
#endif
        }
    }
    void foreignChangeWhileSaving()
    {
        QFETCH(bool, async);
        QFETCH(QString, kind);
        Fixture f;
        Writer writer;
        f.write(u"journals/A.md"_s, "- moja\n");
        f.write(u"journals/B.md"_s, "- b\n");
        QVERIFY(f.open());
        f.vault->setWatchDelay(30);
        f.vault->startWatching();
        writer.attach(*f.vault, true);
        QTest::qWait(20);
        // An own save first, so the folder's stamp is known as the save left it.
        QCOMPARE(f.vault->writeTextIfUnchanged(u"journals/A.md"_s, u"- moja\n- 1\n"_s, u"- moja\n"_s),
                 WriteOutcome::Written);
        QTest::qWait(150);
        f.vault->processPendingEvents();
        f.spy->clear();
        const QString root = f.root();
        f.vault->setBeforeRenameHook([root, kind](const VaultPath &) {
            if (kind == "created"_L1) {
                createPlain(root, u"journals/C.md"_s, "- z innego urządzenia\n");
            } else {
                createPlain(root, u"journals/.syncthing.B.md.tmp"_s, "- b z innego urządzenia\n");
                std::rename(QFile::encodeName(root + u"/journals/.syncthing.B.md.tmp"_s).constData(),
                            QFile::encodeName(root + u"/journals/B.md"_s).constData());
            }
        });
        const QString next = u"- moja\n- 1\n- 2\n"_s;
        if (async) {
            auto result = std::make_shared<Result>();
            startWrite(*f.vault, u"journals/A.md"_s, next, u"- moja\n- 1\n"_s, result);
            QTRY_VERIFY(result->done);
            QCOMPARE(result->outcome, WriteOutcome::Written);
        } else {
            QCOMPARE(f.vault->writeTextIfUnchanged(u"journals/A.md"_s, next, u"- moja\n- 1\n"_s),
                     WriteOutcome::Written);
        }
        f.vault->setBeforeRenameHook({});
        const QString expected =
                kind == "created"_L1 ? u"create:journals/C.md@external"_s : u"modify:journals/B.md@external"_s;
        QTRY_VERIFY2(f.changes().contains(expected), qPrintable(f.changes().join(u", "_s)));
        QCOMPARE(f.changes().first(), u"modify:journals/A.md@user"_s);
        if (kind != "created"_L1)
            QCOMPARE(f.text(u"journals/B.md"_s), u"- b z innego urządzenia\n"_s);
    }
};

QTEST_GUILESS_MAIN(TestVaultAsync)
#include "tst_vault_async.moc"

// Large files synced in pieces (SyncEngine::streamAbove()): downloads into a staging file next to
// their place, uploads read as they are sent, never a whole file in memory — and what a broken
// download or a full disk leaves behind: nothing.
#include "synctest.h"

#include "text/text.h"

#include <QTest>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/resource.h>
#include <unistd.h>
#endif

using namespace synctest;

namespace {

constexpr qint64 MiB = 1024 * 1024;

/// `size` bytes of a pattern that differs with `seed` (no two files alike by accident).
QByteArray pattern(qint64 size, int seed = 0)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qint64 i = 0; i < size; ++i)
        out[i] = char((i * 7919 + 13 + seed * 31) & 0xff);
    return out;
}

FakeWebDavServer::Options streaming()
{
    FakeWebDavServer::Options options;
    options.streams = true;
    return options;
}

/// The vault's own temporary files (a staging file left behind would be one).
QStringList leftovers(const Device &device)
{
    QStringList out;
    for (const QString &rel : filesBelow(device.root)) {
        if (rel.section(u'/', -1).startsWith(u".brulion-tmp-"_s))
            out.append(rel);
    }
    return out;
}

QByteArray fileBytes(const Device &device, const QString &rel)
{
    QFile file(device.root + u'/' + rel);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestSyncStreamed : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void largeDownloadGoesThroughAStagingFile()
    {
        FakeWebDavServer server(streaming());
        server.mkdir(kFolder);
        const QByteArray film = pattern(20 * MiB);
        server.putFile(u"Notatki/Wakacje/łódź.mp4"_s, film);
        server.putText(u"Notatki/Notatka.md"_s, u"Zażółć gęślą jaźń\n"_s);
        Device a(server, u"laptop"_s);
        const SyncReport r = a.sync();
        QCOMPARE(counts(r), (Counts{.down = 2}));
        QCOMPARE(fileBytes(a, u"Wakacje/łódź.mp4"_s), film);
        QCOMPARE(a.read(u"Notatka.md"_s), u"Zażółć gęślą jaźń\n"_s);
        QVERIFY(leftovers(a).isEmpty());
        // Nothing of the film crossed the transport whole: it came in pieces.
        QVERIFY2(server.largestPayload() < MiB, qPrintable(QString::number(server.largestPayload())));
        QCOMPARE(a.engine->stateOf(u"Wakacje/łódź.mp4"_s)->localHash, text::fnv1a64Hex(film));
        // Synced: nothing to do the next time (the hash read in pieces matches).
        QCOMPARE(counts(a.sync()), Counts{});
    }

    void largeUploadIsReadAsItIsSent()
    {
        FakeWebDavServer server(streaming());
        const QByteArray scan = pattern(12 * MiB, 1);
        Device a(server, u"laptop"_s, {{u"Skany/umowa.pdf"_s, scan}, {u"a.md"_s, "x\n"}});
        QCOMPARE(counts(a.sync()), (Counts{.up = 2}));
        QCOMPARE(server.bytes(u"Notatki/Skany/umowa.pdf"_s), std::optional<QByteArray>(scan));
        QVERIFY2(server.largestPayload() < MiB, qPrintable(QString::number(server.largestPayload())));
        // Changed locally (another program): uploaded again, from the file.
        const QByteArray signed_ = pattern(12 * MiB, 2);
        QVERIFY(writeFileBehind(a.root, u"Skany/umowa.pdf"_s, signed_, 1'800'000'000'000));
        a.vault->rescan();
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        QCOMPARE(server.bytes(u"Notatki/Skany/umowa.pdf"_s), std::optional<QByteArray>(signed_));
        // Changed on the server: downloaded over it.
        const QByteArray stamped = pattern(12 * MiB, 3);
        server.putFile(u"Notatki/Skany/umowa.pdf"_s, stamped);
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QCOMPARE(fileBytes(a, u"Skany/umowa.pdf"_s), stamped);
        QVERIFY(leftovers(a).isEmpty());
    }

    void smallFilesAndNotesStayInMemory()
    {
        // Below the limit (and every note): whole, as before — one piece each way.
        FakeWebDavServer server(streaming());
        const QByteArray icon = pattern(64 * 1024);
        Device a(server, u"laptop"_s, {{u"ikona.png"_s, icon}});
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        QCOMPARE(server.largestPayload(), icon.size());
    }

    void droppedConnectionLeavesNoPartialFile_data()
    {
        QTest::addColumn<bool>("existing");
        QTest::newRow("new file") << false;
        QTest::newRow("file replaced") << true;
    }
    void droppedConnectionLeavesNoPartialFile()
    {
        QFETCH(bool, existing);
        FakeWebDavServer server(streaming());
        server.mkdir(kFolder);
        const QByteArray old = pattern(6 * MiB, 4);
        Device a(server, u"laptop"_s,
                 existing ? QMap<QString, QByteArray>{{u"nagranie.m4a"_s, old}} : QMap<QString, QByteArray>{});
        if (existing)
            QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        const QByteArray recording = pattern(16 * MiB, 5);
        server.putFile(u"Notatki/nagranie.m4a"_s, recording);
        server.dropStreamedGets(u".m4a"_s, 5 * MiB);
        const SyncReport broken = a.sync();
        QCOMPARE(broken.downloaded, 0);
        QVERIFY(!broken.errors.isEmpty() || broken.remoteError.isError());
        // Never a part of the download under the real name; the old version stays as it was.
        if (existing)
            QCOMPARE(fileBytes(a, u"nagranie.m4a"_s), old);
        else
            QVERIFY(!QFileInfo::exists(a.root + u"/nagranie.m4a"_s));
        QVERIFY(leftovers(a).isEmpty());
        // The connection is back: the next sync brings the whole file.
        server.dropStreamedGets({}, -1);
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QCOMPARE(fileBytes(a, u"nagranie.m4a"_s), recording);
        QVERIFY(leftovers(a).isEmpty());
    }

#ifdef Q_OS_LINUX
    void fullDiskLeavesNoPartialFile()
    {
        // A file size limit stands for a full disk: writes past it fail (EFBIG; SIGXFSZ ignored).
        FakeWebDavServer server(streaming());
        server.mkdir(kFolder);
        const QByteArray film = pattern(20 * MiB, 6);
        server.putFile(u"Notatki/film.mkv"_s, film);
        Device a(server, u"laptop"_s);
        rlimit before{};
        QCOMPARE(getrlimit(RLIMIT_FSIZE, &before), 0);
        const auto previous = std::signal(SIGXFSZ, SIG_IGN);
        rlimit small = before;
        small.rlim_cur = rlim_t(8 * MiB);
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &small), 0);
        const SyncReport r = a.sync();
        setrlimit(RLIMIT_FSIZE, &before);
        std::signal(SIGXFSZ, previous);
        QCOMPARE(r.downloaded, 0);
        QCOMPARE(r.errors.size(), 1);
        QCOMPARE(r.errors.first().code, u"write-failed"_s);
        QVERIFY(!r.remoteError.isError()); // (a local failure: the network is fine)
        QVERIFY(!QFileInfo::exists(a.root + u"/film.mkv"_s));
        QVERIFY(leftovers(a).isEmpty());
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QCOMPARE(fileBytes(a, u"film.mkv"_s), film);
    }

    void unreadableLocalFileIsNeverUploadedEmpty()
    {
        if (geteuid() == 0)
            QSKIP("permissions are not enforced for root");
        FakeWebDavServer server(streaming());
        const QByteArray v1 = pattern(6 * MiB, 7);
        Device a(server, u"laptop"_s, {{u"wideo.mov"_s, v1}});
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        QVERIFY(writeFileBehind(a.root, u"wideo.mov"_s, pattern(6 * MiB, 8), 1'800'000'000'000));
        a.vault->rescan();
        QFile::setPermissions(a.root + u"/wideo.mov"_s, QFileDevice::WriteOwner);
        const SyncReport r = a.sync();
        QFile::setPermissions(a.root + u"/wideo.mov"_s, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(r.uploaded, 0);
        QCOMPARE(r.errors.size(), 1);
        QCOMPARE(r.errors.first().code, u"read-failed"_s);
        QCOMPARE(server.bytes(u"Notatki/wideo.mov"_s), std::optional<QByteArray>(v1));
    }
#endif

    void largeConflictKeepsBothVersions()
    {
        FakeWebDavServer server(streaming());
        const QByteArray base = pattern(6 * MiB, 9);
        Device a(server, u"laptop"_s, {{u"Projekt/plan.pdf"_s, base}});
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        const QByteArray local = pattern(6 * MiB, 10);
        const QByteArray remote = pattern(7 * MiB, 11);
        QVERIFY(writeFileBehind(a.root, u"Projekt/plan.pdf"_s, local, 1'800'000'000'000));
        a.vault->rescan();
        server.putFile(u"Notatki/Projekt/plan.pdf"_s, remote);
        const SyncReport r = a.sync();
        QCOMPARE(r.conflicts, (QList<VaultPath>{u"Projekt/plan.pdf"_s}));
        QCOMPARE(r.conflictCopies.size(), 1);
        const VaultPath copy = r.conflictCopies.first();
        QVERIFY(copy.startsWith(u"Projekt/"_s));
        // Local wins at the path, the server's version is kept as a copy — on both sides.
        QCOMPARE(fileBytes(a, u"Projekt/plan.pdf"_s), local);
        QCOMPARE(fileBytes(a, copy), remote);
        QCOMPARE(server.bytes(u"Notatki/Projekt/plan.pdf"_s), std::optional<QByteArray>(local));
        QCOMPARE(server.bytes(u"Notatki/"_s + copy), std::optional<QByteArray>(remote));
        QVERIFY(server.largestPayload() < MiB);
        QVERIFY(leftovers(a).isEmpty());
        QCOMPARE(counts(a.sync()), Counts{});
    }

    void sameContentDownloadedIsOnlyRemembered()
    {
        // A new ETag, the same bytes: the staged download is compared by its hash and dropped.
        FakeWebDavServer server(streaming());
        const QByteArray photo = pattern(5 * MiB, 12);
        Device a(server, u"laptop"_s, {{u"zdjęcie.jpg"_s, photo}});
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        server.touchEtag(u"Notatki/zdjęcie.jpg"_s);
        a.changes.clear();
        QCOMPARE(counts(a.sync()), Counts{});
        QVERIFY(a.changes.isEmpty()); // the file was not written
        QVERIFY(leftovers(a).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSyncStreamed)
#include "tst_sync_streamed.moc"

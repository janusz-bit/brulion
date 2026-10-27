// Folders over Nextcloud: empty folders both ways, folder deletions that never take anything
// unsynced along, moves and renames that do not trip the deletion guards (while a real mass
// deletion still asks), folder and page renames moved on the server with one MOVE, renames on the
// server (oc:fileid, else the ETags a move keeps) renamed here too, and every interleaving of
// renames and edits on two devices without losing a file. Plus the WebDAV client's MOVE, folder
// listing and mkdirs.
#include "synctest.h"

#include <QSignalSpy>

using namespace synctest;

namespace {

QString noteName(int i)
{
    return u"Notatka %1 ąę.md"_s.arg(i, 3, 10, QLatin1Char('0'));
}

/// `count` notes in `folder` ("" = the vault root), each with its own text.
QMap<QString, QByteArray> notesIn(const QString &folder, int count, int first = 0)
{
    QMap<QString, QByteArray> out;
    for (int i = first; i < first + count; ++i) {
        const QString path = folder.isEmpty() ? noteName(i) : folder + u'/' + noteName(i);
        out.insert(path,
                   u8(u"Notatka %1 w „%2” — zażółć gęślą jaźń\nDruga linia\nTrzecia linia\n"_s.arg(i).arg(folder)));
    }
    return out;
}

QMap<QString, QByteArray> operator+(QMap<QString, QByteArray> a, const QMap<QString, QByteArray> &b)
{
    a.insert(b);
    return a;
}

bool isLocalDir(const Device &d, const QString &rel)
{
    return QFileInfo(d.root + u'/' + rel).isDir();
}

QStringList requestPaths(const FakeWebDavServer &server, const QByteArray &method)
{
    QStringList out;
    for (const auto &request : server.requests(method))
        out.append(request.path.mid(server.abs(kFolder).size() + 1));
    return out;
}

QStringList folderChanges(const SyncReport &report, SyncAction action)
{
    QStringList out;
    for (const SyncChange &change : report.changes) {
        if (change.action == action)
            out.append(change.path);
    }
    out.sort();
    return out;
}

QString nfd(const QString &s)
{
    return s.normalized(QString::NormalizationForm_D);
}

/// The kinds of server for the renames done on the server (the shared data rows): what proves
/// that a file under a new name is the one synced before.
void addServerRows()
{
    QTest::addColumn<bool>("fileIds");
    QTest::addColumn<bool>("keepsEtags");
    QTest::addColumn<bool>("adopted");
    QTest::newRow("Nextcloud (fileid, ETags kept)") << true << true << true;
    QTest::newRow("fileid, MOVE gives new ETags") << true << false << true;
    QTest::newRow("ETags only, no fileid") << false << true << true;
    QTest::newRow("neither: download and trash as before") << false << false << false;
}

/// Server options of the current row of addServerRows().
FakeWebDavServer::Options serverRowOptions()
{
    QFETCH(bool, fileIds);
    QFETCH(bool, keepsEtags);
    FakeWebDavServer::Options options;
    options.fileIds = fileIds;
    options.moveKeepsEtags = keepsEtags;
    return options;
}

/// Renamed (ChangeType::Renamed, source Sync) changes of a device's vault: new path -> old path.
QMap<QString, QString> syncRenames(const Device &d)
{
    QMap<QString, QString> out;
    for (const VaultChange &change : d.changes) {
        if (change.type == ChangeType::Renamed && change.source == ChangeSource::Sync)
            out.insert(change.path, change.oldPath);
    }
    return out;
}

/// Server, clock and two devices ("laptop" = a, "telefon" = b).
struct Pair {
    explicit Pair(const QMap<QString, QByteArray> &aFiles = {}, FakeWebDavServer::Options options = {})
        : server(serverOptions(clock, std::move(options)))
        , a(server, u"laptop"_s, aFiles, withClock())
        , b(server, u"telefon"_s, {}, withClock())
    { }
    DeviceOptions withClock()
    {
        DeviceOptions o;
        o.clock = &clock;
        return o;
    }
    /// Both devices synced and quiet.
    void settle()
    {
        a.sync();
        b.sync();
        a.sync();
    }
    TestClock clock;
    FakeWebDavServer server;
    Device a;
    Device b;
};

template<typename Result>
Result await(const std::function<void(std::function<void(const Result &)>)> &start)
{
    auto result = std::make_shared<std::optional<Result>>();
    start([result](const Result &r) { *result = r; });
    waitFor([result] { return result->has_value(); });
    return result->value_or(Result{});
}

} // namespace

class TestSyncFolders : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- WebDavClient -----------------------------------------------------------------------

    void clientMovesFilesAndFolders()
    {
        FakeWebDavServer server;
        server.putText(u"Notatki/Projekty/Dom.md"_s, u"dom"_s);
        server.putText(u"Notatki/Projekty/Pod/Ogród.md"_s, u"ogród"_s);
        server.putText(u"Notatki/Zajęty/x.md"_s, u"x"_s);
        const QString etag = server.etag(u"Notatki/Projekty/Dom.md"_s);
        WebDavClient client(server.url(kFolder), u"jan"_s, server.options().password, &server);
        const auto move = [&](const QString &from, const QString &to, MoveOptions options) {
            return await<WebDavError>([&](auto done) { client.move(from, to, options, done); });
        };
        MoveOptions folder;
        folder.folder = true;
        // Missing parents of the destination are created; the content and its ETags travel along.
        QVERIFY(!move(u"Projekty"_s, u"Archiwum/2026/Projekty żółć"_s, folder).isError());
        QCOMPARE(server.files(kFolder).keys(),
                 (QStringList{u"Archiwum/2026/Projekty żółć/Dom.md"_s, u"Archiwum/2026/Projekty żółć/Pod/Ogród.md"_s,
                              u"Zajęty/x.md"_s}));
        QCOMPARE(server.etag(u"Notatki/Archiwum/2026/Projekty żółć/Dom.md"_s), etag);
        const auto request = server.requests("MOVE").constFirst();
        QCOMPARE(request.header("overwrite"), QByteArray("F"));
        QVERIFY(request.header("destination").startsWith(server.url(u"Notatki/Archiwum/2026/"_s).toUtf8()));
        // Never over an existing destination; a missing source; the base folder; into itself.
        QVERIFY(move(u"Archiwum/2026/Projekty żółć"_s, u"Zajęty"_s, folder).is(WebDavErrorCode::Precondition));
        QVERIFY(move(u"Nie ma"_s, u"Inny"_s, folder).is(WebDavErrorCode::NotFound));
        QVERIFY(move(QString(), u"Inny"_s, folder).isError());
        QVERIFY(move(u"Zajęty"_s, u"Zajęty/w środku"_s, folder).isError());
        QVERIFY(server.text(u"Notatki/Zajęty/x.md"_s));
        // A file.
        QVERIFY(!move(u"Zajęty/x.md"_s, u"y.md"_s, {}).isError());
        QCOMPARE(server.text(u"Notatki/y.md"_s), std::optional<QString>(u"x"_s));
        // A server without MOVE.
        FakeWebDavServer::Options options;
        options.supportsMove = false;
        FakeWebDavServer old(options);
        old.putText(u"Notatki/A/a.md"_s, u"a"_s);
        WebDavClient oldClient(old.url(kFolder), u"jan"_s, old.options().password, &old);
        const WebDavError refused =
                await<WebDavError>([&](auto done) { oldClient.move(u"A"_s, u"B"_s, folder, done); });
        QVERIFY(refused.is(WebDavErrorCode::NotAllowed));
        QVERIFY(old.text(u"Notatki/A/a.md"_s));
    }

    void clientListsOneFolderAndCreatesFolders()
    {
        FakeWebDavServer server;
        server.putText(u"Notatki/Łódź/a.md"_s, u"a"_s);
        server.putText(u"Notatki/Łódź/Pod/b.md"_s, u"b"_s);
        server.mkdir(u"Notatki/Puste"_s);
        WebDavClient client(server.url(kFolder), u"jan"_s, server.options().password, &server);
        const auto list = [&](const QString &path) {
            return await<ListResult>([&](auto done) { client.listFolder(path, done); });
        };
        const ListResult listed = list(u"Łódź"_s);
        QVERIFY(!listed.error.isError());
        QCOMPARE(listed.entries.size(), 2); // children only, not the grandchildren
        QCOMPARE(listed.entries.at(0).path, u"Łódź/Pod"_s);
        QVERIFY(listed.entries.at(0).isDir);
        QCOMPARE(listed.entries.at(1).path, u"Łódź/a.md"_s);
        QVERIFY(list(u"Puste"_s).entries.isEmpty());
        QVERIFY(list(u"Nie ma"_s).error.is(WebDavErrorCode::NotFound));
        const WebDavError made = await<WebDavError>([&](auto done) { client.mkdirs(u"Nowy/Głęboko/Żółw"_s, done); });
        QVERIFY(!made.isError());
        QVERIFY(server.isDir(u"Notatki/Nowy/Głęboko/Żółw"_s));
        const WebDavError again = await<WebDavError>([&](auto done) { client.mkdirs(u"Nowy/Głęboko/Żółw"_s, done); });
        QVERIFY(!again.isError());
    }

    // ---- Empty folders ----------------------------------------------------------------------

    void emptyFoldersTravelBothWays()
    {
        Pair p(notesIn(u""_s, 2));
        QVERIFY(p.a.vault->createFolder(u"Projekty/Puste ąę"_s));
        p.server.mkdir(u"Notatki/Z chmury/Żółw"_s);
        const SyncReport r = p.a.sync();
        QVERIFY(!r.failed());
        QCOMPARE(folderChanges(r, SyncAction::CreateFolderRemote),
                 (QStringList{u"Projekty"_s, u"Projekty/Puste ąę"_s}));
        QCOMPARE(folderChanges(r, SyncAction::CreateFolderLocal), (QStringList{u"Z chmury"_s, u"Z chmury/Żółw"_s}));
        QCOMPARE(r.foldersCreated, 4);
        QVERIFY(p.server.isDir(u"Notatki/Projekty/Puste ąę"_s));
        QVERIFY(isLocalDir(p.a, u"Z chmury/Żółw"_s));
        QVERIFY(p.a.vault->exists(u"Z chmury/Żółw"_s));
        for (const QString &folder : {u"Projekty"_s, u"Projekty/Puste ąę"_s, u"Z chmury"_s, u"Z chmury/Żółw"_s})
            QVERIFY2(p.a.engine->knowsFolder(folder), qPrintable(folder));
        // The other device gets them all; nothing more to do on either.
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.foldersCreated, 4);
        for (const QString &folder : {u"Projekty/Puste ąę"_s, u"Z chmury/Żółw"_s})
            QVERIFY2(isLocalDir(p.b, folder), qPrintable(folder));
        p.server.clearLog();
        QCOMPARE(p.a.sync().changeCount(), 0);
        QCOMPARE(p.b.sync().changeCount(), 0);
        QVERIFY(p.server.requests("MKCOL").isEmpty());
        QVERIFY(p.server.requests("DELETE").isEmpty());
    }

    void ignoredFoldersNeverTravel()
    {
        Pair p(notesIn(u""_s, 2));
        // Created behind the vault (ignored folders are not the vault's).
        QVERIFY(QDir().mkpath(p.a.root + u"/.obsidian/plugins"_s));
        QVERIFY(QDir().mkpath(p.a.root + u"/logseq"_s));
        QVERIFY(QDir().mkpath(p.a.root + u"/Projekty/.git"_s));
        p.server.mkdir(u"Notatki/.git/hooks"_s);
        p.server.mkdir(u"Notatki/.GIT"_s);
        p.server.mkdir(u"Notatki/.trash/stare"_s);
        p.server.mkdir(u"Notatki/node_modules"_s);
        p.a.vault->rescan();
        p.a.sync();
        QVERIFY(!p.server.isDir(u"Notatki/.obsidian"_s));
        QVERIFY(!p.server.isDir(u"Notatki/logseq"_s));
        QVERIFY(!p.server.isDir(u"Notatki/Projekty/.git"_s));
        QVERIFY(p.server.isDir(u"Notatki/Projekty"_s)); // a visible folder (holding an ignored one)
        QVERIFY(!isLocalDir(p.a, u".GIT"_s));
        QVERIFY(!isLocalDir(p.a, u".git"_s));
        QVERIFY(!isLocalDir(p.a, u"node_modules"_s));
        QVERIFY(!QFileInfo::exists(p.a.root + u"/.trash/stare"_s));
        for (const QString &path : requestPaths(p.server, "PROPFIND"))
            QVERIFY2(!path.contains(u".git"_s, Qt::CaseInsensitive), qPrintable(path));
    }

    void deletedEmptyFolderIsDeletedOnTheOtherSide_data()
    {
        QTest::addColumn<bool>("onServer");
        QTest::newRow("deleted here") << false;
        QTest::newRow("deleted on the server") << true;
    }

    void deletedEmptyFolderIsDeletedOnTheOtherSide()
    {
        QFETCH(bool, onServer);
        Pair p(notesIn(u""_s, 3));
        QVERIFY(p.a.vault->createFolder(u"Pusty żółw/Głębiej"_s));
        p.settle();
        QVERIFY(isLocalDir(p.b, u"Pusty żółw/Głębiej"_s));
        if (onServer)
            p.server.remove(u"Notatki/Pusty żółw"_s);
        else
            QVERIFY(p.a.vault->remove(u"Pusty żółw"_s));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(r.foldersDeleted, 2); // the inner one first
        QCOMPARE(folderChanges(r, onServer ? SyncAction::DeleteFolderLocal : SyncAction::DeleteFolderRemote),
                 (QStringList{u"Pusty żółw"_s, u"Pusty żółw/Głębiej"_s}));
        QVERIFY(!p.server.isDir(u"Notatki/Pusty żółw"_s));
        QVERIFY(!isLocalDir(p.a, u"Pusty żółw"_s));
        QVERIFY(!p.a.vault->exists(u"Pusty żółw"_s));
        QVERIFY(!p.a.engine->knowsFolder(u"Pusty żółw"_s));
        QVERIFY(!p.a.engine->knowsFolder(u"Pusty żółw/Głębiej"_s));
        // The other device follows; nobody brings the folder back.
        QCOMPARE(p.b.sync().foldersDeleted, 2);
        QVERIFY(!isLocalDir(p.b, u"Pusty żółw"_s));
        QCOMPARE(p.a.sync().changeCount(), 0);
        QCOMPARE(p.b.sync().changeCount(), 0);
        QVERIFY(!p.server.isDir(u"Notatki/Pusty żółw"_s));
    }

    void folderEmptiedInTheSameRunIsDeleted_data()
    {
        QTest::addColumn<bool>("onServer");
        QTest::newRow("deleted here") << false;
        QTest::newRow("deleted on the server") << true;
    }

    void folderEmptiedInTheSameRunIsDeleted()
    {
        QFETCH(bool, onServer);
        Pair p(notesIn(u"Projekty/Dom"_s, 3) + notesIn(u""_s, 20, 10));
        p.settle();
        if (onServer)
            p.server.remove(u"Notatki/Projekty"_s);
        else
            QVERIFY(p.a.vault->remove(u"Projekty"_s));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(counts(r), onServer ? Counts{.delL = 3} : Counts{.delR = 3});
        QCOMPARE(r.foldersDeleted, 2);
        QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
        QVERIFY(!isLocalDir(p.a, u"Projekty"_s));
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), Counts{.delL = 3});
        QCOMPARE(rb.foldersDeleted, 2);
        QVERIFY(!isLocalDir(p.b, u"Projekty"_s));
        QCOMPARE(p.b.trash().size(), 3); // the notes themselves went to the trash as before
    }

    void localFolderHoldingUnsyncedFilesIsKept()
    {
        Pair p(notesIn(u"Projekty"_s, 2) + notesIn(u"Inne"_s, 2) + notesIn(u""_s, 20, 10));
        p.settle();
        // Deleted on the server while this device has a new note and an ignored file there.
        p.server.remove(u"Notatki/Projekty"_s);
        p.server.remove(u"Notatki/Inne"_s);
        QVERIFY(p.a.write(u"Projekty/Nowa — niezsynchronizowana.md"_s, u"nowa\n"_s));
        QVERIFY(writeFileBehind(p.a.root, u"Inne/.git/config"_s, "[core]\n"));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(counts(r), (Counts{.up = 1, .delL = 4}));
        // The new note went up (the server has the folder again); nothing was lost.
        QCOMPARE(p.server.text(u"Notatki/Projekty/Nowa — niezsynchronizowana.md"_s),
                 std::optional<QString>(u"nowa\n"_s));
        QVERIFY(isLocalDir(p.a, u"Projekty"_s));
        // The folder holding only an ignored file stays here and does not come back on the server.
        QVERIFY(QFileInfo::exists(p.a.root + u"/Inne/.git/config"_s));
        QVERIFY(!p.server.isDir(u"Notatki/Inne"_s));
        QVERIFY(p.a.engine->knowsFolder(u"Inne"_s));
        QCOMPARE(p.a.sync().changeCount(), 0);
        QVERIFY(!p.server.isDir(u"Notatki/Inne"_s));
        // Once the ignored file is gone, so is the folder.
        QVERIFY(QDir(p.a.root + u"/Inne/.git"_s).removeRecursively());
        QCOMPARE(p.a.sync().foldersDeleted, 1);
        QVERIFY(!isLocalDir(p.a, u"Inne"_s));
    }

    void serverFolderHoldingUnsyncedFilesIsKept()
    {
        Pair p(notesIn(u"Projekty"_s, 2) + notesIn(u"Inne"_s, 2) + notesIn(u""_s, 20, 10));
        p.settle();
        p.server.putText(u"Notatki/Projekty/.git/HEAD"_s, u"ref: refs/heads/main\n"_s);
        QVERIFY(p.a.vault->remove(u"Projekty"_s));
        QVERIFY(p.a.vault->remove(u"Inne"_s));
        // Another device adds a note to "Inne" after this device listed the server, before it
        // deletes the folder: the fresh look before the deletion finds it.
        int listings = 0;
        p.server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (request.method == "PROPFIND" && path == p.server.abs(u"Notatki/Inne"_s) && ++listings == 2)
                p.server.putText(u"Notatki/Inne/Z telefonu.md"_s, u"dopisane\n"_s);
            return std::nullopt;
        });
        const SyncReport r = p.a.sync();
        QCOMPARE(counts(r), Counts{.delR = 4});
        QCOMPARE(r.foldersDeleted, 0);
        QVERIFY(p.server.text(u"Notatki/Projekty/.git/HEAD"_s));
        QCOMPARE(p.server.text(u"Notatki/Inne/Z telefonu.md"_s), std::optional<QString>(u"dopisane\n"_s));
        QVERIFY(p.server.requests("DELETE").size() == 4); // the notes only
        // The new note comes down next time (an addition wins over the folder's deletion).
        p.server.hooks.clear();
        QCOMPARE(counts(p.a.sync()), Counts{.down = 1});
        QCOMPARE(p.a.read(u"Inne/Z telefonu.md"_s), u"dopisane\n"_s);
        QVERIFY(p.server.text(u"Notatki/Projekty/.git/HEAD"_s));
    }

    void heldMassDeletionKeepsTheFoldersUntilConfirmed()
    {
        Pair p(notesIn(u"Archiwum"_s, 12) + notesIn(u""_s, 8, 20));
        QVERIFY(p.a.vault->createFolder(u"Archiwum/Puste"_s));
        p.settle();
        QVERIFY(p.a.vault->remove(u"Archiwum"_s));
        const SyncReport held = p.a.sync();
        QVERIFY(held.needsConfirmation);
        QCOMPARE(held.foldersDeleted, 0);
        QVERIFY(p.server.isDir(u"Notatki/Archiwum/Puste"_s));
        SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        const SyncReport confirmed = p.a.sync(confirm);
        QCOMPARE(counts(confirmed), Counts{.delR = 12});
        QCOMPARE(confirmed.foldersDeleted, 2);
        QVERIFY(!p.server.isDir(u"Notatki/Archiwum"_s));
        // The other device asks as well; after confirming, the folders go there too.
        QVERIFY(p.b.sync().needsConfirmation);
        QVERIFY(isLocalDir(p.b, u"Archiwum/Puste"_s));
        QCOMPARE(p.b.sync(confirm).foldersDeleted, 2);
        QVERIFY(!isLocalDir(p.b, u"Archiwum"_s));
    }

    void networkFailureDuringTheFolderPassIsRetried()
    {
        Pair p(notesIn(u""_s, 2));
        p.a.sync();
        QVERIFY(p.a.vault->createFolder(u"Pusty A"_s));
        QVERIFY(p.a.vault->createFolder(u"Pusty B"_s));
        bool failed = false;
        p.server.hooks.append([&](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
            if (request.method == "MKCOL" && !std::exchange(failed, true))
                return FakeWebDavServer::status(0); // no answer: the run stops
            return std::nullopt;
        });
        const SyncReport r = p.a.sync();
        QVERIFY(r.remoteError.is(WebDavErrorCode::Network));
        QCOMPARE(r.foldersCreated, 0);
        QVERIFY(!p.a.engine->knowsFolder(u"Pusty A"_s));
        const SyncReport again = p.a.sync();
        QVERIFY(!again.failed());
        QCOMPARE(again.foldersCreated, 2);
        QVERIFY(p.server.isDir(u"Notatki/Pusty A"_s));
        QVERIFY(p.server.isDir(u"Notatki/Pusty B"_s));
        QVERIFY(p.a.engine->knowsFolder(u"Pusty B"_s));
    }

    void resetForgetsTheFolders()
    {
        Pair p;
        QVERIFY(p.a.vault->createFolder(u"Pusty"_s));
        p.a.sync();
        QVERIFY(p.a.engine->knowsFolder(u"Pusty"_s));
        p.a.engine->resetState();
        QVERIFY(!p.a.engine->knowsFolder(u"Pusty"_s));
        // A first sync after a reset deletes nothing: the folder missing here is created again.
        QVERIFY(p.a.vault->remove(u"Pusty"_s));
        QCOMPARE(p.a.sync().foldersCreated, 1);
        QVERIFY(isLocalDir(p.a, u"Pusty"_s));
        QVERIFY(p.server.isDir(u"Notatki/Pusty"_s));
    }

    // ---- Moves and the deletion guards ------------------------------------------------------

    void folderRenameIsMovedOnTheServer()
    {
        Pair p(notesIn(u"Projekty ąę"_s, 30) + notesIn(u"Projekty ąę/Pod"_s, 5, 40) + notesIn(u""_s, 5, 50));
        QVERIFY(p.a.vault->createFolder(u"Projekty ąę/Pusty"_s));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty ąę"_s, u"Archiwum/Projekty 2026"_s));
        p.server.clearLog();
        const SyncReport r = p.a.sync();
        QVERIFY(!r.failed());
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(counts(r), Counts{});
        QCOMPARE(r.foldersMoved, 1);
        QCOMPARE(r.folderMoves, (QList<SyncFolderMove>{{u"Projekty ąę"_s, u"Archiwum/Projekty 2026"_s}}));
        QCOMPARE(requestPaths(p.server, "MOVE"), QStringList{u"Projekty ąę"_s});
        QVERIFY(p.server.requests("PUT").isEmpty());
        QVERIFY(p.server.requests("DELETE").isEmpty());
        QVERIFY(p.server.requests("GET").isEmpty());
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(p.server.isDir(u"Notatki/Archiwum/Projekty 2026/Pusty"_s));
        QVERIFY(!p.server.isDir(u"Notatki/Projekty ąę"_s));
        // The bookkeeping followed the folder: nothing to do next time, merges keep their base.
        const QString moved = u"Archiwum/Projekty 2026/"_s + noteName(0);
        QVERIFY(p.a.engine->stateOf(moved));
        QVERIFY(p.a.engine->mergeBaseOf(moved));
        QVERIFY(!p.a.engine->stateOf(u"Projekty ąę/"_s + noteName(0)));
        QVERIFY(p.a.engine->knowsFolder(u"Archiwum/Projekty 2026/Pusty"_s));
        QVERIFY(!p.a.engine->knowsFolder(u"Projekty ąę/Pusty"_s));
        p.server.clearLog();
        QCOMPARE(p.a.sync().changeCount(), 0);
        QVERIFY(p.server.requests("GET").isEmpty());

        // The other device renames its folder too (the server kept the ETags): nothing comes down,
        // nothing goes to the trash (serverFolderRenameIsRenamedHere has the details).
        const SyncReport rb = p.b.sync();
        QVERIFY(!rb.needsConfirmation);
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.foldersMovedHere, 1);
        QCOMPARE(p.b.files(), p.a.files());
        QVERIFY(!isLocalDir(p.b, u"Projekty ąę"_s));
        QVERIFY(isLocalDir(p.b, u"Archiwum/Projekty 2026/Pusty"_s));
        QVERIFY(p.b.trash().isEmpty());
        QCOMPARE(p.b.sync().changeCount(), 0);
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void folderRenameWithoutMove_data()
    {
        QTest::addColumn<bool>("supportsMove");
        QTest::addColumn<bool>("moveKeepsEtags");
        QTest::newRow("server without MOVE") << false << true;
        QTest::newRow("MOVE gives new ETags") << true << false;
    }

    void folderRenameWithoutMove()
    {
        QFETCH(bool, supportsMove);
        QFETCH(bool, moveKeepsEtags);
        FakeWebDavServer::Options options;
        options.supportsMove = supportsMove;
        options.moveKeepsEtags = moveKeepsEtags;
        Pair p(notesIn(u"Projekty"_s, 30) + notesIn(u""_s, 5, 50), options);
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty 2026"_s));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        if (supportsMove)
            QCOMPARE(counts(r), Counts{});
        else
            QCOMPARE(counts(r), (Counts{.up = 30, .delR = 30}));
        QCOMPARE(r.foldersMoved, supportsMove ? 1 : 0);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
        QCOMPARE(counts(p.a.sync()), Counts{});
        const SyncReport rb = p.b.sync();
        QVERIFY(!rb.needsConfirmation);
        QCOMPARE(counts(rb), (Counts{.down = 30, .delL = 30}));
        QCOMPARE(p.b.files(), p.a.files());
        QVERIFY(!isLocalDir(p.b, u"Projekty"_s));
    }

    void movingManyPagesDoesNotAsk()
    {
        Pair p(notesIn(u""_s, 30) + notesIn(u"Inne"_s, 5, 40));
        p.settle();
        // Page by page into a new folder, some of them renamed too.
        for (int i = 0; i < 30; ++i) {
            const QString target = i % 3 == 0 ? u"Archiwum/Stara "_s + noteName(i) : u"Archiwum/"_s + noteName(i);
            QVERIFY(p.a.vault->rename(noteName(i), target));
        }
        p.server.clearLog();
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        // One MOVE per page, nothing uploaded or deleted; the other device renames them too.
        QCOMPARE(counts(r), Counts{});
        QCOMPARE(r.filesMoved, 30);
        QCOMPARE(p.server.requests("MOVE").size(), 30);
        QVERIFY(p.server.requests("PUT").isEmpty());
        QVERIFY(p.server.requests("DELETE").isEmpty());
        QCOMPARE(p.server.files(kFolder), p.a.files());
        const SyncReport rb = p.b.sync();
        QVERIFY(!rb.needsConfirmation);
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.filesMovedHere, 30);
        QCOMPARE(p.b.files(), p.a.files());
        QVERIFY(p.b.trash().isEmpty());
        QCOMPARE(p.a.sync().changeCount(), 0);
        QCOMPARE(p.b.sync().changeCount(), 0);
    }

    void aRealMassDeletionStillAsks_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("deleted, other notes created") << u"replaced"_s;
        // By another program: no rename the vault saw, and no content left to tell it by.
        QTest::newRow("renamed and every note rewritten") << u"rewritten"_s;
        QTest::newRow("many empty notes, one new empty") << u"empty"_s;
    }

    void aRealMassDeletionStillAsks()
    {
        QFETCH(QString, kind);
        QMap<QString, QByteArray> files = notesIn(u""_s, 5, 50);
        for (int i = 0; i < 30; ++i)
            files.insert(u"Projekty/"_s + noteName(i), kind == u"empty"_s ? QByteArray() : u8(u"treść %1\n"_s.arg(i)));
        Pair p(files);
        p.settle();
        if (kind == u"replaced"_s) {
            QVERIFY(p.a.vault->remove(u"Projekty"_s));
            for (int i = 0; i < 30; ++i)
                QVERIFY(p.a.write(u"Nowe/"_s + noteName(i), u"zupełnie inna treść %1\n"_s.arg(i)));
        } else if (kind == u"rewritten"_s) {
            QVERIFY(QDir().rename(p.a.root + u"/Projekty"_s, p.a.root + u"/Projekty 2"_s));
            for (int i = 0; i < 30; ++i)
                QVERIFY(writeFileBehind(p.a.root, u"Projekty 2/"_s + noteName(i), u8(u"przepisana %1\n"_s.arg(i))));
            p.a.vault->rescan();
        } else {
            QVERIFY(p.a.vault->remove(u"Projekty"_s));
            QVERIFY(p.a.write(u"Pusta.md"_s, QString()));
        }
        const SyncReport r = p.a.sync();
        QVERIFY(r.needsConfirmation);
        QVERIFY(r.foldersMoved == 0);
        QVERIFY(p.server.text(u"Notatki/Projekty/"_s + noteName(0)));
        SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        p.a.sync(confirm);
        QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
        // The other device sees the deletions coming and asks as well.
        const SyncReport rb = p.b.sync();
        QVERIFY(rb.needsConfirmation);
        QVERIFY(p.b.vault->exists(u"Projekty/"_s + noteName(0)));
    }

    void wholeNotebookInOneRenamedFolder_data()
    {
        QTest::addColumn<bool>("supportsMove");
        QTest::addColumn<bool>("moveKeepsEtags");
        QTest::addColumn<bool>("fileIds");
        QTest::newRow("MOVE") << true << true << false;
        QTest::newRow("MOVE gives new ETags, fileid") << true << false << true;
        QTest::newRow("MOVE gives new ETags") << true << false << false;
        QTest::newRow("no MOVE") << false << true << true;
    }

    void wholeNotebookInOneRenamedFolder()
    {
        // Every note in one folder: renaming it leaves no synced file at its old path on either
        // side — moved, not an emptied vault or a wiped server.
        QFETCH(bool, supportsMove);
        QFETCH(bool, moveKeepsEtags);
        QFETCH(bool, fileIds);
        FakeWebDavServer::Options options;
        options.supportsMove = supportsMove;
        options.moveKeepsEtags = moveKeepsEtags;
        options.fileIds = fileIds;
        Pair p(notesIn(u"Wszystko"_s, 12), options);
        p.settle();
        QVERIFY(p.a.vault->rename(u"Wszystko"_s, u"Wszystko 2026"_s));
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        const SyncReport rb = p.b.sync();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QVERIFY(!rb.needsConfirmation);
        // Renamed here too when the server proves the files are the same (its MOVE kept their ids
        // or ETags); uploaded anew (no MOVE) or without proof they come down and the old go.
        const bool proven = supportsMove && (moveKeepsEtags || fileIds);
        QCOMPARE(counts(rb), (proven ? Counts{} : Counts{.down = 12, .delL = 12}));
        QCOMPARE(rb.foldersMovedHere, proven ? 1 : 0);
        QCOMPARE(p.b.trash().size(), proven ? 0 : 12);
        QCOMPARE(p.b.files(), p.a.files());
        QVERIFY(!isLocalDir(p.b, u"Wszystko"_s));
        QCOMPARE(p.b.sync().changeCount(), 0);
    }

    void wholeNotebookRenamedOnTheServerWithAnEditAsks()
    {
        // Every note in one folder renamed on the server and one of them edited there since (a new
        // ETag, no file ids): the server looked empty, so every file under the new name must be
        // proven — it is not renamed here, the notes come down and the deletion of the old ones
        // waits for the user. Nothing is lost.
        Pair p(notesIn(u"Wszystko"_s, 12));
        p.settle();
        QVERIFY(p.server.moveNode(u"Notatki/Wszystko"_s, u"Notatki/Wszystko 2026"_s));
        p.server.putText(u"Notatki/Wszystko 2026/"_s + noteName(4), u"Poprawione w przeglądarce — źdźbło\n"_s);
        const SyncReport rb = p.b.sync();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QCOMPARE(rb.foldersMovedHere, 0);
        QCOMPARE(rb.filesMovedHere, 0);
        QCOMPARE(counts(rb), Counts{.down = 12});
        QVERIFY(rb.needsConfirmation);
        QVERIFY(p.b.trash().isEmpty());
        QCOMPARE(p.b.read(u"Wszystko 2026/"_s + noteName(4)), u"Poprawione w przeglądarce — źdźbło\n"_s);
        QVERIFY(p.b.vault->exists(u"Wszystko/"_s + noteName(4)));
    }

    void anEmptiedVaultIsStillRefused()
    {
        // New local files with other content do not pass for moved ones.
        Pair p(notesIn(u"Wszystko"_s, 12));
        p.settle();
        QVERIFY(p.a.vault->remove(u"Wszystko"_s));
        QVERIFY(p.a.write(u"Dziennik/2026-10-14.md"_s, u"- \n"_s));
        const SyncReport r = p.a.sync();
        QVERIFY(r.syncError.is(SyncErrorCode::LocalEmpty));
        QCOMPARE(p.server.files(kFolder).size(), 12);
        QVERIFY(p.server.requests("PUT").size() == 12); // the first sync's uploads only
        QVERIFY(p.server.isDir(u"Notatki/Wszystko"_s));
    }

    void aWipedServerWithNewFilesOfTheSameSizesIsStillRefused()
    {
        Pair p(notesIn(u"Wszystko"_s, 4));
        p.settle();
        p.server.remove(u"Notatki/Wszystko"_s);
        // Another device uploads notes of the very same sizes, other content.
        for (const auto &[path, bytes] : notesIn(u"Inne"_s, 4).asKeyValueRange()) {
            QByteArray other = bytes;
            other[0] = 'n';
            p.server.putFile(kFolder + u'/' + path, other);
        }
        const SyncReport r = p.a.sync();
        QVERIFY(r.syncError.is(SyncErrorCode::RemoteEmpty));
        QCOMPARE(p.a.trash().size(), 0);
        QVERIFY(p.a.vault->exists(u"Wszystko/"_s + noteName(0)));
        QVERIFY(isLocalDir(p.a, u"Wszystko"_s));
    }

    // ---- Interleavings ----------------------------------------------------------------------

    void localRenameWhileTheServerCopyWasEdited()
    {
        Pair p(notesIn(u"Projekty"_s, 3) + notesIn(u""_s, 2, 10));
        p.settle();
        const QString old = u"Projekty/"_s + noteName(0);
        QVERIFY(p.b.write(old, p.b.read(old) + u"Dopisane na telefonie ż\n"_s));
        QCOMPARE(counts(p.b.sync()), Counts{.up = 1});
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        const SyncReport r = p.a.sync();
        QCOMPARE(r.foldersMoved, 1);
        QCOMPARE(counts(r), Counts{.down = 1});
        const QString moved = u"Projekty 2/"_s + noteName(0);
        QVERIFY(p.a.read(moved).endsWith(u"Dopisane na telefonie ż\n"_s));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.foldersMovedHere, 1);
        QCOMPARE(p.b.files(), p.a.files());
    }

    void localRenameAndEditsOnBothSidesMerge()
    {
        Pair p(notesIn(u"Projekty"_s, 4) + notesIn(u""_s, 2, 10));
        p.settle();
        const QString old = u"Projekty/"_s + noteName(0);
        const QString text = p.a.read(old);
        QVERIFY(p.b.write(old, text + u"Z telefonu\n"_s));
        p.b.sync();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        const QString moved = u"Projekty 2/"_s + noteName(0);
        QVERIFY(p.a.write(moved, u"Na laptopie\n"_s + text));
        // ... and a note deleted after the rename: deleted on the server too.
        QVERIFY(p.a.remove(u"Projekty 2/"_s + noteName(3)));
        const SyncReport r = p.a.sync();
        QCOMPARE(r.foldersMoved, 1);
        QCOMPARE(r.merged, 1);
        QCOMPARE(r.deletedRemote, 1);
        QVERIFY(r.conflicts.isEmpty());
        QCOMPARE(p.a.read(moved), u"Na laptopie\n"_s + text + u"Z telefonu\n"_s);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        p.b.sync();
        QCOMPARE(p.b.files(), p.a.files());
    }

    void localRenameWhileTheServerDeletedANote()
    {
        // As for a single file, renaming wins over a deletion on the other side: the note is
        // uploaded again under the new name (no folder MOVE, which would take it for deleted
        // there); the other notes are moved one by one.
        Pair p(notesIn(u"Projekty"_s, 4) + notesIn(u""_s, 2, 10));
        p.settle();
        QVERIFY(p.b.remove(u"Projekty/"_s + noteName(0)));
        QCOMPARE(counts(p.b.sync()), Counts{.delR = 1});
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(r.foldersMoved, 0);
        QCOMPARE(r.filesMoved, 3);
        QCOMPARE(counts(r), (Counts{.up = 1}));
        QVERIFY(p.a.vault->exists(u"Projekty 2/"_s + noteName(0)));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
        p.b.sync();
        QCOMPARE(p.b.files(), p.a.files());
    }

    void serverRenameWhileTheLocalCopyWasEdited_data() { addServerRows(); }

    void serverRenameWhileTheLocalCopyWasEdited()
    {
        QFETCH(bool, adopted);
        Pair p(notesIn(u"Projekty"_s, 3) + notesIn(u""_s, 2, 10), serverRowOptions());
        p.settle();
        QVERIFY(p.b.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        QCOMPARE(p.b.sync().foldersMoved, 1);
        const QString old = u"Projekty/"_s + noteName(0);
        const QString moved = u"Projekty 2/"_s + noteName(0);
        const QString edited = p.a.read(old) + u"Poprawka z laptopa — żółw\n"_s;
        QVERIFY(p.a.write(old, edited));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        if (adopted) {
            // The folder is renamed here too: the edited note follows and goes up there; no
            // "Projekty" anywhere.
            QCOMPARE(counts(r), (Counts{.up = 1}));
            QCOMPARE(r.foldersMovedHere, 1);
            QCOMPARE(p.a.read(moved), edited);
            QCOMPARE(p.server.text(kFolder + u'/' + moved), std::optional<QString>(edited));
            QVERIFY(!isLocalDir(p.a, u"Projekty"_s));
            QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
        } else {
            // As before: the edited note stays (and goes up) where it was; the others moved.
            QCOMPARE(counts(r), (Counts{.up = 1, .down = 3, .delL = 2}));
            QCOMPARE(p.a.read(old), edited);
            QCOMPARE(p.server.text(kFolder + u'/' + old), std::optional<QString>(edited));
            QVERIFY(p.a.vault->exists(moved));
        }
        QCOMPARE(p.server.files(kFolder), p.a.files());
        p.b.sync();
        QCOMPARE(p.b.files(), p.a.files());
    }

    void serverRenameCarriesUnsyncedFilesAlong_data() { addServerRows(); }

    void serverRenameCarriesUnsyncedFilesAlong()
    {
        QFETCH(bool, adopted);
        Pair p(notesIn(u"Projekty"_s, 3) + notesIn(u"Dom"_s, 3, 10) + notesIn(u""_s, 2, 20), serverRowOptions());
        p.settle();
        QVERIFY(p.b.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        QVERIFY(p.b.vault->rename(u"Dom"_s, u"Dom 2"_s));
        QCOMPARE(p.b.sync().foldersMoved, 2);
        // Meanwhile on the laptop: a new note in one old folder, a file Brulion does not sync in
        // the other.
        QVERIFY(p.a.write(u"Projekty/Nowa z laptopa.md"_s, u"nowa\n"_s));
        QVERIFY(writeFileBehind(p.a.root, u"Dom/.DS_Store"_s, "Bud1"));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        if (adopted) {
            // Both folders renamed here too, with what they held: the new note goes up under the
            // new name, the Finder's .DS_Store (never synced) moved along; nothing trashed.
            QCOMPARE(counts(r), (Counts{.up = 1}));
            QCOMPARE(r.foldersMovedHere, 2);
            QCOMPARE(p.server.text(u"Notatki/Projekty 2/Nowa z laptopa.md"_s), std::optional<QString>(u"nowa\n"_s));
            QVERIFY(QFileInfo::exists(p.a.root + u"/Dom 2/.DS_Store"_s));
            QVERIFY(!isLocalDir(p.a, u"Projekty"_s));
            QVERIFY(!isLocalDir(p.a, u"Dom"_s));
            QVERIFY(p.a.trash().isEmpty());
        } else {
            QCOMPARE(counts(r), (Counts{.up = 1, .down = 6, .delL = 6}));
            // Both old folders stay: one holds the new note (now on the server too), the other the
            // Finder's .DS_Store (never synced).
            QCOMPARE(p.server.text(u"Notatki/Projekty/Nowa z laptopa.md"_s), std::optional<QString>(u"nowa\n"_s));
            QVERIFY(QFileInfo::exists(p.a.root + u"/Dom/.DS_Store"_s));
            QCOMPARE(r.foldersDeleted, 0);
        }
        QVERIFY(!p.server.isDir(u"Notatki/Dom"_s));
        QMap<QString, QString> synced = p.a.files();
        QVERIFY(synced.remove(adopted ? u"Dom 2/.DS_Store"_s : u"Dom/.DS_Store"_s));
        QCOMPARE(p.server.files(kFolder), synced);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(p.a.vault->exists(u"Projekty 2/"_s + noteName(i)));
            QVERIFY(p.a.vault->exists(u"Dom 2/"_s + noteName(10 + i)));
        }
    }

    void bothDevicesRenameTheSameFolder()
    {
        Pair p(notesIn(u"Projekty"_s, 3) + notesIn(u""_s, 2, 10));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty A"_s));
        QVERIFY(p.b.vault->rename(u"Projekty"_s, u"Projekty B"_s));
        QCOMPARE(p.a.sync().foldersMoved, 1);
        const SyncReport rb = p.b.sync();
        QVERIFY(!rb.failed());
        QVERIFY(!rb.needsConfirmation);
        QCOMPARE(rb.foldersMoved, 0);
        QCOMPARE(counts(rb), (Counts{.up = 3, .down = 3}));
        QVERIFY(!p.a.sync().needsConfirmation);
        // Both names hold every note on every side: nothing lost, nothing asked.
        QCOMPARE(p.a.files(), p.b.files());
        QCOMPARE(p.server.files(kFolder), p.a.files());
        for (const QString &folder : {u"Projekty A/"_s, u"Projekty B/"_s}) {
            for (int i = 0; i < 3; ++i)
                QVERIFY(p.a.vault->exists(folder + noteName(i)));
        }
        QVERIFY(!isLocalDir(p.a, u"Projekty"_s));
        QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
    }

    void caseOnlyFolderRename_data()
    {
        QTest::addColumn<bool>("otherNotes");
        QTest::addColumn<bool>("supportsMove");
        QTest::newRow("MOVE") << true << true;
        QTest::newRow("MOVE, every note in the folder") << false << true;
        QTest::newRow("no MOVE, every note in the folder") << false << false;
    }

    void caseOnlyFolderRename()
    {
        QFETCH(bool, otherNotes);
        QFETCH(bool, supportsMove);
        FakeWebDavServer::Options options;
        options.supportsMove = supportsMove;
        Pair p(notesIn(u"projekty"_s, 12) + (otherNotes ? notesIn(u""_s, 2, 20) : QMap<QString, QByteArray>()),
               options);
        p.settle();
        QVERIFY(p.a.vault->rename(u"projekty"_s, u"Projekty"_s));
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(r.foldersMoved, supportsMove ? 1 : 0);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(!p.server.isDir(u"Notatki/projekty"_s));
        const SyncReport rb = p.b.sync();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QVERIFY(!rb.needsConfirmation);
        if (!brulion::test::isCaseSensitive(p.b.root)) {
            // Case-insensitive storage (Windows, Android shared storage): "Projekty" is the very
            // folder "projekty" — renamed here too, nothing to transfer, no note trashed.
            QCOMPARE(counts(rb), Counts{});
            QCOMPARE(rb.foldersMovedHere, 1);
            QCOMPARE(p.b.files(), p.a.files());
            QVERIFY(p.b.vault->trash().isEmpty());
            QCOMPARE(p.server.files(kFolder), p.a.files());
            QCOMPARE(p.b.sync().changeCount(), 0);
            return;
        }
        // Case-sensitive storage: renamed here too when the server's MOVE kept the ETags; files
        // uploaded anew (no MOVE) come down under the new name and the old ones go.
        QCOMPARE(counts(rb), (supportsMove ? Counts{} : Counts{.down = 12, .delL = 12}));
        QCOMPARE(rb.foldersMovedHere, supportsMove ? 1 : 0);
        QCOMPARE(p.b.files(), p.a.files());
        QVERIFY(!isLocalDir(p.b, u"projekty"_s));
        QCOMPARE(p.b.sync().changeCount(), 0);
    }

    void caseOnlyFolderRenamedOnBothSides()
    {
        // Case-insensitive storage: "projekty" renamed to "Projekty" here and to "PROJEKTY" on
        // the server since the last run. The vault's spelling moves to the server's, the notes
        // with it — none trashed, none downloaded again, the synced state follows.
        Pair p(notesIn(u"projekty"_s, 4) + notesIn(u""_s, 2, 20));
        // Links cannot stand in here: the vault would track every spelling as a folder of its own.
        if (brulion::test::isCaseSensitive(p.a.root))
            QSKIP("needs case-insensitive storage (Windows)");
        p.settle();
        QVERIFY(p.a.vault->rename(u"projekty"_s, u"Projekty"_s));
        QVERIFY(p.server.moveNode(u"Notatki/projekty"_s, u"Notatki/PROJEKTY"_s));
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(p.a.trash().size(), 0);
        QCOMPARE(counts(r).down, 0);
        QVERIFY(p.a.vault->exists(u"PROJEKTY/"_s + noteName(0)));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void caseOnlyRenameOfAnEmptyFolder_data()
    {
        QTest::addColumn<bool>("onServer");
        QTest::addColumn<bool>("aliased");
        QTest::newRow("renamed here") << false << false;
        QTest::newRow("renamed on the server") << true << false;
        QTest::newRow("renamed on the server, case-insensitive storage") << true << true;
    }

    void caseOnlyRenameOfAnEmptyFolder()
    {
        QFETCH(bool, onServer);
        QFETCH(bool, aliased);
        Pair p(notesIn(u""_s, 2));
        // Storage that is case-insensitive itself (Windows) is the aliased case without a link.
        const bool caseSensitive = brulion::test::isCaseSensitive(p.a.root);
        if (aliased && !caseSensitive)
            QSKIP("the storage is case-insensitive itself: the row \"renamed on the server\" is this case");
        aliased = aliased || (onServer && !caseSensitive);
        QVERIFY(p.a.vault->createFolder(u"projekty"_s));
        p.a.sync();
        QVERIFY(p.a.engine->knowsFolder(u"projekty"_s));
        if (onServer)
            QVERIFY(p.server.moveNode(u"Notatki/projekty"_s, u"Notatki/Projekty"_s));
        else
            QVERIFY(p.a.vault->rename(u"projekty"_s, u"Projekty"_s));
        // Case-insensitive storage, simulated: "Projekty" opens the very folder "projekty".
        if (aliased && caseSensitive)
            QVERIFY(QFile::link(p.a.root + u"/projekty"_s, p.a.root + u"/Projekty"_s));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.failed());
        if (aliased) {
            // One folder: neither removed nor created a second time, now or later.
            QCOMPARE(r.foldersDeleted + r.foldersCreated, 0);
            QVERIFY(isLocalDir(p.a, u"projekty"_s));
            QVERIFY(p.server.isDir(u"Notatki/Projekty"_s));
            QCOMPARE(p.a.sync().changeCount(), 0);
            QVERIFY(isLocalDir(p.a, u"projekty"_s));
            return;
        }
        // Two folders on case-sensitive storage: the old one goes, the new one comes, in one run.
        const SyncAction deleted = onServer ? SyncAction::DeleteFolderLocal : SyncAction::DeleteFolderRemote;
        const SyncAction created = onServer ? SyncAction::CreateFolderLocal : SyncAction::CreateFolderRemote;
        QCOMPARE(folderChanges(r, deleted), QStringList{u"projekty"_s});
        QCOMPARE(folderChanges(r, created), QStringList{u"Projekty"_s});
        if (caseSensitive)
            QVERIFY(!isLocalDir(p.a, u"projekty"_s));
        QVERIFY(isLocalDir(p.a, u"Projekty"_s));
        QVERIFY(!p.server.isDir(u"Notatki/projekty"_s));
        QVERIFY(p.server.isDir(u"Notatki/Projekty"_s));
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void caseOnlyFileRenamesAreNotDeletions()
    {
        // Twelve notes renamed in letter case only (no folder rename): one MOVE each, and the other
        // device renames them too — it neither asks nor trashes.
        Pair p(notesIn(u""_s, 12) + notesIn(u"Inne"_s, 2, 20));
        p.settle();
        for (int i = 0; i < 12; ++i)
            QVERIFY(p.a.vault->rename(noteName(i), noteName(i).replace(u"Notatka"_s, u"notatka"_s)));
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(r.filesMoved, 12);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        const SyncReport rb = p.b.sync();
        QVERIFY(!rb.needsConfirmation);
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.filesMovedHere, 12);
        QVERIFY(p.b.trash().isEmpty());
        QCOMPARE(p.b.files(), p.a.files());
    }

    void renameAndAConflictKeepBothVersions()
    {
        Pair p(notesIn(u"Projekty"_s, 2) + notesIn(u""_s, 2, 10));
        p.settle();
        const QString old = u"Projekty/"_s + noteName(0);
        const QString text = p.a.read(old);
        QVERIFY(p.b.write(old, QString(text).replace(u"Druga linia"_s, u"Druga linia z telefonu"_s)));
        p.b.sync();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        const QString moved = u"Projekty 2/"_s + noteName(0);
        QVERIFY(p.a.write(moved, QString(text).replace(u"Druga linia"_s, u"Druga linia z laptopa"_s)));
        const SyncReport r = p.a.sync();
        QCOMPARE(r.foldersMoved, 1);
        QCOMPARE(r.conflicts, QList<VaultPath>{moved});
        QCOMPARE(r.conflictCopies.size(), 1);
        // The local version at the new path, the server's next to it: nothing lost.
        QVERIFY(p.a.read(moved).contains(u"z laptopa"_s));
        QVERIFY(r.conflictCopies.first().startsWith(u"Projekty 2/"_s));
        QVERIFY(p.a.read(r.conflictCopies.first()).contains(u"z telefonu"_s));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        p.b.sync();
        QCOMPARE(p.b.files(), p.a.files());
        // A folder holding a conflict copy moves with it.
        QVERIFY(p.b.vault->rename(u"Projekty 2"_s, u"Archiwum/Projekty"_s));
        p.server.clearLog();
        QCOMPARE(p.b.sync().foldersMoved, 1);
        QVERIFY(p.server.requests("PUT").isEmpty());
        p.a.sync();
        QCOMPARE(p.a.files(), p.b.files());
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(!isLocalDir(p.a, u"Projekty 2"_s));
    }

    void cancelledAfterTheMoveResumes()
    {
        Pair p(notesIn(u"Projekty"_s, 12) + notesIn(u""_s, 2, 20));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Archiwum"_s));
        QVERIFY(p.a.write(u"Archiwum/Nowa.md"_s, u"nowa\n"_s));
        p.server.hooks.append([&](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
            if (request.method == "MOVE")
                p.a.engine->cancel(); // the MOVE itself goes through
            return std::nullopt;
        });
        const SyncReport r = p.a.sync();
        QVERIFY(r.cancelled);
        QCOMPARE(r.foldersMoved, 1);
        p.server.hooks.clear();
        QCOMPARE(counts(p.a.sync()), Counts{.up = 1});
        QCOMPARE(p.server.files(kFolder), p.a.files());
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), (Counts{.down = 1}));
        QCOMPARE(rb.foldersMovedHere, 1);
        QCOMPARE(p.b.files(), p.a.files());
    }

    void unsyncedContentOfAMovedFolderTravelsAlong()
    {
        Pair p(notesIn(u"Projekty"_s, 3) + notesIn(u""_s, 2, 10));
        p.settle();
        p.server.putText(u"Notatki/Projekty/.git/HEAD"_s, u"ref\n"_s);
        QVERIFY(writeFileBehind(p.a.root, u"Projekty/.git/HEAD"_s, "ref\n"));
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        QCOMPARE(p.a.sync().foldersMoved, 1);
        QCOMPARE(p.server.text(u"Notatki/Projekty 2/.git/HEAD"_s), std::optional<QString>(u"ref\n"_s));
        QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
    }

    void emptyFolderRenamed()
    {
        Pair p(notesIn(u""_s, 2));
        QVERIFY(p.a.vault->createFolder(u"Pusty"_s));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Pusty"_s, u"Pusty 2"_s));
        const SyncReport r = p.a.sync();
        QCOMPARE(folderChanges(r, SyncAction::DeleteFolderRemote), QStringList{u"Pusty"_s});
        QCOMPARE(folderChanges(r, SyncAction::CreateFolderRemote), QStringList{u"Pusty 2"_s});
        const SyncReport rb = p.b.sync();
        QCOMPARE(folderChanges(rb, SyncAction::DeleteFolderLocal), QStringList{u"Pusty"_s});
        QCOMPARE(folderChanges(rb, SyncAction::CreateFolderLocal), QStringList{u"Pusty 2"_s});
        QVERIFY(isLocalDir(p.b, u"Pusty 2"_s));
        QVERIFY(!isLocalDir(p.b, u"Pusty"_s));
    }

    void moveAnsweredTooLateLosesNothing()
    {
        // The server moved the folder but the answer never came (network): the next run finds the
        // files under the new name on both sides, unknown, and only records them.
        Pair p(notesIn(u"Projekty"_s, 12) + notesIn(u""_s, 2, 20));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Archiwum"_s));
        p.server.hooks.append([&](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
            if (request.method != "MOVE")
                return std::nullopt;
            p.server.moveNode(u"Notatki/Projekty"_s, u"Notatki/Archiwum"_s);
            return FakeWebDavServer::status(0);
        });
        const SyncReport failed = p.a.sync();
        QVERIFY(failed.remoteError.is(WebDavErrorCode::Network));
        p.server.hooks.clear();
        const SyncReport r = p.a.sync();
        QVERIFY(!r.failed());
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(counts(r), Counts{});
        QVERIFY(r.conflictCopies.isEmpty());
        QVERIFY(p.server.requests("PUT").size() == 14); // the first sync's uploads only
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(p.a.engine->stateOf(u"Archiwum/"_s + noteName(0)));
        QVERIFY(!p.a.engine->stateOf(u"Projekty/"_s + noteName(0)));
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.foldersMovedHere, 1);
        QCOMPARE(p.b.files(), p.a.files());
    }

    void newNameTakenMeanwhileFallsBack()
    {
        Pair p(notesIn(u"Projekty"_s, 12) + notesIn(u""_s, 2, 20));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Archiwum"_s));
        // Another device creates "Archiwum" right before the MOVE: never moved over it — the notes
        // are moved into it one by one instead.
        bool once = true;
        p.server.hooks.append([&](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
            if (request.method == "MOVE" && std::exchange(once, false))
                p.server.putText(u"Notatki/Archiwum/Z telefonu.md"_s, u"obcy\n"_s);
            return std::nullopt;
        });
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(r.foldersMoved, 0);
        QCOMPARE(r.filesMoved, 12);
        QCOMPARE(counts(r), Counts{});
        QCOMPARE(p.server.text(u"Notatki/Archiwum/Z telefonu.md"_s), std::optional<QString>(u"obcy\n"_s));
        QCOMPARE(counts(p.a.sync()), Counts{.down = 1});
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(!p.server.isDir(u"Notatki/Projekty"_s));
    }

    void folderSpelledInNfdOnTheServerIsMoved()
    {
        Pair p;
        const QString dir = nfd(u"Źródła"_s);
        p.server.putText(u"Notatki/"_s + dir + u"/stary ąę.md"_s, u"stary\n"_s);
        p.server.putText(u"Notatki/"_s + dir + u"/"_s + nfd(u"Łąka"_s) + u"/b.md"_s, u"b\n"_s);
        p.server.putText(u"Notatki/inny.md"_s, u"x\n"_s);
        p.a.sync();
        QVERIFY(p.a.vault->rename(u"Źródła"_s, u"Źródła 2026"_s));
        const SyncReport r = p.a.sync();
        QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.value(0).message));
        QCOMPARE(r.foldersMoved, 1);
        QVERIFY(p.server.requests("PUT").isEmpty());
        QVERIFY(!p.server.isDir(u"Notatki/"_s + dir));
        QCOMPARE(p.server.text(u"Notatki/Źródła 2026/stary ąę.md"_s), std::optional<QString>(u"stary\n"_s));
        // The subfolder keeps the server's spelling; a new note goes into it, not into a twin.
        QVERIFY(p.server.isDir(u"Notatki/Źródła 2026/"_s + nfd(u"Łąka"_s)));
        QVERIFY(p.a.write(u"Źródła 2026/Łąka/nowy.md"_s, u"nowy\n"_s));
        const SyncReport again = p.a.sync();
        QVERIFY2(again.errors.isEmpty(), qPrintable(again.errors.value(0).message));
        QCOMPARE(p.server.text(u"Notatki/Źródła 2026/"_s + nfd(u"Łąka"_s) + u"/nowy.md"_s),
                 std::optional<QString>(u"nowy\n"_s));
        QCOMPARE(counts(p.a.sync()), Counts{});
    }

    void renameIntoNewFoldersAndPartialMoves()
    {
        Pair p(notesIn(u"Projekty"_s, 4) + notesIn(u"Dom"_s, 4, 10) + notesIn(u""_s, 2, 20));
        p.settle();
        // A folder moved two levels down into folders that do not exist yet: one MOVE.
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Archiwum/2026/Projekty"_s));
        // Half of another folder moved: files (one MOVE each), not a folder rename.
        QVERIFY(p.a.vault->rename(u"Dom/"_s + noteName(10), u"Ogród/"_s + noteName(10)));
        QVERIFY(p.a.vault->rename(u"Dom/"_s + noteName(11), u"Ogród/"_s + noteName(11)));
        const SyncReport r = p.a.sync();
        QCOMPARE(r.folderMoves, (QList<SyncFolderMove>{{u"Projekty"_s, u"Archiwum/2026/Projekty"_s}}));
        QCOMPARE(r.fileMoves, (QList<SyncFolderMove>{{u"Dom/"_s + noteName(10), u"Ogród/"_s + noteName(10)},
                                                     {u"Dom/"_s + noteName(11), u"Ogród/"_s + noteName(11)}}));
        QCOMPARE(counts(r), Counts{});
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(p.server.isDir(u"Notatki/Dom"_s));
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.localFolderMoves, r.folderMoves);
        QCOMPARE(rb.localFileMoves, r.fileMoves);
        QCOMPARE(p.b.files(), p.a.files());
    }

    void folderChangesReachTheEventLog()
    {
        Pair p(notesIn(u"Projekty"_s, 3));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty"_s, u"Projekty 2"_s));
        QVERIFY(p.a.vault->createFolder(u"Pusty"_s));
        static QStringList lines;
        lines.clear();
        const QtMessageHandler previous =
                qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &message) {
                    if (type != QtDebugMsg && context.category && QByteArrayView(context.category) == "brulion.sync")
                        lines.append(message);
                });
        const SyncReport r = p.a.sync();
        qInstallMessageHandler(previous);
        QCOMPARE(r.foldersMoved, 1);
        QCOMPARE(r.foldersCreated, 1);
        QCOMPARE(r.changeCount(), 2);
        const QString all = lines.join(u'\n');
        QVERIFY2(all.contains(u"folder Projekty renamed here: moved to Projekty 2 on the server"_s), qPrintable(all));
        QVERIFY2(all.contains(u"folders: 1 created, 0 deleted, 1 moved"_s), qPrintable(all));
    }

    // ---- Renames on the server ------------------------------------------------------------

    void serverFolderRenameIsRenamedHere_data() { addServerRows(); }

    void serverFolderRenameIsRenamedHere()
    {
        // Another device renames and moves a folder (one MOVE on the server). This device renames
        // its folder too: no download, no trash, and one Renamed change (source Sync) per file —
        // what the version history, favourites and folds of the notes follow.
        QFETCH(bool, keepsEtags);
        QFETCH(bool, adopted);
        Pair p(notesIn(u"Projekty ąę"_s, 30) + notesIn(u"Projekty ąę/Pod"_s, 5, 40) + notesIn(u""_s, 5, 50),
               serverRowOptions());
        QVERIFY(p.a.vault->createFolder(u"Projekty ąę/Pusty"_s));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Projekty ąę"_s, u"Archiwum/Projekty 2026"_s));
        QCOMPARE(p.a.sync().foldersMoved, 1);
        p.b.changes.clear();
        p.server.clearLog();
        const SyncReport rb = p.b.sync();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QVERIFY(!rb.needsConfirmation);
        QCOMPARE(p.b.files(), p.a.files());
        QVERIFY(!isLocalDir(p.b, u"Projekty ąę"_s));
        QVERIFY(isLocalDir(p.b, u"Archiwum/Projekty 2026/Pusty"_s));
        if (!adopted) {
            QCOMPARE(counts(rb), (Counts{.down = 35, .delL = 35}));
            QCOMPARE(rb.foldersMovedHere, 0);
            QCOMPARE(p.b.trash().size(), 35);
            QVERIFY(syncRenames(p.b).isEmpty());
            QCOMPARE(p.b.sync().changeCount(), 0);
            return;
        }
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.foldersMovedHere, 1);
        QCOMPARE(rb.localFolderMoves, (QList<SyncFolderMove>{{u"Projekty ąę"_s, u"Archiwum/Projekty 2026"_s}}));
        QCOMPARE(folderChanges(rb, SyncAction::MoveLocal), QStringList{u"Archiwum/Projekty 2026"_s});
        QVERIFY(folderChanges(rb, SyncAction::DeleteFolderLocal).isEmpty());
        // Nothing transferred (with new ETags the files are fetched once to compare them).
        if (keepsEtags)
            QVERIFY(p.server.requests("GET").isEmpty());
        QVERIFY(p.server.requests("PUT").isEmpty());
        QVERIFY(p.server.requests("DELETE").isEmpty());
        QVERIFY(p.b.trash().isEmpty());
        // The bookkeeping followed: states and merge bases at the new paths, the folders known.
        const QString moved = u"Archiwum/Projekty 2026/Pod/"_s + noteName(40);
        QVERIFY(p.b.engine->stateOf(moved));
        QVERIFY(p.b.engine->mergeBaseOf(moved));
        QVERIFY(!p.b.engine->stateOf(u"Projekty ąę/Pod/"_s + noteName(40)));
        QVERIFY(p.b.engine->knowsFolder(u"Archiwum/Projekty 2026/Pusty"_s));
        QVERIFY(!p.b.engine->knowsFolder(u"Projekty ąę"_s));
        const QMap<QString, QString> renames = syncRenames(p.b);
        QCOMPARE(renames.size(), 35);
        QCOMPARE(renames.value(moved), u"Projekty ąę/Pod/"_s + noteName(40));
        QCOMPARE(p.b.sync().changeCount(), 0);
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void serverRenameAndEditsOnBothSidesMerge_data()
    {
        QTest::addColumn<bool>("fileIds");
        QTest::newRow("fileid") << true;
        QTest::newRow("ETags only") << false;
    }

    void serverRenameAndEditsOnBothSidesMerge()
    {
        // Renamed on the Nextcloud web page and edited there afterwards; edited here before this
        // sync: renamed here too and merged at the new path with the merge base — no conflict.
        QFETCH(bool, fileIds);
        FakeWebDavServer::Options options;
        options.fileIds = fileIds;
        Pair p(notesIn(u"Projekty"_s, 4) + notesIn(u""_s, 2, 10), options);
        p.settle();
        const QString old = u"Projekty/"_s + noteName(0);
        const QString moved = u"Projekty 2/"_s + noteName(0);
        const QString text = p.a.read(old);
        QVERIFY(p.a.write(old, u"Na laptopie ą\n"_s + text));
        QVERIFY(p.server.moveNode(u"Notatki/Projekty"_s, u"Notatki/Projekty 2"_s));
        p.server.putText(kFolder + u'/' + moved, text + u"Dopisane na telefonie ż\n"_s);
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(r.foldersMovedHere, 1);
        QCOMPARE(counts(r), (Counts{.merged = 1}));
        QVERIFY(r.conflictCopies.isEmpty());
        QCOMPARE(p.a.read(moved), u"Na laptopie ą\n"_s + text + u"Dopisane na telefonie ż\n"_s);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(!isLocalDir(p.a, u"Projekty"_s));
        QVERIFY(p.a.trash().isEmpty());
    }

    void serverRenameTargetTakenHere()
    {
        // This device made a "Projekty 2" of its own meanwhile: the folder is not renamed onto it.
        // The notes are renamed into it one by one (their ids prove them) and nothing is lost.
        FakeWebDavServer::Options options;
        options.fileIds = true;
        Pair p(notesIn(u"Projekty"_s, 3) + notesIn(u""_s, 2, 10), options);
        p.settle();
        QVERIFY(p.server.moveNode(u"Notatki/Projekty"_s, u"Notatki/Projekty 2"_s));
        QVERIFY(p.a.write(u"Projekty 2/Moja notatka ł.md"_s, u"tylko tutaj\n"_s));
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(r.foldersMovedHere, 0);
        QCOMPARE(r.filesMovedHere, 3);
        QCOMPARE(counts(r), (Counts{.up = 1}));
        QCOMPARE(p.a.read(u"Projekty 2/Moja notatka ł.md"_s), u"tylko tutaj\n"_s);
        QVERIFY(p.a.trash().isEmpty());
        QVERIFY(!isLocalDir(p.a, u"Projekty"_s));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void aServerFolderRebuiltFromCopiesIsNotARename_data()
    {
        QTest::addColumn<bool>("fileIds");
        QTest::newRow("fileid") << true;
        QTest::newRow("ETags only") << false;
    }

    void aServerFolderRebuiltFromCopiesIsNotARename()
    {
        // On the server "Projekty" is deleted and the same notes are uploaded anew into "Kopia"
        // (new ids, new ETags): not the same files, nothing is renamed here; the guard rules apply
        // as before (the content arrived under new names: moved, so no question).
        QFETCH(bool, fileIds);
        FakeWebDavServer::Options options;
        options.fileIds = fileIds;
        Pair p(notesIn(u"Projekty"_s, 12) + notesIn(u""_s, 2, 20), options);
        p.settle();
        const QMap<QString, QString> before = p.server.files(u"Notatki/Projekty"_s);
        p.server.remove(u"Notatki/Projekty"_s);
        for (const auto &[name, text] : before.asKeyValueRange())
            p.server.putText(u"Notatki/Kopia/"_s + name, text);
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(r.foldersMovedHere, 0);
        QCOMPARE(r.filesMovedHere, 0);
        QVERIFY(syncRenames(p.a).isEmpty());
        QCOMPARE(counts(r), (Counts{.down = 12, .delL = 12}));
        QCOMPARE(p.a.trash().size(), 12);
        QCOMPARE(p.server.files(kFolder), p.a.files());
    }

    void renameHereRefusedFallsBack()
    {
        // The vault cannot rename the folder (read-only while the run looks at it; on Windows a
        // file another program holds open): the notes come down under the new name and the old
        // ones go to the trash, as before. Nothing is lost.
        BRULION_SKIP_WITHOUT_UNIX_PERMISSIONS();
        FakeWebDavServer::Options options;
        options.fileIds = true;
        Pair p(notesIn(u"Projekty"_s, 4) + notesIn(u""_s, 2, 10), options);
        p.settle();
        const QString dir = p.a.root + u"/Projekty"_s;
        const auto writable = [&dir](bool on) {
            const QFileDevice::Permissions read = QFileDevice::ReadOwner | QFileDevice::ExeOwner;
            QFile::setPermissions(dir, on ? read | QFileDevice::WriteOwner : read);
        };
        writable(false);
        QFile probe(dir + u"/.próba"_s);
        if (probe.open(QIODevice::WriteOnly)) {
            probe.remove();
            writable(true);
            QSKIP("running with permissions that ignore file modes (root)");
        }
        writable(true);
        QVERIFY(p.server.moveNode(u"Notatki/Projekty"_s, u"Notatki/Archiwum/Projekty"_s));
        // Read-only from the listing on (the renames are decided then), writable once notes come
        // down (the old ones are trashed after that).
        p.server.hooks.append([&](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
            if (request.method == "PROPFIND")
                writable(false);
            else if (request.method == "GET")
                writable(true);
            return std::nullopt;
        });
        const SyncReport r = p.a.sync();
        writable(true);
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(r.foldersMovedHere, 0);
        QCOMPARE(r.filesMovedHere, 0);
        QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.value(0).message));
        QCOMPARE(counts(r), (Counts{.down = 4, .delL = 4}));
        QCOMPARE(p.a.trash().size(), 4);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        p.server.hooks.clear();
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void nestedServerRenames_data()
    {
        QTest::addColumn<int>("inA");
        QTest::addColumn<int>("inX");
        QTest::addColumn<QStringList>("moves");
        QTest::newRow("most notes in the folder") << 4 << 2 << QStringList{u"A ą -> B ę"_s, u"B ę/x -> C/x"_s};
        // Most of the notes are below "C" now, but the folder follows its own notes (the
        // subfolder counts as one part): "A ą" is not taken for moved to "C".
        QTest::newRow("most notes in the subfolder") << 2 << 4 << QStringList{u"A ą -> B ę"_s, u"B ę/x -> C/x"_s};
        QTest::newRow("one note beside a big subfolder")
                << 1 << 6 << QStringList{u"A ą -> B ę"_s, u"B ę/x -> C/x"_s};
    }

    void nestedServerRenames()
    {
        // The server renames "A ą" to "B ę" and moves its subfolder "x" to "C/x": both are
        // renamed here as there, whichever folder holds most of the notes — none downloaded, none
        // trashed, and a note not synced yet stays in "A ą" under its new name.
        QFETCH(int, inA);
        QFETCH(int, inX);
        QFETCH(QStringList, moves);
        Pair p(notesIn(u"A ą"_s, inA) + notesIn(u"A ą/x"_s, inX, 10) + notesIn(u""_s, 2, 20));
        p.settle();
        QVERIFY(p.a.write(u"A ą/nowa źdźbło.md"_s, u"jeszcze nie wysłana\n"_s));
        QVERIFY(p.server.moveNode(u"Notatki/A ą"_s, u"Notatki/B ę"_s));
        QVERIFY(p.server.moveNode(u"Notatki/B ę/x"_s, u"Notatki/C/x"_s));
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(counts(r), Counts{.up = 1});
        QStringList done;
        for (const SyncFolderMove &move : r.localFolderMoves)
            done.append(move.from + u" -> "_s + move.to);
        QCOMPARE(done, moves);
        QCOMPARE(r.foldersMovedHere, 2);
        QCOMPARE(r.filesMovedHere, 0);
        QVERIFY(p.a.trash().isEmpty());
        QVERIFY(!isLocalDir(p.a, u"A ą"_s));
        QCOMPARE(p.server.text(u"Notatki/B ę/nowa źdźbło.md"_s), std::optional<QString>(u"jeszcze nie wysłana\n"_s));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void nestedLocalRenames()
    {
        // The same done here: "A ą" renamed to "B ę", then its big subfolder moved to "C/x". The
        // folder is moved on the server as "B ę" (the subfolder's notes follow one by one), so what
        // the phone put into "A ą" meanwhile lands in "B ę" too.
        Pair p(notesIn(u"A ą"_s, 1) + notesIn(u"A ą/x"_s, 6, 10) + notesIn(u""_s, 2, 20));
        p.settle();
        QVERIFY(p.b.write(u"A ą/z telefonu ż.md"_s, u"z telefonu\n"_s));
        QCOMPARE(counts(p.b.sync()), Counts{.up = 1});
        QVERIFY(p.a.vault->rename(u"A ą"_s, u"B ę"_s));
        QVERIFY(p.a.vault->rename(u"B ę/x"_s, u"C/x"_s));
        p.server.clearLog();
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(r.folderMoves, (QList<SyncFolderMove>{{u"A ą"_s, u"B ę"_s}}));
        QCOMPARE(r.filesMoved, 6);
        QCOMPARE(counts(r), Counts{.down = 1});
        QVERIFY(p.server.requests("PUT").isEmpty());
        QCOMPARE(p.a.read(u"B ę/z telefonu ż.md"_s), u"z telefonu\n"_s);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QCOMPARE(p.a.sync().changeCount(), 0);
    }

    void remoteIdsAreBackfilled()
    {
        // Uploads learn no id (PUT answers carry none here), and states of older versions have
        // none: the next run records them from its listing, with no other request.
        FakeWebDavServer::Options options;
        options.fileIds = true;
        Pair p(notesIn(u"Źródła"_s, 3), options);
        p.a.sync();
        const QStringList paths = p.a.files().keys();
        for (const QString &path : paths)
            QVERIFY(p.a.engine->stateOf(path)->remoteId.isEmpty());
        p.server.clearLog();
        QCOMPARE(p.a.sync().changeCount(), 0);
        QCOMPARE(p.server.requests("PROPFIND").size(), p.server.logSize());
        for (const QString &path : paths) {
            const QString id = p.server.fileId(kFolder + u'/' + path);
            QVERIFY(!id.isEmpty());
            QCOMPARE(p.a.engine->stateOf(path)->remoteId, id);
        }
        // A downloaded file has it at once.
        p.b.sync();
        QCOMPARE(p.b.engine->stateOf(paths.first())->remoteId, p.server.fileId(kFolder + u'/' + paths.first()));
    }

    // ---- Pages renamed ----------------------------------------------------------------------

    void serverPageRenameIsRenamedHere_data() { addServerRows(); }

    void serverPageRenameIsRenamedHere()
    {
        // A page renamed and moved into a folder on the Nextcloud web page: renamed here too.
        QFETCH(bool, adopted);
        Pair p(notesIn(u""_s, 4) + QMap<QString, QByteArray>{{u"Stara nazwa ąę.md"_s, u8(u"treść ąę\n"_s)}},
               serverRowOptions());
        p.settle();
        QVERIFY(p.server.moveNode(u"Notatki/Stara nazwa ąę.md"_s, u"Notatki/Projekty/Nowa nazwa żółw.md"_s));
        p.b.changes.clear();
        const SyncReport rb = p.b.sync();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QCOMPARE(p.server.files(kFolder), p.b.files());
        QCOMPARE(p.b.read(u"Projekty/Nowa nazwa żółw.md"_s), u"treść ąę\n"_s);
        if (!adopted) {
            QCOMPARE(counts(rb), (Counts{.down = 1, .delL = 1}));
            QCOMPARE(p.b.trash().size(), 1);
            return;
        }
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(rb.filesMovedHere, 1);
        QCOMPARE(rb.localFileMoves,
                 (QList<SyncFolderMove>{{u"Stara nazwa ąę.md"_s, u"Projekty/Nowa nazwa żółw.md"_s}}));
        QCOMPARE(syncRenames(p.b),
                 (QMap<QString, QString>{{u"Projekty/Nowa nazwa żółw.md"_s, u"Stara nazwa ąę.md"_s}}));
        QVERIFY(p.b.trash().isEmpty());
        QVERIFY(p.b.engine->mergeBaseOf(u"Projekty/Nowa nazwa żółw.md"_s));
        QCOMPARE(p.b.sync().changeCount(), 0);
    }

    void serverPageRenameWhileEditedHere()
    {
        FakeWebDavServer::Options options;
        options.fileIds = true;
        Pair p(notesIn(u""_s, 4), options);
        p.settle();
        const QString old = noteName(1);
        const QString edited = p.a.read(old) + u"Dopisane na laptopie — ćma\n"_s;
        QVERIFY(p.a.write(old, edited));
        QVERIFY(p.server.moveNode(kFolder + u'/' + old, u"Notatki/Przemianowana ń.md"_s));
        const SyncReport r = p.a.sync();
        QCOMPARE(r.filesMovedHere, 1);
        QCOMPARE(counts(r), (Counts{.up = 1}));
        QCOMPARE(p.server.text(u"Notatki/Przemianowana ń.md"_s), std::optional<QString>(edited));
        QVERIFY(!p.a.vault->exists(old));
        QCOMPARE(p.server.files(kFolder), p.a.files());
    }

    void pageRenamedHereIsMovedOnTheServer()
    {
        // One MOVE (no upload, no deletion); an edit the server got meanwhile comes down into the
        // renamed page.
        Pair p(notesIn(u""_s, 4) + notesIn(u"Dom"_s, 2, 10));
        p.settle();
        const QString old = noteName(0);
        const QString newer = p.b.read(old) + u"Z telefonu — gęś\n"_s;
        QVERIFY(p.b.write(old, newer));
        QCOMPARE(counts(p.b.sync()), Counts{.up = 1});
        QVERIFY(p.a.vault->rename(old, u"Dom/Nowe miejsce ź.md"_s));
        p.server.clearLog();
        const SyncReport r = p.a.sync();
        QCOMPARE(r.fileMoves, (QList<SyncFolderMove>{{old, u"Dom/Nowe miejsce ź.md"_s}}));
        QCOMPARE(folderChanges(r, SyncAction::MoveFileRemote), QStringList{u"Dom/Nowe miejsce ź.md"_s});
        QCOMPARE(counts(r), Counts{.down = 1});
        QCOMPARE(requestPaths(p.server, "MOVE"), QStringList{old});
        QVERIFY(p.server.requests("PUT").isEmpty());
        QVERIFY(p.server.requests("DELETE").isEmpty());
        QCOMPARE(p.a.read(u"Dom/Nowe miejsce ź.md"_s), newer);
        QCOMPARE(p.server.files(kFolder), p.a.files());
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.filesMovedHere, 1);
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(p.b.files(), p.a.files());
    }

    void lookAlikePagesAreNotMoved()
    {
        // Pages with the same content (copies, one template) tell nothing apart unless the name
        // does: uploaded and deleted as before, nothing lost.
        // Renamed by another program (a file manager): the vault saw no rename, only content.
        const QByteArray same = u8(u"# Szablon\n- [ ] zadanie ą\n"_s);
        Pair p({{u"Szablon 1.md"_s, same}, {u"Szablon 2.md"_s, same}, {u"Plan.md"_s, "plan\n"},
                {u"inna.md"_s, "x\n"}, {u"Dom/x.md"_s, "y\n"}});
        p.settle();
        const auto renameBehind = [&p](const QString &from, const QString &to) {
            return QFile::rename(p.a.root + u'/' + from, p.a.root + u'/' + to);
        };
        QVERIFY(renameBehind(u"Szablon 1.md"_s, u"Nowy A.md"_s));
        QVERIFY(renameBehind(u"Szablon 2.md"_s, u"Nowy B.md"_s));
        // Two copies of one page, the page itself gone.
        QVERIFY(writeFileBehind(p.a.root, u"Plan kopia.md"_s, "plan\n"));
        QVERIFY(renameBehind(u"Plan.md"_s, u"Plan 2.md"_s));
        p.a.vault->rescan();
        QVERIFY(p.a.engine->renamedFrom(u"Plan 2.md"_s).isEmpty());
        const SyncReport r = p.a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(r.filesMoved, 0);
        QCOMPARE(counts(r), (Counts{.up = 4, .delR = 3}));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        p.b.sync();
        QCOMPARE(p.b.files(), p.a.files());
    }

    void renameJournal()
    {
        // What the vault renamed since the last run, followed to the synced name; a rename back,
        // a deletion and files never synced leave nothing.
        Pair p(notesIn(u"Dom"_s, 2) + notesIn(u""_s, 2, 10));
        p.a.sync();
        const QString synced = u"Dom/"_s + noteName(0);
        QVERIFY(p.a.vault->rename(synced, u"Plan ą.md"_s));
        QCOMPARE(p.a.engine->renamedFrom(u"Plan ą.md"_s), synced);
        QVERIFY(p.a.vault->rename(u"Plan ą.md"_s, u"Inne/Plan ę.md"_s));
        QVERIFY(p.a.engine->renamedFrom(u"Plan ą.md"_s).isEmpty());
        QCOMPARE(p.a.engine->renamedFrom(u"Inne/Plan ę.md"_s), synced);
        QVERIFY(p.a.vault->rename(u"Inne/Plan ę.md"_s, synced));
        QVERIFY(p.a.engine->renamedFrom(synced).isEmpty());
        // A folder file by file.
        QVERIFY(p.a.vault->rename(u"Dom"_s, u"Mieszkanie ż"_s));
        QCOMPARE(p.a.engine->renamedFrom(u"Mieszkanie ż/"_s + noteName(1)), u"Dom/"_s + noteName(1));
        QVERIFY(p.a.remove(u"Mieszkanie ż/"_s + noteName(1)));
        QVERIFY(p.a.engine->renamedFrom(u"Mieszkanie ż/"_s + noteName(1)).isEmpty());
        QVERIFY(p.a.write(u"nowa.md"_s, u"nowa\n"_s));
        QVERIFY(p.a.vault->rename(u"nowa.md"_s, u"nowsza.md"_s));
        QVERIFY(p.a.engine->renamedFrom(u"nowsza.md"_s).isEmpty());
        // Synced: done.
        QVERIFY(!p.a.sync().failed());
        QVERIFY(p.a.engine->renamedFrom(u"Mieszkanie ż/"_s + noteName(0)).isEmpty());
        QVERIFY(p.a.store->keys(SyncEngine::renamedKey(QString())).isEmpty());
        // A reset forgets it.
        QVERIFY(p.a.vault->rename(u"Mieszkanie ż/"_s + noteName(0), u"x.md"_s));
        QVERIFY(!p.a.engine->renamedFrom(u"x.md"_s).isEmpty());
        p.a.engine->resetState();
        QVERIFY(p.a.engine->renamedFrom(u"x.md"_s).isEmpty());
    }

    void pageRenamedAndEditedIsMovedOnTheServer_data()
    {
        QTest::addColumn<bool>("fileIds");
        QTest::addColumn<bool>("keepsEtags");
        QTest::addColumn<bool>("folder");
        QTest::newRow("page, Nextcloud") << true << true << false;
        QTest::newRow("page, MOVE gives new ETags") << true << false << false;
        QTest::newRow("folder with every page edited") << true << true << true;
    }

    void pageRenamedAndEditedIsMovedOnTheServer()
    {
        // Renamed in Brulion and typed on before the sync ran (or its links rewritten): the rename
        // journal proves the rename, so one MOVE and then the edit; the other device renames its
        // copy (the server kept the file id) and downloads the edit — nothing trashed there.
        QFETCH(bool, fileIds);
        QFETCH(bool, keepsEtags);
        QFETCH(bool, folder);
        FakeWebDavServer::Options options;
        options.fileIds = fileIds;
        options.moveKeepsEtags = keepsEtags;
        Pair p(notesIn(u""_s, 4) + notesIn(u"Dom"_s, 2, 10), options);
        p.settle();
        QStringList renamed; // new paths
        if (folder) {
            QVERIFY(p.a.vault->rename(u"Dom"_s, u"Mieszkanie ż"_s));
            for (int i = 10; i < 12; ++i)
                renamed.append(u"Mieszkanie ż/"_s + noteName(i));
        } else {
            QVERIFY(p.a.vault->rename(noteName(0), u"Dom/Plan 2026 ź.md"_s));
            renamed.append(u"Dom/Plan 2026 ź.md"_s);
        }
        for (const QString &path : std::as_const(renamed))
            QVERIFY(p.a.write(path, p.a.read(path) + u"Dopisane po zmianie nazwy — ćma\n"_s));
        p.server.clearLog();
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(p.server.requests("MOVE").size(), 1);
        QVERIFY(p.server.requests("DELETE").isEmpty());
        QCOMPARE(r.foldersMoved + r.filesMoved, 1);
        QCOMPARE(counts(r), (Counts{.up = int(renamed.size())}));
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(p.a.store->keys(SyncEngine::renamedKey(QString())).isEmpty());
        p.b.changes.clear();
        const SyncReport rb = p.b.sync();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QCOMPARE(rb.foldersMovedHere + rb.filesMovedHere, 1);
        QCOMPARE(counts(rb), (Counts{.down = int(renamed.size())}));
        QVERIFY(p.b.trash().isEmpty());
        QCOMPARE(p.b.files(), p.a.files());
        QCOMPARE(p.a.sync().changeCount(), 0);
        QCOMPARE(p.b.sync().changeCount(), 0);
    }

    void repeatedEtagsProveNoPageRename()
    {
        // A server whose ETags repeat (mtime and size on coarse storage), no file ids: two synced
        // pages of one size share an ETag, one is renamed and the other deleted there. Nothing
        // here is renamed on such evidence: the renamed page comes down, both old ones go to the
        // trash — the deleted page's text never ends up under the renamed one's name.
        FakeWebDavServer::Options options;
        options.moveKeepsEtags = true;
        Pair p({{u"notatka 1.md"_s, "pierwsza ą\n"}, {u"notatka 2.md"_s, "druga ęę\n"}, {u"inna.md"_s, "x\n"},
                {u"Dom/y.md"_s, "y\n"}},
               options);
        p.a.sync();
        p.server.setEtag(u"Notatki/notatka 1.md"_s, u"\"5f1-12\""_s);
        p.server.setEtag(u"Notatki/notatka 2.md"_s, u"\"5f1-12\""_s);
        p.b.sync();
        QCOMPARE(p.b.engine->stateOf(u"notatka 1.md"_s)->remoteEtag, p.b.engine->stateOf(u"notatka 2.md"_s)->remoteEtag);
        QVERIFY(p.server.moveNode(u"Notatki/notatka 2.md"_s, u"Notatki/nowa 2.md"_s));
        p.server.remove(u"Notatki/notatka 1.md"_s);
        const SyncReport rb = p.b.sync();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QCOMPARE(rb.filesMovedHere, 0);
        QCOMPARE(counts(rb), (Counts{.down = 1, .delL = 2}));
        QCOMPARE(p.b.read(u"nowa 2.md"_s), u"druga ęę\n"_s);
        QCOMPARE(p.server.files(kFolder), p.b.files());
    }

    void manyPagesMovedAtOnce()
    {
        // Every root page moved into one folder (select all, Move — on the server, then here):
        // one rename here per page and one MOVE per page, without walking every file each time.
        constexpr int count = 1500;
        FakeWebDavServer::Options options;
        options.fileIds = true;
        Pair p(notesIn(u""_s, count) + notesIn(u"Dom"_s, 2, 2000), options);
        p.settle();
        for (int i = 0; i < count; ++i)
            QVERIFY(p.server.moveNode(kFolder + u'/' + noteName(i), u"Notatki/Archiwum ą/"_s + noteName(i)));
        QElapsedTimer timer;
        timer.start();
        const SyncReport r = p.a.sync();
        const qint64 adopted = timer.elapsed();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(r.filesMovedHere, count);
        QCOMPARE(counts(r), Counts{});
        QCOMPARE(p.server.files(kFolder), p.a.files());
        for (int i = 0; i < count; ++i)
            QVERIFY(p.b.vault->rename(noteName(i), u"Archiwum ą/"_s + noteName(i)));
        p.server.clearLog();
        timer.restart();
        const SyncReport rb = p.b.sync();
        const qint64 moved = timer.elapsed();
        QVERIFY2(!rb.failed(), qPrintable(rb.failureMessage()));
        QCOMPARE(rb.filesMoved, 0); // the server has them there already: nothing to move
        QCOMPARE(counts(rb), Counts{});
        QCOMPARE(p.b.files(), p.a.files());
        // Now the other way: moved here, one MOVE each.
        for (int i = 0; i < count; ++i)
            QVERIFY(p.a.vault->rename(u"Archiwum ą/"_s + noteName(i), noteName(i)));
        timer.restart();
        const SyncReport back = p.a.sync();
        const qint64 sent = timer.elapsed();
        QVERIFY2(!back.failed(), qPrintable(back.failureMessage()));
        QCOMPARE(back.filesMoved, count);
        QCOMPARE(counts(back), Counts{});
        QCOMPARE(p.server.files(kFolder), p.a.files());
        qInfo("%d pages: renamed here in %lld ms, matched in %lld ms, moved on the server in %lld ms", count,
              adopted, moved, sent);
    }

    // ---- An older copy on a side that lost everything ---------------------------------------

    void anOlderCopyOnAnEmptiedSideAsks_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<bool>("onServer");
        QTest::newRow("vault, notes in a folder") << u"Wszystko"_s << false;
        QTest::newRow("vault, notes at the root") << QString() << false;
        QTest::newRow("server, notes in a folder") << u"Wszystko"_s << true;
        QTest::newRow("server, notes at the root") << QString() << true;
    }

    void anOlderCopyOnAnEmptiedSideAsks()
    {
        // One side lost every note and an older backup was put back below "Kopia/": most notes
        // are found unchanged (moved), so the side passes the empty-side guard — but the notes
        // edited since the backup must neither be overwritten with their old text (as if the
        // folder had been renamed and edited) nor deleted without the user's say, however few.
        QFETCH(QString, folder);
        QFETCH(bool, onServer);
        const auto at = [&](int i) { return folder.isEmpty() ? noteName(i) : folder + u'/' + noteName(i); };
        Pair p(notesIn(folder, 20));
        p.settle();
        QMap<QString, QString> backup;
        for (int i = 0; i < 20; ++i)
            backup.insert(at(i), p.a.read(at(i)));
        QStringList newer;
        for (int i = 0; i < 6; ++i) {
            const QString text = p.a.read(at(i)) + u"Nowsza wersja — zażółć\n"_s;
            QVERIFY(p.a.write(at(i), text));
            newer.append(text);
        }
        QCOMPARE(counts(p.a.sync()), Counts{.up = 6});
        QCOMPARE(counts(p.b.sync()), Counts{.down = 6});
        if (onServer) {
            for (int i = 0; i < 20; ++i)
                p.server.remove(kFolder + u'/' + at(i));
            if (!folder.isEmpty())
                p.server.remove(kFolder + u'/' + folder);
            for (const auto &[path, text] : backup.asKeyValueRange())
                p.server.putText(kFolder + u"/Kopia/"_s + path, text);
        } else {
            for (int i = 0; i < 20; ++i)
                QVERIFY(QFile::remove(p.a.root + u'/' + at(i)));
            if (!folder.isEmpty())
                QVERIFY(QDir(p.a.root + u'/' + folder).removeRecursively());
            for (const auto &[path, text] : backup.asKeyValueRange())
                QVERIFY(writeFileBehind(p.a.root, u"Kopia/"_s + path, text.toUtf8()));
            p.a.vault->rescan();
        }
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QVERIFY(r.needsConfirmation);
        QCOMPARE(r.foldersMoved, 0);
        QCOMPARE(r.deletedLocal + r.deletedRemote, 0);
        // The side that kept the newer notes still has them where they were, and the older copy
        // has arrived next to them; the other device keeps them too.
        const auto kept = [&](const QString &path) {
            return onServer ? p.a.read(path) : p.server.text(kFolder + u'/' + path).value_or(QString());
        };
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(kept(at(i)), newer.at(i));
            QCOMPARE(kept(u"Kopia/"_s + at(i)), backup.value(at(i)));
        }
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.deletedLocal + rb.deletedRemote, 0);
        QCOMPARE(rb.needsConfirmation, onServer);
        for (int i = 0; i < 6; ++i)
            QCOMPARE(p.b.read(at(i)), newer.at(i));
        // The question stays until the user decides.
        QVERIFY(p.a.sync().needsConfirmation);
    }

    void aRenamedNotebookFolderWithANoteDeletedAsks()
    {
        // The whole notebook in one folder, renamed and one note deleted before the sync: the
        // folder is moved, the deletion is not proven to be a move — it waits for the user.
        Pair p(notesIn(u"Wszystko"_s, 12));
        p.settle();
        QVERIFY(p.a.vault->rename(u"Wszystko"_s, u"Wszystko 2026"_s));
        QVERIFY(p.a.remove(u"Wszystko 2026/"_s + noteName(3)));
        const SyncReport r = p.a.sync();
        QVERIFY2(!r.failed(), qPrintable(r.failureMessage()));
        QCOMPARE(r.foldersMoved, 1);
        QVERIFY(r.needsConfirmation);
        QCOMPARE(r.pendingDeletions,
                 (QList<SyncChange>{{u"Wszystko 2026/"_s + noteName(3), SyncAction::DeleteRemote}}));
        SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        QCOMPARE(counts(p.a.sync(confirm)), Counts{.delR = 1});
        QCOMPARE(p.server.files(kFolder), p.a.files());
    }

    // ---- Folder deletions on the server ----------------------------------------------------

    void serverFolderWithAnEntryTheClientCannotNameIsKept()
    {
        // A name the client cannot map to a path of its own ("\" becomes a separator and the
        // entry collapses onto the folder): the folder is not empty, whatever the listing makes of
        // it, and WebDAV would delete it with everything in it.
        Pair p(notesIn(u""_s, 3));
        QVERIFY(p.a.vault->createFolder(u"Pusty"_s));
        p.settle();
        QVERIFY(p.a.engine->knowsFolder(u"Pusty"_s));
        p.server.putText(u"Notatki/Pusty/\\"_s, u"nie usuwaj\n"_s);
        QVERIFY(p.a.vault->remove(u"Pusty"_s));
        const SyncReport r = p.a.sync();
        QCOMPARE(r.foldersDeleted, 0);
        QCOMPARE(p.server.text(u"Notatki/Pusty/\\"_s), std::optional<QString>(u"nie usuwaj\n"_s));
        QVERIFY(p.server.requests("DELETE").isEmpty());
        // The client says so instead of calling the folder empty.
        const ListResult listed = await<ListResult>([&](auto done) { p.a.client->listFolder(u"Pusty"_s, done); });
        QVERIFY(listed.error.is(WebDavErrorCode::Parse));
        QVERIFY(listed.entries.isEmpty());
    }

    void firstRunAfterAnUpgradeDeletesNoFolder()
    {
        // The store of a version without folder records: the folders one side lacks (leftovers of
        // file deletions the old version did not follow with the folder) are made again, none is
        // deleted, and the other device has nothing to do but take them.
        Pair p(notesIn(u"Projekty"_s, 3) + notesIn(u""_s, 2, 10));
        QVERIFY(p.a.vault->createFolder(u"Pusty wspólny"_s));
        p.settle();
        p.server.mkdir(u"Notatki/Stary na serwerze"_s);
        QVERIFY(p.a.vault->createFolder(u"Stary tutaj/Głębiej"_s));
        QVERIFY(p.a.vault->remove(u"Pusty wspólny"_s));
        p.a.store->removePrefix(u"sync/folder/"_s);
        p.b.store->removePrefix(u"sync/folder/"_s);
        const SyncReport r = p.a.sync();
        QVERIFY(!r.failed());
        QCOMPARE(r.foldersDeleted, 0);
        QCOMPARE(counts(r), Counts{});
        QVERIFY(p.server.isDir(u"Notatki/Stary na serwerze"_s));
        QVERIFY(p.server.isDir(u"Notatki/Stary tutaj/Głębiej"_s));
        QVERIFY(p.server.isDir(u"Notatki/Pusty wspólny"_s));
        QVERIFY(isLocalDir(p.a, u"Stary na serwerze"_s));
        QVERIFY(isLocalDir(p.a, u"Pusty wspólny"_s));
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.foldersDeleted, 0);
        QCOMPARE(counts(rb), Counts{});
        QVERIFY(isLocalDir(p.b, u"Stary tutaj/Głębiej"_s));
    }
};

QTEST_GUILESS_MAIN(TestSyncFolders)
#include "tst_sync_folders.moc"

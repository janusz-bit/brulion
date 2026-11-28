// The sync with a folder of this device: FolderDavTransport answering WebDAV over a FolderStorage
// (statuses, conditions, the worker thread), WebDavClient over it, and SyncEngine keeping a vault
// in sync with a LocalFolderStorage — the same engine as Nextcloud's, with a folder that cannot be
// read never taken for an empty one.
#include "synctest.h"

#include "sync/folderstorage.h"
#include "sync/foldertransport.h"

#include <QBuffer>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <atomic>

using namespace synctest;

namespace {

const QString kId = u"5f2a9c0d1e3b4a76"_s;

/// A storage around LocalFolderStorage whose operations can be made to fail, or to say they cannot
/// move (Android's document providers), and which notes the threads it was called on.
class TestStorage final : public FolderStorage {
public:
    explicit TestStorage(const QString &root) : m_inner(root) {}

    QString describe() const override { return m_inner.describe(); }
    FolderStatResult stat(const QString &path) override
    {
        note();
        if (statError.isError())
            return {std::nullopt, statError};
        return m_inner.stat(path);
    }
    FolderListResult list(const QString &folder) override
    {
        note();
        if (listError.isError())
            return {{}, listError};
        return m_inner.list(folder);
    }
    FolderReadResult read(const QString &path, qint64 maxBytes) override
    {
        note();
        FolderReadResult read = m_inner.read(path, maxBytes);
        largestWhole = std::max(largestWhole.load(), qint64(read.data.size()));
        return read;
    }
    FolderStatResult write(const QString &path, const QByteArray &data) override
    {
        note();
        largestWhole = std::max(largestWhole.load(), qint64(data.size()));
        if (writeError.isError())
            return {std::nullopt, writeError};
        return m_inner.write(path, data);
    }
    bool streams() const override { return streaming; }
    FolderError readInto(const QString &path, qint64 maxBytes, const PieceSink &sink) override
    {
        if (!streaming)
            return FolderStorage::readInto(path, maxBytes, sink);
        note();
        return m_inner.readInto(path, maxBytes, [this, &sink](QByteArrayView piece) {
            largestPiece = std::max(largestPiece.load(), qint64(piece.size()));
            ++piecesRead;
            return sink(piece);
        });
    }
    FolderStatResult writeFrom(const QString &path, const PieceSource &source) override
    {
        if (!streaming)
            return FolderStorage::writeFrom(path, source);
        note();
        if (writeError.isError())
            return {std::nullopt, writeError};
        return m_inner.writeFrom(path, [this, &source]() {
            std::optional<QByteArray> piece = source();
            if (piece)
                largestPiece = std::max(largestPiece.load(), qint64(piece->size()));
            return piece;
        });
    }
    FolderError makeFolder(const QString &path) override
    {
        note();
        if (path.isEmpty() && !canCreateRoot)
            return FolderError::make(FolderErrorCode::NotAllowed, u"the picked folder itself cannot be made"_s);
        return m_inner.makeFolder(path);
    }
    FolderError remove(const QString &path) override
    {
        note();
        return m_inner.remove(path);
    }
    FolderError move(const QString &from, const QString &to, bool overwrite) override
    {
        note();
        ++moves;
        if (!canMove)
            return FolderStorage::move(from, to, overwrite);
        return m_inner.move(from, to, overwrite);
    }

    FolderError statError;
    FolderError listError;
    FolderError writeError;
    bool canCreateRoot = true;
    bool canMove = true;
    bool streaming = true; ///< false: as a storage that reads and writes files whole
    std::atomic<qint64> largestWhole{0}; ///< the largest file read or written whole
    std::atomic<qint64> largestPiece{0}; ///< the largest piece read or written
    std::atomic<int> piecesRead{0};
    int moves = 0;
    std::atomic<int> calls{0};
    std::atomic<int> offThread{0};
    QThread *owner = QThread::currentThread();

private:
    void note()
    {
        ++calls;
        if (QThread::currentThread() != owner)
            ++offThread;
    }
    LocalFolderStorage m_inner;
};

HttpRequest request(const QByteArray &method, const QString &path, QList<std::pair<QByteArray, QByteArray>> headers = {},
                    const QByteArray &body = {})
{
    HttpRequest r;
    r.method = method;
    r.url = QUrl(joinUrl(deviceFolderUrl(kId), path, path.endsWith(u'/')), QUrl::TolerantMode);
    r.headers = std::move(headers);
    r.body = body;
    return r;
}

/// One request answered synchronously (FolderDavTransport::answer).
HttpResponse ask(FolderStorage &storage, const HttpRequest &r)
{
    return FolderDavTransport::answer(storage, urlPathname(deviceFolderUrl(kId)), r);
}

QStringList hrefNames(const HttpResponse &response)
{
    QStringList out;
    const MultistatusResult parsed = parseMultistatus(response.body);
    for (const DavResource &res : parsed.resources) {
        const std::optional<QString> rel = relativeTo(urlPathname(res.href), urlPathname(deviceFolderUrl(kId)));
        out.append((rel ? *rel : u"?"_s) + (res.isCollection ? u"/"_s : QString()));
    }
    out.sort();
    return out;
}

/// A vault and a folder of the device, synced by an engine through FolderDavTransport.
struct FolderDevice {
    explicit FolderDevice(const QMap<QString, QByteArray> &vaultFiles = {},
                          const QMap<QString, QByteArray> &folderFiles = {}, bool synchronous = false)
        : vaultRoot(tmp.path() + u"/vault"_s)
        , folderRoot(tmp.path() + u"/folder"_s)
    {
        for (auto it = vaultFiles.cbegin(); it != vaultFiles.cend(); ++it)
            writeFileBehind(vaultRoot, it.key(), it.value());
        for (auto it = folderFiles.cbegin(); it != folderFiles.cend(); ++it)
            writeFileBehind(folderRoot, it.key(), it.value(), kFolderTime);
        QDir().mkpath(vaultRoot);
        QDir().mkpath(folderRoot);
        storage = std::make_shared<TestStorage>(folderRoot);
        vault = std::make_unique<Vault>(vaultRoot, QString());
        vault->setClock(clock.fn());
        vault->open();
        store = std::make_unique<LocalStore>(QString());
        transport = std::make_unique<FolderDavTransport>(storage, deviceFolderUrl(kId));
        transport->setSynchronous(synchronous);
        client = std::make_unique<WebDavClient>(deviceFolderUrl(kId), u"device"_s, QString(), transport.get());
        client->setListConcurrency(1);
        engine = std::make_unique<SyncEngine>(vault.get(), client.get(), store.get(), u"Telefon"_s);
        engine->setClock(clock.fn());
        engine->setConcurrency(1);
    }

    SyncReport sync(const SyncRunOptions &options = {}) { return Device::runSync(*engine, options); }

    /// A file as Syncthing would write it into the folder (its own modification time).
    void writeInFolder(const QString &rel, const QString &text)
    {
        folderTime += 60'000;
        QVERIFY(writeFileBehind(folderRoot, rel, text.toUtf8(), folderTime));
    }
    QString folderText(const QString &rel) const
    {
        QFile f(folderRoot + u'/' + rel);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    QStringList folderFiles() const { return filesBelow(folderRoot); }
    QStringList vaultFiles() const
    {
        QStringList out;
        for (const QString &rel : filesBelow(vaultRoot)) {
            if (!rel.startsWith(u".trash/"_s))
                out.append(rel);
        }
        return out;
    }

    static constexpr qint64 kFolderTime = 1'790'000'000'000; // 2026-09-21
    qint64 folderTime = kFolderTime;
    QTemporaryDir tmp;
    QString vaultRoot;
    QString folderRoot;
    TestClock clock;
    std::shared_ptr<TestStorage> storage;
    std::unique_ptr<Vault> vault;
    std::unique_ptr<LocalStore> store;
    std::unique_ptr<FolderDavTransport> transport;
    std::unique_ptr<WebDavClient> client;
    std::unique_ptr<SyncEngine> engine;
};

constexpr qint64 MiB = 1024 * 1024;

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// `size` bytes that differ from `seed` to `seed`.
QByteArray pattern(qint64 size, int seed = 0)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qint64 i = 0; i < size; ++i)
        out[i] = char((i * 7919 + 13 + seed * 31) & 0xff);
    return out;
}

/// An upload's body that cannot be read past `failAt` bytes (a file that went away half-way).
class FailingDevice : public QIODevice {
public:
    FailingDevice(qint64 size, qint64 failAt) : m_size(size), m_failAt(failAt) { open(QIODevice::ReadOnly); }
    qint64 size() const override { return m_size; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (pos() >= m_failAt)
            return -1;
        const qint64 n = std::min(maxSize, m_failAt - pos());
        std::fill(data, data + n, 'z');
        return n;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    qint64 m_size;
    qint64 m_failAt;
};

} // namespace

class TestSyncFolder : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- addresses -----------------------------------------------------------------------------
    void urls()
    {
        QCOMPARE(deviceFolderUrl(u"abc"_s), u"brulion-folder://device/abc"_s);
        QVERIFY(isDeviceFolderUrl(u"brulion-folder://device/abc"_s));
        QVERIFY(isDeviceFolderUrl(u"BRULION-FOLDER://device/abc"_s));
        QVERIFY(!isDeviceFolderUrl(u"https://chmura.example.pl/remote.php/dav/files/jan"_s));
        const QString uri = u"content://com.android.externalstorage.documents/tree/primary%3ABrulion"_s;
        QCOMPARE(deviceFolderId(uri).size(), 16);
        QCOMPARE(deviceFolderId(uri), deviceFolderId(uri));
        QVERIFY(deviceFolderId(uri) != deviceFolderId(uri + u"2"_s));
    }

    void etags()
    {
        QCOMPARE(FolderDavTransport::etagOf(FolderEntry{u"a.md"_s, false, 10, 0x1f}), u"1f-a"_s);
        QCOMPARE(FolderDavTransport::etagOf(FolderEntry{u"Dziennik"_s, true, 0, 0x1f}), u"d1f"_s);
        // A change of size or time is a new version.
        QVERIFY(FolderDavTransport::etagOf({u"a"_s, false, 10, 5}) != FolderDavTransport::etagOf({u"a"_s, false, 11, 5}));
        QVERIFY(FolderDavTransport::etagOf({u"a"_s, false, 10, 5}) != FolderDavTransport::etagOf({u"a"_s, false, 10, 6}));
    }

    // --- the transport alone -------------------------------------------------------------------
    void propfind()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"Zażółć gęślą jaźń.md"_s, "# Pchnąć\n"));
        QVERIFY(writeFileBehind(tmp.path(), u"Dziennik/2026-11-22.md"_s, "- [ ] łódź\n"));
        QVERIFY(QDir(tmp.path()).mkpath(u"Pusty folder"_s));
        LocalFolderStorage storage(tmp.path());

        const HttpResponse root = ask(storage, request("PROPFIND", QString(), {{"Depth", "1"}}));
        QCOMPARE(root.status, 207);
        QVERIFY(parseMultistatus(root.body).ok());
        QCOMPARE(hrefNames(root),
                 (QStringList{u"/"_s, u"Dziennik/"_s, u"Pusty folder/"_s, u"Zażółć gęślą jaźń.md"_s}));

        const HttpResponse folder = ask(storage, request("PROPFIND", u"Dziennik/"_s, {{"Depth", "1"}}));
        QCOMPARE(folder.status, 207);
        QCOMPARE(hrefNames(folder), (QStringList{u"Dziennik/"_s, u"Dziennik/2026-11-22.md"_s}));
        const QList<DavResource> resources = parseMultistatus(folder.body).resources;
        const auto file = std::ranges::find_if(resources, [](const DavResource &r) { return !r.isCollection; });
        QVERIFY(file != resources.end());
        QCOMPARE(file->size, qint64(QByteArray("- [ ] łódź\n").size()));
        QVERIFY(!file->etag.isEmpty());
        QVERIFY(file->mtime > 0);

        // Depth 0: the entry alone; no depth (infinity) is refused as Nextcloud does.
        QCOMPARE(hrefNames(ask(storage, request("PROPFIND", u"Dziennik/"_s, {{"Depth", "0"}}))),
                 QStringList{u"Dziennik/"_s});
        QCOMPARE(ask(storage, request("PROPFIND", u"Dziennik/"_s)).status, 403);
        QCOMPARE(ask(storage, request("PROPFIND", u"Brak/"_s, {{"Depth", "1"}})).status, 404);
    }

    void getAndPut()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"Notatka.md"_s, "stara treść\n"));
        LocalFolderStorage storage(tmp.path());

        const HttpResponse got = ask(storage, request("GET", u"Notatka.md"_s));
        QCOMPARE(got.status, 200);
        QCOMPARE(got.body, QByteArray("stara treść\n"));
        const QString etag = normalizeEtag(QString::fromUtf8(got.header("etag")));
        QVERIFY(!etag.isEmpty());
        QCOMPARE(ask(storage, request("GET", u"Brak.md"_s)).status, 404);

        // Conditions: another version, or a file that exists for "create only".
        QCOMPARE(ask(storage, request("PUT", u"Notatka.md"_s, {{"If-Match", "\"0-0\""}}, "x")).status, 412);
        QCOMPARE(ask(storage, request("PUT", u"Notatka.md"_s, {{"If-None-Match", "*"}}, "x")).status, 412);
        QCOMPARE(ask(storage, request("PUT", u"Nowa.md"_s, {{"If-Match", quoteEtag(etag).toUtf8()}}, "x")).status, 412);
        QCOMPARE(storage.read(u"Notatka.md"_s, 1000).data, QByteArray("stara treść\n"));

        const HttpResponse replaced = ask(storage, request("PUT", u"Notatka.md"_s,
                                                           {{"If-Match", quoteEtag(etag).toUtf8()}}, "nowa treść\n"));
        QCOMPARE(replaced.status, 204);
        QVERIFY(!replaced.header("etag").isEmpty());
        QCOMPARE(storage.read(u"Notatka.md"_s, 1000).data, QByteArray("nowa treść\n"));

        const HttpResponse created = ask(storage, request("PUT", u"Źródła/łąka.md"_s, {}, "x"));
        QCOMPARE(created.status, 409); // no parent folder: the client makes it first
        QCOMPARE(ask(storage, request("MKCOL", u"Źródła/"_s)).status, 201);
        QCOMPARE(ask(storage, request("PUT", u"Źródła/łąka.md"_s, {{"If-None-Match", "*"}}, "x")).status, 201);
        QVERIFY(QFile::exists(tmp.path() + u"/Źródła/łąka.md"_s));
        // A folder is not a file.
        QCOMPARE(ask(storage, request("PUT", u"Źródła"_s, {}, "x")).status, 405);
        QCOMPARE(ask(storage, request("GET", u"Źródła"_s)).status, 405);
    }

    void folders()
    {
        QTemporaryDir tmp;
        LocalFolderStorage storage(tmp.path());
        QCOMPARE(ask(storage, request("MKCOL", u"Projekty/"_s)).status, 201);
        QCOMPARE(ask(storage, request("MKCOL", u"Projekty/"_s)).status, 405);
        QCOMPARE(ask(storage, request("MKCOL", u"Brak/Głęboko/"_s)).status, 409);
        // The folder itself exists: MKCOL of the base answers 405 (the client's ensureBase()).
        QCOMPARE(ask(storage, request("MKCOL", QString())).status, 405);

        QVERIFY(writeFileBehind(tmp.path(), u"Projekty/Plan.md"_s, "plan"));
        const FolderStatResult plan = storage.stat(u"Projekty/Plan.md"_s);
        QVERIFY(plan.entry);
        QCOMPARE(ask(storage, request("DELETE", u"Projekty/Plan.md"_s, {{"If-Match", "\"zła\""}})).status, 412);
        QCOMPARE(ask(storage, request("DELETE", u"Projekty/Plan.md"_s,
                                      {{"If-Match", quoteEtag(FolderDavTransport::etagOf(*plan.entry)).toUtf8()}}))
                         .status,
                 204);
        QVERIFY(!QFile::exists(tmp.path() + u"/Projekty/Plan.md"_s));
        QCOMPARE(ask(storage, request("DELETE", u"Projekty/Plan.md"_s)).status, 404);
        // Never the folder itself.
        QCOMPARE(ask(storage, request("DELETE", QString())).status, 403);
        QVERIFY(QDir(tmp.path()).exists());
    }

    void move()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"A/notatka.md"_s, "a"));
        QVERIFY(writeFileBehind(tmp.path(), u"B/inna.md"_s, "b"));
        LocalFolderStorage storage(tmp.path());
        const auto destination = [](const QString &path) {
            return std::pair<QByteArray, QByteArray>("Destination", joinUrl(deviceFolderUrl(kId), path, false).toUtf8());
        };
        QCOMPARE(ask(storage, request("MOVE", u"A/notatka.md"_s, {destination(u"B/inna.md"_s), {"Overwrite", "F"}})).status,
                 412);
        QCOMPARE(ask(storage, request("MOVE", u"A/notatka.md"_s, {destination(u"C/x.md"_s)})).status, 409);
        QCOMPARE(ask(storage, request("MOVE", u"A/"_s, {destination(u"A/w środku"_s)})).status, 403);
        QCOMPARE(ask(storage, request("MOVE", u"A/notatka.md"_s,
                                      {{"Destination", "https://chmura.example.pl/remote.php/dav/files/jan/x.md"}}))
                         .status,
                 502);
        QCOMPARE(ask(storage, request("MOVE", u"A/notatka.md"_s, {destination(u"B/przeniesiona.md"_s), {"Overwrite", "F"}}))
                         .status,
                 201);
        QVERIFY(QFile::exists(tmp.path() + u"/B/przeniesiona.md"_s));
        QCOMPARE(ask(storage, request("MOVE", u"B/przeniesiona.md"_s, {destination(u"B/inna.md"_s), {"Overwrite", "T"}}))
                         .status,
                 204);
        QCOMPARE(storage.read(u"B/inna.md"_s, 10).data, QByteArray("a"));

        // Storage that cannot move: 405, and the engine copies and deletes instead.
        TestStorage cannot(tmp.path());
        cannot.canMove = false;
        QCOMPARE(ask(cannot, request("MOVE", u"B/inna.md"_s, {destination(u"A/z.md"_s)})).status, 405);
    }

    void refusesWhatIsOutside_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("another folder") << u"brulion-folder://device/0000000000000000/a.md"_s;
        QTest::newRow("above") << u"brulion-folder://device/a.md"_s;
        QTest::newRow("dot dot") << deviceFolderUrl(kId) + u"/../0000/a.md"_s;
        QTest::newRow("dot dot encoded") << deviceFolderUrl(kId) + u"/%2E%2E/a.md"_s;
    }
    void refusesWhatIsOutside()
    {
        QFETCH(QString, url);
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path() + u"/../"_s, u"a.md"_s, "poza"));
        LocalFolderStorage storage(tmp.path());
        HttpRequest r;
        r.method = "GET";
        r.url = QUrl(url, QUrl::TolerantMode);
        const HttpResponse response = ask(storage, r);
        QVERIFY2(response.status == 403 || response.status == 404, qPrintable(QString::number(response.status)));
        QVERIFY(response.body != "poza");
    }

    void storageFailures_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<int>("status");
        QTest::newRow("permission taken back") << int(FolderErrorCode::NotAllowed) << 403;
        QTest::newRow("storage removed") << int(FolderErrorCode::Unavailable) << 503;
        QTest::newRow("read failed") << int(FolderErrorCode::Failed) << 500;
    }
    void storageFailures()
    {
        QFETCH(int, code);
        QFETCH(int, status);
        QTemporaryDir tmp;
        TestStorage storage(tmp.path());
        storage.listError = FolderError::make(FolderErrorCode(code), u"nie można"_s);
        const HttpResponse listing = ask(storage, request("PROPFIND", QString(), {{"Depth", "1"}}));
        QCOMPARE(listing.status, status);
        QCOMPARE(QString::fromUtf8(listing.body), u"nie można"_s);
        storage.listError = {};
        storage.writeError = FolderError::make(FolderErrorCode(code), u"zapis"_s);
        QCOMPARE(ask(storage, request("PUT", u"a.md"_s, {}, "x")).status, status);
    }

    void tooLargeAnswer()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"duży.bin"_s, QByteArray(5000, 'x')));
        LocalFolderStorage storage(tmp.path());
        HttpRequest r = request("GET", u"duży.bin"_s);
        r.maxBodyBytes = 1000;
        const HttpResponse response = ask(storage, r);
        QVERIFY(response.tooLarge);
        QCOMPARE(response.status, 0);
    }

    // --- the worker thread ---------------------------------------------------------------------
    void answersOnTheOwnerThread()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"a.md"_s, "ą"));
        auto storage = std::make_shared<TestStorage>(tmp.path());
        FolderDavTransport transport(storage, deviceFolderUrl(kId));
        QStringList order;
        bool ownerThread = true;
        for (int i = 0; i < 5; ++i) {
            transport.send(request("GET", u"a.md"_s), [&order, &ownerThread, &transport, i](const HttpResponse &r) {
                ownerThread = ownerThread && QThread::currentThread() == transport.thread();
                order.append(u"%1:%2"_s.arg(i).arg(r.status));
            });
        }
        QVERIFY(order.isEmpty()); // never inside send()
        QVERIFY(waitFor([&order] { return order.size() == 5; }));
        QCOMPARE(order, (QStringList{u"0:200"_s, u"1:200"_s, u"2:200"_s, u"3:200"_s, u"4:200"_s}));
        QVERIFY(ownerThread);
        QCOMPARE(storage->offThread.load(), storage->calls.load()); // the storage only on the worker
    }

    void destroyedWithRequestsQueued()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"a.md"_s, "ą"));
        auto storage = std::make_shared<TestStorage>(tmp.path());
        int answered = 0;
        {
            FolderDavTransport transport(storage, deviceFolderUrl(kId));
            for (int i = 0; i < 50; ++i)
                transport.send(request("GET", u"a.md"_s), [&answered](const HttpResponse &) { ++answered; });
        }
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QCOMPARE(answered, 0); // dropped with the transport, never called afterwards
    }

    void synchronousMode()
    {
        QTemporaryDir tmp;
        auto storage = std::make_shared<TestStorage>(tmp.path());
        FolderDavTransport transport(storage, deviceFolderUrl(kId));
        transport.setSynchronous(true);
        int status = 0;
        transport.send(request("MKCOL", u"Nowy/"_s), [&status](const HttpResponse &r) { status = r.status; });
        QCOMPARE(status, 0); // still from the event loop
        QVERIFY(waitFor([&status] { return status != 0; }));
        QCOMPARE(status, 201);
        QCOMPARE(storage->offThread.load(), 0);
    }

    // --- the WebDAV client over it -------------------------------------------------------------
    void client()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"Dziennik/2026-11-22.md"_s, "- [ ] kupić żółty ser\n"));
        QVERIFY(writeFileBehind(tmp.path(), u"Strona główna.md"_s, "# Brulion\n"));
        auto storage = std::make_shared<LocalFolderStorage>(tmp.path());
        FolderDavTransport transport(storage, deviceFolderUrl(kId));
        WebDavClient client(deviceFolderUrl(kId), u"device"_s, QString(), &transport);
        client.setListConcurrency(1);

        std::optional<ListResult> listed;
        client.list({}, [&listed](const ListResult &r) { listed = r; });
        QVERIFY(waitFor([&listed] { return listed.has_value(); }));
        QVERIFY2(!listed->error.isError(), qPrintable(listed->error.message));
        QStringList paths;
        for (const RemoteEntry &e : listed->entries)
            paths.append(e.path + (e.isDir ? u"/"_s : QString()));
        QCOMPARE(paths, (QStringList{u"Dziennik/"_s, u"Dziennik/2026-11-22.md"_s, u"Strona główna.md"_s}));

        std::optional<PutResult> put;
        client.put(u"Nowy/Głęboki/łąka.md"_s, "trawa", {}, [&put](const PutResult &r) { put = r; });
        QVERIFY(waitFor([&put] { return put.has_value(); }));
        QVERIFY2(!put->error.isError(), qPrintable(put->error.message));
        QVERIFY(!put->etag.isEmpty());
        std::optional<GetResult> got;
        client.get(u"Nowy/Głęboki/łąka.md"_s, {}, [&got](const GetResult &r) { got = r; });
        QVERIFY(waitFor([&got] { return got.has_value(); }));
        QCOMPARE(got->data, QByteArray("trawa"));
        QCOMPARE(got->etag, put->etag);

        std::optional<WebDavTestResult> tested;
        client.test([&tested](const WebDavTestResult &r) { tested = r; });
        QVERIFY(waitFor([&tested] { return tested.has_value(); }));
        QVERIFY(tested->ok);
    }

    // --- the engine ----------------------------------------------------------------------------
    void firstSyncFetchesTheFolder()
    {
        FolderDevice d({}, {{u"Strona główna.md"_s, "# Brulion\n"}, {u"Dziennik/2026-11-22.md"_s, "- [ ] żółw\n"},
                            {u"obrazek.png"_s, QByteArray("\x89PNG\r\n\x1a\n\0\x01", 10)}});
        const SyncReport report = d.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        QCOMPARE(counts(report), (Counts{.down = 3}));
        QCOMPARE(d.vault->readText(u"Dziennik/2026-11-22.md"_s), u"- [ ] żółw\n"_s);
        QCOMPARE(d.vault->readBinary(u"obrazek.png"_s), QByteArray("\x89PNG\r\n\x1a\n\0\x01", 10));
        // Nothing to do the second time.
        QCOMPARE(counts(d.sync()), Counts{});
    }

    void firstSyncFillsAnEmptyFolder()
    {
        FolderDevice d({{u"Strona główna.md"_s, "# Brulion\n"}, {u"Projekty/Łódź.md"_s, "rejs\n"}});
        const SyncReport report = d.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        QCOMPARE(counts(report), (Counts{.up = 2}));
        QCOMPARE(d.folderText(u"Projekty/Łódź.md"_s), u"rejs\n"_s);
        QCOMPARE(counts(d.sync()), Counts{});
    }

    void changesTravelBothWays()
    {
        FolderDevice d({{u"Notatka.md"_s, "jeden\ndwa\ntrzy\n"}});
        QVERIFY(!d.sync().failed());
        // Syncthing brings a new note and a change to the third line; the app changes the first.
        d.writeInFolder(u"Z komputera.md"_s, u"napisane na komputerze\n"_s);
        d.writeInFolder(u"Notatka.md"_s, u"jeden\ndwa\ntrzy — z komputera\n"_s);
        d.clock.advance(60'000);
        QVERIFY(d.vault->writeText(u"Notatka.md"_s, u"jeden — z telefonu\ndwa\ntrzy\n"_s));
        const SyncReport report = d.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        QCOMPARE(report.downloaded, 1);
        QCOMPARE(report.merged, 1);
        QCOMPARE(report.conflicts.size(), 0);
        const QString merged = u"jeden — z telefonu\ndwa\ntrzy — z komputera\n"_s;
        QCOMPARE(d.vault->readText(u"Notatka.md"_s), merged);
        QCOMPARE(d.folderText(u"Notatka.md"_s), merged);
        QCOMPARE(d.vault->readText(u"Z komputera.md"_s), u"napisane na komputerze\n"_s);
        QCOMPARE(counts(d.sync()), Counts{});
    }

    void conflictKeepsBothVersions()
    {
        FolderDevice d({{u"Lista.md"_s, "mleko\n"}});
        QVERIFY(!d.sync().failed());
        d.writeInFolder(u"Lista.md"_s, u"mleko owsiane\n"_s);
        d.clock.advance(60'000);
        QVERIFY(d.vault->writeText(u"Lista.md"_s, u"mleko krowie\n"_s));
        const SyncReport report = d.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        QCOMPARE(report.conflicts.size(), 1);
        QCOMPARE(d.vault->readText(u"Lista.md"_s), u"mleko krowie\n"_s);
        QCOMPARE(d.folderText(u"Lista.md"_s), u"mleko krowie\n"_s);
        // The folder's version is kept as a conflict copy on both sides.
        const QStringList copies = d.folderFiles().filter(u".conflict-"_s);
        QCOMPARE(copies.size(), 1);
        QCOMPARE(d.folderText(copies.first()), u"mleko owsiane\n"_s);
        QVERIFY(d.vaultFiles().contains(copies.first()));
    }

    void deletionsTravel()
    {
        FolderDevice d({{u"a.md"_s, "a"}, {u"b.md"_s, "b"}, {u"c.md"_s, "c"}});
        QVERIFY(!d.sync().failed());
        QVERIFY(QFile::remove(d.folderRoot + u"/a.md"_s));
        QVERIFY(d.vault->remove(u"b.md"_s));
        const SyncReport report = d.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        QCOMPARE(report.deletedLocal, 1);
        QCOMPARE(report.deletedRemote, 1);
        QCOMPARE(d.vaultFiles(), QStringList{u"c.md"_s});
        QCOMPARE(d.folderFiles(), QStringList{u"c.md"_s});
    }

    void renamedFolderMoves_data()
    {
        QTest::addColumn<bool>("canMove");
        QTest::newRow("the storage moves") << true;
        QTest::newRow("copy and delete") << false;
    }
    void renamedFolderMoves()
    {
        QFETCH(bool, canMove);
        FolderDevice d({{u"Stare/1.md"_s, "jeden"}, {u"Stare/2.md"_s, "dwa"}});
        d.storage->canMove = canMove;
        QVERIFY(!d.sync().failed());
        QVERIFY(d.vault->rename(u"Stare"_s, u"Nowe"_s));
        const SyncReport report = d.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        QCOMPARE(d.folderFiles(), (QStringList{u"Nowe/1.md"_s, u"Nowe/2.md"_s}));
        QCOMPARE(d.storage->moves, 1);
        QCOMPARE(counts(d.sync()), Counts{});
    }

    /// A folder that cannot be read is never an empty one: nothing is deleted in the notebook.
    void unreadableFolderDeletesNothing_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<int>("webdav");
        QTest::newRow("permission taken back") << int(FolderErrorCode::NotAllowed) << int(WebDavErrorCode::Forbidden);
        QTest::newRow("storage removed") << int(FolderErrorCode::Unavailable) << int(WebDavErrorCode::Server);
    }
    void unreadableFolderDeletesNothing()
    {
        QFETCH(int, code);
        QFETCH(int, webdav);
        FolderDevice d({{u"a.md"_s, "a"}, {u"b.md"_s, "b"}});
        QVERIFY(!d.sync().failed());
        d.storage->listError = FolderError::make(FolderErrorCode(code), u"brak dostępu"_s);
        const SyncReport report = d.sync();
        QVERIFY(report.failed());
        QCOMPARE(int(report.remoteError.code), webdav);
        QCOMPARE(report.deletedLocal, 0);
        QCOMPARE(d.vaultFiles(), (QStringList{u"a.md"_s, u"b.md"_s}));
        // Access back: nothing to do.
        d.storage->listError = {};
        QCOMPARE(counts(d.sync()), Counts{});
    }

    /// The picked folder deleted (Android cannot make it again): the run fails, nothing deleted.
    void vanishedFolderDeletesNothing()
    {
        FolderDevice d({{u"a.md"_s, "a"}, {u"b.md"_s, "b"}});
        QVERIFY(!d.sync().failed());
        d.storage->canCreateRoot = false;
        QVERIFY(QDir(d.folderRoot).removeRecursively());
        const SyncReport report = d.sync();
        QVERIFY(report.failed());
        QCOMPARE(report.remoteError.code, WebDavErrorCode::Forbidden);
        QCOMPARE(d.vaultFiles(), (QStringList{u"a.md"_s, u"b.md"_s}));
        QVERIFY(!QDir(d.folderRoot).exists());
    }

    /// A folder emptied behind the app's back (a card swapped, an empty folder picked by mistake):
    /// the engine's guard refuses to mirror it.
    void emptiedFolderIsRefused()
    {
        FolderDevice d({{u"a.md"_s, "a"}, {u"b.md"_s, "b"}, {u"c.md"_s, "c"}});
        QVERIFY(!d.sync().failed());
        QVERIFY(QDir(d.folderRoot).removeRecursively());
        QVERIFY(QDir().mkpath(d.folderRoot));
        const SyncReport report = d.sync();
        QVERIFY(report.failed());
        QCOMPARE(report.syncError.code, SyncErrorCode::RemoteEmpty);
        QCOMPARE(d.vaultFiles(), (QStringList{u"a.md"_s, u"b.md"_s, u"c.md"_s}));
    }

    void writeFailureIsPerFile()
    {
        FolderDevice d({{u"a.md"_s, "a"}});
        QVERIFY(!d.sync().failed());
        QVERIFY(d.vault->writeText(u"a.md"_s, u"zmiana"_s));
        d.storage->writeError = FolderError::make(FolderErrorCode::Failed, u"dysk pełny"_s);
        const SyncReport failed = d.sync();
        QCOMPARE(failed.errors.size(), 1);
        d.storage->writeError = {};
        const SyncReport retried = d.sync();
        QVERIFY2(!retried.failed(), qPrintable(retried.failureMessage()));
        QCOMPARE(retried.uploaded, 1);
        QCOMPARE(d.folderText(u"a.md"_s), u"zmiana"_s);
    }

    void synchronousTransportSyncsToo()
    {
        FolderDevice d({{u"a.md"_s, "a"}}, {{u"b.md"_s, "b"}}, true);
        const SyncReport report = d.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        QCOMPARE(counts(report), (Counts{.up = 1, .down = 1}));
        QCOMPARE(d.storage->offThread.load(), 0);
    }

    // --- large files in pieces -----------------------------------------------------------------
    void largeFilesTravelInPieces()
    {
        const QByteArray film = pattern(20 * MiB, 1);
        const QByteArray scan = pattern(12 * MiB, 2);
        FolderDevice d({{u"Filmy/wakacje.mov"_s, film}, {u"Notatka.md"_s, "Zażółć\n"}},
                       {{u"Skany/umowa.pdf"_s, scan}});
        QVERIFY(d.transport->streams());
        {
            const SyncReport report = d.sync();
            QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        }
        QCOMPARE(readFile(d.folderRoot + u"/Filmy/wakacje.mov"_s), film);
        QCOMPARE(readFile(d.vaultRoot + u"/Skany/umowa.pdf"_s), scan);
        // Neither file was held whole on its way: pieces of at most 256 KiB.
        QVERIFY2(d.storage->largestWhole.load() < MiB, qPrintable(QString::number(d.storage->largestWhole.load())));
        QVERIFY(d.storage->largestPiece.load() > 0);
        QVERIFY(d.storage->largestPiece.load() <= HttpRequest::StreamPieceBytes);
    }

    void aStorageThatDoesNotStreamGetsWholeFiles()
    {
        const QByteArray film = pattern(6 * MiB, 3);
        FolderDevice d({{u"wideo.mov"_s, film}});
        d.storage->streaming = false;
        QVERIFY(!d.transport->streams());
        {
            const SyncReport report = d.sync();
            QVERIFY2(!report.failed(), qPrintable(report.failureMessage()));
        }
        QCOMPARE(readFile(d.folderRoot + u"/wideo.mov"_s), film);
        QCOMPARE(d.storage->largestWhole.load(), film.size());
        QCOMPARE(d.storage->largestPiece.load(), 0);
    }

    void piecesAreHandedOverOnTheOwnerThread()
    {
        QTemporaryDir tmp;
        const QByteArray photo = pattern(3 * MiB + 17, 4);
        QVERIFY(writeFileBehind(tmp.path(), u"zdjęcie.jpg"_s, photo));
        auto storage = std::make_shared<TestStorage>(tmp.path());
        FolderDavTransport transport(storage, deviceFolderUrl(kId));
        QByteArray received;
        bool ownerThread = true;
        qint64 largest = 0;
        std::optional<HttpResponse> answer;
        HttpRequest r = request("GET", u"zdjęcie.jpg"_s);
        r.bodySink = [&](QByteArrayView piece) {
            ownerThread = ownerThread && QThread::currentThread() == transport.thread();
            largest = std::max(largest, qint64(piece.size()));
            received.append(piece);
            return true;
        };
        transport.send(r, [&answer](const HttpResponse &response) { answer = response; });
        QVERIFY(waitFor([&answer] { return answer.has_value(); }));
        QCOMPARE(answer->status, 200);
        QVERIFY(answer->body.isEmpty());
        QVERIFY(!answer->header("etag").isEmpty());
        QCOMPARE(received, photo);
        QVERIFY(ownerThread);
        QVERIFY(largest <= HttpRequest::StreamPieceBytes);
        QCOMPARE(storage->offThread.load(), storage->calls.load());
    }

    void aRefusedPieceStopsTheDownload()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"a.bin"_s, pattern(2 * MiB, 5)));
        auto storage = std::make_shared<TestStorage>(tmp.path());
        FolderDavTransport transport(storage, deviceFolderUrl(kId));
        int pieces = 0;
        std::optional<HttpResponse> answer;
        HttpRequest r = request("GET", u"a.bin"_s);
        r.bodySink = [&pieces](QByteArrayView) { return ++pieces < 2; }; // a full disk at the second
        transport.send(r, [&answer](const HttpResponse &response) { answer = response; });
        QVERIFY(waitFor([&answer] { return answer.has_value(); }));
        QCOMPARE(pieces, 2);
        QCOMPARE(answer->status, 0);
        QVERIFY(!answer->networkError.isEmpty());
    }

    void destroyedWhileAPieceWaits()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"a.bin"_s, pattern(4 * MiB, 6)));
        auto storage = std::make_shared<TestStorage>(tmp.path());
        int pieces = 0;
        int answered = 0;
        {
            FolderDavTransport transport(storage, deviceFolderUrl(kId));
            HttpRequest r = request("GET", u"a.bin"_s);
            r.bodySink = [&pieces](QByteArrayView) { ++pieces; return true; };
            transport.send(r, [&answered](const HttpResponse &) { ++answered; });
            QTest::qWait(20); // the worker reads its first piece and waits for this thread
        }
        const int handed = pieces;
        QTest::qWait(50);
        QCOMPARE(pieces, handed); // nothing after the transport has gone
        QCOMPARE(answered, 0);
        // The worker stopped at the piece it waited with: at most one read past those handed (not all 16).
        QVERIFY2(storage->piecesRead.load() <= handed + 1,
                 qPrintable(u"%1 read, %2 handed"_s.arg(storage->piecesRead.load()).arg(handed)));
    }

    void aStorageThatDoesNotStreamAnswersPiecesToo()
    {
        // FolderDavTransport::answer() with a sink or a body device over a storage that reads
        // and writes files whole (FolderStorage's own readInto() and writeFrom()).
        QTemporaryDir tmp;
        TestStorage storage(tmp.path());
        storage.streaming = false;
        const QByteArray photo = pattern(MiB + 3, 8);
        QVERIFY(writeFileBehind(tmp.path(), u"zdjęcie.jpg"_s, photo));
        QByteArray received;
        qint64 largest = 0;
        HttpRequest r = request("GET", u"zdjęcie.jpg"_s);
        r.bodySink = [&](QByteArrayView piece) {
            largest = std::max(largest, qint64(piece.size()));
            received.append(piece);
            return true;
        };
        QCOMPARE(ask(storage, r).status, 200);
        QCOMPARE(received, photo);
        QCOMPARE(largest, HttpRequest::StreamPieceBytes);
        QCOMPARE(storage.largestWhole.load(), photo.size());
        // A file of whole pieces: one call per piece, no empty one after them.
        QVERIFY(writeFileBehind(tmp.path(), u"równo.bin"_s, pattern(2 * HttpRequest::StreamPieceBytes, 3)));
        int calls = 0;
        HttpRequest even = request("GET", u"równo.bin"_s);
        even.bodySink = [&calls](QByteArrayView piece) { ++calls; return !piece.isEmpty(); };
        QCOMPARE(ask(storage, even).status, 200);
        QCOMPARE(calls, 2);
        r.bodySink = [](QByteArrayView) { return false; };
        QVERIFY(!ask(storage, r).networkError.isEmpty());

        auto buffer = std::make_shared<QBuffer>();
        buffer->setData(photo);
        QVERIFY(buffer->open(QIODevice::ReadOnly));
        HttpRequest put = request("PUT", u"kopia.jpg"_s);
        put.bodyDevice = buffer;
        QCOMPARE(ask(storage, put).status, 201);
        QCOMPARE(readFile(tmp.path() + u"/kopia.jpg"_s), photo);
        put.bodyDevice = std::make_shared<FailingDevice>(3 * MiB, MiB);
        QVERIFY(ask(storage, put).status >= 400);
        QCOMPARE(readFile(tmp.path() + u"/kopia.jpg"_s), photo); // as it was
    }

    void anUploadReadInPieces()
    {
        QTemporaryDir tmp;
        LocalFolderStorage storage(tmp.path());
        const QByteArray film = pattern(5 * MiB + 3, 7);
        auto buffer = std::make_shared<QBuffer>();
        buffer->setData(film);
        QVERIFY(buffer->open(QIODevice::ReadOnly));
        HttpRequest r = request("PUT", u"film.mov"_s);
        r.bodyDevice = buffer;
        QCOMPARE(ask(storage, r).status, 201);
        QCOMPARE(readFile(tmp.path() + u"/film.mov"_s), film);
    }

    void anUploadThatFailsHalfWayLeavesNoFile()
    {
        QTemporaryDir tmp;
        QVERIFY(writeFileBehind(tmp.path(), u"stary.mov"_s, "poprzednia wersja"));
        LocalFolderStorage storage(tmp.path());
        for (const QString &name : {u"nowy.mov"_s, u"stary.mov"_s}) {
            HttpRequest r = request("PUT", name);
            r.bodyDevice = std::make_shared<FailingDevice>(3 * MiB, MiB);
            const HttpResponse response = ask(storage, r);
            QVERIFY2(response.status >= 400, qPrintable(QString::number(response.status)));
        }
        QVERIFY(!QFile::exists(tmp.path() + u"/nowy.mov"_s));
        QCOMPARE(readFile(tmp.path() + u"/stary.mov"_s), QByteArray("poprzednia wersja"));
        QCOMPARE(QDir(tmp.path()).entryList(QDir::Files | QDir::Hidden), (QStringList{u"stary.mov"_s}));
    }

    void anUploadShorterThanItsSizeIsRefused()
    {
        QTemporaryDir tmp;
        LocalFolderStorage storage(tmp.path());
        auto buffer = std::make_shared<QBuffer>();
        buffer->setData(pattern(1000));
        QVERIFY(buffer->open(QIODevice::ReadOnly));
        HttpRequest r = request("PUT", u"krótki.bin"_s);
        r.bodyDevice = buffer;
        QVERIFY(buffer->read(10).size() == 10); // (no longer at its start: 990 bytes left of 1000)
        QVERIFY(ask(storage, r).status >= 400);
        QVERIFY(!QFile::exists(tmp.path() + u"/krótki.bin"_s));
    }
};

QTEST_GUILESS_MAIN(TestSyncFolder)
#include "tst_sync_folder.moc"

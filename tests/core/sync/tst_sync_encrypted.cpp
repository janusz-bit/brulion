// The end-to-end encrypted sync (src/core/sync/encryptingtransport.h, docs/SYNC-ENCRYPTION.md):
// devices syncing one notebook through the fake WebDAV server with EncryptingTransport in between —
// the server holds no plain name and no plain byte, yet notes, folders, renames, deletions,
// merges, conflicts and large attachments in pieces travel as without it; what is not the
// notebook's on the server is left alone, a file changed there is never taken, a name too long to
// encrypt is one file's error, and a device with another key sees an empty folder and deletes
// nothing.
#include "synctest.h"

#include "sync/encryptingtransport.h"
#include "sync/davxml.h"
#include "sync/encryption.h"

#include <QBuffer>
#include <QTest>

#include <utility>

using namespace synctest;
namespace encryption = brulion::sync::encryption;

namespace {

constexpr qint64 MiB = 1024 * 1024;

QByteArray notebookKey(char fill = 'k')
{
    return QByteArray(encryption::NotebookKeySize, fill);
}

QByteArray pattern(qint64 size, int seed = 0)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qint64 i = 0; i < size; ++i)
        out[i] = char((i * 7919 + 13 + seed * 31) & 0xff);
    return out;
}

/// A file that says it is `size` bytes long but holds only `data` (it shrank meanwhile).
class ShrunkDevice : public QBuffer {
public:
    ShrunkDevice(const QByteArray &data, qint64 size) : m_size(size)
    {
        setData(data);
        open(QIODevice::ReadOnly);
    }
    qint64 size() const override { return m_size; }

private:
    qint64 m_size;
};

/// A device whose client reaches the server through EncryptingTransport.
struct EncryptedDevice {
    EncryptedDevice(FakeWebDavServer &server, const QString &name, const QMap<QString, QByteArray> &files = {},
                    const QByteArray &key = notebookKey())
        : transport(&server, server.url(kFolder), encryption::Keys::fromNotebookKey(key))
        , device(server, name, files, options())
    {
    }
    DeviceOptions options()
    {
        DeviceOptions o;
        o.transport = &transport;
        return o;
    }
    SyncReport sync() { return device.sync(); }

    brulion::sync::EncryptingTransport transport;
    Device device;
};

bool ok(const SyncReport &report)
{
    if (report.failed())
        qWarning() << report.failureMessage();
    return !report.failed();
}

/// The server's files of the notebook's folder whose names are not encrypted ones.
QStringList plainNamesOnServer(const FakeWebDavServer &server, const QByteArray &key = notebookKey())
{
    const encryption::Keys keys = encryption::Keys::fromNotebookKey(key);
    QStringList plain;
    const QMap<QString, QString> files = server.files(kFolder);
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!encryption::decryptPath(keys, it.key()))
            plain.append(it.key());
    }
    return plain;
}

/// Some server file of the notebook holds `bytes` as they are.
bool serverHolds(const FakeWebDavServer &server, const QByteArray &bytes)
{
    const QMap<QString, QString> files = server.files(kFolder);
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (server.bytes(kFolder + u'/' + it.key())->contains(bytes))
            return true;
    }
    return false;
}

QString serverPathOf(const QString &plain, const QByteArray &key = notebookKey())
{
    return kFolder + u'/' + *encryption::encryptPath(encryption::Keys::fromNotebookKey(key), plain);
}

/// An inner transport that answers every request with `answer` (after recording it).
class Canned : public brulion::sync::HttpTransport {
public:
    HttpResponse answer;
    QList<HttpRequest> sent;
    bool streaming = false;
    void send(const HttpRequest &request, brulion::sync::HttpCallback done) override
    {
        sent.append(request);
        if (request.bodySink && answer.status >= 200 && answer.status < 300) {
            HttpResponse r = answer;
            const QByteArray body = std::exchange(r.body, {});
            for (qsizetype at = 0; at < body.size(); at += 1000) {
                if (!request.bodySink(QByteArrayView(body).sliced(at, std::min<qsizetype>(1000, body.size() - at)))) {
                    HttpResponse stopped;
                    stopped.networkError = u"stopped by the sink"_s;
                    done(stopped);
                    return;
                }
            }
            done(r);
            return;
        }
        done(answer);
    }
    bool streams() const override { return streaming; }
};

const QString kBase = u"https://chmura.example.pl/remote.php/dav/files/jan/Notatki"_s;

HttpRequest requestFor(const QByteArray &method, const QString &path)
{
    HttpRequest r;
    r.method = method;
    r.url = QUrl(kBase + u'/' + path);
    return r;
}

/// `request` sent through `transport`, its answer (synchronous inner transports only).
HttpResponse ask(brulion::sync::EncryptingTransport &transport, const HttpRequest &request)
{
    HttpResponse answer;
    transport.send(request, [&answer](const HttpResponse &r) { answer = r; });
    return answer;
}

} // namespace

class TestSyncEncrypted : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void twoDevicesShareAnEncryptedNotebook()
    {
        FakeWebDavServer server;
        server.mkdir(kFolder);
        const QByteArray photo = pattern(300 * 1024, 1);
        EncryptedDevice a(server, u"Laptop"_s,
                          {{u"Dziennik/2026-11-28.md"_s, "- Zażółć gęślą jaźń\n"_ba},
                           {u"Projekty/Łódź/plan.md"_s, "# Plan\n- [ ] bilety\n"_ba},
                           {u"assets/zdjęcie.jpg"_s, photo}});
        QVERIFY(ok(a.sync()));
        QVERIFY(!server.files(kFolder).isEmpty());
        QCOMPARE(plainNamesOnServer(server), QStringList());
        QVERIFY(!serverHolds(server, "Zażółć"_ba));
        QVERIFY(!serverHolds(server, "bilety"_ba));
        QVERIFY(!serverHolds(server, photo.mid(1000, 64)));

        EncryptedDevice b(server, u"Telefon"_s);
        const SyncReport first = b.sync();
        QVERIFY(ok(first));
        QCOMPARE(first.downloaded, 3);
        QCOMPARE(b.device.read(u"Dziennik/2026-11-28.md"_s), u"- Zażółć gęślą jaźń\n"_s);
        QCOMPARE(b.device.read(u"Projekty/Łódź/plan.md"_s), u"# Plan\n- [ ] bilety\n"_s);
        QCOMPARE(b.device.readBytes(u"assets/zdjęcie.jpg"_s), photo);
        // Settled: nothing more either way.
        QCOMPARE(counts(a.sync()), Counts{});
        QCOMPARE(counts(b.sync()), Counts{});
    }

    void changesMovesDeletionsAndConflicts()
    {
        FakeWebDavServer server;
        server.mkdir(kFolder);
        EncryptedDevice a(server, u"Laptop"_s,
                          {{u"Stare/notatka.md"_s, "jeden\n"_ba}, {u"lista.md"_s, "a\nb\nc\n"_ba}, {u"usuń.md"_s, "x\n"_ba}});
        QVERIFY(ok(a.sync()));
        EncryptedDevice b(server, u"Telefon"_s);
        QVERIFY(ok(b.sync()));

        // A renames a folder (one MOVE on the server) and deletes a note; B edits another.
        a.device.clock->advance(60'000);
        QVERIFY(a.device.vault->rename(u"Stare"_s, u"Nowe"_s));
        QVERIFY(a.device.remove(u"usuń.md"_s));
        QVERIFY(b.device.write(u"lista.md"_s, u"a\nb\nc\nd z telefonu\n"_s));
        server.clearLog();
        QVERIFY(ok(a.sync()));
        QCOMPARE(server.requests("MOVE").size(), 1);
        QVERIFY(ok(b.sync()));
        QCOMPARE(b.device.read(u"Nowe/notatka.md"_s), u"jeden\n"_s);
        QVERIFY(!b.device.files().contains(u"usuń.md"_s));
        QVERIFY(ok(a.sync()));
        QCOMPARE(a.device.read(u"lista.md"_s), u"a\nb\nc\nd z telefonu\n"_s);

        // Both change the same line: merged where they can be, a conflict copy where not.
        QVERIFY(a.device.write(u"lista.md"_s, u"A z laptopa\nb\nc\nd z telefonu\n"_s));
        QVERIFY(b.device.write(u"lista.md"_s, u"A z telefonu\nb\nc\nd z telefonu\n"_s));
        QVERIFY(ok(a.sync()));
        const SyncReport conflicted = b.sync();
        QVERIFY(ok(conflicted));
        QCOMPARE(conflicted.conflicts.size(), 1);
        QVERIFY(ok(a.sync()));
        QCOMPARE(a.device.files().size(), b.device.files().size());
        QCOMPARE(plainNamesOnServer(server), QStringList());
    }

    void largeAttachmentsInPieces()
    {
        FakeWebDavServer::Options options;
        options.streams = true;
        FakeWebDavServer server(options);
        server.mkdir(kFolder);
        const QByteArray film = pattern(20 * MiB, 2);
        EncryptedDevice a(server, u"Laptop"_s, {{u"Filmy/wakacje.mov"_s, film}});
        a.device.engine->setStreamAbove(0);
        QVERIFY(a.transport.streams());
        QVERIFY(ok(a.sync()));
        QVERIFY2(server.largestPayload() < MiB, qPrintable(QString::number(server.largestPayload())));
        QCOMPARE(server.bytes(serverPathOf(u"Filmy/wakacje.mov"_s))->size(), encryption::encryptedSize(film.size()));
        EncryptedDevice b(server, u"Telefon"_s);
        b.device.engine->setStreamAbove(0);
        QVERIFY(ok(b.sync()));
        QCOMPARE(b.device.readBytes(u"Filmy/wakacje.mov"_s), film);
        QVERIFY2(server.largestPayload() < MiB, qPrintable(QString::number(server.largestPayload())));
    }

    void whatIsNotTheNotebooksIsLeftAlone()
    {
        FakeWebDavServer server;
        server.putText(kFolder + u"/.brulion/encryption.json"_s, u"{}"_s);
        server.putText(kFolder + u"/README.txt"_s, u"This folder is encrypted by Brulion."_s);
        server.putText(kFolder + u"/Notatka z innej aplikacji.md"_s, u"jawna"_s);
        EncryptedDevice a(server, u"Laptop"_s, {{u"moja.md"_s, "moja\n"_ba}});
        QVERIFY(ok(a.sync()));
        QCOMPARE(a.device.files().keys(), QStringList{u"moja.md"_s});
        QCOMPARE(server.text(kFolder + u"/README.txt"_s), std::optional<QString>(u"This folder is encrypted by Brulion."_s));
        QCOMPARE(server.text(kFolder + u"/Notatka z innej aplikacji.md"_s), std::optional<QString>(u"jawna"_s));
        QCOMPARE(plainNamesOnServer(server).size(), 3);
    }

    void aFileChangedOnTheServerIsNotTaken()
    {
        FakeWebDavServer server;
        server.mkdir(kFolder);
        EncryptedDevice a(server, u"Laptop"_s, {{u"a.md"_s, "pierwsza\n"_ba}, {u"b.md"_s, "druga\n"_ba}});
        QVERIFY(ok(a.sync()));
        QByteArray changed = *server.bytes(serverPathOf(u"a.md"_s));
        changed[changed.size() - 3] = char(changed[changed.size() - 3] ^ 0x20);
        server.putFile(serverPathOf(u"a.md"_s), changed);
        EncryptedDevice b(server, u"Telefon"_s);
        const SyncReport report = b.sync();
        QCOMPARE(report.errors.size(), 1);
        QCOMPARE(report.errors.first().path, u"a.md"_s);
        QVERIFY2(report.errors.first().message.contains(u"cannot be decrypted"_s), qPrintable(report.errors.first().message));
        QCOMPARE(report.errors.first().code, u"http"_s); // (one file's error: never "locked", never the network)
        QCOMPARE(b.device.files().keys(), QStringList{u"b.md"_s});
        // … and A, which has the file, keeps its own version.
        const SyncReport again = a.sync();
        QCOMPARE(a.device.read(u"a.md"_s), u"pierwsza\n"_s);
        Q_UNUSED(again);
    }

    void aNameTooLongToEncryptIsOneFilesError()
    {
        FakeWebDavServer server;
        server.mkdir(kFolder);
        const QString longName = QString(80, u'ż') + u".md"_s; // 163 bytes of UTF-8
        EncryptedDevice a(server, u"Laptop"_s, {{longName, "długa\n"_ba}, {u"krótka.md"_s, "krótka\n"_ba}});
        const SyncReport report = a.sync();
        QVERIFY(ok(report));
        QCOMPARE(report.uploaded, 1);
        QCOMPARE(report.errors.size(), 1);
        QCOMPARE(report.errors.first().path, longName);
        QVERIFY2(report.errors.first().message.contains(u"too long to encrypt"_s), qPrintable(report.errors.first().message));
        QCOMPARE(report.errors.first().code, u"http"_s);
    }

    void anotherKeySeesAnEmptyFolder()
    {
        FakeWebDavServer server;
        server.mkdir(kFolder);
        EncryptedDevice a(server, u"Laptop"_s, {{u"a.md"_s, "a\n"_ba}, {u"b.md"_s, "b\n"_ba}});
        QVERIFY(ok(a.sync()));
        const QMap<QString, QString> before = server.files(kFolder);
        EncryptedDevice stranger(server, u"Obcy"_s, {{u"c.md"_s, "c\n"_ba}}, notebookKey('z'));
        QVERIFY(ok(stranger.sync()));
        QCOMPARE(stranger.device.files().keys(), QStringList{u"c.md"_s});
        // Nothing of A's was deleted or overwritten; the stranger's note sits beside them.
        const QMap<QString, QString> after = server.files(kFolder);
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            QCOMPARE(after.value(it.key()), it.value());
        QCOMPARE(after.size(), before.size() + 1);
        QVERIFY(ok(a.sync()));
        QCOMPARE(a.device.files().keys(), (QStringList{u"a.md"_s, u"b.md"_s}));
    }

    void requestsOutsideTheFolderAreRefused()
    {
        FakeWebDavServer server;
        brulion::sync::EncryptingTransport transport(&server, server.url(kFolder),
                                                     encryption::Keys::fromNotebookKey(notebookKey()));
        HttpRequest request;
        request.method = "GET";
        request.url = QUrl(server.url(u"Inny"_s) + u"/a.md"_s);
        HttpResponse refusal;
        QVERIFY(!transport.encrypted(request, &refusal));
        QCOMPARE(refusal.status, 403);
        // The folder itself goes as it is; below it every name is encrypted.
        request.url = QUrl(server.url(kFolder) + u"/"_s);
        QCOMPARE(transport.encrypted(request, &refusal)->url, request.url);
        request.url = QUrl(server.url(kFolder) + u"/Dziennik/"_s);
        const QString sent = transport.encrypted(request, &refusal)->url.toString();
        QVERIFY(!sent.contains(u"Dziennik"_s));
        QVERIFY(sent.endsWith(u'/'));
    }

    void anUploadIsEncryptedAsItIsRead()
    {
        FakeWebDavServer server;
        brulion::sync::EncryptingTransport transport(&server, server.url(kFolder),
                                                     encryption::Keys::fromNotebookKey(notebookKey()));
        const QByteArray film = pattern(3 * encryption::ChunkSize + 77, 3);
        auto buffer = std::make_shared<QBuffer>();
        buffer->setData(film);
        QVERIFY(buffer->open(QIODevice::ReadOnly));
        HttpRequest request;
        request.method = "PUT";
        request.url = QUrl(server.url(kFolder) + u"/film.mov"_s);
        request.bodyDevice = buffer;
        HttpResponse refusal;
        const std::optional<HttpRequest> sent = transport.encrypted(request, &refusal);
        QVERIFY(sent && sent->bodyDevice && sent->bodyDevice != request.bodyDevice);
        QIODevice &device = *sent->bodyDevice;
        QVERIFY(device.isSequential());
        QCOMPARE(device.size(), encryption::encryptedSize(film.size())); // the Content-Length
        QCOMPARE(device.bytesAvailable(), qint64(encryption::HeaderSize)); // the header is ready at once
        QByteArray body;
        while (!device.atEnd()) {
            const QByteArray piece = device.read(1000);
            QVERIFY(!piece.isEmpty());
            body += piece;
        }
        QCOMPARE(body.size(), device.size());
        QCOMPARE(encryption::open(encryption::Keys::fromNotebookKey(notebookKey()), body)->toByteArray(), film);
        // A file that shrank after its size was taken: the body fails (a read error), it is never
        // sent shorter than its Content-Length as if that were all of it.
        auto shrunk = std::make_shared<ShrunkDevice>(film.left(1000), 5000);
        request.bodyDevice = shrunk;
        HttpRequest cut = *transport.encrypted(request, &refusal);
        QCOMPARE(cut.bodyDevice->size(), encryption::encryptedSize(5000));
        char chunk[4096];
        qint64 n = 0;
        qint64 total = 0;
        while ((n = cut.bodyDevice->read(chunk, sizeof chunk)) > 0)
            total += n;
        QCOMPARE(n, qint64(-1));
        QVERIFY(total < cut.bodyDevice->size());
    }

    void streamedDownloadsChangedOrCutAreRefused_data()
    {
        QTest::addColumn<bool>("cut");
        QTest::newRow("a byte changed") << false;
        QTest::newRow("the last chunk gone") << true;
    }

    void streamedDownloadsChangedOrCutAreRefused()
    {
        QFETCH(bool, cut);
        FakeWebDavServer::Options options;
        options.streams = true;
        FakeWebDavServer server(options);
        server.mkdir(kFolder);
        const QByteArray film = pattern(2 * encryption::ChunkSize + 500, 4);
        EncryptedDevice a(server, u"Laptop"_s, {{u"film.mov"_s, film}, {u"notatka.md"_s, "n\n"_ba}});
        QVERIFY(ok(a.sync()));
        QByteArray stored = *server.bytes(serverPathOf(u"film.mov"_s));
        if (cut)
            stored.chop(500 + encryption::TagSize);
        else
            stored[encryption::HeaderSize + 70000] = char(stored[encryption::HeaderSize + 70000] ^ 1);
        server.putFile(serverPathOf(u"film.mov"_s), stored);
        EncryptedDevice b(server, u"Telefon"_s);
        b.device.engine->setStreamAbove(0);
        const SyncReport report = b.sync();
        QVERIFY(ok(report));
        QCOMPARE(report.errors.size(), 1);
        QCOMPARE(report.errors.first().path, u"film.mov"_s);
        QCOMPARE(b.device.files().keys(), QStringList{u"notatka.md"_s});
        QCOMPARE(b.device.trash(), QStringList()); // (no partial file anywhere)
    }

    void aListingIsRewritten()
    {
        const encryption::Keys keys = encryption::Keys::fromNotebookKey(notebookKey());
        const QString base = u"/remote.php/dav/files/jan/Notatki"_s;
        auto resource = [](const QString &href, bool dir, qint64 size) {
            brulion::sync::DavResource r;
            r.href = href;
            r.isCollection = dir;
            r.size = size;
            r.etag = u"e1"_s;
            return r;
        };
        Canned inner;
        inner.answer.status = 207;
        inner.answer.body = brulion::sync::writeMultistatus({
                resource(base + u"/"_s, true, 0),
                resource(base + u"/"_s + *encryption::encryptName(keys, u"Łódź"_s) + u"/"_s, true, 0),
                resource(base + u"/"_s + *encryption::encryptName(keys, u"a.md"_s), false, encryption::encryptedSize(5)),
                resource(base + u"/"_s + *encryption::encryptName(keys, u"zły rozmiar.md"_s), false, 47), // no file has it
                resource(base + u"/README.txt"_s, false, 10),
                resource(u"/remote.php/dav/files/jan/Inny/x"_s, false, 49),                              // outside
        });
        brulion::sync::EncryptingTransport transport(&inner, kBase, encryption::Keys::fromNotebookKey(notebookKey()));
        const HttpResponse answer = ask(transport, requestFor("PROPFIND", QString()));
        QCOMPARE(answer.status, 207);
        const brulion::sync::MultistatusResult listed = brulion::sync::parseMultistatus(answer.body);
        QVERIFY(listed.ok());
        QStringList hrefs;
        for (const brulion::sync::DavResource &r : listed.resources)
            hrefs.append(r.href + u' ' + QString::number(r.size));
        QCOMPARE(hrefs, (QStringList{base + u"/ 0"_s, base + u"/%C5%81%C3%B3d%C5%BA/ 0"_s, base + u"/a.md 5"_s}));
        // Answers that are not listings go back as they came.
        inner.answer = {};
        inner.answer.status = 404;
        QCOMPARE(ask(transport, requestFor("PROPFIND", u"Brak"_s)).status, 404);
        inner.answer.status = 207;
        inner.answer.body = "<html>login</html>";
        QCOMPARE(ask(transport, requestFor("PROPFIND", QString())).body, QByteArray("<html>login</html>"));
    }

    void requestLimitsAndRefusals()
    {
        Canned inner;
        inner.answer.status = 201;
        brulion::sync::EncryptingTransport transport(&inner, kBase, encryption::Keys::fromNotebookKey(notebookKey()));
        HttpResponse refusal;
        HttpRequest get = requestFor("GET", u"a.md"_s);
        get.maxBodyBytes = 1000;
        QCOMPARE(transport.encrypted(get, &refusal)->maxBodyBytes, encryption::encryptedSize(1000));
        HttpRequest list = requestFor("PROPFIND", QString());
        list.maxBodyBytes = 1000;
        QCOMPARE(transport.encrypted(list, &refusal)->maxBodyBytes, 3000);
        // A MOVE whose destination cannot be encrypted goes nowhere.
        HttpRequest move = requestFor("MOVE", u"a.md"_s);
        move.headers = {{"Destination", (kBase + u'/' + QString(150, u'x')).toUtf8()}};
        const HttpResponse moved = ask(transport, move);
        QCOMPARE(moved.status, 400);
        QVERIFY(moved.reason.contains(u"too long"_s));
        QVERIFY(inner.sent.isEmpty());
        // A DELETE goes with its path encrypted, its answer as it came.
        QCOMPARE(ask(transport, requestFor("DELETE", u"a.md"_s)).status, 201);
        QVERIFY(!inner.sent.last().url.toString().contains(u"a.md"_s));
        // No transport behind it: a network error.
        brulion::sync::EncryptingTransport alone(nullptr, kBase, encryption::Keys::fromNotebookKey(notebookKey()));
        QVERIFY(!ask(alone, requestFor("GET", u"a.md"_s)).networkError.isEmpty());
    }

    void streamedDownloadsRefusedOrFailed()
    {
        const encryption::Keys keys = encryption::Keys::fromNotebookKey(notebookKey());
        Canned inner;
        inner.streaming = true;
        inner.answer.status = 200;
        inner.answer.body = encryption::seal(keys, pattern(5000, 9));
        brulion::sync::EncryptingTransport transport(&inner, kBase, encryption::Keys::fromNotebookKey(notebookKey()));
        QVERIFY(transport.streams());
        // The client's sink refuses a piece: the download stops.
        HttpRequest get = requestFor("GET", u"a.md"_s);
        int pieces = 0;
        get.bodySink = [&pieces](QByteArrayView) { return ++pieces < 1; };
        QVERIFY(!ask(transport, get).networkError.isEmpty());
        // A failed answer goes back as it came, nothing handed to the sink.
        inner.answer = {};
        inner.answer.status = 500;
        QByteArray received;
        get.bodySink = [&received](QByteArrayView piece) { received.append(piece); return true; };
        QCOMPARE(ask(transport, get).status, 500);
        QVERIFY(received.isEmpty());
        // A whole download of a failed answer, too.
        get.bodySink = nullptr;
        QCOMPARE(ask(transport, get).status, 500);
    }
};

QTEST_GUILESS_MAIN(TestSyncEncrypted)
#include "tst_sync_encrypted.moc"

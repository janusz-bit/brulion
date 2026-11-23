// WebDavClient against the in-memory server (listing, transfers, preconditions, errors,
// timeouts) and NetworkHttpTransport against a minimal local HTTP server.
#include "fakewebdavserver.h"
#include "synctest.h"

#include "log/redact.h"
#include "sync/http.h"
#include "sync/webdavclient.h"
#include "text/text.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimeZone>
#include <QTimer>

#include <limits>
#include <memory>
#include <optional>

using namespace synctest;

namespace {

/// A download expected to be `size` bytes.
GetOptions sized(qint64 size)
{
    GetOptions options;
    options.size = size;
    return options;
}

/// Run an asynchronous operation and wait for its result.
template <typename Result, typename Start>
Result await(Start &&start)
{
    auto result = std::make_shared<std::optional<Result>>();
    start([result](const Result &r) { *result = r; });
    waitFor([result] { return result->has_value(); });
    return result->value_or(Result{});
}

/// A transport that answers every request with a fixed response (optionally synchronously).
class FixedTransport : public HttpTransport {
public:
    HttpResponse answer;
    bool synchronous = false;
    QList<HttpRequest> requests;
    QObject context;

    void send(const HttpRequest &request, HttpCallback done) override
    {
        requests.append(request);
        if (synchronous)
            done(answer);
        else
            QTimer::singleShot(0, &context, [done, r = answer] { done(r); });
    }
};

HttpResponse answer(int status, const QByteArray &body = {})
{
    HttpResponse r;
    r.status = status;
    r.body = body;
    return r;
}

struct Setup {
    explicit Setup(FakeWebDavServer::Options options = {}, const QString &folder = kFolder)
        : server(std::move(options))
        , client(server.url(folder), server.options().username, server.options().password, &server)
    {
        server.mkdir(folder);
    }
    FakeWebDavServer server;
    WebDavClient client;
};

QStringList listing(const ListResult &result)
{
    QStringList out;
    for (const RemoteEntry &e : result.entries)
        out.append((e.isDir ? u"dir:"_s : u"file:"_s) + e.path);
    return out;
}

QStringList requestPaths(const QList<FakeWebDavServer::LoggedRequest> &requests)
{
    QStringList out;
    for (const auto &r : requests)
        out.append(r.path);
    return out;
}

/// A tiny HTTP/1.1 server on 127.0.0.1 for NetworkHttpTransport (one request per connection).
class MiniHttpServer : public QObject {
public:
    struct Request {
        QByteArray method, path, body;
        QHash<QByteArray, QByteArray> headers; ///< lower-case names
    };
    std::function<QByteArray(const Request &)> handler; ///< full raw response, empty = never answer
    QList<Request> requests;

    MiniHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
            }
        });
    }
    quint16 port() const { return m_server.serverPort(); }
    QString url(const QString &path) const { return u"http://127.0.0.1:%1%2"_s.arg(port()).arg(path); }

private:
    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        Request request;
        const QList<QByteArray> lines = buffer.left(end).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        request.method = first.value(0);
        request.path = first.value(1);
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0)
                request.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
        }
        const qsizetype length = request.headers.value("content-length", "0").toLongLong();
        if (buffer.size() < end + 4 + length)
            return; // body not complete yet
        request.body = buffer.mid(end + 4, length);
        m_buffers.remove(socket);
        requests.append(request);
        const QByteArray response = handler ? handler(request) : QByteArray();
        if (response.isEmpty())
            return; // stall
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

QByteArray rawResponse(int status, const QByteArray &reason, const QByteArray &body,
                       const QList<QPair<QByteArray, QByteArray>> &headers = {})
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    for (const auto &[name, value] : headers)
        out += name + ": " + value + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    return out;
}

/// A device of `size` bytes of a fixed pattern that counts how it is read: the largest piece asked
/// for, and the bytes handed out (an upload read as it is sent, never held whole).
class CountingDevice : public QIODevice {
public:
    explicit CountingDevice(qint64 size) : m_size(size) { open(QIODevice::ReadOnly); }
    static char byteAt(qint64 at) { return char((at * 7919 + 13) & 0xff); }
    static QByteArray pattern(qint64 size)
    {
        QByteArray out(size, Qt::Uninitialized);
        for (qint64 i = 0; i < size; ++i)
            out[i] = byteAt(i);
        return out;
    }
    qint64 size() const override { return m_size; }
    bool isSequential() const override { return false; }
    qint64 largestRead = 0;
    qint64 handedOut = 0;

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        largestRead = std::max(largestRead, maxSize);
        const qint64 n = std::min(maxSize, m_size - pos());
        for (qint64 i = 0; i < n; ++i)
            data[i] = byteAt(pos() + i);
        handedOut += std::max<qint64>(n, 0);
        return std::max<qint64>(n, 0);
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    qint64 m_size;
};

} // namespace

class TestSyncWebDav : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- test() --------------------------------------------------------------------------------
    void testSucceedsWithDepthZero()
    {
        Setup s;
        const WebDavTestResult r = await<WebDavTestResult>([&](auto cb) { s.client.test(cb); });
        QVERIFY(r.ok);
        QVERIFY(!r.error.isError());
        const auto propfinds = s.server.requests("PROPFIND");
        QCOMPARE(propfinds.size(), 1);
        QCOMPARE(propfinds.first().header("depth"), QByteArray("0"));
        QCOMPARE(propfinds.first().path, u"/remote.php/dav/files/jan/Notatki"_s);
    }

    void testReportsWrongCredentials()
    {
        Setup s;
        WebDavClient bad(s.server.url(kFolder), u"jan"_s, u"złe-hasło"_s, &s.server);
        const WebDavTestResult r = await<WebDavTestResult>([&](auto cb) { bad.test(cb); });
        QVERIFY(!r.ok);
        QVERIFY(r.error.is(WebDavErrorCode::Auth));
        QCOMPARE(r.error.status, 401);
    }

    void testReportsForbiddenAndNotFound()
    {
        Setup s;
        s.server.hooks.append([](const HttpRequest &, const QString &) { return FakeWebDavServer::status(403); });
        const WebDavTestResult forbidden = await<WebDavTestResult>([&](auto cb) { s.client.test(cb); });
        QVERIFY(forbidden.error.is(WebDavErrorCode::Forbidden));
        QCOMPARE(forbidden.error.status, 403);

        FakeWebDavServer other;
        WebDavClient missing(other.url(u"Brak"_s), u"jan"_s, other.options().password, &other);
        const WebDavTestResult notFound = await<WebDavTestResult>([&](auto cb) { missing.test(cb); });
        QVERIFY(notFound.error.is(WebDavErrorCode::NotFound));
        QCOMPARE(notFound.error.status, 404);
    }

    void testReportsNetworkFailures()
    {
        FixedTransport transport;
        transport.answer.networkError = u"Connection refused"_s;
        WebDavClient c(u"https://h/dav"_s, u"a"_s, u"b"_s, &transport);
        const WebDavTestResult r = await<WebDavTestResult>([&](auto cb) { c.test(cb); });
        QVERIFY(!r.ok);
        QVERIFY(r.error.is(WebDavErrorCode::Network));
        QCOMPARE(r.error.status, 0);
        QVERIFY(r.error.message.contains(u"Connection refused"_s));
        QVERIFY(r.error.isFatal());
    }

    void testReportsHtmlLoginPagesAsParseErrors()
    {
        FixedTransport transport;
        transport.answer = answer(207, "<html>login</html>");
        WebDavClient c(u"https://h/dav"_s, u"a"_s, u"b"_s, &transport);
        const WebDavTestResult r = await<WebDavTestResult>([&](auto cb) { c.test(cb); });
        QVERIFY(r.error.is(WebDavErrorCode::Parse));
    }

    void sendsAuthorizationAndXRequestedWith()
    {
        Setup s;
        await<WebDavTestResult>([&](auto cb) { s.client.test(cb); });
        const FakeWebDavServer::LoggedRequest request = s.server.requests().first();
        QCOMPARE(request.header("authorization"), "Basic " + u"jan:hasło-źdźbło"_s.toUtf8().toBase64());
        QCOMPARE(request.header("x-requested-with"), QByteArray("XMLHttpRequest"));
    }

    void callbacksNeverRunInsideTheCall()
    {
        FixedTransport transport;
        transport.synchronous = true;
        transport.answer = answer(207, "<d:multistatus xmlns:d=\"DAV:\"/>");
        WebDavClient c(u"https://h/dav"_s, u"a"_s, u"b"_s, &transport);
        bool called = false;
        c.test([&](const WebDavTestResult &r) {
            called = true;
            QVERIFY(r.ok);
        });
        QVERIFY(!called);
        QTRY_VERIFY(called);
        // Errors detected before any request are delivered later too.
        bool failed = false;
        c.put(QString(), "x", {}, [&](const PutResult &r) { failed = r.error.isError(); });
        QVERIFY(!failed);
        QTRY_VERIFY(failed);
    }

    void callbacksAreDroppedWhenTheClientIsGone()
    {
        Setup s;
        s.server.setLatency("PROPFIND", 20);
        auto client = std::make_unique<WebDavClient>(s.server.url(kFolder), u"jan"_s, s.server.options().password, &s.server);
        bool called = false;
        client->test([&](const WebDavTestResult &) { called = true; });
        client.reset();
        QTest::qWait(60);
        QVERIFY(!called);
    }

    // --- list() --------------------------------------------------------------------------------
    void listsRecursivelyWithDepthOne()
    {
        Setup s;
        s.server.putText(u"Notatki/Strona.md"_s, u"abc"_s);
        s.server.putText(u"Notatki/journals/2026-10-14.md"_s, u"dziennik"_s);
        s.server.putText(u"Notatki/Zażółć gęślą jaźń/notatka #1?.md"_s, u"ą"_s);
        s.server.putText(u"Notatki/a/b/c/głęboko.md"_s, u"x"_s);
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QVERIFY(!r.error.isError());
        QCOMPARE(listing(r), (QStringList{
                                 u"file:Strona.md"_s,
                                 u"dir:Zażółć gęślą jaźń"_s,
                                 u"file:Zażółć gęślą jaźń/notatka #1?.md"_s,
                                 u"dir:a"_s,
                                 u"dir:a/b"_s,
                                 u"dir:a/b/c"_s,
                                 u"file:a/b/c/głęboko.md"_s,
                                 u"dir:journals"_s,
                                 u"file:journals/2026-10-14.md"_s,
                             }));
        for (const auto &request : s.server.requests("PROPFIND"))
            QCOMPARE(request.header("depth"), QByteArray("1"));
        const RemoteEntry file = r.entries.first();
        const qint64 eight = QDateTime(QDate(2026, 10, 14), QTime(8, 0), QTimeZone::UTC).toMSecsSinceEpoch();
        QCOMPARE(file, (RemoteEntry{u"Strona.md"_s, false, s.server.etag(u"Notatki/Strona.md"_s), eight, 3, {}}));
    }

    void listAndStatCarryTheFileIds_data()
    {
        QTest::addColumn<bool>("fileIds");
        QTest::addColumn<bool>("unknown");
        QTest::newRow("Nextcloud") << true << false;
        QTest::newRow("a server without them (404 propstat)") << false << true;
        QTest::newRow("a server that ignores them") << false << false;
    }

    void listAndStatCarryTheFileIds()
    {
        QFETCH(bool, fileIds);
        QFETCH(bool, unknown);
        FakeWebDavServer::Options options;
        options.fileIds = fileIds;
        options.fileIdsUnknown = unknown;
        Setup s(options);
        s.server.putText(u"Notatki/Łódź/żółw.md"_s, u"1"_s);
        const auto expected = [&](const QString &rel) { return fileIds ? s.server.fileId(rel) : QString(); };
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QVERIFY(!r.error.isError());
        QCOMPARE(r.entries.size(), 2);
        QCOMPARE(r.entries.at(0).fileId, expected(u"Notatki/Łódź"_s));
        QCOMPARE(r.entries.at(1).fileId, expected(u"Notatki/Łódź/żółw.md"_s));
        QCOMPARE(r.entries.at(1).etag, s.server.etag(u"Notatki/Łódź/żółw.md"_s));
        if (fileIds)
            QVERIFY(r.entries.at(0).fileId != r.entries.at(1).fileId);
        const StatResult file = await<StatResult>([&](auto cb) { s.client.stat(u"Łódź/żółw.md"_s, cb); });
        QVERIFY(file.entry);
        QCOMPARE(file.entry->fileId, expected(u"Notatki/Łódź/żółw.md"_s));
        // Kept when the file is overwritten and moved, like on Nextcloud.
        const QString id = s.server.fileId(u"Notatki/Łódź/żółw.md"_s);
        s.server.putText(u"Notatki/Łódź/żółw.md"_s, u"2"_s);
        QVERIFY(s.server.moveNode(u"Notatki/Łódź"_s, u"Notatki/Kraków"_s));
        QCOMPARE(s.server.fileId(u"Notatki/Kraków/żółw.md"_s), id);
    }

    void listWorksWithOtherHrefStylesAndPrefixes_data()
    {
        QTest::addColumn<bool>("fullUrl");
        QTest::addColumn<QString>("prefix");
        QTest::newRow("full URLs") << true << u"d"_s;
        QTest::newRow("D: prefix") << false << u"D"_s;
        QTest::newRow("default namespace") << false << QString();
    }
    void listWorksWithOtherHrefStylesAndPrefixes()
    {
        QFETCH(bool, fullUrl);
        QFETCH(QString, prefix);
        FakeWebDavServer::Options options;
        options.fullUrlHrefs = fullUrl;
        options.prefix = prefix;
        Setup s(options);
        s.server.putText(u"Notatki/x/ż.md"_s, u"1"_s);
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QCOMPARE(listing(r), (QStringList{u"dir:x"_s, u"file:x/ż.md"_s}));
    }

    void listRespectsTheConcurrencyLimit()
    {
        Setup s;
        for (const QString &folder : {u"a"_s, u"b"_s, u"c"_s, u"d"_s, u"e"_s, u"f"_s, u"ż"_s})
            s.server.putText(u"Notatki/"_s + folder + u"/x.md"_s, u"1"_s);
        s.server.setLatency("PROPFIND", 5);
        s.client.setListConcurrency(2);
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QCOMPARE(r.entries.size(), 14);
        QCOMPARE(s.server.peakInFlight("PROPFIND"), 2);
    }

    void listDoesNotDescendIntoSkippedFolders()
    {
        Setup s;
        s.server.putText(u"Notatki/.git/objects/aa"_s, u"x"_s);
        s.server.putText(u"Notatki/a.md"_s, u"x"_s);
        ListOptions options;
        options.skipDir = [](const VaultPath &p) { return p == u".git"_s; };
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list(options, cb); });
        QCOMPARE(listing(r), (QStringList{u"dir:.git"_s, u"file:a.md"_s}));
        for (const auto &request : s.server.requests("PROPFIND"))
            QVERIFY(!request.path.endsWith(u".git"_s));
    }

    void listOfAMissingBaseIsNotFound()
    {
        FakeWebDavServer server;
        WebDavClient c(server.url(u"Brak"_s), u"jan"_s, server.options().password, &server);
        const ListResult r = await<ListResult>([&](auto cb) { c.list({}, cb); });
        QVERIFY(r.error.is(WebDavErrorCode::NotFound));
        QVERIFY(r.entries.isEmpty());
    }

    void listSkipsASubFolderDeletedMeanwhile()
    {
        Setup s;
        s.server.putText(u"Notatki/a/x.md"_s, u"1"_s);
        s.server.putText(u"Notatki/b.md"_s, u"2"_s);
        s.server.hooks.append([](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (request.method == "PROPFIND" && path.endsWith(u"/a"_s))
                return FakeWebDavServer::status(404);
            return std::nullopt;
        });
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QVERIFY(!r.error.isError());
        QCOMPARE(listing(r), (QStringList{u"dir:a"_s, u"file:b.md"_s}));
    }

    void listFailsOnOtherSubFolderErrors()
    {
        Setup s;
        s.server.putText(u"Notatki/a/x.md"_s, u"1"_s);
        s.server.hooks.append([](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (request.method == "PROPFIND" && path.endsWith(u"/a"_s))
                return FakeWebDavServer::status(500);
            return std::nullopt;
        });
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QVERIFY(r.error.is(WebDavErrorCode::Server));
        QVERIFY(r.entries.isEmpty());
    }

    void listStopsWhenCancelled()
    {
        Setup s;
        s.server.putText(u"Notatki/a/x.md"_s, u"1"_s);
        ListOptions options;
        options.cancelled = [] { return true; };
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list(options, cb); });
        QVERIFY(r.error.is(WebDavErrorCode::Cancelled));
        QVERIFY(s.server.requests("PROPFIND").isEmpty());
    }

    void listGivesUpOnAServerWithoutAnEnd()
    {
        // A server with one more folder in every folder: the listing ends all the same.
        class EndlessTransport : public HttpTransport {
        public:
            int requests = 0;
            QObject context;
            void send(const HttpRequest &request, HttpCallback done) override
            {
                ++requests;
                const auto folder = [](const QByteArray &href) {
                    return "<d:response><d:href>" + href
                           + "</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop>"
                             "</d:propstat></d:response>";
                };
                const QByteArray self = request.url.path(QUrl::FullyEncoded).toUtf8();
                HttpResponse r = answer(207, "<d:multistatus xmlns:d=\"DAV:\">" + folder(self)
                                                     + folder(self + "g%C5%82%C4%85b/") + "</d:multistatus>");
                QTimer::singleShot(0, &context, [done, r] { done(r); });
            }
        };
        EndlessTransport transport;
        WebDavClient client(u"https://example.org/dav/Notatki"_s, u"jan"_s, u"x"_s, &transport);
        const ListResult r = await<ListResult>([&](auto cb) { client.list({}, cb); });
        QVERIFY(r.error.is(WebDavErrorCode::Oversized));
        QVERIFY(!r.error.isFatal());
        QVERIFY2(r.error.message.contains(u"64 deep"_s) && r.error.message.contains(u"głąb/głąb"_s),
                 qPrintable(r.error.message));
        QVERIFY(r.entries.isEmpty());
        QCOMPARE(transport.requests, client.listLimits().maxDepth);
    }

    void listGivesUpAboveItsLimits_data()
    {
        QTest::addColumn<int>("maxDepth");
        QTest::addColumn<int>("maxEntries");
        QTest::addColumn<QString>("problem"); // "" = listed
        // On the server: a/b/c/głęboko.md and nine files beside the folder a — 13 entries, 4 deep.
        QTest::newRow("defaults") << 64 << 500'000 << QString();
        QTest::newRow("as deep and as many as allowed") << 4 << 13 << QString();
        QTest::newRow("a folder too deep") << 3 << 13 << u"folders more than 3 deep (a/b/c)"_s;
        QTest::newRow("one entry too many") << 4 << 12 << u"more than 12 files and folders"_s;
        QTest::newRow("limits below one are one") << 0 << 0 << u"more than 1 "_s; // (deep, or files and folders)
    }
    void listGivesUpAboveItsLimits()
    {
        QFETCH(int, maxDepth);
        QFETCH(int, maxEntries);
        QFETCH(QString, problem);
        Setup s;
        s.server.putText(u"Notatki/a/b/c/głęboko.md"_s, u"1"_s);
        for (int i = 0; i < 9; ++i)
            s.server.putText(u"Notatki/plik%1.md"_s.arg(i), u"x"_s);
        s.client.setListLimits(ListLimits{maxDepth, maxEntries});
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        if (problem.isEmpty()) {
            QVERIFY2(!r.error.isError(), qPrintable(r.error.message));
            QCOMPARE(r.entries.size(), 13);
            return;
        }
        QVERIFY(r.error.is(WebDavErrorCode::Oversized));
        QVERIFY2(r.error.message.contains(problem), qPrintable(r.error.message));
        QVERIFY(r.entries.isEmpty());
    }

    void listFailsOnAFolderAnswerWithoutAnEnd()
    {
        // (The limit of a folder's answer is 128 MiB: the transport reports what the network one
        // does when the server sends more.)
        FixedTransport transport;
        transport.answer.tooLarge = true;
        transport.answer.networkError = u"The answer to PROPFIND … is larger than 134217728 bytes"_s;
        WebDavClient client(u"https://example.org/dav/Notatki"_s, u"jan"_s, u"x"_s, &transport);
        const ListResult r = await<ListResult>([&](auto cb) { client.list({}, cb); });
        QVERIFY(r.error.is(WebDavErrorCode::Oversized));
        QCOMPARE(transport.requests.size(), 1);
        QCOMPARE(transport.requests.first().maxBodyBytes, qint64(128) * 1024 * 1024);
    }

    void listIgnoresHrefsOutsideTheBase()
    {
        FixedTransport transport;
        transport.answer = answer(207, "<d:multistatus xmlns:d=\"DAV:\">\n"
        "<d:response><d:href>/dav/N/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response>\n"
        "<d:response><d:href>/elsewhere/x.md</d:href><d:propstat><d:prop><d:resourcetype/></d:prop></d:propstat></d:response>\n"
        "<d:response><d:href>/dav/N/../escape.md</d:href><d:propstat><d:prop><d:resourcetype/></d:prop></d:propstat></d:response>\n"
        "<d:response><d:href>y.md</d:href><d:propstat><d:prop><d:resourcetype/><d:getetag>\"1\"</d:getetag></d:prop></d:propstat></d:response>\n"
        "</d:multistatus>");
        WebDavClient c(u"https://h/dav/N"_s, u"a"_s, u"b"_s, &transport);
        const ListResult r = await<ListResult>([&](auto cb) { c.list({}, cb); });
        QCOMPARE(r.entries, (QList<RemoteEntry>{RemoteEntry{u"y.md"_s, false, u"1"_s, 0, 0, {}}}));
    }

    void keepsTheServerSpellingOfNfdNames()
    {
        Setup s;
        const QString nfd = u"Zażółć.md"_s.normalized(QString::NormalizationForm_D);
        s.server.putText(u"Notatki/"_s + nfd, u"treść"_s);
        const ListResult r = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QCOMPARE(listing(r), QStringList{u"file:Zażółć.md"_s});
        const GetResult got = await<GetResult>([&](auto cb) { s.client.get(u"Zażółć.md"_s, {}, cb); });
        QVERIFY(!got.error.isError());
        QCOMPARE(QString::fromUtf8(got.data), u"treść"_s);
        // Uploads go to the same (NFD) file instead of creating an NFC twin.
        const PutResult put = await<PutResult>([&](auto cb) { s.client.put(u"Zażółć.md"_s, "nowa", {}, cb); });
        QVERIFY(!put.error.isError());
        QCOMPARE(s.server.text(u"Notatki/"_s + nfd), std::optional<QString>(u"nowa"_s));
        QCOMPARE(s.server.files(kFolder).size(), 1);
    }

    void newFilesGoIntoFoldersTheServerSpellsInNfd()
    {
        // A macOS client created "Źródła" in NFD; a new note in it (NFC locally) must land in that
        // folder, not fail with 409 (or create an NFC twin) on a normalisation-sensitive server.
        Setup s;
        const QString nfdDir = u"Źródła"_s.normalized(QString::NormalizationForm_D);
        QVERIFY(nfdDir != u"Źródła"_s);
        s.server.putText(u"Notatki/"_s + nfdDir + u"/stary.md"_s, u"stary"_s);
        const ListResult listed = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QCOMPARE(listing(listed), (QStringList{u"dir:Źródła"_s, u"file:Źródła/stary.md"_s}));

        const PutResult put = await<PutResult>([&](auto cb) { s.client.put(u"Źródła/nowy.md"_s, "nowy", {}, cb); });
        QVERIFY2(!put.error.isError(), qPrintable(put.error.message));
        QCOMPARE(s.server.text(u"Notatki/"_s + nfdDir + u"/nowy.md"_s), std::optional<QString>(u"nowy"_s));
        // A new sub-folder below it, too.
        const PutResult nested =
            await<PutResult>([&](auto cb) { s.client.put(u"Źródła/Łąka/głęboki.md"_s, "g", {}, cb); });
        QVERIFY2(!nested.error.isError(), qPrintable(nested.error.message));
        QCOMPARE(s.server.text(u"Notatki/"_s + nfdDir + u"/Łąka/głęboki.md"_s), std::optional<QString>(u"g"_s));
        QVERIFY(!s.server.isDir(u"Notatki/Źródła"_s)); // no NFC twin of the folder
        // stat, get and remove of the new file use the folder's server spelling as well.
        const StatResult st = await<StatResult>([&](auto cb) { s.client.stat(u"Źródła/nowy.md"_s, cb); });
        QVERIFY(st.entry);
        const GetResult got = await<GetResult>([&](auto cb) { s.client.get(u"Źródła/nowy.md"_s, {}, cb); });
        QCOMPARE(got.data, QByteArray("nowy"));
        const WebDavError removed =
            await<WebDavError>([&](auto cb) { s.client.remove(u"Źródła/nowy.md"_s, {}, cb); });
        QVERIFY(!removed.isError());
        QVERIFY(!s.server.text(u"Notatki/"_s + nfdDir + u"/nowy.md"_s));
    }

    void hugeTimeoutsDoNotOverflow()
    {
        FixedTransport transport;
        transport.answer = answer(200, "x");
        WebDavClient c(u"https://h/dav"_s, u"a"_s, u"b"_s, &transport);
        c.setTimeout(std::numeric_limits<int>::max() / 2);
        const GetResult r = await<GetResult>([&](auto cb) { c.get(u"a.md"_s, {}, cb); });
        QVERIFY(!r.error.isError());
        QCOMPARE(transport.requests.size(), 1);
        // Ten times the base timeout for an unknown size, clamped instead of wrapping around.
        QCOMPARE(transport.requests.first().timeoutMs, std::numeric_limits<int>::max());
    }

    // --- file operations -------------------------------------------------------------------------
    void getReturnsBytesAndEtag()
    {
        Setup s;
        s.server.putText(u"Notatki/a.md"_s, u"zażółć"_s);
        const GetResult r = await<GetResult>([&](auto cb) { s.client.get(u"a.md"_s, {}, cb); });
        QCOMPARE(QString::fromUtf8(r.data), u"zażółć"_s);
        QCOMPARE(r.etag, s.server.etag(u"Notatki/a.md"_s));
        const GetResult missing = await<GetResult>([&](auto cb) { s.client.get(u"brak.md"_s, {}, cb); });
        QVERIFY(missing.error.is(WebDavErrorCode::NotFound));
    }

    void getStopsAtWhatTheFileMayHold()
    {
        Setup s;
        const qsizetype mebibyte = 1024 * 1024;
        s.server.putFile(u"Notatki/duży.bin"_s, QByteArray(3 * mebibyte, 'x'));
        // Listed with ten bytes, sent with three mebibytes: more than a file grows meanwhile.
        const GetResult refused = await<GetResult>([&](auto cb) { s.client.get(u"duży.bin"_s, sized(10), cb); });
        QVERIFY(refused.error.is(WebDavErrorCode::Oversized));
        QVERIFY(!refused.error.isFatal());
        QVERIFY(refused.data.isEmpty());
        QVERIFY2(refused.error.message.contains(u"larger than 1048586 bytes"_s), qPrintable(refused.error.message));
        // Twice the expected size is what a file may have grown to.
        const GetResult grown =
                await<GetResult>([&](auto cb) { s.client.get(u"duży.bin"_s, sized(2 * mebibyte), cb); });
        QVERIFY2(!grown.error.isError(), qPrintable(grown.error.message));
        QCOMPARE(grown.data.size(), 3 * mebibyte);
        // Of unknown size: whatever fits the default.
        const GetResult unknown = await<GetResult>([&](auto cb) { s.client.get(u"duży.bin"_s, {}, cb); });
        QCOMPARE(unknown.data.size(), 3 * mebibyte);
    }

    void downloadLimits_data()
    {
        QTest::addColumn<qint64>("size"); // -1: unknown
        QTest::addColumn<qint64>("limit");
        const qint64 mebibyte = 1024 * 1024;
        const qint64 most = std::numeric_limits<qint64>::max();
        QTest::newRow("unknown") << qint64(-1) << 256 * mebibyte;
        QTest::newRow("empty") << qint64(0) << mebibyte;
        QTest::newRow("a note") << qint64(2000) << mebibyte + 2000;
        QTest::newRow("a mebibyte") << mebibyte << 2 * mebibyte;
        QTest::newRow("a film") << 150 * mebibyte << 300 * mebibyte;
        QTest::newRow("above the default") << 300 * mebibyte << 600 * mebibyte;
        QTest::newRow("no size a file has") << most << most;
        QTest::newRow("half of that") << most / 2 + 1 << most;
    }
    void downloadLimits()
    {
        QFETCH(qint64, size);
        QFETCH(qint64, limit);
        QCOMPARE(WebDavClient::maxDownloadBytes(size < 0 ? std::nullopt : std::optional<qint64>(size)), limit);
        if (size < 0)
            QCOMPARE(WebDavClient::maxDownloadBytes(std::optional<qint64>(-5)), limit); // (a size no server means)
    }

    void answersToChangesAreSmall()
    {
        // PUT, DELETE, MKCOL and MOVE are answered with a status and, at most, an error page.
        Setup s;
        s.server.putText(u"Notatki/a.md"_s, u"1"_s);
        s.server.hooks.append([](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
            if (request.method == "PROPFIND" || request.method == "GET")
                return std::nullopt;
            if (request.maxBodyBytes != 1024 * 1024)
                return FakeWebDavServer::status(599);
            HttpResponse r = FakeWebDavServer::status(500);
            r.body = QByteArray(2 * 1024 * 1024, 'e'); // an error page without an end
            return r;
        });
        const PutResult put = await<PutResult>([&](auto cb) { s.client.put(u"b.md"_s, "x", {}, cb); });
        QVERIFY2(put.error.is(WebDavErrorCode::Oversized), qPrintable(put.error.message));
        const WebDavError removed = await<WebDavError>([&](auto cb) { s.client.remove(u"a.md"_s, {}, cb); });
        QVERIFY2(removed.is(WebDavErrorCode::Oversized), qPrintable(removed.message));
        const WebDavError made = await<WebDavError>([&](auto cb) { s.client.mkcol(u"nowy"_s, cb); });
        QVERIFY2(made.is(WebDavErrorCode::Oversized), qPrintable(made.message));
        const WebDavError moved = await<WebDavError>([&](auto cb) { s.client.move(u"a.md"_s, u"c.md"_s, {}, cb); });
        QVERIFY2(moved.is(WebDavErrorCode::Oversized), qPrintable(moved.message));
    }

    void putCreatesMissingParentsOnce()
    {
        Setup s;
        const PutResult r = await<PutResult>([&](auto cb) { s.client.put(u"a/b/ć.md"_s, "x", {}, cb); });
        QVERIFY(!r.error.isError());
        QCOMPARE(s.server.text(u"Notatki/a/b/ć.md"_s), std::optional<QString>(u"x"_s));
        QCOMPARE(r.etag, s.server.etag(u"Notatki/a/b/ć.md"_s));
        QCOMPARE(requestPaths(s.server.requests("MKCOL")),
                 (QStringList{u"/remote.php/dav/files/jan/Notatki/a"_s, u"/remote.php/dav/files/jan/Notatki/a/b"_s}));
        await<PutResult>([&](auto cb) { s.client.put(u"a/b/d.md"_s, "y", {}, cb); });
        QCOMPARE(s.server.requests("MKCOL").size(), 2);
    }

    void putAcceptsExistingFoldersItDidNotKnow()
    {
        Setup s;
        s.server.putText(u"Notatki/istniejący/x.md"_s, u"1"_s);
        const PutResult r = await<PutResult>([&](auto cb) { s.client.put(u"istniejący/y.md"_s, "2", {}, cb); });
        QVERIFY(!r.error.isError());
        QCOMPARE(s.server.text(u"Notatki/istniejący/y.md"_s), std::optional<QString>(u"2"_s));
    }

    void putRecreatesFoldersDeletedBehindItsBack()
    {
        Setup s;
        s.server.putText(u"Notatki/f/x.md"_s, u"1"_s);
        await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        s.server.remove(u"Notatki/f"_s);
        const PutResult r = await<PutResult>([&](auto cb) { s.client.put(u"f/y.md"_s, "2", {}, cb); });
        QVERIFY(!r.error.isError());
        QCOMPARE(s.server.text(u"Notatki/f/y.md"_s), std::optional<QString>(u"2"_s));
    }

    void putRecreatesNestedFoldersWhenAnAncestorVanished()
    {
        Setup s;
        s.server.putText(u"Notatki/a/b/x.md"_s, u"1"_s);
        await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        s.server.remove(u"Notatki/a"_s);
        const PutResult r = await<PutResult>([&](auto cb) { s.client.put(u"a/b/c/y.md"_s, "2", {}, cb); });
        QVERIFY2(!r.error.isError(), qPrintable(r.error.message));
        QCOMPARE(s.server.text(u"Notatki/a/b/c/y.md"_s), std::optional<QString>(u"2"_s));
    }

    void putHonoursPreconditions()
    {
        Setup s;
        s.server.putText(u"Notatki/a.md"_s, u"v1"_s);
        const QString etag = s.server.etag(u"Notatki/a.md"_s);
        PutOptions ifMatch;
        ifMatch.ifMatch = etag;
        QVERIFY(!await<PutResult>([&](auto cb) { s.client.put(u"a.md"_s, "v2", ifMatch, cb); }).error.isError());
        QCOMPARE(s.server.text(u"Notatki/a.md"_s), std::optional<QString>(u"v2"_s));
        const PutResult stale = await<PutResult>([&](auto cb) { s.client.put(u"a.md"_s, "v3", ifMatch, cb); });
        QVERIFY(stale.error.is(WebDavErrorCode::Precondition));
        QCOMPARE(stale.error.status, 412);
        QCOMPARE(s.server.text(u"Notatki/a.md"_s), std::optional<QString>(u"v2"_s));
        PutOptions create;
        create.ifNoneMatch = true;
        QVERIFY(await<PutResult>([&](auto cb) { s.client.put(u"a.md"_s, "v4", create, cb); }).error.is(WebDavErrorCode::Precondition));
        QVERIFY(!await<PutResult>([&](auto cb) { s.client.put(u"nowy.md"_s, "n", create, cb); }).error.isError());
        QCOMPARE(s.server.text(u"Notatki/nowy.md"_s), std::optional<QString>(u"n"_s));
        QCOMPARE(s.server.requests("PUT").last().header("if-none-match"), QByteArray("*"));
    }

    void putSendsQuotedIfMatchAndMtime()
    {
        Setup s;
        s.server.putText(u"Notatki/a.md"_s, u"v1"_s);
        const QString etag = s.server.etag(u"Notatki/a.md"_s);
        PutOptions options;
        options.ifMatch = etag;
        options.mtime = 1'760'000'000'500;
        await<PutResult>([&](auto cb) { s.client.put(u"a.md"_s, "v2", options, cb); });
        const auto put = s.server.requests("PUT").first();
        QCOMPARE(put.header("if-match"), QByteArray("\"" + etag.toUtf8() + "\""));
        QVERIFY(QRegularExpression(u"^\"e[0-9a-f]+\"$"_s).match(QString::fromLatin1(put.header("if-match"))).hasMatch());
        QCOMPARE(put.header("x-oc-mtime"), QByteArray("1760000000"));
        QCOMPARE(put.header("content-type"), QByteArray("application/octet-stream"));
    }

    void putWithoutEtagHeader()
    {
        FakeWebDavServer::Options options;
        options.putEtag = false;
        Setup s(options);
        const PutResult r = await<PutResult>([&](auto cb) { s.client.put(u"a.md"_s, "x", {}, cb); });
        QVERIFY(!r.error.isError());
        QVERIFY(r.etag.isEmpty());
        const StatResult st = await<StatResult>([&](auto cb) { s.client.stat(u"a.md"_s, cb); });
        QCOMPARE(st.entry->etag, s.server.etag(u"Notatki/a.md"_s));
    }

    void putRefusesTheBaseFolder()
    {
        Setup s;
        const PutResult r = await<PutResult>([&](auto cb) { s.client.put(u"/"_s, "x", {}, cb); });
        QVERIFY(r.error.isError());
        QVERIFY(s.server.requests("PUT").isEmpty());
    }

    void removeDeletesToleratesMissingAndHonoursIfMatch()
    {
        Setup s;
        s.server.putText(u"Notatki/a.md"_s, u"1"_s);
        s.server.putText(u"Notatki/b.md"_s, u"1"_s);
        QVERIFY(!await<WebDavError>([&](auto cb) { s.client.remove(u"a.md"_s, {}, cb); }).isError());
        QVERIFY(!s.server.text(u"Notatki/a.md"_s));
        QVERIFY(!await<WebDavError>([&](auto cb) { s.client.remove(u"a.md"_s, {}, cb); }).isError());
        RemoveOptions stale;
        stale.ifMatch = u"stare"_s;
        QVERIFY(await<WebDavError>([&](auto cb) { s.client.remove(u"b.md"_s, stale, cb); }).is(WebDavErrorCode::Precondition));
        QCOMPARE(s.server.text(u"Notatki/b.md"_s), std::optional<QString>(u"1"_s));
        const WebDavError base = await<WebDavError>([&](auto cb) { s.client.remove(QString(), {}, cb); });
        QVERIFY(base.isError());
        QVERIFY(base.message.contains(u"base folder"_s));
    }

    void mkcolCreatesAndAcceptsExistingFolders()
    {
        Setup s;
        QVERIFY(!await<WebDavError>([&](auto cb) { s.client.mkcol(u"nowy folder"_s, cb); }).isError());
        QVERIFY(!await<WebDavError>([&](auto cb) { s.client.mkcol(u"nowy folder"_s, cb); }).isError());
        QVERIFY(s.server.isDir(u"Notatki/nowy folder"_s));
        QVERIFY(await<WebDavError>([&](auto cb) { s.client.mkcol(u"a/b"_s, cb); }).is(WebDavErrorCode::Conflict));
    }

    void ensureBaseCreatesANestedBase()
    {
        FakeWebDavServer server;
        WebDavClient client(server.url(u"Dokumenty/Moje notatki/Brulion"_s), u"jan"_s, server.options().password, &server);
        QVERIFY(!await<WebDavError>([&](auto cb) { client.ensureBase(cb); }).isError());
        QVERIFY(server.isDir(u"Dokumenty/Moje notatki/Brulion"_s));
        const ListResult r = await<ListResult>([&](auto cb) { client.list({}, cb); });
        QVERIFY(!r.error.isError());
        QVERIFY(r.entries.isEmpty());
        QVERIFY(!await<WebDavError>([&](auto cb) { client.ensureBase(cb); }).isError());
    }

    void statOfFilesFoldersAndMissingPaths()
    {
        Setup s;
        s.server.putText(u"Notatki/f/a.md"_s, u"123"_s);
        const StatResult missing = await<StatResult>([&](auto cb) { s.client.stat(u"brak.md"_s, cb); });
        QVERIFY(!missing.error.isError());
        QVERIFY(!missing.entry);
        const StatResult folder = await<StatResult>([&](auto cb) { s.client.stat(u"f"_s, cb); });
        QVERIFY(folder.entry && folder.entry->isDir);
        QCOMPARE(folder.entry->path, u"f"_s);
        QCOMPARE(folder.entry->size, 0);
        const StatResult file = await<StatResult>([&](auto cb) { s.client.stat(u"f/a.md"_s, cb); });
        QVERIFY(file.entry && !file.entry->isDir);
        QCOMPARE(file.entry->size, 3);
    }

    void mapsServerErrorsToCodes_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("code");
        QTest::newRow("500") << 500 << int(WebDavErrorCode::Server);
        QTest::newRow("507") << 507 << int(WebDavErrorCode::InsufficientStorage);
        QTest::newRow("423") << 423 << int(WebDavErrorCode::Locked);
        QTest::newRow("413") << 413 << int(WebDavErrorCode::TooLarge);
        QTest::newRow("418") << 418 << int(WebDavErrorCode::Http);
    }
    void mapsServerErrorsToCodes()
    {
        QFETCH(int, status);
        QFETCH(int, code);
        Setup s;
        s.server.hooks.append([status](const HttpRequest &, const QString &) { return FakeWebDavServer::status(status); });
        const GetResult r = await<GetResult>([&](auto cb) { s.client.get(u"a.md"_s, {}, cb); });
        QCOMPARE(int(r.error.code), code);
        QCOMPARE(r.error.status, status);
        QVERIFY(r.error.message.contains(QString::number(status)));
    }

    void identityNamesAccountAndFolder()
    {
        Setup s;
        QCOMPARE(s.client.identity(), u"jan@https://chmura.example.pl/remote.php/dav/files/jan/Notatki"_s);
        WebDavClient trailing(u" https://h/dav/N/ "_s, u"ola"_s, u"x"_s, &s.server);
        QCOMPARE(trailing.identity(), u"ola@https://h/dav/N"_s);
    }

    // --- timeouts ------------------------------------------------------------------------------
    void stalledRequestsTimeOut()
    {
        Setup s;
        s.server.hooks.append([](const HttpRequest &, const QString &) { return FakeWebDavServer::noResponse(); });
        s.client.setTimeout(30);
        const WebDavTestResult t = await<WebDavTestResult>([&](auto cb) { s.client.test(cb); });
        QVERIFY(t.error.is(WebDavErrorCode::Timeout));
        QCOMPARE(t.error.status, 0);
        const ListResult l = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QVERIFY(l.error.is(WebDavErrorCode::Timeout));
        QVERIFY(l.error.message.contains(u"timed out"_s));
        QVERIFY(l.error.isFatal());
        QVERIFY(await<WebDavError>([&](auto cb) { s.client.remove(u"a.md"_s, {}, cb); }).is(WebDavErrorCode::Timeout));
    }

    void transfersGetExtraTimeForTheirSize()
    {
        Setup s;
        s.server.setLatency("PUT", 300);
        s.server.setLatency("GET", 300);
        s.client.setTimeout(100);
        s.client.setMinBytesPerSecond(1'000'000);
        // 400 kB at 1 MB/s adds 400 ms to the 100 ms base: the 300 ms upload succeeds.
        const PutResult big = await<PutResult>([&](auto cb) { s.client.put(u"d/duży.bin"_s, QByteArray(400'000, 'x'), {}, cb); });
        QVERIFY2(!big.error.isError(), qPrintable(big.error.message));
        const PutResult small = await<PutResult>([&](auto cb) { s.client.put(u"mały.bin"_s, "x", {}, cb); });
        QVERIFY(small.error.is(WebDavErrorCode::Timeout));
        GetOptions expected;
        expected.size = 400'000;
        QVERIFY(!await<GetResult>([&](auto cb) { s.client.get(u"d/duży.bin"_s, expected, cb); }).error.isError());
        // Without a size hint a download gets ten times the base timeout (1 s here).
        QVERIFY(!await<GetResult>([&](auto cb) { s.client.get(u"d/duży.bin"_s, {}, cb); }).error.isError());
    }

    void zeroTimeoutDisablesTheLimit()
    {
        Setup s;
        s.server.setLatency("PROPFIND", 50);
        s.client.setTimeout(0);
        QVERIFY(await<WebDavTestResult>([&](auto cb) { s.client.test(cb); }).ok);
    }

    // --- NetworkHttpTransport --------------------------------------------------------------------
    void networkTransportSendsCustomVerbs()
    {
        MiniHttpServer http;
        http.handler = [](const MiniHttpServer::Request &) {
            return rawResponse(207, "Multi-Status", "<d:multistatus xmlns:d=\"DAV:\"/>",
                               {{"Content-Type", "application/xml"}, {"ETag", "\"abc\""}});
        };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "PROPFIND";
        request.url = QUrl(http.url(u"/dav/Zażółć/"_s));
        request.headers = {{"Depth", "1"}, {"X-Test", "ąę"}};
        request.body = "<propfind/>";
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 207);
        QVERIFY(r.networkError.isEmpty());
        QCOMPARE(r.body, QByteArray("<d:multistatus xmlns:d=\"DAV:\"/>"));
        QCOMPARE(r.header("ETag"), QByteArray("\"abc\""));
        QVERIFY(r.headers.contains("content-type")); // names are lower-case
        QCOMPARE(http.requests.size(), 1);
        QCOMPARE(http.requests.first().method, QByteArray("PROPFIND"));
        QCOMPARE(http.requests.first().path, QByteArray("/dav/Za%C5%BC%C3%B3%C5%82%C4%87/"));
        QCOMPARE(http.requests.first().headers.value("depth"), QByteArray("1"));
        QCOMPARE(http.requests.first().body, QByteArray("<propfind/>"));
    }

    void networkTransportReportsHttpErrorsAsAnswers()
    {
        MiniHttpServer http;
        http.handler = [](const MiniHttpServer::Request &) { return rawResponse(404, "Not Found", "brak"); };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "GET";
        request.url = QUrl(http.url(u"/x"_s));
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 404);
        QCOMPARE(r.body, QByteArray("brak"));
        QVERIFY(r.networkError.isEmpty());
    }

    void networkTransportHandsTheBodyOnAsItComes()
    {
        // request.onData: the body as it arrives (a model's answer as server-sent events), all of
        // it in the response too; an error's body is not handed on.
        const QByteArray events = "data: {\"tekst\": \"Zażółć\"}\n\ndata: [DONE]\n\n";
        MiniHttpServer http;
        http.handler = [&](const MiniHttpServer::Request &request) {
            if (request.path == "/brak")
                return rawResponse(404, "Not Found", "brak");
            return rawResponse(200, "OK", events, {{"Content-Type", "text/event-stream"}});
        };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "POST";
        request.url = QUrl(http.url(u"/v1/messages"_s));
        QByteArray handed;
        request.onData = [&](QByteArrayView piece) { handed += piece.toByteArray(); };
        HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 200);
        QCOMPARE(r.body, events);
        QCOMPARE(handed, events);
        handed.clear();
        request.url = QUrl(http.url(u"/brak"_s));
        r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 404);
        QCOMPARE(r.body, QByteArray("brak"));
        QVERIFY(handed.isEmpty());
    }

    void networkTransportStopsARequest()
    {
        // request.abort: a request on its way ends as a broken connection would.
        MiniHttpServer http; // never answers
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "POST";
        request.url = QUrl(http.url(u"/v1/messages"_s));
        request.abort = std::make_shared<HttpAbort>();
        QTimer::singleShot(50, [abort = request.abort] { abort->abort(); });
        QElapsedTimer clock;
        clock.start();
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        // Ended by the abort: before await() gives up on a request that never ends (20 s), with
        // room for a stalled runner.
        QVERIFY2(clock.elapsed() < 15000, qPrintable(QString::number(clock.elapsed())));
        QCOMPARE(r.status, 0);
        QVERIFY(!r.networkError.isEmpty());
        QVERIFY(!r.timedOut);
        QVERIFY(request.abort->aborted());
        request.abort->abort(); // (again, after the end: nothing)
        // Stopped before it was sent: it ends at once.
        request.abort = std::make_shared<HttpAbort>();
        request.abort->abort();
        const HttpResponse early = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(early.status, 0);
        QVERIFY(!early.networkError.isEmpty());
    }

    void networkTransportReportsRefusedConnections()
    {
        quint16 port = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost));
            port = probe.serverPort();
        }
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "GET";
        request.url = QUrl(u"http://127.0.0.1:%1/"_s.arg(port));
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 0);
        QVERIFY(!r.networkError.isEmpty());
        QVERIFY(!r.timedOut);
    }

    void networkTransportTimesOut()
    {
        MiniHttpServer http; // never answers
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "GET";
        request.url = QUrl(http.url(u"/slow"_s));
        request.timeoutMs = 150;
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 0);
        QVERIFY(r.timedOut);
    }

    void networkTransportFollowsOnlySameHostRedirects()
    {
        MiniHttpServer http;
        const quint16 port = http.port();
        http.handler = [port](const MiniHttpServer::Request &request) {
            if (request.path == "/same")
                return rawResponse(307, "Temporary Redirect", {}, {{"Location", "/target"}});
            if (request.path == "/other")
                return rawResponse(307, "Temporary Redirect", {},
                                   {{"Location", QByteArray("http://localhost:" + QByteArray::number(port) + "/target")}});
            return rawResponse(200, "OK", QByteArray("cel:" + request.headers.value("authorization")));
        };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest same;
        same.method = "GET";
        same.url = QUrl(http.url(u"/same"_s));
        same.headers = {{"Authorization", "Basic eA=="}};
        const HttpResponse followed = await<HttpResponse>([&](auto cb) { transport.send(same, cb); });
        QCOMPARE(followed.status, 200);
        QCOMPARE(followed.body, QByteArray("cel:Basic eA=="));
        HttpRequest other = same;
        other.url = QUrl(http.url(u"/other"_s));
        const HttpResponse refused = await<HttpResponse>([&](auto cb) { transport.send(other, cb); });
        QCOMPARE(refused.status, 0);
        QVERIFY(refused.networkError.contains(u"redirect"_s, Qt::CaseInsensitive));
        for (const auto &request : http.requests)
            QVERIFY(request.path != "/target" || request.headers.value("host").startsWith("127.0.0.1"));
    }

    void networkTransportRefusesRedirectsToAnotherPort()
    {
        // Another port of the same host is another program, perhaps another user's: the
        // Authorization header does not go there.
        MiniHttpServer other;
        other.handler = [](const MiniHttpServer::Request &request) {
            return rawResponse(200, "OK", QByteArray("cel:" + request.headers.value("authorization")));
        };
        MiniHttpServer http;
        const QByteArray target = other.url(u"/target"_s).toUtf8();
        http.handler = [target](const MiniHttpServer::Request &) {
            return rawResponse(307, "Temporary Redirect", {}, {{"Location", target}});
        };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "GET";
        request.url = QUrl(http.url(u"/x"_s));
        request.headers = {{"Authorization", "Basic eA=="}};
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 0);
        QVERIFY2(r.networkError.contains(u"redirect"_s, Qt::CaseInsensitive), qPrintable(r.networkError));
        QCOMPARE(http.requests.size(), 1);
        QVERIFY(other.requests.isEmpty());
    }

    void redirectsStayWithTheSameServer_data()
    {
        QTest::addColumn<QString>("origin");
        QTest::addColumn<QString>("target");
        QTest::addColumn<bool>("same");
        QTest::newRow("another path") << u"https://chmura.pl/dav/"_s << u"https://chmura.pl/remote.php/dav/"_s << true;
        QTest::newRow("the host in other case") << u"https://chmura.pl/"_s << u"https://Chmura.PL/x"_s << true;
        QTest::newRow("the default port spelled out")
                << u"https://chmura.pl/"_s << u"https://chmura.pl:443/x"_s << true;
        QTest::newRow("http's default port spelled out")
                << u"http://chmura.pl:80/"_s << u"http://chmura.pl/x"_s << true;
        QTest::newRow("the same other port") << u"http://dom.lan:8080/"_s << u"http://dom.lan:8080/x"_s << true;
        QTest::newRow("upgraded to https") << u"http://chmura.pl/"_s << u"https://chmura.pl/"_s << true;
        QTest::newRow("upgraded on the same port") << u"http://dom.lan:8443/"_s << u"https://dom.lan:8443/"_s << true;
        QTest::newRow("another host") << u"https://chmura.pl/"_s << u"https://zla.example/"_s << false;
        QTest::newRow("a host that only starts alike")
                << u"https://chmura.pl/"_s << u"https://chmura.pl.zla.example/"_s << false;
        QTest::newRow("another port") << u"https://chmura.pl/"_s << u"https://chmura.pl:8443/"_s << false;
        QTest::newRow("another port of http") << u"http://dom.lan:8080/"_s << u"http://dom.lan:9090/"_s << false;
        QTest::newRow("upgraded to another port") << u"http://dom.lan:8080/"_s << u"https://dom.lan/"_s << false;
        QTest::newRow("upgraded from the default port to another")
                << u"http://dom.lan/"_s << u"https://dom.lan:8443/"_s << false;
        QTest::newRow("down to http") << u"https://chmura.pl/"_s << u"http://chmura.pl/"_s << false;
        QTest::newRow("down to http on https's port") << u"https://chmura.pl/"_s << u"http://chmura.pl:443/"_s << false;
        QTest::newRow("another scheme") << u"https://chmura.pl/"_s << u"ftp://chmura.pl/"_s << false;
        QTest::newRow("a file") << u"https://chmura.pl/"_s << u"file:///etc/passwd"_s << false;
    }
    void redirectsStayWithTheSameServer()
    {
        QFETCH(QString, origin);
        QFETCH(QString, target);
        QFETCH(bool, same);
        QCOMPARE(sync::isSameServer(QUrl(origin), QUrl(target)), same);
    }

    void networkTransportStopsAtTheBodyLimit_data()
    {
        QTest::addColumn<QByteArray>("raw");
        QTest::addColumn<qint64>("limit");
        QTest::addColumn<int>("status"); // 0: stopped as too large
        const QByteArray body(200'000, 'x');
        const QByteArray unannounced = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n" + body;
        QTest::newRow("as large as the limit") << rawResponse(200, "OK", body) << qint64(200'000) << 200;
        QTest::newRow("a byte more") << rawResponse(200, "OK", body) << qint64(199'999) << 0;
        // No length announced: the answer ends where the connection does.
        QTest::newRow("sent without a length") << unannounced << qint64(50'000) << 0;
        QTest::newRow("without a length, within the limit") << unannounced << qint64(200'000) << 200;
        // Announced and never sent: stopped before a byte of it is kept.
        QTest::newRow("announced only") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 4000000000\r\n\r\nabc")
                                        << qint64(1'000'000) << 0;
        QTest::newRow("an error page") << rawResponse(500, "Internal Server Error", body) << qint64(1000) << 0;
        QTest::newRow("an error page within the limit") << rawResponse(404, "Not Found", "brak") << qint64(4) << 404;
    }
    void networkTransportStopsAtTheBodyLimit()
    {
        QFETCH(QByteArray, raw);
        QFETCH(qint64, limit);
        QFETCH(int, status);
        MiniHttpServer http;
        http.handler = [raw](const MiniHttpServer::Request &) { return raw; };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "GET";
        request.url = QUrl(http.url(u"/du%C5%BCy"_s));
        request.maxBodyBytes = limit;
        request.timeoutMs = 20'000;
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, status);
        QCOMPARE(r.tooLarge, status == 0);
        QVERIFY(!r.timedOut);
        if (status == 0) {
            QVERIFY(r.body.isEmpty());
            QVERIFY2(r.networkError.contains(u"GET"_s) && r.networkError.contains(u"/duży"_s)
                             && r.networkError.contains(u"larger than %1 bytes"_s.arg(limit)),
                     qPrintable(r.networkError));
        } else {
            QVERIFY(r.networkError.isEmpty());
            QVERIFY(r.body.size() <= limit);
        }
    }

    void theBodyLimitHasADefault()
    {
        HttpRequest request;
        QCOMPARE(request.bodyLimit(), qint64(256) * 1024 * 1024);
        request.maxBodyBytes = 5;
        QCOMPARE(request.bodyLimit(), 5);
        request.maxBodyBytes = -1;
        QCOMPARE(request.bodyLimit(), HttpRequest::DefaultMaxBodyBytes);
        request.method = "GET";
        request.url = QUrl(u"https://jan:haslo@chmura.pl/%C5%BC%C3%B3%C5%82w%201.md"_s);
        const HttpResponse r = HttpResponse::oversized(request);
        QVERIFY(r.tooLarge);
        QCOMPARE(r.status, 0);
        // (As a person reads the address, and without the password in it.)
        QCOMPARE(r.networkError, u"The answer to GET https://chmura.pl/żółw 1.md is larger than 268435456 bytes"_s);
    }

    void webDavClientOverTheNetworkTransport()
    {
        MiniHttpServer http;
        http.handler = [](const MiniHttpServer::Request &request) {
            if (request.method == "PROPFIND")
                return rawResponse(207, "Multi-Status",
                                   "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/dav/N/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop></d:propstat></d:response>\n"
        "<d:response><d:href>/dav/N/%C5%BC.md</d:href><d:propstat><d:prop><d:getetag>\"7\"</d:getetag><d:getcontentlength>2</d:getcontentlength><d:resourcetype/></d:prop></d:propstat></d:response></d:multistatus>");
            return rawResponse(405, "Method Not Allowed", {});
        };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        WebDavClient client(http.url(u"/dav/N"_s), u"jan"_s, u"hasło"_s, &transport);
        const ListResult r = await<ListResult>([&](auto cb) { client.list({}, cb); });
        QVERIFY2(!r.error.isError(), qPrintable(r.error.message));
        QCOMPARE(r.entries, (QList<RemoteEntry>{RemoteEntry{u"ż.md"_s, false, u"7"_s, 0, 2, {}}}));
        QCOMPARE(http.requests.first().headers.value("authorization"), sync::basicAuth(u"jan"_s, u"hasło"_s));
    }

    // --- instrumentation: exchanges, in-flight requests, listing progress, log lines ----------
    void emitsAnExchangePerRequest()
    {
        Setup s;
        s.server.putText(u"Notatki/Dziennik/2026-10-14.md"_s, u"wpis"_s);
        QList<HttpExchange> exchanges;
        QObject::connect(&s.client, &WebDavClient::exchanged, &s.client,
                         [&](const HttpExchange &e) { exchanges.append(e); });
        const ListResult l = await<ListResult>([&](auto cb) { s.client.list({}, cb); });
        QVERIFY(!l.error.isError());
        const GetResult g = await<GetResult>([&](auto cb) { s.client.get(u"Dziennik/2026-10-14.md"_s, {}, cb); });
        QCOMPARE(g.data, QByteArray("wpis"));
        const PutResult p = await<PutResult>([&](auto cb) { s.client.put(u"Zażółć.md"_s, "gęślą", {}, cb); });
        QVERIFY(!p.error.isError());
        QCOMPARE(exchanges.size(), int(s.server.logSize()));
        QCOMPARE(exchanges.at(0).method, QByteArray("PROPFIND"));
        QCOMPARE(exchanges.at(0).status, 207);
        QVERIFY(exchanges.at(0).bytesReceived > 0);
        QVERIFY(exchanges.at(0).bytesSent > 0); // the PROPFIND body
        QVERIFY(exchanges.at(0).url.userInfo().isEmpty());
        const HttpExchange &get = exchanges.at(2);
        QCOMPARE(get.method, QByteArray("GET"));
        QCOMPARE(get.bytesReceived, 4);
        QCOMPARE(s.client.relativePath(get.url), u"Dziennik/2026-10-14.md"_s);
        QCOMPARE(s.client.relativePath(exchanges.at(1).url), u"Dziennik/"_s);
        QCOMPARE(s.client.relativePath(exchanges.at(0).url), QString());
        const HttpExchange &put = exchanges.last();
        QCOMPARE(put.method, QByteArray("PUT"));
        QCOMPARE(put.bytesSent, QByteArray("gęślą").size());
        QCOMPARE(s.client.relativePath(put.url), u"Zażółć.md"_s);
        for (const HttpExchange &e : exchanges)
            QVERIFY(e.elapsedMs >= 0);
        QCOMPARE(s.client.requestsInFlight(), 0);
        QCOMPARE(s.client.idleMs(), 0);
        QVERIFY(s.client.inFlight().isEmpty());
        // Elsewhere than below the base: the full URL without user info.
        QCOMPARE(s.client.relativePath(QUrl(u"https://jan:x@other.example/a"_s)), u"https://other.example/a"_s);
    }

    void tracksRequestsInFlight()
    {
        Setup s;
        s.server.setLatency("GET", 300);
        s.server.putText(u"Notatki/a.md"_s, u"a"_s);
        s.server.putText(u"Notatki/Folder z ę/b.md"_s, u"b"_s);
        std::optional<GetResult> a;
        std::optional<GetResult> b;
        s.client.get(u"a.md"_s, {}, [&](const GetResult &r) { a = r; });
        s.client.get(u"Folder z ę/b.md"_s, {}, [&](const GetResult &r) { b = r; });
        QCOMPARE(s.client.requestsInFlight(), 2);
        QCOMPARE(s.client.inFlight(), (QStringList{u"GET a.md"_s, u"GET Folder z ę/b.md"_s}));
        QCOMPARE(s.client.inFlight(1), QStringList{u"GET a.md"_s});
        QTest::qWait(50);
        QVERIFY(s.client.idleMs() >= 40);
        QVERIFY(waitFor([&] { return a && b; }));
        QCOMPARE(s.client.requestsInFlight(), 0);
        QCOMPARE(s.client.idleMs(), 0);
    }

    void reportsListingProgress()
    {
        Setup s;
        for (const QString &f : {u"A"_s, u"B"_s, u"A/Ż"_s})
            s.server.mkdir(u"Notatki/"_s + f);
        s.server.putText(u"Notatki/A/Ż/x.md"_s, u"x"_s);
        QList<std::tuple<int, int, int>> steps;
        ListOptions options;
        options.onProgress = [&](int done, int known, int entries) { steps.append({done, known, entries}); };
        const ListResult l = await<ListResult>([&](auto cb) { s.client.list(options, cb); });
        QVERIFY(!l.error.isError());
        QCOMPARE(steps.size(), 4);
        QCOMPARE(std::get<0>(steps.last()), 4);
        QCOMPARE(std::get<1>(steps.last()), 4);
        QCOMPARE(std::get<2>(steps.last()), 4); // A, B, A/Ż, A/Ż/x.md
        for (const auto &[done, known, entries] : steps)
            QVERIFY(done <= known);
    }

    void logsEachRequestWithoutSecrets()
    {
        static QStringList lines;
        lines.clear();
        QLoggingCategory::setFilterRules(u"brulion.net.debug=true"_s);
        const QtMessageHandler previous =
                qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &message) {
                    if (context.category && QByteArrayView(context.category) == "brulion.net")
                        lines.append(QString::number(int(type)) + u' ' + message);
                });
        {
            Setup s;
            s.client.setLogTag(u"#42"_s);
            s.server.putText(u"Notatki/Dziennik/a.md"_s, u"wpis"_s);
            await<ListResult>([&](auto cb) { s.client.list({}, cb); });
            s.server.hooks.append([](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
                if (request.method == "GET")
                    return FakeWebDavServer::noResponse();
                return std::nullopt;
            });
            s.client.setTimeout(30);
            await<GetResult>([&](auto cb) { s.client.get(u"Dziennik/a.md"_s, sized(4), cb); });
            s.server.hooks = {[](const HttpRequest &, const QString &) -> std::optional<HttpResponse> {
                HttpResponse r = FakeWebDavServer::status(200);
                r.headers.insert("content-type", "text/html; charset=UTF-8");
                r.body = "<html>login</html>";
                return r;
            }};
            await<WebDavTestResult>([&](auto cb) { s.client.test(cb); });
        }
        qInstallMessageHandler(previous);
        QLoggingCategory::setFilterRules(QString());
        const QString all = lines.join(u'\n');
        QVERIFY2(all.contains(QRegularExpression(
                         u"^0 #42 PROPFIND \\(base\\) -> 207 in \\d+ ms \\(sent \\d+ B, received [0-9.]+ (B|KB)\\)$"_s,
                         QRegularExpression::MultilineOption)),
                 qPrintable(all));
        QVERIFY2(all.contains(u"#42 PROPFIND Dziennik/ -> 207"_s), qPrintable(all));
        QVERIFY2(all.contains(u"#42 GET Dziennik/a.md timed out after"_s), qPrintable(all));
        QVERIFY2(all.contains(u"#42 PROPFIND (base) -> 200 text/html; charset=UTF-8: not a WebDAV answer"_s),
                 qPrintable(all));
        QVERIFY(!all.contains(u"hasło"_s));
        QVERIFY(!all.contains(u"Authorization"_s, Qt::CaseInsensitive));
        QVERIFY(!all.contains(u"wpis"_s)); // never bodies
    }

    void registersItsSecrets()
    {
        const int before = logging::secretCount();
        {
            WebDavClient client(u"https://h/dav"_s, u"jan"_s, u"Zażółć-Gęślą-42"_s, nullptr);
            QCOMPARE(logging::secretCount(), before + 2);
            QCOMPARE(logging::redact(u"x Zażółć-Gęślą-42 y"_s), u"x *** y"_s);
            const QString token = QString::fromLatin1(sync::basicAuth(u"jan"_s, u"Zażółć-Gęślą-42"_s).mid(6));
            QCOMPARE(logging::redact(token), u"***"_s);
        }
        QCOMPARE(logging::secretCount(), before);
    }

    void networkTransportReportsProtocolAndProgress()
    {
        MiniHttpServer http;
        http.handler = [](const MiniHttpServer::Request &) { return rawResponse(200, "OK", QByteArray(5000, 'x')); };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest request;
        request.method = "PUT";
        request.url = QUrl(http.url(u"/dav/a.md"_s));
        request.body = QByteArray(3000, 'y');
        qint64 lastSent = 0;
        qint64 lastReceived = 0;
        request.onProgress = [&](qint64 sent, qint64 received) {
            lastSent = qMax(lastSent, sent);
            lastReceived = qMax(lastReceived, received);
        };
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(request, cb); });
        QCOMPARE(r.status, 200);
        QCOMPARE(r.protocol, QByteArray("http/1.1"));
        QCOMPARE(lastReceived, 5000);
        QCOMPARE(lastSent, 3000);
    }

    // --- streamed transfers (SyncEngine::streamAbove()) ----------------------------------------
    void getHandsTheBodyToASinkInPieces_data()
    {
        QTest::addColumn<bool>("streams");
        QTest::newRow("streaming transport") << true;
        QTest::newRow("transport without streaming") << false;
    }
    void getHandsTheBodyToASinkInPieces()
    {
        QFETCH(bool, streams);
        FakeWebDavServer::Options options;
        options.streams = streams;
        options.streamPiece = 64 * 1024;
        Setup s(options);
        const QByteArray film = CountingDevice::pattern(5 * 1024 * 1024 + 17);
        s.server.putFile(u"Notatki/załączniki/film.mp4"_s, film);
        QByteArray kept;
        qint64 largest = 0;
        int pieces = 0;
        GetOptions get = sized(film.size());
        get.sink = [&](QByteArrayView piece) {
            kept += piece;
            largest = std::max<qint64>(largest, piece.size());
            ++pieces;
            return true;
        };
        const GetResult r = await<GetResult>([&](auto cb) { s.client.get(u"załączniki/film.mp4"_s, get, cb); });
        QVERIFY2(!r.error.isError(), qPrintable(r.error.message));
        QVERIFY(r.data.isEmpty()); // (all of it went to the sink)
        QVERIFY(!r.etag.isEmpty());
        QCOMPARE(kept, film);
        if (streams) {
            QCOMPARE(largest, options.streamPiece);
            QVERIFY(pieces > 80);
            QVERIFY(s.server.largestPayload() < 1024 * 1024); // nothing whole crossed the transport
        } else {
            QCOMPARE(pieces, 1); // handed over at once
        }
    }

    void getStopsWhenTheSinkRefuses()
    {
        FakeWebDavServer::Options options;
        options.streams = true;
        Setup s(options);
        s.server.putFile(u"Notatki/skan.pdf"_s, QByteArray(1024 * 1024, 'p'));
        qint64 kept = 0;
        GetOptions get = sized(1024 * 1024);
        get.sink = [&](QByteArrayView piece) {
            if (kept >= 128 * 1024)
                return false; // a full disk
            kept += piece.size();
            return true;
        };
        const GetResult r = await<GetResult>([&](auto cb) { s.client.get(u"skan.pdf"_s, get, cb); });
        QVERIFY(r.error.is(WebDavErrorCode::Cancelled));
        QVERIFY(!r.error.isFatal()); // (the network is fine: other files go on)
        QCOMPARE(kept, 128 * 1024);
    }

    void putFromReadsTheSourceAsItIsSent_data()
    {
        QTest::addColumn<bool>("streams");
        QTest::newRow("streaming transport") << true;
        QTest::newRow("transport without streaming") << false;
    }
    void putFromReadsTheSourceAsItIsSent()
    {
        QFETCH(bool, streams);
        FakeWebDavServer::Options options;
        options.streams = streams;
        Setup s(options);
        constexpr qint64 Size = 3 * 1024 * 1024 + 5;
        int opened = 0;
        const UploadSource source = [&](QString *) -> std::unique_ptr<QIODevice> {
            ++opened;
            return std::make_unique<CountingDevice>(Size);
        };
        const PutResult r = await<PutResult>(
                [&](auto cb) { s.client.putFrom(u"Zdjęcia/łąka.jpg"_s, Size, source, PutOptions{}, cb); });
        QVERIFY2(!r.error.isError(), qPrintable(r.error.message));
        QCOMPARE(opened, 1);
        QCOMPARE(s.server.bytes(u"Notatki/Zdjęcia/łąka.jpg"_s),
                 std::optional<QByteArray>(CountingDevice::pattern(Size)));
        if (streams)
            QVERIFY(s.server.largestPayload() < 1024 * 1024);
    }

    void putFromNeverSendsWhatItCannotRead_data()
    {
        QTest::addColumn<bool>("unreadable");
        QTest::newRow("cannot be opened") << true;
        QTest::newRow("another size") << false;
    }
    void putFromNeverSendsWhatItCannotRead()
    {
        // A file that cannot be read is no empty upload: the server's version stays.
        QFETCH(bool, unreadable);
        FakeWebDavServer::Options options;
        options.streams = true;
        Setup s(options);
        s.server.putFile(u"Notatki/raport.pdf"_s, "stara wersja");
        s.server.clearLog();
        const UploadSource source = [&](QString *error) -> std::unique_ptr<QIODevice> {
            if (unreadable) {
                *error = u"Brak dostępu do pliku „raport.pdf”"_s;
                return nullptr;
            }
            return std::make_unique<CountingDevice>(10); // not the 4096 bytes announced
        };
        const PutResult r =
                await<PutResult>([&](auto cb) { s.client.putFrom(u"raport.pdf"_s, 4096, source, PutOptions{}, cb); });
        QVERIFY(r.error.is(WebDavErrorCode::Cancelled));
        QVERIFY(!r.error.isFatal());
        if (unreadable)
            QVERIFY2(r.error.message.contains(u"Brak dostępu"_s), qPrintable(r.error.message));
        QVERIFY(s.server.requests("PUT").isEmpty());
        QCOMPARE(s.server.text(u"Notatki/raport.pdf"_s), std::optional<QString>(u"stara wersja"_s));
    }

    void networkTransportStreamsBothWays()
    {
        // A download handed on in pieces of at most StreamPieceBytes (Qt's read buffer bounded so),
        // an upload read from its device in small pieces: neither is ever held whole.
        constexpr qint64 Size = 24LL * 1024 * 1024 + 3;
        const QByteArray film = CountingDevice::pattern(Size);
        MiniHttpServer http;
        http.handler = [&](const MiniHttpServer::Request &request) {
            if (request.method == "PUT")
                return rawResponse(request.body == film ? 201 : 400, "Created", {});
            return rawResponse(200, "OK", film);
        };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        QVERIFY(transport.streams());

        HttpRequest get;
        get.method = "GET";
        get.url = QUrl(http.url(u"/dav/film.mp4"_s));
        get.maxBodyBytes = 2 * Size;
        text::Fnv1a64 hash;
        qint64 received = 0;
        qint64 largest = 0;
        get.bodySink = [&](QByteArrayView piece) {
            hash.add(piece);
            received += piece.size();
            largest = std::max<qint64>(largest, piece.size());
            return true;
        };
        const HttpResponse got = await<HttpResponse>([&](auto cb) { transport.send(get, cb); });
        QCOMPARE(got.status, 200);
        QVERIFY(got.body.isEmpty());
        QCOMPARE(received, Size);
        QCOMPARE(hash.value(), text::fnv1a64(film));
        QVERIFY(largest <= HttpRequest::StreamPieceBytes);

        HttpRequest put;
        put.method = "PUT";
        put.url = QUrl(http.url(u"/dav/film.mp4"_s));
        auto device = std::make_shared<CountingDevice>(Size);
        put.bodyDevice = device;
        const HttpResponse sent = await<HttpResponse>([&](auto cb) { transport.send(put, cb); });
        QCOMPARE(sent.status, 201);
        QCOMPARE(device->handedOut, Size);
        QVERIFY2(device->largestRead <= 1024 * 1024, qPrintable(QString::number(device->largestRead)));
    }

    void networkTransportStopsWhenTheSinkRefuses()
    {
        MiniHttpServer http;
        http.handler = [](const MiniHttpServer::Request &) { return rawResponse(200, "OK", QByteArray(4 << 20, 'z')); };
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        NetworkHttpTransport transport(&manager);
        HttpRequest get;
        get.method = "GET";
        get.url = QUrl(http.url(u"/dav/duży.bin"_s));
        qint64 kept = 0;
        get.bodySink = [&](QByteArrayView piece) {
            kept += piece.size();
            return kept < 512 * 1024;
        };
        const HttpResponse r = await<HttpResponse>([&](auto cb) { transport.send(get, cb); });
        QCOMPARE(r.status, 0);
        QVERIFY2(r.networkError.contains(u"could not be kept"_s), qPrintable(r.networkError));
        QVERIFY(kept < 4 << 20);
    }
};

QTEST_GUILESS_MAIN(TestSyncWebDav)
#include "tst_sync_webdav.moc"

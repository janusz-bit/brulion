// WebDavClient against the in-memory server (listing, transfers, preconditions, errors,
// timeouts) and NetworkHttpTransport against a minimal local HTTP server.
#include "fakewebdavserver.h"
#include "synctest.h"

#include "sync/http.h"
#include "sync/webdavclient.h"

#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QTimeZone>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <limits>
#include <memory>
#include <optional>

using namespace synctest;

namespace {

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
        QCOMPARE(file, (RemoteEntry{u"Strona.md"_s, false, s.server.etag(u"Notatki/Strona.md"_s), eight, 3}));
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
        QCOMPARE(r.entries, (QList<RemoteEntry>{RemoteEntry{u"y.md"_s, false, u"1"_s, 0, 0}}));
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
        GetOptions sized;
        sized.size = 400'000;
        QVERIFY(!await<GetResult>([&](auto cb) { s.client.get(u"d/duży.bin"_s, sized, cb); }).error.isError());
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
        QCOMPARE(r.entries, (QList<RemoteEntry>{RemoteEntry{u"ż.md"_s, false, u"7"_s, 0, 2}}));
        QCOMPARE(http.requests.first().headers.value("authorization"), sync::basicAuth(u"jan"_s, u"hasło"_s));
    }
};

QTEST_GUILESS_MAIN(TestSyncWebDav)
#include "tst_sync_webdav.moc"

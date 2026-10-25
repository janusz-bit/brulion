// Tests of the webpicture module: web URLs, format signatures, attachment and display names, the
// device's cache and the fetcher against a tiny HTTP server on 127.0.0.1 (limits, redirects,
// errors, no cookies), private hosts and the redirects allowed.
#include "webpicture/picturecache.h"
#include "webpicture/picturefetcher.h"
#include "webpicture/webpicture.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

using namespace Qt::StringLiterals;
using namespace brulion::webpicture;

Q_DECLARE_METATYPE(brulion::webpicture::FetchError)

namespace {

QByteArray pngBytes()
{
    return QByteArray::fromBase64(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
}

QByteArray jpegBytes()
{
    return QByteArray("\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00", 20);
}

/// A tiny HTTP/1.1 server on 127.0.0.1 (one request per connection).
class MiniHttpServer : public QObject {
public:
    struct Request {
        QByteArray method, path;
        QHash<QByteArray, QByteArray> headers; ///< lower-case names
    };
    std::function<QByteArray(const Request &)> handler; ///< the raw response, empty = never answer
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
    QUrl url(const QString &path) const
    {
        return QUrl(u"http://127.0.0.1:%1%2"_s.arg(m_server.serverPort()).arg(path));
    }

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
                request.headers.insert(lines.at(i).left(colon).trimmed().toLower(),
                                       lines.at(i).mid(colon + 1).trimmed());
        }
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

QByteArray rawResponse(int status, const QByteArray &body, const QList<QPair<QByteArray, QByteArray>> &headers = {},
                       qint64 announcedLength = -1)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n";
    for (const auto &[name, value] : headers)
        out += name + ": " + value + "\r\n";
    out += "Content-Length: " + QByteArray::number(announcedLength >= 0 ? announcedLength : body.size())
           + "\r\nConnection: close\r\n\r\n" + body;
    return out;
}

} // namespace

class TestWebPicture : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { qRegisterMetaType<FetchError>(); }

    void webUrls_data();
    void webUrls();
    void formats_data();
    void formats();
    void attachmentNames_data();
    void attachmentNames();
    void displayNames_data();
    void displayNames();
    void privateHosts_data();
    void privateHosts();
    void redirectRule_data();
    void redirectRule();

    void cacheStoresAndFinds();
    void cacheReplacesFormatAndRescans();
    void cachePrunesOldest();
    void cacheLookupMarksUsed();
    void cacheWithoutFolder();

    void fetchesPicture();
    void sendsNoCookiesAndFollowsRedirects();
    void failures_data();
    void failures();
    void refusesOtherSchemes();
    void oneRequestPerUrlAndQueue();
    void stallTimesOut();
    void refusesRedirectToPrivateHost();
    void tooManyRedirectsIsNoHttpError();
    void cancelsOneUrl();
    void cancelsManyWithoutStartingThem();
};

void TestWebPicture::webUrls_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<bool>("web");
    QTest::newRow("https") << u"https://example.com/a.png"_s << true;
    QTest::newRow("http") << u"http://example.com/a"_s << true;
    QTest::newRow("upper scheme") << u"HTTPS://Example.com/zdjęcie.jpg"_s << true;
    QTest::newRow("query") << u"https://cdn.example.pl/img?id=3&w=200"_s << true;
    QTest::newRow("polish path") << u"https://przykład.pl/źdźbło żółw.png"_s << true;
    QTest::newRow("spaces around") << u"  https://example.com/a.png "_s << true;
    QTest::newRow("no host") << u"https:///a.png"_s << false;
    QTest::newRow("relative") << u"assets/a.png"_s << false;
    QTest::newRow("file") << u"file:///etc/passwd"_s << false;
    QTest::newRow("data") << u"data:image/png;base64,AAAA"_s << false;
    QTest::newRow("ftp") << u"ftp://example.com/a.png"_s << false;
    QTest::newRow("javascript") << u"javascript:alert(1)"_s << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("scheme only") << u"https://"_s << false;
}

void TestWebPicture::webUrls()
{
    QFETCH(QString, target);
    QFETCH(bool, web);
    QCOMPARE(isWebUrl(target), web);
    QCOMPARE(webUrl(target).isValid(), web);
}

void TestWebPicture::formats_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("format");
    QTest::newRow("png") << pngBytes() << u"png"_s;
    QTest::newRow("jpeg") << jpegBytes() << u"jpg"_s;
    QTest::newRow("gif89") << QByteArray("GIF89a\x01\x00\x01\x00") << u"gif"_s;
    QTest::newRow("gif87") << QByteArray("GIF87a....") << u"gif"_s;
    QTest::newRow("webp") << QByteArray("RIFF\x10\x00\x00\x00WEBPVP8 ", 16) << u"webp"_s;
    QTest::newRow("riff wave") << QByteArray("RIFF\x10\x00\x00\x00WAVEfmt ", 16) << QString();
    QTest::newRow("bmp") << (QByteArray("BM\x46\x00\x00\x00\x00\x00\x00\x00\x36\x00\x00\x00", 14)
                             + QByteArray(20, '\0'))
                         << u"bmp"_s;
    QTest::newRow("text starting BM") << QByteArray("BMW is a car brand, not a picture") << QString();
    QTest::newRow("ico") << (QByteArray("\x00\x00\x01\x00\x01\x00", 6) + QByteArray(20, '\x10')) << u"ico"_s;
    QTest::newRow("ico without pictures")
            << (QByteArray("\x00\x00\x01\x00\x00\x00", 6) + QByteArray(20, '\x10')) << QString();
    QTest::newRow("avif") << QByteArray("\x00\x00\x00\x1c"
                                        "ftypavif\x00\x00\x00\x00"
                                        "avifmif1miaf",
                                        28)
                          << u"avif"_s;
    QTest::newRow("avif compatible") << QByteArray("\x00\x00\x00\x1c"
                                                   "ftypmif1\x00\x00\x00\x00"
                                                   "mif1avifmiaf",
                                                   28)
                                     << u"avif"_s;
    QTest::newRow("mp4") << QByteArray("\x00\x00\x00\x18"
                                       "ftypisom\x00\x00\x00\x00"
                                       "isommp42",
                                       24)
                         << QString();
    QTest::newRow("svg") << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"/>") << u"svg"_s;
    QTest::newRow("svg xml decl bom") << QByteArray(
            "\xef\xbb\xbf  <?xml version=\"1.0\"?>\n<!-- ąę -->\n<SVG width=\"1\"/>")
                                      << u"svg"_s;
    QTest::newRow("svg doctype") << QByteArray("<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\"><svg/>") << u"svg"_s;
    QTest::newRow("html") << QByteArray("<!DOCTYPE html><html><body>Zaloguj się</body></html>") << QString();
    QTest::newRow("xhtml with svg") << QByteArray("<?xml version=\"1.0\"?><html><svg/></html>") << QString();
    QTest::newRow("json") << QByteArray("{\"error\":\"not found\"}") << QString();
    QTest::newRow("empty") << QByteArray() << QString();
    QTest::newRow("short png") << QByteArray("\x89PNG") << QString();
}

void TestWebPicture::formats()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, format);
    QCOMPARE(sniffFormat(data), format);
}

void TestWebPicture::attachmentNames_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("format");
    QTest::addColumn<QString>("name");
    QTest::newRow("plain") << u"https://example.com/photos/kot.png"_s << u"png"_s << u"kot.png"_s;
    QTest::newRow("jpeg kept") << u"https://example.com/Kot.JPEG"_s << u"jpg"_s << u"Kot.JPEG"_s;
    QTest::newRow("wrong ext") << u"https://example.com/kot.jpg"_s << u"png"_s << u"kot.png"_s;
    QTest::newRow("no ext") << u"https://example.com/image/12345"_s << u"webp"_s << u"12345.webp"_s;
    QTest::newRow("script ext") << u"https://example.com/thumb.php?id=4"_s << u"gif"_s << u"thumb.php.gif"_s;
    QTest::newRow("percent polish") << u"https://example.com/%C5%BC%C3%B3%C5%82w%20na%20pla%C5%BCy.jpg"_s << u"jpg"_s
                                    << u"żółw na plaży.jpg"_s;
    QTest::newRow("trailing slash") << u"https://example.com/galeria/zdjęcie/"_s << u"png"_s << u"zdjęcie.png"_s;
    QTest::newRow("root") << u"https://example.com/"_s << u"svg"_s << u"example.com picture.svg"_s;
    QTest::newRow("no path") << u"https://example.com"_s << u"png"_s << u"example.com picture.png"_s;
    QTest::newRow("reserved chars") << u"https://example.com/a%3Ab%7Cc%23d.png"_s << u"png"_s << u"a b c d.png"_s;
    QTest::newRow("leading dots") << u"https://example.com/..hidden.png"_s << u"png"_s << u"hidden.png"_s;
    QTest::newRow("encoded slash") << u"https://example.com/a%2F..%2Fb.png"_s << u"png"_s << u"b.png"_s;
}

void TestWebPicture::attachmentNames()
{
    QFETCH(QString, url);
    QFETCH(QString, format);
    QFETCH(QString, name);
    const QString got = attachmentName(QUrl(url), format);
    QCOMPARE(got, name);
    QVERIFY(!got.contains(u'/'));
}

void TestWebPicture::displayNames_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("shown");
    QTest::newRow("file") << u"https://example.com/a/b/kot.png?x=1"_s << u"example.com › kot.png"_s;
    QTest::newRow("polish") << u"https://example.com/%C5%BC%C3%B3%C5%82w.jpg"_s << u"example.com › żółw.jpg"_s;
    QTest::newRow("host only") << u"https://example.com/"_s << u"example.com"_s;
}

void TestWebPicture::displayNames()
{
    QFETCH(QString, url);
    QFETCH(QString, shown);
    QCOMPARE(displayName(QUrl(url)), shown);
}

void TestWebPicture::privateHosts_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("isPrivate");
    QTest::newRow("public name") << u"https://example.com/a.png"_s << false;
    QTest::newRow("polish name") << u"https://zdjęcia.przykład.pl/kot.png"_s << false;
    QTest::newRow("name not resolved") << u"http://nas.example.com/żółw.png"_s << false;
    QTest::newRow("lan suffix in the middle") << u"http://zdjęcia.lan.example.pl/a.png"_s << false;
    QTest::newRow("not .local") << u"http://example.locally.pl/a.png"_s << false;
    QTest::newRow("100.63 is public") << u"http://100.63.255.255/a.png"_s << false;
    QTest::newRow("100.128 is public") << u"http://100.128.0.1/a.png"_s << false;
    QTest::newRow("fec0 not as prefix") << u"http://[2001:db8::fec0]/a.png"_s << false;
    QTest::newRow("single label") << u"http://nas/zdjęcie.jpg"_s << true;
    QTest::newRow("single label polish") << u"http://drukarka-żółta:631/a.png"_s << true;
    QTest::newRow(".lan") << u"http://nas.lan/żółw.png"_s << true;
    QTest::newRow(".local") << u"http://drukarka.local/status.png"_s << true;
    QTest::newRow(".LOCAL.") << u"http://Drukarka.LOCAL./status.png"_s << true;
    QTest::newRow(".home.arpa") << u"http://router.home.arpa/a.png"_s << true;
    QTest::newRow(".internal") << u"http://grafana.internal/a.png"_s << true;
    QTest::newRow("fritz.box") << u"http://fritz.box/a.png"_s << true;
    QTest::newRow("cgnat tailscale") << u"http://100.101.102.103:5000/webapi/a.png"_s << true;
    QTest::newRow("tailscale resolver") << u"http://100.100.100.100/a.png"_s << true;
    QTest::newRow("cgnat last") << u"http://100.127.255.254/a.png"_s << true;
    QTest::newRow("ip4-mapped cgnat") << u"http://[::ffff:100.64.0.1]/a.png"_s << true;
    QTest::newRow("ip6 site-local") << u"http://[fec0::1]/a.png"_s << true;
    QTest::newRow("ip6 site-local end") << u"http://[feff::1]/a.png"_s << true;
    QTest::newRow("public ip4") << u"http://93.184.216.34/a.png"_s << false;
    QTest::newRow("public ip6") << u"http://[2606:2800:220:1::1]/a.png"_s << false;
    QTest::newRow("172.15 is public") << u"http://172.15.0.1/a.png"_s << false;
    QTest::newRow("172.32 is public") << u"http://172.32.0.1/a.png"_s << false;
    QTest::newRow("localhost") << u"http://localhost:631/admin"_s << true;
    QTest::newRow("LOCALHOST.") << u"http://LOCALHOST./a.png"_s << true;
    QTest::newRow("sub.localhost") << u"http://drukarka.localhost/a.png"_s << true;
    QTest::newRow("not localhost") << u"http://localhost.example.com/a.png"_s << false;
    QTest::newRow("loopback") << u"http://127.0.0.1:8080/a.png"_s << true;
    QTest::newRow("loopback 127.8") << u"http://127.8.9.10/a.png"_s << true;
    QTest::newRow("loopback short") << u"http://127.1/a.png"_s << true;
    QTest::newRow("loopback decimal") << u"http://2130706433/a.png"_s << true;
    QTest::newRow("10/8") << u"http://10.1.2.3/a.png"_s << true;
    QTest::newRow("172.16/12") << u"http://172.31.255.1/a.png"_s << true;
    QTest::newRow("router") << u"http://192.168.1.1/cgi-bin/reboot?x=1"_s << true;
    QTest::newRow("link-local") << u"http://169.254.169.254/latest/meta-data"_s << true;
    QTest::newRow("unspecified") << u"http://0.0.0.0:8000/a.png"_s << true;
    QTest::newRow("this network") << u"http://0.1.2.3/a.png"_s << true;
    QTest::newRow("ip6 loopback") << u"http://[::1]:8080/a.png"_s << true;
    QTest::newRow("ip6 unspecified") << u"http://[::]/a.png"_s << true;
    QTest::newRow("ip6 link-local") << u"http://[fe80::1]/a.png"_s << true;
    QTest::newRow("ip6 unique local") << u"http://[fd12:3456::1]/a.png"_s << true;
    QTest::newRow("ip4-mapped loopback") << u"http://[::ffff:127.0.0.1]/a.png"_s << true;
    QTest::newRow("ip4-mapped lan") << u"http://[::ffff:192.168.0.10]/a.png"_s << true;
}

void TestWebPicture::privateHosts()
{
    QFETCH(QString, url);
    QFETCH(bool, isPrivate);
    const QUrl web = webUrl(url);
    QVERIFY2(web.isValid(), qPrintable(url));
    QCOMPARE(isPrivateHost(web.host()), isPrivate);
}

void TestWebPicture::redirectRule_data()
{
    QTest::addColumn<QString>("from");
    QTest::addColumn<QString>("to");
    QTest::addColumn<bool>("allowed");
    QTest::newRow("public to public") << u"http://example.com/a.png"_s << u"https://cdn.example.org/ą.png"_s << true;
    QTest::newRow("http to https") << u"http://example.com/a"_s << u"https://example.com/a"_s << true;
    QTest::newRow("https to https") << u"https://example.com/a"_s << u"https://example.com/b"_s << true;
    QTest::newRow("https to http") << u"https://example.com/a"_s << u"http://example.com/a"_s << false;
    QTest::newRow("HTTPS to http") << u"HTTPS://example.com/a"_s << u"http://example.com/a"_s << false;
    QTest::newRow("public to router") << u"http://evil.example/kot.png"_s << u"http://192.168.0.1/reset"_s << false;
    QTest::newRow("public to localhost") << u"http://evil.example/a"_s << u"http://localhost:631/admin"_s << false;
    QTest::newRow("public to loopback") << u"http://evil.example/a"_s << u"http://127.0.0.1:8080/x"_s << false;
    QTest::newRow("public to ip6 loopback") << u"http://evil.example/a"_s << u"http://[::1]/x"_s << false;
    QTest::newRow("public to link-local") << u"https://evil.example/a"_s << u"https://169.254.169.254/x"_s << false;
    QTest::newRow("public to .local") << u"http://evil.example/a"_s << u"http://drukarka.local/status"_s << false;
    QTest::newRow("public to single label") << u"https://evil.example/a"_s << u"https://nas/zdjęcie.jpg"_s << false;
    QTest::newRow("public to tailscale") << u"https://evil.example/a"_s << u"https://100.101.102.103/x"_s << false;
    QTest::newRow("nas to nas") << u"http://192.168.1.5/zdjęcie.jpg"_s << u"http://192.168.1.5/f/zdjęcie.jpg"_s << true;
    QTest::newRow("nas to public") << u"http://192.168.1.5/a"_s << u"https://example.com/a"_s << true;
    QTest::newRow("to ftp") << u"http://example.com/a"_s << u"ftp://example.com/a"_s << false;
    QTest::newRow("to file") << u"http://localhost/a"_s << u"file:///etc/passwd"_s << false;
    QTest::newRow("to nothing") << u"http://example.com/a"_s << QString() << false;
}

void TestWebPicture::redirectRule()
{
    QFETCH(QString, from);
    QFETCH(QString, to);
    QFETCH(bool, allowed);
    QCOMPARE(redirectAllowed(QUrl(from), QUrl(to)), allowed);
    // A swapped check: nothing private, so only the schemes count.
    const bool schemesOnly = redirectAllowed(QUrl(from), QUrl(to), [](const QString &) { return false; });
    const QString scheme = QUrl(to).scheme();
    QCOMPARE(schemesOnly, (scheme == u"https" || (scheme == u"http" && QUrl(from).scheme().toLower() == u"http"))
                                  && !QUrl(to).host().isEmpty());
}

void TestWebPicture::cacheStoresAndFinds()
{
    QTemporaryDir tmp;
    PictureCache cache(tmp.filePath(u"pictures"_s));
    const QUrl url(u"https://example.com/kot.png"_s);
    QVERIFY(cache.lookup(url).isEmpty());
    const QString path = cache.store(url, pngBytes(), u"png"_s);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.endsWith(u".png"_s));
    QCOMPARE(QFileInfo(path).fileName(), PictureCache::keyOf(url) + u".png"_s);
    QCOMPARE(cache.lookup(url), path);
    QCOMPARE(cache.read(url), pngBytes());
    QVERIFY(cache.lookup(QUrl(u"https://example.com/kot.png?x"_s)).isEmpty());
    // A new cache object finds the files already there.
    PictureCache again(tmp.filePath(u"pictures"_s));
    QCOMPARE(again.lookup(url), path);
    // A file removed behind the cache's back is not found.
    QVERIFY(QFile::remove(path));
    QVERIFY(again.lookup(url).isEmpty());
    cache.store(url, pngBytes(), u"png"_s);
    cache.remove(url);
    QVERIFY(cache.lookup(url).isEmpty());
    QVERIFY(!QFileInfo::exists(path));
}

void TestWebPicture::cacheReplacesFormatAndRescans()
{
    QTemporaryDir tmp;
    PictureCache cache(tmp.path());
    const QUrl url(u"https://example.com/zmienny"_s);
    const QString png = cache.store(url, pngBytes(), u"png"_s);
    const QString jpg = cache.store(url, jpegBytes(), u"jpg"_s);
    QVERIFY(jpg.endsWith(u".jpg"_s));
    QVERIFY(!QFileInfo::exists(png));
    QCOMPARE(cache.lookup(url), jpg);
    // Foreign files in the folder are ignored.
    QFile other(tmp.filePath(u"notatka.txt"_s));
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("x");
    other.close();
    PictureCache fresh(tmp.path());
    QCOMPARE(fresh.lookup(url), jpg);
}

void TestWebPicture::cachePrunesOldest()
{
    QTemporaryDir tmp;
    PictureCache cache(tmp.path());
    const QByteArray big = pngBytes() + QByteArray(1000, 'x');
    QList<QUrl> urls;
    for (int i = 0; i < 5; ++i) {
        urls.append(QUrl(u"https://example.com/%1.png"_s.arg(i)));
        const QString path = cache.store(urls.last(), big, u"png"_s);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        const QDateTime when = QDateTime::currentDateTime().addSecs(-1000 + i * 100);
        QVERIFY(f.setFileTime(when, QFileDevice::FileModificationTime));
        f.setFileTime(when, QFileDevice::FileAccessTime);
    }
    cache.prune(3 * big.size());
    QVERIFY(cache.lookup(urls[0]).isEmpty());
    QVERIFY(cache.lookup(urls[1]).isEmpty());
    QVERIFY(!cache.lookup(urls[2]).isEmpty());
    QVERIFY(!cache.lookup(urls[4]).isEmpty());
    cache.prune(); // below the default: nothing happens
    QVERIFY(!cache.lookup(urls[4]).isEmpty());
}

void TestWebPicture::cacheLookupMarksUsed()
{
    QTemporaryDir tmp;
    PictureCache cache(tmp.path());
    const QByteArray big = pngBytes() + QByteArray(1000, 'x');
    const QDateTime now = QDateTime::currentDateTime();
    const auto setTime = [](const QString &path, const QDateTime &when) {
        QFile f(path);
        return f.open(QIODevice::ReadWrite) && f.setFileTime(when, QFileDevice::FileModificationTime)
               && f.setFileTime(when, QFileDevice::FileAccessTime);
    };
    // Fetched long ago, in this order; the file system keeps no access times worth trusting.
    QList<QUrl> urls;
    for (int i = 0; i < 3; ++i) {
        urls.append(QUrl(u"https://example.com/zdjęcie-%1.png"_s.arg(i)));
        QVERIFY(setTime(cache.store(urls.last(), big, u"png"_s), now.addDays(-10 + i)));
    }
    // The oldest is looked at: marked used now, so the next-oldest goes first.
    const QString looked = cache.lookup(urls[0]);
    QVERIFY(QFileInfo(looked).lastModified().secsTo(now) < 60);
    // contains() marks nothing.
    QVERIFY(cache.contains(urls[1]));
    const QString second = QDir(tmp.path()).filePath(PictureCache::keyOf(urls[1]) + u".png"_s);
    QVERIFY(QFileInfo(second).lastModified() < now.addDays(-8));
    cache.prune(2 * big.size());
    QVERIFY(!cache.contains(urls[1]));
    QVERIFY(cache.contains(urls[0]));
    QVERIFY(cache.contains(urls[2]));
    // Used again within the hour: the time is not rewritten.
    const QDateTime recent = now.addSecs(-30 * 60);
    QVERIFY(setTime(looked, recent));
    QCOMPARE(QFileInfo(cache.lookup(urls[0])).lastModified().toSecsSinceEpoch(), recent.toSecsSinceEpoch());
}

void TestWebPicture::cacheWithoutFolder()
{
    PictureCache cache;
    const QUrl url(u"https://example.com/a.png"_s);
    QVERIFY(cache.store(url, pngBytes(), u"png"_s).isEmpty());
    QVERIFY(cache.lookup(url).isEmpty());
    cache.prune(0);
    QTemporaryDir tmp;
    cache.setDir(tmp.path());
    QVERIFY(cache.store(url, QByteArray(), u"png"_s).isEmpty());
    QVERIFY(!cache.store(url, pngBytes(), u"png"_s).isEmpty());
}

void TestWebPicture::fetchesPicture()
{
    MiniHttpServer server;
    server.handler = [](const MiniHttpServer::Request &) {
        return rawResponse(200, pngBytes(), {{"Content-Type", "image/png"}});
    };
    PictureFetcher fetcher;
    QSignalSpy fetched(&fetcher, &PictureFetcher::fetched);
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    const QUrl url = server.url(u"/kot.png"_s);
    fetcher.fetch(url);
    QVERIFY(fetcher.isFetching(url));
    QVERIFY(fetched.wait(5000));
    QCOMPARE(failed.size(), 0);
    QCOMPARE(fetched.at(0).at(0).toUrl(), url);
    QCOMPARE(fetched.at(0).at(1).toByteArray(), pngBytes());
    QCOMPARE(fetched.at(0).at(2).toString(), u"png"_s);
    QVERIFY(!fetcher.isFetching(url));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).method, QByteArray("GET"));
}

void TestWebPicture::sendsNoCookiesAndFollowsRedirects()
{
    MiniHttpServer server;
    server.handler = [&server](const MiniHttpServer::Request &r) {
        if (r.path == "/stare.png")
            return rawResponse(
                    302, {},
                    {{"Location", server.url(u"/nowe.png"_s).toEncoded()}, {"Set-Cookie", "sledzenie=1; Path=/"}});
        return rawResponse(200, jpegBytes(), {{"Content-Type", "image/jpeg"}, {"Set-Cookie", "sledzenie=2"}});
    };
    PictureFetcher fetcher;
    QSignalSpy fetched(&fetcher, &PictureFetcher::fetched);
    fetcher.fetch(server.url(u"/stare.png"_s));
    QVERIFY(fetched.wait(5000));
    QCOMPARE(fetched.at(0).at(0).toUrl(), server.url(u"/stare.png"_s)); // the URL as asked for
    QCOMPARE(fetched.at(0).at(2).toString(), u"jpg"_s);
    // A second fetch carries no cookie either.
    fetcher.fetch(server.url(u"/nowe.png"_s));
    QVERIFY(fetched.wait(5000));
    QCOMPARE(server.requests.size(), 3);
    for (const auto &r : std::as_const(server.requests)) {
        QVERIFY2(!r.headers.contains("cookie"), r.path.constData());
        QVERIFY(!r.headers.contains("referer"));
        QVERIFY(r.headers.value("accept").startsWith("image/"));
        // Nothing telling the app or the device's language.
        QCOMPARE(r.headers.value("user-agent"), QByteArray("Mozilla/5.0"));
        QCOMPARE(r.headers.value("accept-language"), QByteArray("*"));
    }
}

void TestWebPicture::failures_data()
{
    QTest::addColumn<QByteArray>("response");
    QTest::addColumn<int>("error");
    QTest::addColumn<QString>("detail");
    QTest::newRow("404") << rawResponse(404, "<html>Nie ma</html>") << int(FetchError::Http) << u"404"_s;
    QTest::newRow("500 with picture") << rawResponse(500, pngBytes()) << int(FetchError::Http) << u"500"_s;
    QTest::newRow("html page") << rawResponse(200, "<!DOCTYPE html><html>Zaloguj</html>",
                                              {{"Content-Type", "text/html"}})
                               << int(FetchError::NotPicture) << u"text/html"_s;
    QTest::newRow("empty body") << rawResponse(200, {}) << int(FetchError::NotPicture) << QString();
    QTest::newRow("announced too large") << rawResponse(200, pngBytes(), {}, 100 * 1024 * 1024)
                                         << int(FetchError::TooLarge) << QString();
    QTest::newRow("too large body") << rawResponse(200, pngBytes() + QByteArray(5000, 'x')) << int(FetchError::TooLarge)
                                    << QString();
}

void TestWebPicture::failures()
{
    QFETCH(QByteArray, response);
    QFETCH(int, error);
    QFETCH(QString, detail);
    MiniHttpServer server;
    server.handler = [response](const MiniHttpServer::Request &) { return response; };
    PictureFetcher fetcher;
    fetcher.setMaxBytes(4096);
    QSignalSpy fetched(&fetcher, &PictureFetcher::fetched);
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    fetcher.fetch(server.url(u"/a.png"_s));
    QVERIFY(failed.wait(5000));
    QCOMPARE(fetched.size(), 0);
    QCOMPARE(int(failed.at(0).at(1).value<FetchError>()), error);
    if (!detail.isEmpty())
        QCOMPARE(failed.at(0).at(2).toString(), detail);
    QVERIFY(!errorName(FetchError(error)).isEmpty());
}

void TestWebPicture::refusesOtherSchemes()
{
    PictureFetcher fetcher;
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    fetcher.fetch(QUrl(u"file:///etc/passwd"_s));
    fetcher.fetch(QUrl(u"ftp://example.com/a.png"_s));
    QVERIFY(failed.wait(2000) || failed.size() == 2);
    QTRY_COMPARE(failed.size(), 2);
    QCOMPARE(failed.at(0).at(1).value<FetchError>(), FetchError::Network);
}

void TestWebPicture::oneRequestPerUrlAndQueue()
{
    MiniHttpServer server;
    server.handler = [](const MiniHttpServer::Request &) { return rawResponse(200, pngBytes()); };
    PictureFetcher fetcher;
    QSignalSpy fetched(&fetcher, &PictureFetcher::fetched);
    const QUrl same = server.url(u"/jeden.png"_s);
    fetcher.fetch(same);
    fetcher.fetch(same);
    for (int i = 0; i < 6; ++i)
        fetcher.fetch(server.url(u"/%1.png"_s.arg(i)));
    QTRY_COMPARE_WITH_TIMEOUT(fetched.size(), 7, 10000);
    QCOMPARE(server.requests.size(), 7);
}

void TestWebPicture::stallTimesOut()
{
    MiniHttpServer server; // never answers
    PictureFetcher fetcher;
    fetcher.setTimeout(300);
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    fetcher.fetch(server.url(u"/wolny.png"_s));
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.at(0).at(1).value<FetchError>(), FetchError::Timeout);
    // cancelAll() aborts without signals.
    fetcher.fetch(server.url(u"/drugi.png"_s));
    fetcher.cancelAll();
    QVERIFY(!failed.wait(800));
    QCOMPARE(failed.size(), 1);
}

void TestWebPicture::refusesRedirectToPrivateHost()
{
    MiniHttpServer server;
    const int port = server.url(u"/"_s).port();
    server.handler = [port](const MiniHttpServer::Request &r) {
        if (r.path == "/kot.png") // "evil.example" sends the request into the network
            return rawResponse(302, {}, {{"Location", "http://localhost:" + QByteArray::number(port) + "/reset"}});
        return rawResponse(200, pngBytes());
    };
    PictureFetcher fetcher;
    // 127.0.0.1 plays a host of the internet, "localhost" the device.
    fetcher.setPrivateHostCheck([](const QString &host) { return host == u"localhost"; });
    QSignalSpy fetched(&fetcher, &PictureFetcher::fetched);
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    fetcher.fetch(server.url(u"/kot.png"_s));
    QVERIFY(failed.wait(5000));
    QCOMPARE(fetched.size(), 0);
    QCOMPARE(failed.at(0).at(0).toUrl(), server.url(u"/kot.png"_s));
    QCOMPARE(failed.at(0).at(1).value<FetchError>(), FetchError::Redirect); // not "error 302"
    QVERIFY(failed.at(0).at(2).toString().contains(u"localhost"_s));
    QCOMPARE(errorName(FetchError::Redirect), u"redirect refused"_s);
    QTest::qWait(100);
    QCOMPARE(server.requests.size(), 1);
    QVERIFY(!fetcher.isFetching(server.url(u"/kot.png"_s)));
}

void TestWebPicture::tooManyRedirectsIsNoHttpError()
{
    MiniHttpServer server;
    server.handler = [&server](const MiniHttpServer::Request &r) {
        const int hop = r.path.mid(5).toInt(); // "/hop/3"
        return rawResponse(301, {}, {{"Location", server.url(u"/hop/%1"_s.arg(hop + 1)).toEncoded()}});
    };
    PictureFetcher fetcher;
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    fetcher.fetch(server.url(u"/hop/0"_s));
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.at(0).at(1).value<FetchError>(), FetchError::Redirect);
    QVERIFY(failed.at(0).at(2).toString() != u"301"_s);
    QCOMPARE(server.requests.size(), 6); // the request and 5 redirects
}

void TestWebPicture::cancelsOneUrl()
{
    MiniHttpServer server;
    server.handler = [](const MiniHttpServer::Request &r) {
        return r.path == "/ostatni.png" ? rawResponse(200, pngBytes()) : QByteArray(); // the rest stall
    };
    PictureFetcher fetcher;
    QSignalSpy fetched(&fetcher, &PictureFetcher::fetched);
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    QList<QUrl> stalled;
    for (int i = 0; i < PictureFetcher::MaxParallel + 1; ++i) {
        stalled.append(server.url(u"/wolny-%1.png"_s.arg(i)));
        fetcher.fetch(stalled.last());
    }
    const QUrl last = server.url(u"/ostatni.png"_s);
    fetcher.fetch(last); // queued behind the stalled ones
    QTRY_COMPARE(server.requests.size(), PictureFetcher::MaxParallel);
    fetcher.cancel(stalled.last()); // queued: never sent
    QVERIFY(!fetcher.isFetching(stalled.last()));
    QVERIFY(fetcher.isFetching(last));
    fetcher.cancel(stalled.first()); // running: aborted, the queue moves on
    QVERIFY(!fetcher.isFetching(stalled.first()));
    QVERIFY(fetched.wait(5000));
    QCOMPARE(fetched.at(0).at(0).toUrl(), last);
    fetcher.cancel(QUrl(u"https://example.com/nieznany.png"_s)); // nothing to cancel
    QTest::qWait(100);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(server.requests.size(), PictureFetcher::MaxParallel + 1);
    for (const auto &r : std::as_const(server.requests))
        QVERIFY(r.path != "/wolny-4.png");
    fetcher.cancelAll();
}

void TestWebPicture::cancelsManyWithoutStartingThem()
{
    MiniHttpServer server;
    server.handler = [](const MiniHttpServer::Request &r) {
        return r.path == "/zostaje.png" ? rawResponse(200, pngBytes()) : QByteArray(); // the rest stall
    };
    PictureFetcher fetcher;
    QSignalSpy fetched(&fetcher, &PictureFetcher::fetched);
    QSignalSpy failed(&fetcher, &PictureFetcher::failed);
    QList<QUrl> automatic;
    for (int i = 0; i < 3 * PictureFetcher::MaxParallel; ++i) {
        automatic.append(server.url(u"/żółw-%1.png"_s.arg(i)));
        fetcher.fetch(automatic.last());
    }
    const QUrl kept = server.url(u"/zostaje.png"_s);
    fetcher.fetch(kept);
    QTRY_COMPARE(server.requests.size(), PictureFetcher::MaxParallel);
    // Running ones first: aborting each must not send the next of the same list.
    QList<QUrl> order = automatic.mid(0, PictureFetcher::MaxParallel);
    order += automatic.mid(PictureFetcher::MaxParallel);
    fetcher.cancel(order);
    for (const QUrl &url : std::as_const(automatic))
        QVERIFY(!fetcher.isFetching(url));
    QVERIFY(fetcher.isFetching(kept));
    QVERIFY(fetched.wait(5000));
    QCOMPARE(fetched.at(0).at(0).toUrl(), kept);
    QTest::qWait(100);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(server.requests.size(), PictureFetcher::MaxParallel + 1);
    fetcher.cancel(QList<QUrl>{}); // nothing
    fetcher.cancelAll();
}

QTEST_GUILESS_MAIN(TestWebPicture)
#include "tst_webpicture.moc"

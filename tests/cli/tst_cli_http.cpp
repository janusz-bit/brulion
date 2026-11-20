// The pieces of `brulion-cli mcp serve` in process (src/cli/http.h, oauth.h, authserver.h): the
// HTTP/1.1 request parser on well-formed, malformed and hostile input (also fed byte by byte),
// form decoding, the browser's language, HTML escaping; the secrets (PBKDF2-HMAC-SHA256 against
// published vectors, PKCE S256 against RFC 7636's), the checks on client IDs, redirect URIs,
// resources and the addresses a metadata document may come from; the state file (written only
// for its owner, a change by another process seen, pruning).
#include "authserver.h"
#include "http.h"
#include "oauth.h"

#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::cli;

namespace {

/// Parse `bytes` (a body of up to `bodyLimit`) the way a connection does: the state, the request.
struct Parsed {
    http::RequestParser::State state = http::RequestParser::State::Incomplete;
    int status = 0;
    http::Request request;
    int requests = 0; ///< finished ones
};

Parsed parseAll(const QByteArray &bytes, qint64 bodyLimit = 1024 * 1024, bool byteByByte = false)
{
    http::RequestParser parser;
    Parsed out;
    const auto step = [&](QByteArrayView data) {
        auto state = parser.feed(data);
        for (;;) {
            if (state == http::RequestParser::State::HeadersDone) {
                parser.setBodyLimit(bodyLimit);
                state = parser.parse();
                continue;
            }
            if (state == http::RequestParser::State::Done) {
                ++out.requests;
                out.request = parser.request();
                parser.next();
                state = parser.parse();
                continue;
            }
            break;
        }
        out.state = state;
        out.status = parser.errorStatus();
    };
    if (byteByByte) {
        for (qsizetype i = 0; i < bytes.size() && out.state != http::RequestParser::State::Failed; ++i)
            step(QByteArrayView(bytes).sliced(i, 1));
    } else {
        step(bytes);
    }
    return out;
}

} // namespace

class TestCliHttp : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wellFormed_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QByteArray>("method");
        QTest::addColumn<QByteArray>("path");
        QTest::addColumn<QByteArray>("query");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("requests");
        QTest::newRow("GET") << "GET /mcp HTTP/1.1\r\nHost: a\r\n\r\n"_ba << "GET"_ba << "/mcp"_ba << ""_ba << ""_ba
                             << 1;
        QTest::newRow("query") << "GET /authorize?client_id=x&state=%C5%BC HTTP/1.1\r\nHost: a\r\n\r\n"_ba << "GET"_ba
                               << "/authorize"_ba << "client_id=x&state=%C5%BC"_ba << ""_ba << 1;
        QTest::newRow("POST with Content-Length")
                << "POST /token HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\nzażółć"_ba << "POST"_ba << "/token"_ba
                << ""_ba << "zażółć"_ba << 1;
        QTest::newRow("chunked, extensions and trailers")
                << "POST /mcp HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n4;ext=1\r\nżó\r\n"
                   "4\r\nłw!\r\n0\r\nX-Trailer: t\r\n\r\n"_ba
                << "POST"_ba << "/mcp"_ba << ""_ba << "żółw!"_ba << 1;
        QTest::newRow("bare LF line ends") << "GET / HTTP/1.1\nHost: a\n\n"_ba << "GET"_ba << "/"_ba << ""_ba << ""_ba << 1;
        QTest::newRow("empty lines before") << "\r\n\r\nGET / HTTP/1.1\r\nHost: a\r\n\r\n"_ba << "GET"_ba << "/"_ba
                                            << ""_ba << ""_ba << 1;
        QTest::newRow("HTTP/1.0 without Host") << "GET / HTTP/1.0\r\n\r\n"_ba << "GET"_ba << "/"_ba << ""_ba << ""_ba
                                               << 1;
        QTest::newRow("absolute form") << "GET http://a:8780/mcp?x HTTP/1.1\r\nHost: a\r\n\r\n"_ba << "GET"_ba
                                       << "/mcp"_ba << "x"_ba << ""_ba << 1;
        QTest::newRow("pipelined") << "GET /a HTTP/1.1\r\nHost: a\r\n\r\nPOST /b HTTP/1.1\r\nHost: a\r\nContent-"
                                      "Length: 2\r\n\r\nok"_ba
                                   << "POST"_ba << "/b"_ba << ""_ba << "ok"_ba << 2;
        QTest::newRow("OPTIONS *") << "OPTIONS * HTTP/1.1\r\nHost: a\r\n\r\n"_ba << "OPTIONS"_ba << "*"_ba << ""_ba
                                   << ""_ba << 1;
        QTest::newRow("value with obs-text and tabs")
                << "GET / HTTP/1.1\r\nHost: a\r\nX-Name:\t\xc5\xbc\xc3\xb3\xc5\x82w \t\r\n\r\n"_ba << "GET"_ba << "/"_ba
                << ""_ba << ""_ba << 1;
    }
    void wellFormed()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QByteArray, method);
        QFETCH(QByteArray, path);
        QFETCH(QByteArray, query);
        QFETCH(QByteArray, body);
        QFETCH(int, requests);
        for (const bool byteByByte : {false, true}) {
            const Parsed p = parseAll(bytes, 1024 * 1024, byteByByte);
            QCOMPARE(p.state, http::RequestParser::State::Incomplete);
            QCOMPARE(p.requests, requests);
            QCOMPARE(p.request.method, method);
            QCOMPARE(p.request.path, path);
            QCOMPARE(p.request.query, query);
            QCOMPARE(p.request.body, body);
        }
    }

    void headerValues()
    {
        const Parsed p = parseAll("GET / HTTP/1.1\r\nHost: a\r\nAccept: application/json,  text/event-stream\r\n"
                                  "accept: */*\r\nX-Name:\t\xc5\xbc\xc3\xb3\xc5\x82w \t\r\n\r\n"_ba);
        QCOMPARE(p.request.headers.value("ACCEPT"), "application/json,  text/event-stream"_ba);
        QCOMPARE(p.request.headers.tokens("accept"),
                 (QList<QByteArray>{"application/json"_ba, "text/event-stream"_ba, "*/*"_ba}));
        QCOMPARE(p.request.headers.value("x-name"), "żółw"_ba);
        QCOMPARE(p.request.headers.count("Accept"), 2);
    }

    void malformed_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("status");
        const QByteArray ok = "Host: a\r\n"_ba;
        QTest::newRow("no Host") << "GET / HTTP/1.1\r\n\r\n"_ba << 400;
        QTest::newRow("two Hosts") << "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n"_ba << 400;
        QTest::newRow("Content-Length and chunked")
                << "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n"_ba << 400;
        QTest::newRow("two Content-Lengths") << "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\nContent-Length: 3"
                                                "\r\n\r\nabc"_ba
                                             << 400;
        QTest::newRow("negative Content-Length") << "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: -1\r\n\r\n"_ba << 400;
        QTest::newRow("Content-Length with a sign") << "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: +5\r\n\r\n"_ba
                                                    << 400;
        QTest::newRow("huge Content-Length") << "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999999999999\r\n\r\n"_ba
                                             << 400;
        QTest::newRow("over the body limit") << "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 2000000\r\n\r\n"_ba << 413;
        QTest::newRow("chunked over the limit") << "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
                                                   "1FFFFF\r\n"_ba
                                                << 413;
        QTest::newRow("folded header") << "GET / HTTP/1.1\r\nHost: a\r\nX: 1\r\n 2\r\n\r\n"_ba << 400;
        QTest::newRow("space before the colon") << "GET / HTTP/1.1\r\nHost : a\r\n\r\n"_ba << 400;
        QTest::newRow("no colon") << "GET / HTTP/1.1\r\nHost a\r\n\r\n"_ba << 400;
        QTest::newRow("NUL in a value") << QByteArray("GET / HTTP/1.1\r\nHost: a\r\nX: a\0b\r\n\r\n", 34) << 400;
        QTest::newRow("bare CR in a value") << "GET / HTTP/1.1\r\nHost: a\r\nX: a\rb\r\n\r\n"_ba << 400;
        QTest::newRow("escape sequence in a value") << "GET / HTTP/1.1\r\nHost: a\r\nX: \x1b[2J\r\n\r\n"_ba << 400;
        QTest::newRow("method with a slash") << "GE/T / HTTP/1.1\r\n"_ba + ok + "\r\n" << 400;
        QTest::newRow("two spaces") << "GET  / HTTP/1.1\r\n"_ba + ok + "\r\n" << 400;
        QTest::newRow("tab separator") << "GET\t/ HTTP/1.1\r\n"_ba + ok + "\r\n" << 400;
        QTest::newRow("HTTP/2.0") << "GET / HTTP/2.0\r\n"_ba + ok + "\r\n" << 505;
        QTest::newRow("HTTP/1.2") << "GET / HTTP/1.2\r\n"_ba + ok + "\r\n" << 505;
        QTest::newRow("not HTTP") << "GET / FTP/1.1\r\n"_ba + ok + "\r\n" << 400;
        QTest::newRow("binary garbage") << "\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03\r\n"_ba << 400;
        QTest::newRow("fragment in the target") << "GET /mcp#x HTTP/1.1\r\n"_ba + ok + "\r\n" << 400;
        QTest::newRow("authority form") << "CONNECT a:443 HTTP/1.1\r\n"_ba + ok + "\r\n" << 400;
        QTest::newRow("non-ASCII target") << "GET /żółw HTTP/1.1\r\n"_ba + ok + "\r\n" << 400;
        QTest::newRow("request line too long") << "GET /"_ba + QByteArray(9000, 'a') + " HTTP/1.1\r\n" << 414;
        QTest::newRow("header fields too large")
                << "GET / HTTP/1.1\r\nHost: a\r\nX: "_ba + QByteArray(40000, 'a') + "\r\n\r\n" << 431;
        QTest::newRow("too many header fields") << [] {
            QByteArray b = "GET / HTTP/1.1\r\nHost: a\r\n"_ba;
            for (int i = 0; i < 120; ++i)
                b += "X-" + QByteArray::number(i) + ": 1\r\n";
            b += "\r\n";
            return b;
        }() << 431;
        QTest::newRow("gzip, chunked") << "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: gzip, chunked\r\n\r\n"_ba
                                       << 501;
        QTest::newRow("identity") << "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: identity\r\n\r\n"_ba << 400;
        QTest::newRow("chunked in HTTP/1.0") << "POST / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n"_ba << 400;
        QTest::newRow("chunk size not hex") << "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n"_ba
                                            << 400;
        QTest::newRow("chunk size too long") << "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n"
                                                "00000000000000001\r\n"_ba
                                             << 400;
        QTest::newRow("chunk without its CRLF")
                << "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nabXX\r\n"_ba << 400;
        QTest::newRow("Expect other than 100-continue") << "GET / HTTP/1.1\r\nHost: a\r\nExpect: 200-ok\r\n\r\n"_ba
                                                        << 417;
        QTest::newRow("many empty lines") << "\r\n\r\n\r\n\r\nGET / HTTP/1.1\r\n"_ba + ok + "\r\n" << 400;
    }
    void malformed()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(int, status);
        for (const bool byteByByte : {false, true}) {
            const Parsed p = parseAll(bytes, 1024 * 1024, byteByByte);
            QCOMPARE(p.state, http::RequestParser::State::Failed);
            QCOMPARE(p.status, status);
        }
    }

    void incompleteStaysIncomplete()
    {
        // Half a request is no request: nothing is guessed.
        const QByteArray whole = "POST /token HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\n\r\nabcde"_ba;
        for (qsizetype n = 0; n < whole.size(); ++n) {
            const Parsed p = parseAll(whole.left(n));
            QCOMPARE(p.state, http::RequestParser::State::Incomplete);
            QCOMPARE(p.requests, 0);
        }
    }

    void bodyIsNotKeptBeforeTheLimit()
    {
        // After the header fields the parser waits for the server's decision: no body byte is
        // taken before it, whatever follows.
        http::RequestParser parser;
        QCOMPARE(parser.feed("POST /mcp HTTP/1.1\r\nHost: a\r\nContent-Length: 4\r\nExpect: 100-continue\r\n\r\nabcd"),
                 http::RequestParser::State::HeadersDone);
        QVERIFY(parser.expectsContinue());
        QVERIFY(parser.hasBody());
        QCOMPARE(parser.contentLength(), 4);
        QVERIFY(parser.request().body.isEmpty());
        QCOMPARE(parser.parse(), http::RequestParser::State::HeadersDone);
        parser.setBodyLimit(3);
        QCOMPARE(parser.state(), http::RequestParser::State::Failed);
        QCOMPARE(parser.errorStatus(), 413);
    }

    void response()
    {
        http::Response r = http::Response::json(201, {{u"a"_s, u"żółw"_s}});
        r.headers.set("X-Evil"_ba, "a\r\nSet-Cookie: x=1"_ba);
        r.close = true;
        const QByteArray out = r.serialize();
        QVERIFY(out.startsWith("HTTP/1.1 201 Created\r\n"));
        QVERIFY(out.contains(QByteArray("Content-Length: " + QByteArray::number(r.body.size()) + "\r\n")));
        QVERIFY(out.contains("Connection: close\r\n"));
        QVERIFY(out.contains("Cache-Control: no-store\r\n"));
        QVERIFY(!out.contains("\r\nSet-Cookie")); // no response splitting
        QVERIFY(out.endsWith("\r\n\r\n{\"a\":\"żółw\"}"));
        QVERIFY(!http::Response::empty(204).serialize().contains("Content-Length"));
        QVERIFY(!http::Response::text(200, "abc"_ba).serialize(true).endsWith("abc\n"));
    }

    void form_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QStringList>("pairs"); // name=value
        QTest::newRow("plain") << "a=1&b=2"_ba << true << QStringList{u"a=1"_s, u"b=2"_s};
        QTest::newRow("plus and escapes") << "x=Ala+ma+%C5%BC%C3%B3%C5%82wia&y=%2B%26%3D"_ba << true
                                          << QStringList{u"x=Ala ma żółwia"_s, u"y=+&="_s};
        QTest::newRow("empty parts and no value") << "&&a&b=&"_ba << true << QStringList{u"a="_s, u"b="_s};
        QTest::newRow("bad escape") << "a=%G1"_ba << false << QStringList{};
        QTest::newRow("cut escape") << "a=%C"_ba << false << QStringList{};
        QTest::newRow("invalid UTF-8") << "a=%C5"_ba << false << QStringList{};
        QTest::newRow("overlong UTF-8") << "a=%C0%AF"_ba << false << QStringList{};
        QTest::newRow("newline") << "a=1%0A2"_ba << false << QStringList{};
        QTest::newRow("NUL") << "a=%00"_ba << false << QStringList{};
    }
    void form()
    {
        QFETCH(QByteArray, data);
        QFETCH(bool, valid);
        QFETCH(QStringList, pairs);
        const auto parsed = http::parseForm(data);
        QCOMPARE(parsed.has_value(), valid);
        if (!valid)
            return;
        QStringList got;
        for (const auto &[name, value] : *parsed)
            got.append(name + u'=' + value);
        QCOMPARE(got, pairs);
        QCOMPARE(http::formEncode(u"a b+ż/?"_s), "a%20b%2B%C5%BC%2F%3F"_ba);
    }

    void language_data()
    {
        QTest::addColumn<QByteArray>("accept");
        QTest::addColumn<QString>("lang");
        QTest::newRow("Polish first") << "pl-PL,pl;q=0.9,en-US;q=0.8,en;q=0.7"_ba << u"pl"_s;
        QTest::newRow("English first") << "en-US,en;q=0.9,pl;q=0.8"_ba << u"en"_s;
        QTest::newRow("by q") << "en;q=0.5, pl;q=0.6"_ba << u"pl"_s;
        QTest::newRow("German") << "de-DE,de;q=0.9"_ba << u"en"_s;
        QTest::newRow("none") << ""_ba << u"en"_s;
        QTest::newRow("Polish refused") << "pl;q=0, en"_ba << u"en"_s;
        QTest::newRow("garbage") << ";;;q=x,,"_ba << u"en"_s;
    }
    void language()
    {
        QFETCH(QByteArray, accept);
        QFETCH(QString, lang);
        http::Request r;
        if (!accept.isEmpty())
            r.headers.add("Accept-Language"_ba, accept);
        QCOMPARE(http::preferredLanguage(r), lang);
    }

    void escape()
    {
        QCOMPARE(http::htmlEscape(u"<script>\"x\" & 'y'</script>\u0007ż"_s),
                 u"&lt;script&gt;&quot;x&quot; &amp; &#39;y&#39;&lt;/script&gt;ż"_s);
        QCOMPARE(http::originOf(QUrl(u"HTTPS://Notes.Example.com:443/mcp"_s)), u"https://notes.example.com"_s);
        QCOMPARE(http::originOf(QUrl(u"http://127.0.0.1:8780/x"_s)), u"http://127.0.0.1:8780"_s);
    }

    // ---- Secrets

    void pbkdf2_data()
    {
        // PBKDF2-HMAC-SHA256 test vectors (RFC 7914 section 11's PBKDF2 and the widely used
        // "password"/"salt" set).
        QTest::addColumn<QString>("password");
        QTest::addColumn<QByteArray>("salt");
        QTest::addColumn<int>("iterations");
        QTest::addColumn<QByteArray>("key");
        QTest::newRow("1") << u"password"_s << "salt"_ba << 1
                           << QByteArray::fromHex("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
        QTest::newRow("2") << u"password"_s << "salt"_ba << 2
                           << QByteArray::fromHex("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
        QTest::newRow("4096") << u"password"_s << "salt"_ba << 4096
                              << QByteArray::fromHex("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
        QTest::newRow("RFC 7914") << u"Password"_s << "NaCl"_ba << 80000
                                  << QByteArray::fromHex("4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56");
    }
    void pbkdf2()
    {
        QFETCH(QString, password);
        QFETCH(QByteArray, salt);
        QFETCH(int, iterations);
        QFETCH(QByteArray, key);
        const QString stored = u"pbkdf2-sha256$%1$%2$%3"_s.arg(QString::number(iterations),
                                                                QString::fromLatin1(salt.toBase64()),
                                                                QString::fromLatin1(key.toBase64()));
        QVERIFY(oauth::isPasswordHash(stored));
        QVERIFY(oauth::verifyPassword(stored, password));
        QVERIFY(!oauth::verifyPassword(stored, password + u'x'));
    }

    void passwordHashes()
    {
        const QString hash = oauth::hashPassword(u"Zażółć gęślą jaźń"_s, 1000);
        QVERIFY(hash.startsWith(u"pbkdf2-sha256$1000$"_s));
        QVERIFY(oauth::verifyPassword(hash, u"Zażółć gęślą jaźń"_s));
        QVERIFY(!oauth::verifyPassword(hash, u"Zazolc gesla jazn"_s));
        QVERIFY(!oauth::verifyPassword(hash, QString()));
        // Fresh salt: the same password never hashes the same.
        QVERIFY(oauth::hashPassword(u"x"_s, 1000) != oauth::hashPassword(u"x"_s, 1000));
        for (const QString &bad : {u""_s, u"plain"_s, u"pbkdf2-sha256$0$c2FsdHNhbHQ=$AA=="_s,
                                   u"pbkdf2-sha256$99999999999$c2FsdHNhbHQ=$AA=="_s, u"md5$1$a$b"_s,
                                   u"pbkdf2-sha256$1000$!!!$AA=="_s, u"pbkdf2-sha256$1000$c2FsdA==$c2hvcnQ="_s}) {
            QVERIFY2(!oauth::isPasswordHash(bad), qPrintable(bad));
            QVERIFY(!oauth::verifyPassword(bad, u"plain"_s));
        }
    }

    void pkce()
    {
        // RFC 7636 appendix B.
        QVERIFY(oauth::pkceMatches(u"dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"_s,
                                   u"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"_s));
        QVERIFY(oauth::isPkceChallenge(u"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"_s));
        QVERIFY(!oauth::pkceMatches(u"dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXl"_s,
                                    u"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"_s));
        QVERIFY(!oauth::pkceMatches(u"short"_s, u"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"_s));
        QVERIFY(!oauth::pkceMatches(QString(129, u'a'), u"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"_s));
        QVERIFY(!oauth::pkceMatches(u"dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXż"_s,
                                    u"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"_s));
        QVERIFY(!oauth::isPkceChallenge(u"plain"_s));
        QVERIFY(!oauth::isPkceChallenge(u"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-c="_s));
    }

    void secrets()
    {
        const QString a = oauth::randomToken(u"bat_"_s);
        QVERIFY(a.startsWith(u"bat_"_s));
        QCOMPARE(a.size(), 4 + 43);
        QVERIFY(a != oauth::randomToken(u"bat_"_s));
        QCOMPARE(oauth::tokenHash(u"abc"_s), u"ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0"_s);
        QVERIFY(oauth::constantTimeEquals("abc", "abc"));
        QVERIFY(!oauth::constantTimeEquals("abc", "abd"));
        QVERIFY(!oauth::constantTimeEquals("abc", "abcd"));
    }

    // ---- Checks

    void metadataClientIds_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("valid");
        QTest::newRow("Claude") << u"https://claude.ai/oauth/mcp-oauth-client-metadata"_s << true;
        QTest::newRow("ChatGPT") << u"https://chatgpt.com/oauth/client.json"_s << true;
        QTest::newRow("port 443") << u"https://example.com:443/c.json"_s << true;
        QTest::newRow("http") << u"http://example.com/c.json"_s << false;
        QTest::newRow("no path") << u"https://example.com"_s << false;
        QTest::newRow("root path") << u"https://example.com/"_s << false;
        QTest::newRow("other port") << u"https://example.com:8443/c.json"_s << false;
        QTest::newRow("IP literal") << u"https://10.0.0.1/c.json"_s << false;
        QTest::newRow("IPv6 literal") << u"https://[::1]/c.json"_s << false;
        QTest::newRow("user info") << u"https://a@example.com/c.json"_s << false;
        QTest::newRow("fragment") << u"https://example.com/c.json#x"_s << false;
        QTest::newRow("dot segment") << u"https://example.com/a/../c.json"_s << false;
        QTest::newRow("registered id") << u"bcl_abcdef"_s << false;
    }
    void metadataClientIds()
    {
        QFETCH(QString, id);
        QFETCH(bool, valid);
        QCOMPARE(oauth::isMetadataClientId(id), valid);
    }

    void redirectUris_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<bool>("valid");
        QTest::newRow("claude.ai") << u"https://claude.ai/api/mcp/auth_callback"_s << true;
        QTest::newRow("ChatGPT") << u"https://chatgpt.com/connector_platform_oauth_redirect"_s << true;
        QTest::newRow("loopback") << u"http://127.0.0.1:33418/callback"_s << true;
        QTest::newRow("localhost") << u"http://localhost/callback"_s << true;
        QTest::newRow("IPv6 loopback") << u"http://[::1]:5000/cb"_s << true;
        QTest::newRow("http elsewhere") << u"http://evil.example/cb"_s << false;
        QTest::newRow("javascript") << u"javascript:alert(1)"_s << false;
        QTest::newRow("custom scheme") << u"cursor://anysphere/cb"_s << false;
        QTest::newRow("fragment") << u"https://a.example/cb#x"_s << false;
        QTest::newRow("user info") << u"https://u:p@a.example/cb"_s << false;
        QTest::newRow("relative") << u"/cb"_s << false;
        QTest::newRow("space") << u"https://a.example/c b"_s << false;
        QTest::newRow("quote") << u"https://a.example/c\"b"_s << false;
        QTest::newRow("non-ASCII") << u"https://żółw.example/cb"_s << false;
    }
    void redirectUris()
    {
        QFETCH(QString, uri);
        QFETCH(bool, valid);
        QCOMPARE(oauth::isAllowedRedirectUri(uri), valid);
    }

    void redirectMatching_data()
    {
        QTest::addColumn<QString>("registered");
        QTest::addColumn<QString>("presented");
        QTest::addColumn<bool>("matches");
        QTest::newRow("same") << u"https://claude.ai/api/mcp/auth_callback"_s
                              << u"https://claude.ai/api/mcp/auth_callback"_s << true;
        QTest::newRow("trailing slash") << u"https://claude.ai/api/mcp/auth_callback"_s
                                        << u"https://claude.ai/api/mcp/auth_callback/"_s << false;
        QTest::newRow("case of the path") << u"https://claude.ai/api/mcp/auth_callback"_s
                                          << u"https://claude.ai/api/MCP/auth_callback"_s << false;
        QTest::newRow("query added") << u"https://claude.ai/cb"_s << u"https://claude.ai/cb?x=1"_s << false;
        QTest::newRow("loopback port") << u"http://127.0.0.1/callback"_s << u"http://127.0.0.1:41234/callback"_s
                                       << true;
        QTest::newRow("localhost port") << u"http://localhost/callback"_s << u"http://localhost:3118/callback"_s
                                        << true;
        QTest::newRow("loopback other path") << u"http://127.0.0.1/callback"_s << u"http://127.0.0.1:4/other"_s
                                             << false;
        QTest::newRow("loopback other host") << u"http://localhost/callback"_s << u"http://127.0.0.1:4/callback"_s
                                             << false;
        QTest::newRow("https port") << u"https://a.example/cb"_s << u"https://a.example:444/cb"_s << false;
    }
    void redirectMatching()
    {
        QFETCH(QString, registered);
        QFETCH(QString, presented);
        QFETCH(bool, matches);
        QCOMPARE(oauth::redirectMatches(registered, presented), matches);
    }

    void resources_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<bool>("same");
        QTest::newRow("equal") << u"https://notes.example.com/mcp"_s << u"https://notes.example.com/mcp"_s << true;
        QTest::newRow("case of host") << u"HTTPS://Notes.Example.com/mcp"_s << u"https://notes.example.com/mcp"_s
                                      << true;
        QTest::newRow("trailing slash") << u"https://notes.example.com/mcp/"_s << u"https://notes.example.com/mcp"_s
                                        << true;
        QTest::newRow("default port") << u"https://notes.example.com:443/mcp"_s << u"https://notes.example.com/mcp"_s
                                      << true;
        QTest::newRow("other path") << u"https://notes.example.com/"_s << u"https://notes.example.com/mcp"_s << false;
        QTest::newRow("other host") << u"https://evil.example/mcp"_s << u"https://notes.example.com/mcp"_s << false;
        QTest::newRow("other port") << u"https://notes.example.com:8443/mcp"_s << u"https://notes.example.com/mcp"_s
                                    << false;
        QTest::newRow("http") << u"http://notes.example.com/mcp"_s << u"https://notes.example.com/mcp"_s << false;
        QTest::newRow("fragment") << u"https://notes.example.com/mcp#x"_s << u"https://notes.example.com/mcp"_s
                                  << false;
    }
    void resources()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(bool, same);
        QCOMPARE(oauth::sameResource(a, b), same);
    }

    void publicAddresses_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("isPublic");
        for (const char *a : {"8.8.8.8", "160.79.104.10", "104.18.32.47", "2606:4700::6810:84e5"})
            QTest::newRow(a) << QString::fromLatin1(a) << true;
        for (const char *a : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "192.168.1.1", "169.254.169.254", "100.100.1.1",
                              "0.0.0.0", "255.255.255.255", "224.0.0.1", "192.0.2.1", "198.18.0.1", "240.0.0.1",
                              "::1", "::", "fe80::1", "fd00::1", "fc00::1", "ff02::1", "::ffff:10.0.0.1",
                              "::ffff:127.0.0.1", "64:ff9b::a00:1", "2001:db8::1"})
            QTest::newRow(a) << QString::fromLatin1(a) << false;
    }
    void publicAddresses()
    {
        QFETCH(QString, address);
        QFETCH(bool, isPublic);
        QCOMPARE(oauth::isPublicAddress(QHostAddress(address)), isPublic);
    }

    // ---- The state file

    void store()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"stan/mcp-remote.json"_s);
        oauth::Store a(path);
        QString error;
        QVERIFY(a.refresh(&error));
        QVERIFY(a.state().grants.isEmpty());
        const QDateTime now = QDateTime::currentDateTimeUtc();
        oauth::Grant grant;
        grant.id = u"bgr_1"_s;
        grant.clientName = u"Żółw"_s;
        grant.scopes = {u"notes:read"_s};
        grant.access = {{oauth::tokenHash(u"bat_x"_s), now.addSecs(60)}, {oauth::tokenHash(u"bat_old"_s), now.addSecs(-1)}};
        grant.refresh = {oauth::tokenHash(u"brt_x"_s), now.addDays(10)};
        oauth::Grant expired = grant;
        expired.id = u"bgr_2"_s;
        expired.access = {{oauth::tokenHash(u"bat_z"_s), now.addSecs(60)}};
        expired.refresh = {oauth::tokenHash(u"brt_z"_s), now.addSecs(-5)};
        QVERIFY(a.update(
                [&](oauth::State &s) {
                    s.password = oauth::hashPassword(u"hasło"_s, 1000);
                    s.grants = {grant, expired};
                    return true;
                },
                &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
#ifndef Q_OS_WIN
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteOther),
                 QFileDevice::Permissions());
#endif
        QFile written(path);
        QVERIFY(written.open(QIODevice::ReadOnly));
        const QByteArray onDisk = written.readAll();
        QVERIFY(!onDisk.contains("bat_x") && !onDisk.contains("brt_x") && !onDisk.contains("hasło")); // hashes only
        QVERIFY(a.grantByAccessToken(u"bat_x"_s));
        QVERIFY(!a.grantByAccessToken(u"bat_y"_s));
        // Another process (another Store) revokes: the first sees it on its next look.
        oauth::Store b(path);
        QVERIFY(b.update(
                [](oauth::State &s) {
                    s.grants.removeIf([](const oauth::Grant &g) { return g.id == u"bgr_1"; });
                    return true;
                },
                &error));
        QVERIFY(a.refresh(&error));
        QVERIFY(!a.grantByAccessToken(u"bat_x"_s));
        QCOMPARE(a.state().grants.size(), 1);
        oauth::State pruned = a.state();
        pruned.prune(now);
        QVERIFY(pruned.grants.isEmpty());
        QVERIFY(oauth::verifyPassword(a.state().password, u"hasło"_s));
        // A file of something else is refused, never overwritten.
        QFile other(dir.filePath(u"inny.json"_s));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("{\"format\":\"something else\"}");
        other.close();
        oauth::Store c(other.fileName());
        QVERIFY(!c.refresh(&error));
        QVERIFY(!c.update([](oauth::State &) { return true; }, &error));
        QFile check(other.fileName());
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), "{\"format\":\"something else\"}"_ba);
    }
};

QTEST_GUILESS_MAIN(TestCliHttp)
#include "tst_cli_http.moc"

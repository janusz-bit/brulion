// Redaction of secrets (log/redact.h): the pattern table, registered live secrets, idempotence,
// malformed input and concurrent use.
#include "log/redact.h"

#include <QTest>
#include <QThread>
#include <QUrl>

#include <atomic>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;
using namespace brulion::logging;

class TestLogRedact : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void cleanup() { clearSecrets(); }

    void patterns_data();
    void patterns();
    void unchanged_data();
    void unchanged();
    void registeredSecrets();
    void shortSecretIgnored();
    void referenceCounting();
    void scopedSecret();
    void malformedInput();
    void largeInput();
    void concurrent();
};

void TestLogRedact::patterns_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("url user info")
            << u"PUT https://jan:tajne@chmura.example.pl/remote.php/dav/files/jan/Brulion/Zażółć.md failed: HTTP 500"_s
            << u"PUT https://***@chmura.example.pl/remote.php/dav/files/jan/Brulion/Zażółć.md failed: HTTP 500"_s;
    QTest::newRow("url ipv6, empty user")
            << u"GET http://:hasło@[::1]:8080/dav ok"_s << u"GET http://***@[::1]:8080/dav ok"_s;
    QTest::newRow("url encoded user info") << u"https://user%40firma:p%C3%B3l@host/"_s << u"https://***@host/"_s;
    QTest::newRow("url user only") << u"webdav://jan@host/x"_s << u"webdav://***@host/x"_s;
    QTest::newRow("authorization header") << u"Authorization: Basic amFuOnRham5l"_s << u"Authorization: ***"_s;
    QTest::newRow("authorization lower case") << u"authorization:Bearer abc.def"_s << u"authorization:***"_s;
    QTest::newRow("header in a multi-line text") << u"PROPFIND /dav\nAuthorization: Basic amFuOnRham5l\nDepth: 1"_s
                                                 << u"PROPFIND /dav\nAuthorization: ***\nDepth: 1"_s;
    QTest::newRow("qdebug pairs")
            << u"QList(std::pair(\"Authorization\",\"Basic amFuOnRham5l\"), std::pair(\"X-Requested-With\",\"XMLHttpRequest\"))"_s
            << u"QList(std::pair(\"Authorization\",\"***\"), std::pair(\"X-Requested-With\",\"XMLHttpRequest\"))"_s;
    QTest::newRow("json authorization") << u"{\"authorization\":\"Bearer eyJhbGciOi.eyJzdWIi.c2lnbmF0dXJl\"}"_s
                                        << u"{\"authorization\":\"***\"}"_s;
    QTest::newRow("api key header") << u"X-API-Key: k3yS3cr3t-Syncthing"_s << u"X-API-Key: ***"_s;
    QTest::newRow("cookie") << u"Cookie: oc_sessionPassphrase=abc; nc_session_id=def"_s << u"Cookie: ***"_s;
    QTest::newRow("set-cookie") << u"Set-Cookie: nc_token=xyz; path=/; HttpOnly"_s << u"Set-Cookie: ***"_s;
    QTest::newRow("bearer in prose") << u"sent Bearer abcdefghijkl to the server"_s
                                     << u"sent Bearer *** to the server"_s;
    QTest::newRow("basic in prose") << u"token Basic amFuOnRham5l==, then"_s << u"token Basic ***, then"_s;
    QTest::newRow("query") << u"GET https://host/index.php?token=abc&x=1&access_token=q failed"_s
                           << u"GET https://host/index.php?token=***&x=1&access_token=*** failed"_s;
    QTest::newRow("query oauth code") << u"https://host/cb?code=12345&state=ok"_s
                                      << u"https://host/cb?code=***&state=ok"_s;
    QTest::newRow("json app password") << u"{\"appPassword\":\"AbCdE-FgHiJ-KlMnO-PqRsT-UvWxY\"}"_s
                                       << u"{\"appPassword\":\"***\"}"_s;
    QTest::newRow("app password shape") << u"użyj AbCdE-FgHiJ-KlMnO-PqRsT-UvWxY w Brulionie"_s
                                        << u"użyj *** w Brulionie"_s;
    QTest::newRow("key=value polish") << u"password=Zażółć gęślą"_s << u"password=***"_s;
    QTest::newRow("key=value then more") << u"user=jan, password=Zażółć gęślą, folder=Brulion"_s
                                         << u"user=jan, password=***, folder=Brulion"_s;
    QTest::newRow("json password polish")
            << u"{\"password\": \"Zażółć\", \"user\": \"jan\"}"_s << u"{\"password\": \"***\", \"user\": \"jan\"}"_s;
    QTest::newRow("json escaped quote") << u"{\"secret\":\"a\\\"b\",\"x\":1}"_s << u"{\"secret\":\"***\",\"x\":1}"_s;
    QTest::newRow("prefixed keys") << u"app_password=abc nc_session_id=def"_s << u"app_password=***"_s;
    QTest::newRow("spaces around =") << u"api_key = abcdef"_s << u"api_key = ***"_s;
    QTest::newRow("short key") << u"sid=12345"_s << u"sid=***"_s;
    QTest::newRow("jwt")
            << u"token eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N_XgL0n3I9PlFUP0THsR8U done"_s
            << u"token *** done"_s;
    QTest::newRow("polish prose kept") << u"hasło aplikacji: nie zapisano"_s << u"hasło aplikacji: nie zapisano"_s;
}

void TestLogRedact::patterns()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    const QString once = redact(input);
    QCOMPARE(once, expected);
    QCOMPARE(redact(once), once); // idempotent
}

void TestLogRedact::unchanged_data()
{
    QTest::addColumn<QString>("input");
    QTest::newRow("note") << u"Notatka: Zakupy na jutro"_s;
    QTest::newRow("keyboard") << u"keyboard=pl"_s;
    QTest::newRow("monkey") << u"monkey=1"_s;
    QTest::newRow("passage") << u"passage of time"_s;
    QTest::newRow("author") << u"author=Jan Kowalski"_s;
    QTest::newRow("journal path") << u"Dziennik/2026-10-18.md"_s;
    QTest::newRow("dash") << u"ab-cd"_s;
    QTest::newRow("uuid-like") << u"12345-6789-abcde"_s;
    QTest::newRow("sync line")
            << u"#42 ok in 14.2 s (edit): up 3, down 1 | remote 8.4 s (41 folders, 3120 files, 1.4 MB) · PROPFIND 41"_s;
    QTest::newRow("url without user") << u"PROPFIND https://chmura.example.pl/remote.php/dav/files/jan/ -> 207"_s;
    QTest::newRow("email") << u"kontakt: jan@example.com"_s;
    QTest::newRow("session line") << u"=== session start: Brulion 0.2.1 (linux), pid 12, UTC+02:00 ==="_s;
    QTest::newRow("basic short") << u"Basic auth failed"_s;
    QTest::newRow("empty") << QString();
}

void TestLogRedact::unchanged()
{
    QFETCH(QString, input);
    QCOMPARE(redact(input), input);
}

void TestLogRedact::registeredSecrets()
{
    const QString secret = u"Zażółć-Gęślą-Jaźń-42!"_s;
    addSecret(secret);
    QCOMPARE(secretCount(), 1);
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(secret));
    const QString base64 = QString::fromLatin1(secret.toUtf8().toBase64());
    const QString text = u"a %1 b %2 c %3 d %4"_s.arg(secret, encoded, encoded.toLower(), base64);
    const QString out = redact(text);
    QVERIFY2(!out.contains(secret), qPrintable(out));
    QVERIFY2(!out.contains(encoded), qPrintable(out));
    QVERIFY2(!out.contains(encoded.toLower()), qPrintable(out));
    QVERIFY2(!out.contains(base64), qPrintable(out));
    QCOMPARE(out, u"a *** b *** c *** d ***"_s);

    // The Basic token of user:secret, registered as a secret of its own (WebDavClient does).
    const QString token = QString::fromLatin1((u"jan:"_s + secret).toUtf8().toBase64());
    addSecret(token);
    QCOMPARE(redact(u"x-token " + token), u"x-token ***"_s);
}

void TestLogRedact::shortSecretIgnored()
{
    addSecret(u"abc"_s);
    QCOMPARE(secretCount(), 0);
    QCOMPARE(redact(u"abc abc"_s), u"abc abc"_s);
}

void TestLogRedact::referenceCounting()
{
    addSecret(u"sekret-1234"_s);
    addSecret(u"sekret-1234"_s);
    removeSecret(u"sekret-1234"_s);
    QCOMPARE(redact(u"sekret-1234"_s), u"***"_s);
    removeSecret(u"sekret-1234"_s);
    QCOMPARE(redact(u"sekret-1234"_s), u"sekret-1234"_s);
    removeSecret(u"never-added"_s); // harmless
    QCOMPARE(secretCount(), 0);
}

void TestLogRedact::scopedSecret()
{
    {
        ScopedSecret a(u"pierwszy-sekret"_s);
        ScopedSecret b = std::move(a);
        QCOMPARE(redact(u"pierwszy-sekret"_s), u"***"_s);
        b.reset(u"drugi-sekret"_s);
        QCOMPARE(redact(u"pierwszy-sekret"_s), u"pierwszy-sekret"_s);
        QCOMPARE(redact(u"drugi-sekret"_s), u"***"_s);
    }
    QCOMPARE(secretCount(), 0);
}

void TestLogRedact::malformedInput()
{
    QString broken = u"password="_s;
    broken += QChar(0xD800); // a lone surrogate
    broken += u" ab\u0000cd"_s;
    const QString out = redact(broken);
    QVERIFY(out.startsWith(u"password=***"_s));
    QCOMPARE(redact(out), out);
    QCOMPARE(redact(u"="_s), u"="_s);
    QCOMPARE(redact(u"://@"_s), u"://@"_s);
    QCOMPARE(redact(u"\"password\":\""_s), u"\"password\":\""_s); // unterminated value stays
}

void TestLogRedact::largeInput()
{
    QString big;
    big.reserve(1024 * 1024 + 100);
    while (big.size() < 1024 * 1024)
        big += u"Dziennik/2026-10-18.md zażółć gęślą jaźń; "_s;
    big += u"password=tajne"_s;
    const QString out = redact(big);
    QVERIFY(out.endsWith(u"password=***"_s));
    QCOMPARE(out.size(), big.size() - 2);
}

void TestLogRedact::concurrent()
{
    std::atomic<int> failures{0};
    std::vector<std::unique_ptr<QThread>> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back(QThread::create([t, &failures] {
            const QString secret = u"sekret-wątku-%1"_s.arg(t);
            for (int i = 0; i < 200; ++i) {
                addSecret(secret);
                if (redact(u"x "_s + secret + u" y password=abc"_s) != u"x *** y password=***"_s)
                    ++failures;
                removeSecret(secret);
            }
        }));
        threads.back()->start();
    }
    for (auto &thread : threads)
        QVERIFY(thread->wait(60'000 * BRULION_TEST_SLOWDOWN));
    QCOMPARE(failures.load(), 0);
    QCOMPARE(secretCount(), 0);
}

QTEST_GUILESS_MAIN(TestLogRedact)
#include "tst_log_redact.moc"

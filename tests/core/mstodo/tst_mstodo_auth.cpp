// The device-code sign-in against FakeMicrosoft: the requests (form bodies, endpoints of the
// authority), polling with authorization_pending and slow_down, every way it ends (declined,
// expired, AADSTS answers explained, a lost connection), cancel, input validation — and that the
// token never shows up in a message.
#include "fakemicrosoft.h"

#include "mstodo/mstodo.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::mstodo::DeviceCodeLogin;
using mstodotest::FakeMicrosoft;

class TestMsTodoAuth : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void signIn()
    {
        FakeMicrosoft ms;
        DeviceCodeLogin login(&ms);
        login.setTimeUnitMs(1);
        QSignalSpy code(&login, &DeviceCodeLogin::codeReady);
        QSignalSpy signedIn(&login, &DeviceCodeLogin::signedIn);
        QSignalSpy failed(&login, &DeviceCodeLogin::failed);
        login.start(u" {3F2504E0-4F89-41D3-9A0C-0305E82C3301} "_s, u"Consumers"_s);
        QVERIFY(login.active());
        QVERIFY(signedIn.wait(5000));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(code.size(), 1);
        QCOMPARE(code.first().at(0).toString(), u"F7GH2K9QX"_s);
        QCOMPARE(code.first().at(1).toUrl(), QUrl(u"https://microsoft.com/devicelogin"_s));
        QCOMPARE(code.first().at(2).toInt(), 900);
        QCOMPARE(signedIn.first().at(0).toString(), ms.accessToken);
        QVERIFY(!login.active());
        QCOMPARE(ms.polls, 3); // pending, pending, the token

        QCOMPARE(ms.requests.size(), 4);
        const auto &first = ms.requests.first();
        QCOMPARE(first.method, QByteArray("POST"));
        QCOMPARE(first.url, QUrl(u"https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode"_s));
        QCOMPARE(first.body, QByteArray("client_id=3f2504e0-4f89-41d3-9a0c-0305e82c3301&scope=Tasks.Read"));
        QCOMPARE(first.header("Content-Type"), QByteArray("application/x-www-form-urlencoded"));
        QVERIFY(first.timeoutMs > 0);
        const auto &poll = ms.requests.last();
        QCOMPARE(poll.url, QUrl(u"https://login.microsoftonline.com/consumers/oauth2/v2.0/token"_s));
        QCOMPARE(poll.body, QByteArray("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&client_id="
                                       "3f2504e0-4f89-41d3-9a0c-0305e82c3301&device_code=fake-device-code"));
        // No refresh token is asked for (no offline_access) and nothing else is contacted.
        for (const auto &r : std::as_const(ms.requests)) {
            QCOMPARE(r.url.host(), u"login.microsoftonline.com"_s);
            QVERIFY(!r.body.contains("offline_access"));
        }
    }

    void authorities_data()
    {
        QTest::addColumn<QString>("authority");
        QTest::addColumn<QString>("path");
        QTest::newRow("default") << QString() << u"/consumers/oauth2/v2.0/devicecode"_s;
        QTest::newRow("organizations") << u"organizations"_s << u"/organizations/oauth2/v2.0/devicecode"_s;
        QTest::newRow("tenant id") << u"72F988BF-86F1-41AF-91AB-2D7CD011DB47"_s
                                   << u"/72f988bf-86f1-41af-91ab-2d7cd011db47/oauth2/v2.0/devicecode"_s;
        QTest::newRow("tenant domain") << u"kowalscy.onmicrosoft.com"_s
                                       << u"/kowalscy.onmicrosoft.com/oauth2/v2.0/devicecode"_s;
    }

    void authorities()
    {
        QFETCH(QString, authority);
        QFETCH(QString, path);
        FakeMicrosoft ms;
        DeviceCodeLogin login(&ms);
        login.setTimeUnitMs(1);
        QSignalSpy code(&login, &DeviceCodeLogin::codeReady);
        login.start(ms.clientId, authority);
        QVERIFY(code.wait(5000));
        QCOMPARE(ms.requests.first().url.path(), path);
        login.cancel();
    }

    void slowDown()
    {
        FakeMicrosoft ms;
        ms.pollScript = {u"authorization_pending"_s, u"slow_down"_s, u"authorization_pending"_s, u"slow_down"_s};
        DeviceCodeLogin login(&ms);
        login.setTimeUnitMs(1);
        QSignalSpy signedIn(&login, &DeviceCodeLogin::signedIn);
        login.start(ms.clientId);
        QCOMPARE(login.intervalSeconds(), 5);
        QVERIFY(signedIn.wait(5000));
        QCOMPARE(login.intervalSeconds(), 15); // +5 s per slow_down
        QCOMPARE(ms.polls, 5);
    }

    void failures_data()
    {
        QTest::addColumn<QStringList>("script");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("messagePart");
        QTest::newRow("declined") << QStringList{u"authorization_pending"_s, u"authorization_declined"_s}
                                  << u"declined"_s << u"declined"_s;
        QTest::newRow("expired") << QStringList{u"expired_token"_s} << u"expired"_s << u"expired"_s;
        QTest::newRow("bad code") << QStringList{u"bad_verification_code"_s} << u"bad-code"_s << u"code"_s;
        QTest::newRow("public client flows off") << QStringList{u"AADSTS7000218"_s} << u"public-client-disabled"_s
                                                 << u"Allow public client flows"_s;
        QTest::newRow("wrong account type") << QStringList{u"AADSTS50059"_s} << u"wrong-account-type"_s
                                            << u"consumers"_s;
        QTest::newRow("wrong audience") << QStringList{u"AADSTS9002331"_s} << u"wrong-account-type"_s
                                        << u"AADSTS9002331"_s;
        QTest::newRow("consent") << QStringList{u"AADSTS65001"_s} << u"consent"_s << u"Tasks.Read"_s;
        QTest::newRow("unknown answer") << QStringList{u"invalid_grant"_s} << u"server"_s << u"AADSTS70016"_s;
        QTest::newRow("connection lost") << QStringList{u"network"_s, u"network"_s, u"network"_s, u"network"_s}
                                         << u"network"_s << u"internet"_s;
    }

    void failures()
    {
        QFETCH(QStringList, script);
        QFETCH(QString, code);
        QFETCH(QString, messagePart);
        FakeMicrosoft ms;
        ms.pollScript = script;
        DeviceCodeLogin login(&ms);
        login.setTimeUnitMs(1);
        QSignalSpy signedIn(&login, &DeviceCodeLogin::signedIn);
        QSignalSpy failed(&login, &DeviceCodeLogin::failed);
        login.start(ms.clientId);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), code);
        const QString message = failed.first().at(1).toString();
        QVERIFY2(message.contains(messagePart), qPrintable(message));
        QVERIFY(!message.contains(ms.accessToken));
        QVERIFY(!message.contains(u"fake-device-code"_s));
        QCOMPARE(signedIn.size(), 0);
        QVERIFY(!login.active());
        // Nothing more is sent after the end.
        const auto sent = ms.requests.size();
        QTest::qWait(30);
        QCOMPARE(ms.requests.size(), sent);
    }

    void networkHiccup()
    {
        // One dropped connection while the user signs in on the phone is no reason to give up.
        FakeMicrosoft ms;
        ms.pollScript = {u"network"_s, u"authorization_pending"_s, u"network"_s};
        DeviceCodeLogin login(&ms);
        login.setTimeUnitMs(1);
        QSignalSpy signedIn(&login, &DeviceCodeLogin::signedIn);
        login.start(ms.clientId);
        QVERIFY(signedIn.wait(5000));
    }

    void unknownClient()
    {
        FakeMicrosoft ms;
        ms.clientId = u"00000000-0000-0000-0000-000000000001"_s;
        DeviceCodeLogin login(&ms);
        QSignalSpy code(&login, &DeviceCodeLogin::codeReady);
        QSignalSpy failed(&login, &DeviceCodeLogin::failed);
        login.start(u"3f2504e0-4f89-41d3-9a0c-0305e82c3301"_s);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), u"unknown-client"_s);
        QVERIFY(failed.first().at(1).toString().contains(u"AADSTS700016"_s));
        // Microsoft's trace id and time stamp are not shown.
        QVERIFY(!failed.first().at(1).toString().contains(u"Trace ID"_s));
        QCOMPARE(code.size(), 0);
    }

    void deviceCodeRefused()
    {
        FakeMicrosoft ms;
        ms.deviceCodeError = QJsonObject{{u"error"_s, u"invalid_request"_s},
                                         {u"error_description"_s, u"AADSTS9002331: Application is configured for use "
                                                                  u"by Microsoft Account users only.\r\nTrace ID: 2"_s},
                                         {u"error_codes"_s, QJsonArray{9002331}}};
        DeviceCodeLogin login(&ms);
        QSignalSpy failed(&login, &DeviceCodeLogin::failed);
        login.start(ms.clientId, u"organizations"_s);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), u"wrong-account-type"_s);
    }

    void expiresWhileWaiting()
    {
        FakeMicrosoft ms;
        ms.expiresIn = 60; // 60 ms with a 1 ms "second"
        ms.interval = 1;
        ms.pollScript = QStringList(1000, u"authorization_pending"_s);
        DeviceCodeLogin login(&ms);
        login.setTimeUnitMs(1);
        QSignalSpy failed(&login, &DeviceCodeLogin::failed);
        login.start(ms.clientId);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), u"expired"_s);
        QVERIFY(ms.polls < 1000);
    }

    void cancel()
    {
        FakeMicrosoft ms;
        ms.pollScript = QStringList(50, u"authorization_pending"_s);
        DeviceCodeLogin login(&ms);
        login.setTimeUnitMs(1);
        QSignalSpy code(&login, &DeviceCodeLogin::codeReady);
        QSignalSpy signedIn(&login, &DeviceCodeLogin::signedIn);
        QSignalSpy failed(&login, &DeviceCodeLogin::failed);
        login.start(ms.clientId);
        QVERIFY(code.wait(5000));
        QTest::qWait(20);
        login.cancel();
        QVERIFY(!login.active());
        const auto sent = ms.requests.size();
        QTest::qWait(50);
        QCOMPARE(ms.requests.size(), sent);
        QCOMPARE(signedIn.size(), 0);
        QCOMPARE(failed.size(), 0);
    }

    void cancelWhileWaitingForTheCode()
    {
        FakeMicrosoft ms;
        DeviceCodeLogin login(&ms);
        QSignalSpy code(&login, &DeviceCodeLogin::codeReady);
        login.start(ms.clientId);
        login.cancel(); // the answer arrives later and is ignored
        QTest::qWait(30);
        QCOMPARE(code.size(), 0);
        QCOMPARE(ms.requests.size(), 1);
    }

    void validation_data()
    {
        QTest::addColumn<QString>("clientId");
        QTest::addColumn<QString>("authority");
        QTest::addColumn<QString>("error"); // "" = valid
        const QString id = u"3f2504e0-4f89-41d3-9a0c-0305e82c3301"_s;
        QTest::newRow("valid") << id << u"consumers"_s << QString();
        QTest::newRow("upper case with braces") << u"{3F2504E0-4F89-41D3-9A0C-0305E82C3301}"_s << u"common"_s << QString();
        QTest::newRow("empty id") << QString() << u"consumers"_s << u"invalid-client-id"_s;
        QTest::newRow("spaces") << u"   "_s << u"consumers"_s << u"invalid-client-id"_s;
        QTest::newRow("too short") << u"3f2504e0-4f89-41d3-9a0c"_s << u"consumers"_s << u"invalid-client-id"_s;
        QTest::newRow("not hex") << u"3f2504e0-4f89-41d3-9a0c-0305e82c330ż"_s << u"consumers"_s
                                 << u"invalid-client-id"_s;
        QTest::newRow("secret instead of id") << u"abc8Q~xyz.secret-value"_s << u"consumers"_s << u"invalid-client-id"_s;
        QTest::newRow("empty authority") << id << QString() << QString();
        QTest::newRow("tenant domain") << id << u"firma.pl"_s << QString();
        QTest::newRow("path in authority") << id << u"../common"_s << u"invalid-authority"_s;
        QTest::newRow("url as authority") << id << u"https://login.microsoftonline.com/consumers"_s
                                          << u"invalid-authority"_s;
        QTest::newRow("spaces in authority") << id << u"moja firma"_s << u"invalid-authority"_s;
        QTest::newRow("polish letters") << id << u"źródło.pl"_s << u"invalid-authority"_s;
    }

    void validation()
    {
        QFETCH(QString, clientId);
        QFETCH(QString, authority);
        QFETCH(QString, error);
        QCOMPARE(brulion::mstodo::validateClientId(clientId).isEmpty(), error != u"invalid-client-id");
        QCOMPARE(brulion::mstodo::validateAuthority(authority).isEmpty(), error != u"invalid-authority");
        FakeMicrosoft ms;
        DeviceCodeLogin login(&ms);
        QSignalSpy failed(&login, &DeviceCodeLogin::failed);
        login.start(clientId, authority);
        if (error.isEmpty()) {
            QCOMPARE(ms.requests.size(), 1);
            login.cancel();
        } else {
            QCOMPARE(failed.size(), 1); // at once, nothing sent
            QCOMPARE(failed.first().at(0).toString(), error);
            QVERIFY(!failed.first().at(1).toString().isEmpty());
            QCOMPARE(ms.requests.size(), 0);
            QVERIFY(!login.active());
        }
    }

    void verificationAddress()
    {
        // Only Microsoft's own addresses are shown to the user.
        FakeMicrosoft ms;
        DeviceCodeLogin login(&ms);
        QSignalSpy code(&login, &DeviceCodeLogin::codeReady);
        login.start(ms.clientId);
        QVERIFY(code.wait(5000));
        QCOMPARE(code.first().at(1).toUrl().host(), u"microsoft.com"_s);
        login.cancel();
    }
};

QTEST_GUILESS_MAIN(TestMsTodoAuth)
#include "tst_mstodo_auth.moc"

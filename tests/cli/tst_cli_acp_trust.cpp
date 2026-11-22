// Pairing without a code in a tailnet (`acp serve --tailscale`, src/cli/acptrust.h) against a fake
// tailscaled over a real local socket (tests/common/faketailscaled.h): tailscaled's whois allowed
// (the owner's phone, a user named with --tailnet-user) and denied (another user, a tagged device,
// nobody known, tailscaled not reached); Tailscale Serve's Tailscale-User-Login header taken only
// from loopback by a server on loopback alone behind Serve — a forged one from the LAN or the
// tailnet, two of them, or one to a server not behind Serve refused, whois winning over it; the host
// over HTTP pairing by identity (and 403 code_needed otherwise, the code still working); and
// brulion-cli itself as a process: `acp serve --tailscale --stop-on-eof` naming the computer by its
// tailnet name, pairing the owner's device without a code, stopping when its standard input ends,
// and refusing without Tailscale or with options that do not go together.
#include "acphost.h"
#include "acptrust.h"
#include "cli_p.h"

#include "faketailscaled.h"
#include "tailscale/localapi.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::cli;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;

const QString kOwner = u"ala.żółć@github"_s;
const QString kOther = u"ola@github"_s;

struct HttpResult {
    int status = 0;
    QJsonObject json;
};

http::Request request(const QString &peer, quint16 port, const QList<std::pair<QByteArray, QByteArray>> &headers = {})
{
    http::Request r;
    r.method = "POST";
    r.path = "/acp/v1/pair";
    r.peer = QHostAddress(peer);
    r.peerPort = port;
    for (const auto &[name, value] : headers)
        r.headers.add(name, value);
    return r;
}

QList<std::pair<QByteArray, QByteArray>> loginHeader(const QString &login)
{
    return {{"Tailscale-User-Login"_ba, login.toUtf8()}};
}

} // namespace

class TestCliAcpTrust : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    QString m_vault;
    std::unique_ptr<Context> m_ctx;
    std::unique_ptr<FakeTailscaled> m_ts;
    std::unique_ptr<tailscale::LocalApi> m_api;
    QNetworkAccessManager *m_nam = nullptr;

    PairTrust verdict(const TailnetTrust &trust, const http::Request &r)
    {
        PairTrust out;
        bool done = false;
        trust.check(r, [&](const PairTrust &v) {
            out = v;
            done = true;
        });
        if (!QTest::qWaitFor([&done] { return done; }, kTimeoutMs))
            out.why = u"no answer"_s;
        return out;
    }

    HttpResult http(const QString &base, const QByteArray &method, const QString &endpoint, const QByteArray &body,
                    const QList<std::pair<QByteArray, QByteArray>> &headers = {})
    {
        QNetworkRequest req(QUrl(base + u"/acp/v1/"_s + endpoint));
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        if (!body.isEmpty())
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"_ba);
        for (const auto &[name, value] : headers)
            req.setRawHeader(name, value);
        QNetworkReply *reply = m_nam->sendCustomRequest(req, method, body);
        HttpResult out;
        if (!QTest::qWaitFor([reply] { return reply->isFinished(); }, kTimeoutMs)) {
            reply->abort();
            reply->deleteLater();
            return out;
        }
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.json = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        return out;
    }

    static QStringList cliEnvironment(const QString &home, const QString &socket)
    {
        QStringList env = QProcess::systemEnvironment();
        env.removeIf([](const QString &v) {
            return v.startsWith(u"BRULION_"_s) || v.startsWith(u"XDG_"_s) || v.startsWith(u"HOME="_s);
        });
        env << u"HOME="_s + home << u"XDG_CONFIG_HOME="_s + home + u"/.config"_s
            << u"XDG_DATA_HOME="_s + home + u"/.local/share"_s << u"BRULION_TAILSCALE_SOCKET="_s + socket
            << u"BRULION_TAILSCALE_PROGRAM="_s << u"LANGUAGE=en"_s;
        return env;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        for (const char *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
            qputenv(name, m_tmp.filePath(QString::fromLatin1(name)).toLocal8Bit());
        QStandardPaths::setTestModeEnabled(true);
        m_vault = m_tmp.filePath(u"Notatki"_s);
        QVERIFY(QDir().mkpath(m_vault));
        QFile note(m_vault + u"/Dom.md"_s);
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("- żółta ćma\n");
        note.close();
        m_ctx = std::make_unique<Context>();
        m_ctx->globals.vault = m_vault;
        m_ctx->globals.stateDir = m_tmp.filePath(u"state"_s);
        m_ctx->serving = true;
        QVERIFY(!m_ctx->resolveVault());

        m_ts = std::make_unique<FakeTailscaled>(m_tmp.filePath(u"tailscaled.sock"_s));
        QVERIFY(m_ts->listening());
        m_ts->status = FakeTailscaled::statusJson(u"nixos.tail1ea20e.ts.net"_s, {u"127.0.0.1"_s}, kOwner);
        m_ts->whois.insert(u"100.98.93.11"_s, FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s));
        m_ts->whois.insert(u"100.98.93.12"_s, FakeTailscaled::whoisJson(kOther, u"telefon-oli"_s, {}, 77));
        m_ts->whois.insert(u"100.94.150.31"_s,
                           FakeTailscaled::whoisJson(u"tagged-devices"_s, u"raspberry-pi-4"_s, {u"tag:rpi"_s}, 80));
        m_ts->whois.insert(u"fd7a:115c:a1e0::6432:5d0c"_s, FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s));
        m_api = std::make_unique<tailscale::LocalApi>();
        m_api->setSocketPath(m_ts->path());
        m_api->setProgram(QString());
        m_nam = new QNetworkAccessManager(this);
    }

    void whoisDecides()
    {
        const TailnetTrust trust(m_api.get(), kOwner, {});
        QCOMPARE(trust.allowedLogins(), QStringList{kOwner});
        PairTrust v = verdict(trust, request(u"100.98.93.11"_s, 41641));
        QVERIFY2(v.trusted, qPrintable(v.why));
        QCOMPARE(v.login, kOwner);
        QCOMPARE(v.node, u"pixel-9-pro-1"_s);
        QCOMPARE(m_ts->targets.last(), u"/localapi/v0/whois?addr=100.98.93.11:41641"_s);
        v = verdict(trust, request(u"fd7a:115c:a1e0::6432:5d0c"_s, 41641));
        QVERIFY2(v.trusted, qPrintable(v.why));
        // Another user's phone, a tagged device, an address tailscaled does not know: the code.
        v = verdict(trust, request(u"100.98.93.12"_s, 5000));
        QVERIFY(!v.trusted);
        QVERIFY(v.why.contains(kOther));
        v = verdict(trust, request(u"100.94.150.31"_s, 5000));
        QVERIFY(!v.trusted);
        QVERIFY(v.why.contains(u"tag:rpi"_s));
        v = verdict(trust, request(u"100.70.0.1"_s, 5000));
        QVERIFY(!v.trusted);
        QVERIFY(v.why.contains(u"does not know"_s));
        // Outside the tailnet tailscaled is not even asked.
        const qsizetype asked = m_ts->targets.size();
        v = verdict(trust, request(u"192.168.1.20"_s, 5000));
        QVERIFY(!v.trusted);
        QCOMPARE(m_ts->targets.size(), asked);
    }

    void moreUsers()
    {
        // --tailnet-user: Ola's phone too; the owner's stays; "tagged-devices" is never a user.
        const TailnetTrust trust(m_api.get(), kOwner, {{u" OLA@github "_s, u"tagged-devices"_s, kOwner}, false, false, {}});
        QCOMPARE(trust.allowedLogins(), (QStringList{kOwner, u"OLA@github"_s}));
        QVERIFY(verdict(trust, request(u"100.98.93.12"_s, 1)).trusted);
        QVERIFY(verdict(trust, request(u"100.98.93.11"_s, 1)).trusted);
        QVERIFY(!verdict(trust, request(u"100.94.150.31"_s, 1)).trusted);
    }

    void forgedHeaderRefused()
    {
        // Behind Serve, on loopback alone: a header from the LAN is someone's own writing.
        const TailnetTrust trust(m_api.get(), kOwner, {{}, true, true, {}});
        const qsizetype asked = m_ts->targets.size();
        PairTrust v = verdict(trust, request(u"192.168.1.20"_s, 5000, loginHeader(kOwner)));
        QVERIFY(!v.trusted);
        QVERIFY(v.why.contains(u"ignored"_s));
        QCOMPARE(m_ts->targets.size(), asked);
        // From the tailnet: whois decides (Ola's phone), not the header.
        v = verdict(trust, request(u"100.98.93.12"_s, 5000, loginHeader(kOwner)));
        QVERIFY(!v.trusted);
        QVERIFY(v.why.contains(kOther));
        QVERIFY(v.why.contains(u"ignored"_s));
    }

    void serveHeader_data()
    {
        QTest::addColumn<bool>("behindServe");
        QTest::addColumn<bool>("loopbackOnly");
        QTest::addColumn<QString>("login");
        QTest::addColumn<bool>("twice");
        QTest::addColumn<bool>("trusted");
        QTest::newRow("serve") << true << true << kOwner << false << true;
        QTest::newRow("serve, letter case") << true << true << u"Ala.Żółć@GitHub"_s << false << true;
        QTest::newRow("another user") << true << true << kOther << false << false;
        QTest::newRow("two headers") << true << true << kOwner << true << false;
        QTest::newRow("not behind serve") << false << true << kOwner << false << false;
        QTest::newRow("listening beyond loopback") << true << false << kOwner << false << false;
    }
    void serveHeader()
    {
        QFETCH(bool, behindServe);
        QFETCH(bool, loopbackOnly);
        QFETCH(QString, login);
        QFETCH(bool, twice);
        QFETCH(bool, trusted);
        const TailnetTrust trust(m_api.get(), kOwner, {{}, behindServe, loopbackOnly, {}});
        QList<std::pair<QByteArray, QByteArray>> headers = loginHeader(login);
        if (twice)
            headers += loginHeader(kOwner);
        // (tailscaled knows nobody at 127.0.0.1:5555: the header is what is left.)
        const PairTrust v = verdict(trust, request(u"127.0.0.1"_s, 5555, headers));
        QCOMPARE(v.trusted, trusted);
        QCOMPARE(m_ts->targets.last(), u"/localapi/v0/whois?addr=127.0.0.1:5555"_s);
    }

    void whoisWinsOverServeHeader()
    {
        // A connection Serve proxied that tailscaled knows (its proxy map): Ola's, whatever the header.
        m_ts->whois.insert(u"127.0.0.1:6000"_s, FakeTailscaled::whoisJson(kOther, u"telefon-oli"_s, {}, 77));
        const TailnetTrust trust(m_api.get(), kOwner, {{}, true, true, {}});
        QVERIFY(!verdict(trust, request(u"127.0.0.1"_s, 6000, loginHeader(kOwner))).trusted);
        m_ts->whois.insert(u"127.0.0.1:6001"_s, FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s));
        const PairTrust v = verdict(trust, request(u"127.0.0.1"_s, 6001));
        QVERIFY(v.trusted);
        QCOMPARE(v.node, u"pixel-9-pro-1"_s);
    }

    void thisComputerItself()
    {
        // Another user of this computer connecting to its own tailnet address: tailscaled says it is
        // this computer (its owner's login) — never paired without a code.
        m_ts->whois.insert(u"100.108.227.25"_s, FakeTailscaled::whoisJson(kOwner, u"nixos"_s));
        TailnetTrustOptions o;
        o.thisNode = u"nixos.tail1ea20e.ts.net"_s;
        const TailnetTrust trust(m_api.get(), kOwner, o);
        const PairTrust v = verdict(trust, request(u"100.108.227.25"_s, 40000));
        QVERIFY(!v.trusted);
        QVERIFY2(v.why.contains(u"this computer"_s), qPrintable(v.why));
        // The owner's phone still is.
        QVERIFY(verdict(trust, request(u"100.98.93.11"_s, 40000)).trusted);
    }

    void tailscaledDown()
    {
        tailscale::LocalApi down;
        down.setSocketPath(m_tmp.filePath(u"none.sock"_s));
        down.setProgram(QString());
        const TailnetTrust trust(&down, kOwner, {});
        const PairTrust v = verdict(trust, request(u"100.98.93.11"_s, 41641));
        QVERIFY(!v.trusted);
        QVERIFY2(v.why.contains(u"not reached"_s), qPrintable(v.why));
    }

    void hostPairsByIdentity()
    {
        AcpHostOptions o;
        o.port = 0;
        o.agents = {{u"fake"_s, u"Udawany"_s, u"sh"_s}};
        o.stateFile = m_tmp.filePath(u"host/acp-remote.json"_s);
        o.maintenanceIntervalMs = 100;
        std::optional<Error> error;
        notebook::Notebook *nb = m_ctx->notebook(&error);
        QVERIFY(nb);
        // Without a trust: a pairing without a code is refused, the device told why.
        {
            AcpHost plain(*m_ctx, nb, o);
            QString startError;
            QVERIFY2(plain.start(&startError), qPrintable(startError));
            const HttpResult r = http(plain.publicUrl(), "POST", u"pair"_s, R"({"device":"Pixel"})"_ba,
                                      loginHeader(kOwner));
            QCOMPARE(r.status, 403);
            QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"code_needed"_s);
        }
        QStringList log;
        AcpHost host(*m_ctx, nb, o);
        host.log = [&log](const QString &line) { log.append(line); };
        const TailnetTrust trust(m_api.get(), kOwner, {{}, true, true, {}});
        QList<quint16> ports;
        host.setPairTrust([&trust, &ports](const http::Request &r, std::function<void(const PairTrust &)> done) {
            ports.append(r.peerPort);
            trust.check(r, std::move(done));
        });
        QString startError;
        QVERIFY2(host.start(&startError), qPrintable(startError));
        const QString base = host.publicUrl();
        // The owner's device behind Serve: a token, without a code.
        HttpResult r = http(base, "POST", u"pair"_s, R"({"device":"Pixel 9 Pro Ali"})"_ba, loginHeader(kOwner));
        QCOMPARE(r.status, 200);
        const QString token = r.json.value(u"token"_s).toString();
        QVERIFY(token.startsWith(u"bat_"_s));
        QCOMPARE(r.json.value(u"device"_s).toObject().value(u"name"_s).toString(), u"Pixel 9 Pro Ali"_s);
        QVERIFY(!ports.isEmpty() && ports.last() != 0);
        QVERIFY(std::ranges::any_of(log, [](const QString &l) { return l.contains(u"without a code"_s); }));
        QCOMPARE(http(base, "GET", u"info"_s, {}, {{"Authorization"_ba, "Bearer "_ba + token.toUtf8()}}).status, 200);
        // Another user's, none: refused, the code asked for.
        r = http(base, "POST", u"pair"_s, R"({"device":"Telefon Oli"})"_ba, loginHeader(kOther));
        QCOMPARE(r.status, 403);
        QCOMPARE(r.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"code_needed"_s);
        QVERIFY(r.json.value(u"error"_s).toObject().value(u"message"_s).toString().contains(kOther));
        r = http(base, "POST", u"pair"_s, R"({"device":"x"})"_ba);
        QCOMPARE(r.status, 403);
        // The code still works.
        const Command *pair = findCommand(u"acp.pair"_s);
        QVERIFY(pair);
        const Result code = execute(*m_ctx, *pair, QJsonObject{{u"stateFile"_s, o.stateFile}});
        r = http(base, "POST", u"pair"_s,
                 QJsonDocument(QJsonObject{{u"code"_s, code.value.toObject().value(u"code"_s).toString()},
                                           {u"device"_s, u"Laptop"_s}})
                         .toJson());
        QCOMPARE(r.status, 200);
        // Both are listed.
        const Command *clients = findCommand(u"acp.clients"_s);
        const Result listed = execute(*m_ctx, *clients, QJsonObject{{u"stateFile"_s, o.stateFile}});
        QCOMPARE(listed.value.toObject().value(u"devices"_s).toArray().size(), 2);
    }

    void serveProcess()
    {
#ifndef BRULION_CLI
        QSKIP("no brulion-cli in this build");
#else
        const QString home = m_tmp.filePath(u"home-serve"_s);
        QVERIFY(QDir().mkpath(home));
        QProcess serve;
        serve.setEnvironment(cliEnvironment(home, m_ts->path()));
        // (The fake says this computer's tailnet address is 127.0.0.1, and that tailscaled proxied
        // the owner's phone from there.)
        m_ts->whois.insert(u"127.0.0.1"_s, FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s));
        serve.start(QStringLiteral(BRULION_CLI),
                    {u"--vault"_s, m_vault, u"--state-dir"_s, m_tmp.filePath(u"state-serve"_s), u"acp"_s, u"serve"_s,
                     u"--agent"_s, u"fake:Udawany=sh"_s, u"--tailscale"_s, u"--port"_s, u"0"_s, u"--stop-on-eof"_s});
        QVERIFY(serve.waitForStarted(kTimeoutMs));
        QVERIFY(QTest::qWaitFor([&serve] { return serve.canReadLine() || serve.state() == QProcess::NotRunning; },
                                kTimeoutMs));
        QString errors;
        connect(&serve, &QProcess::readyReadStandardError, this,
                [&serve, &errors] { errors += QString::fromUtf8(serve.readAllStandardError()); });
        const QJsonObject started = QJsonDocument::fromJson(serve.readLine()).object();
        QVERIFY2(started.value(u"ok"_s).toBool(), qPrintable(errors));
        const QJsonObject result = started.value(u"result"_s).toObject();
        const QString url = result.value(u"url"_s).toString();
        QVERIFY2(url.startsWith(u"http://nixos.tail1ea20e.ts.net:"_s), qPrintable(url));
        const QJsonObject tailnet = result.value(u"tailnet"_s).toObject();
        QCOMPARE(tailnet.value(u"name"_s).toString(), u"nixos.tail1ea20e.ts.net"_s);
        QCOMPARE(tailnet.value(u"login"_s).toString(), kOwner);
        QVERIFY(tailnet.value(u"trust"_s).toBool());
        const int port = result.value(u"port"_s).toInt();
        QVERIFY(port > 0);
        // The owner's phone pairs without a code (reached here at 127.0.0.1).
        const HttpResult r =
                http(u"http://127.0.0.1:%1"_s.arg(port), "POST", u"pair"_s, R"({"device":"Pixel 9 Pro Ali"})"_ba);
        QCOMPARE(r.status, 200);
        QVERIFY(r.json.value(u"token"_s).toString().startsWith(u"bat_"_s));
        // A connection tailscaled says is this computer's own (another local user): the code.
        m_ts->whois.insert(u"127.0.0.1"_s, FakeTailscaled::whoisJson(kOwner, u"nixos"_s));
        const HttpResult own =
                http(u"http://127.0.0.1:%1"_s.arg(port), "POST", u"pair"_s, R"({"device":"inny użytkownik"})"_ba);
        QCOMPARE(own.status, 403);
        QCOMPARE(own.json.value(u"error"_s).toObject().value(u"code"_s).toString(), u"code_needed"_s);
        // Standard input ends: the server stops by itself.
        serve.closeWriteChannel();
        QVERIFY(QTest::qWaitFor([&serve] { return serve.state() == QProcess::NotRunning; }, kTimeoutMs));
        QCOMPARE(serve.exitStatus(), QProcess::NormalExit);
        QCOMPARE(serve.exitCode(), 0);
        errors += QString::fromUtf8(serve.readAllStandardError());
        QVERIFY2(errors.contains(u"pair without a code"_s), qPrintable(errors));
        QVERIFY2(errors.contains(u"paired Pixel 9 Pro Ali"_s), qPrintable(errors));
#endif
    }

    void serveRefusals_data()
    {
        QTest::addColumn<QStringList>("options");
        QTest::addColumn<bool>("tailscaled");
        QTest::addColumn<int>("exit");
        QTest::addColumn<QString>("code");
        QTest::newRow("no tailscale") << QStringList{u"--tailscale"_s} << false << 5 << u"no_tailscale"_s;
        QTest::newRow("both") << QStringList{u"--tailscale"_s, u"--tailscale-serve"_s} << true << 2
                              << u"bad_tailscale"_s;
        QTest::newRow("users without tailscale") << QStringList{u"--tailnet-user"_s, kOther} << true << 2
                                                 << u"bad_tailscale"_s;
        QTest::newRow("serve beyond loopback") << QStringList{u"--tailscale-serve"_s, u"--listen"_s, u"0.0.0.0"_s}
                                               << true << 2 << u"bad_listen"_s;
        QTest::newRow("plain http on the lan")
                << QStringList{u"--listen"_s, u"127.0.0.1"_s, u"--public-url"_s, u"http://192.168.1.10:8790"_s} << true
                << 5 << u"cannot_serve"_s;
    }
    void serveRefusals()
    {
#ifndef BRULION_CLI
        QSKIP("no brulion-cli in this build");
#else
        QFETCH(QStringList, options);
        QFETCH(bool, tailscaled);
        QFETCH(int, exit);
        QFETCH(QString, code);
        const QString home = m_tmp.filePath(u"home-refusals"_s);
        QVERIFY(QDir().mkpath(home));
        QProcess serve;
        serve.setEnvironment(cliEnvironment(home, tailscaled ? m_ts->path() : m_tmp.filePath(u"none.sock"_s)));
        serve.start(QStringLiteral(BRULION_CLI), QStringList{u"--vault"_s, m_vault, u"--state-dir"_s,
                                                             m_tmp.filePath(u"state-refusals"_s), u"acp"_s, u"serve"_s,
                                                             u"--agent"_s, u"fake=sh"_s, u"--port"_s, u"0"_s}
                                                         + options);
        // (Not waitForFinished(): the fake tailscaled answers on this event loop.)
        QVERIFY(QTest::qWaitFor([&serve] { return serve.state() == QProcess::NotRunning; }, kTimeoutMs));
        const QJsonObject out = QJsonDocument::fromJson(serve.readAllStandardOutput()).object();
        QCOMPARE(serve.exitCode(), exit);
        QCOMPARE(out.value(u"error"_s).toObject().value(u"code"_s).toString(), code);
#endif
    }
};

QTEST_GUILESS_MAIN(TestCliAcpTrust)
#include "tst_cli_acp_trust.moc"

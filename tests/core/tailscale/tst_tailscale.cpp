// This computer in its tailnet (src/core/tailscale): tailscaled's status and whois read (as
// tailscaled 1.104 writes them, missing and malformed fields), the tailnet's addresses and names,
// the trust decisions of a pairing without a code — whois allowed and denied, a tagged device, the
// Serve header taken only from loopback to a server on loopback alone behind Serve, a header from
// any other peer never, outside the tailnet the code — and the LocalAPI client against a fake
// tailscaled over a real local socket (its Host, the address asked for, 404, no socket, a hang,
// garbage, the `tailscale` program when the socket is not reached).
#include "faketailscaled.h"
#include "tailscale/localapi.h"
#include "tailscale/tailscale.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::tailscale;

namespace {

constexpr int kTimeoutMs = 10000 * BRULION_TEST_SLOWDOWN;

const QString kOwner = u"ala.żółć@github"_s;

PeerFacts facts(const QString &peer, bool loopbackOnly = false, bool serve = false, const QString &header = {},
                bool hasHeader = false)
{
    PeerFacts f;
    f.peer = QHostAddress(peer);
    f.listensOnLoopbackOnly = loopbackOnly;
    f.behindServe = serve;
    f.hasServeHeader = hasHeader || !header.isEmpty();
    f.serveLogin = header;
    return f;
}

Identity person(const QString &login, const QString &node = u"pixel-9-pro"_s, const QStringList &tags = {})
{
    Identity id;
    id.login = login;
    id.node = node;
    id.tags = tags;
    return id;
}

} // namespace

class TestTailscale : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void status()
    {
        const QByteArray json = FakeTailscaled::statusJson(u"nixos.tail1ea20e.ts.net"_s,
                                                           {u"fd7a:115c:a1e0::4b32:e31a"_s, u"100.108.227.25"_s}, kOwner);
        QString error;
        const std::optional<Status> s = parseStatus(json, &error);
        QVERIFY2(s, qPrintable(error));
        QVERIFY(s->running());
        QCOMPARE(s->dnsName, u"nixos.tail1ea20e.ts.net"_s); // (the trailing dot dropped)
        QCOMPARE(s->hostName, u"nixos"_s);
        QCOMPARE(s->login, kOwner);
        QCOMPARE(s->displayName, u"Ala Żółć"_s);
        QCOMPARE(s->tailnet, u"ala.github"_s);
        QCOMPARE(s->userId, 6448733692706832LL);
        QCOMPARE(s->addresses.size(), 2);
        QCOMPARE(s->addresses.first(), QHostAddress(u"100.108.227.25"_s)); // IPv4 first
        QCOMPARE(s->ipv4(), QHostAddress(u"100.108.227.25"_s));
    }

    void statusNotRunning()
    {
        const std::optional<Status> s = parseStatus(
                FakeTailscaled::statusJson(u"nixos.tail1ea20e.ts.net"_s, {}, kOwner, 1, u"NeedsLogin"_s));
        QVERIFY(s);
        QVERIFY(!s->running());
        QVERIFY(s->ipv4().isNull());
    }

    void statusMalformed_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("not JSON") << QByteArray("<html>Tailscale</html>");
        QTest::newRow("a list") << QByteArray("[1, 2]");
        QTest::newRow("no Self") << QByteArray(R"({"BackendState": "Running"})");
        QTest::newRow("Self not an object") << QByteArray(R"({"BackendState": "Running", "Self": "nixos"})");
    }
    void statusMalformed()
    {
        QFETCH(QByteArray, json);
        QString error;
        QVERIFY(!parseStatus(json, &error));
        QVERIFY(!error.isEmpty());
    }

    void statusOddFields()
    {
        // A user missing from User, addresses that are none, a user id as a string.
        const QByteArray json = R"({"BackendState":"Running","Self":{"DNSName":"Laptop.Tail1.TS.NET.","HostName":"laptop",
            "TailscaleIPs":["nonsense","100.64.0.7",""],"UserID":"42"},"User":{}})";
        const std::optional<Status> s = parseStatus(json);
        QVERIFY(s);
        QCOMPARE(s->dnsName, u"laptop.tail1.ts.net"_s);
        QCOMPARE(s->addresses, QList<QHostAddress>{QHostAddress(u"100.64.0.7"_s)});
        QCOMPARE(s->userId, 42LL);
        QVERIFY(s->login.isEmpty());
    }

    void whois()
    {
        QString error;
        const std::optional<Identity> id = parseWhois(FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s), &error);
        QVERIFY2(id, qPrintable(error));
        QCOMPARE(id->login, kOwner);
        QCOMPARE(id->node, u"pixel-9-pro-1"_s);
        QCOMPARE(id->nodeDnsName, u"pixel-9-pro-1.tail1ea20e.ts.net"_s);
        QVERIFY(id->tags.isEmpty());
        const std::optional<Identity> tagged =
                parseWhois(FakeTailscaled::whoisJson(u"tagged-devices"_s, u"raspberry-pi-4"_s, {u"tag:rpi"_s}));
        QVERIFY(tagged);
        QCOMPARE(tagged->tags, QStringList{u"tag:rpi"_s});
        // No ComputedName: the DNS name's first label.
        const std::optional<Identity> named = parseWhois(
                R"({"Node":{"Name":"stary-laptop.tail1.ts.net."},"UserProfile":{"LoginName":"ala@github"}})");
        QVERIFY(named);
        QCOMPARE(named->node, u"stary-laptop"_s);
        QVERIFY(!parseWhois("{}"));
        QVERIFY(!parseWhois(R"({"Node":{"Name":"x"}})"));
        QVERIFY(!parseWhois("no match for IP:port"));
    }

    void addresses_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("tailnet");
        QTest::newRow("first") << u"100.64.0.0"_s << true;
        QTest::newRow("a device") << u"100.98.93.11"_s << true;
        QTest::newRow("last") << u"100.127.255.255"_s << true;
        QTest::newRow("below") << u"100.63.255.255"_s << false;
        QTest::newRow("above") << u"100.128.0.0"_s << false;
        QTest::newRow("home") << u"192.168.1.10"_s << false;
        QTest::newRow("loopback") << u"127.0.0.1"_s << false;
        QTest::newRow("ipv6") << u"fd7a:115c:a1e0::6432:5d0c"_s << true;
        QTest::newRow("ipv6 neighbour") << u"fd7a:115c:a1e1::1"_s << false;
        QTest::newRow("ipv6 loopback") << u"::1"_s << false;
        QTest::newRow("mapped") << u"::ffff:100.98.93.11"_s << true;
        QTest::newRow("mapped home") << u"::ffff:192.168.1.10"_s << false;
    }
    void addresses()
    {
        QFETCH(QString, address);
        QFETCH(bool, tailnet);
        QCOMPARE(isTailnetAddress(QHostAddress(address)), tailnet);
    }

    void hosts_data()
    {
        QTest::addColumn<QString>("host");
        QTest::addColumn<bool>("tailnet");
        QTest::newRow("magicdns") << u"nixos.tail1ea20e.ts.net"_s << true;
        QTest::newRow("letter case, dot") << u"NixOS.Tail1EA20E.ts.net."_s << true;
        QTest::newRow("tailnet only") << u"tail1ea20e.ts.net"_s << false;
        QTest::newRow("ts.net") << u"ts.net"_s << false;
        QTest::newRow("look-alike") << u"nixos.tail1ea20e.ts.net.example.com"_s << false;
        QTest::newRow("suffix in a label") << u"nixos.tail1ea20e-ts.net"_s << false;
        QTest::newRow("empty label") << u"nixos..ts.net"_s << false;
        QTest::newRow("odd letters") << u"nix_os.tail1.ts.net"_s << false;
        QTest::newRow("polish") << u"zażółć.tail1.ts.net"_s << false;
        QTest::newRow("address") << u"100.98.93.11"_s << true;
        QTest::newRow("ipv6 bracketed") << u"[fd7a:115c:a1e0::1]"_s << true;
        QTest::newRow("lan") << u"192.168.1.10"_s << false;
        QTest::newRow("name") << u"agent.example.com"_s << false;
        QTest::newRow("empty") << QString() << false;
    }
    void hosts()
    {
        QFETCH(QString, host);
        QFETCH(bool, tailnet);
        QCOMPARE(isTailnetHost(host), tailnet);
    }

    void logins()
    {
        QVERIFY(sameLogin(u"Ala.Żółć@GitHub"_s, kOwner));
        QVERIFY(!sameLogin(u"ala@github"_s, u"ala@gitlab"_s));
        QVERIFY(!sameLogin(QString(), QString()));
    }

    // ---- the decisions -------------------------------------------------------------------------

    void plan_data()
    {
        QTest::addColumn<QString>("peer");
        QTest::addColumn<bool>("loopbackOnly");
        QTest::addColumn<bool>("serve");
        QTest::addColumn<QString>("header");
        QTest::addColumn<bool>("askWhois");
        QTest::addColumn<bool>("serveHeader");
        QTest::addColumn<bool>("headerIgnored");
        QTest::newRow("tailnet peer") << u"100.98.93.11"_s << false << false << QString() << true << false << false;
        QTest::newRow("tailnet ipv6") << u"fd7a:115c:a1e0::6432:5d0c"_s << false << false << QString() << true << false
                                      << false;
        QTest::newRow("lan peer") << u"192.168.1.20"_s << false << false << QString() << false << false << false;
        QTest::newRow("loopback: whois first") << u"127.0.0.1"_s << true << false << QString() << true << false << false;
        QTest::newRow("serve") << u"127.0.0.1"_s << true << true << kOwner << true << true << false;
        QTest::newRow("serve, ipv6 loopback") << u"::1"_s << true << true << kOwner << true << true << false;
        // Never the header: any other peer could have written it.
        QTest::newRow("forged from the lan") << u"192.168.1.20"_s << true << true << kOwner << false << false << true;
        QTest::newRow("forged from the tailnet") << u"100.98.93.11"_s << false << true << kOwner << true << false
                                                 << true;
        QTest::newRow("loopback, not behind serve") << u"127.0.0.1"_s << true << false << kOwner << true << false
                                                    << true;
        QTest::newRow("loopback, listening beyond it") << u"127.0.0.1"_s << false << true << kOwner << true << false
                                                       << true;
    }
    void plan()
    {
        QFETCH(QString, peer);
        QFETCH(bool, loopbackOnly);
        QFETCH(bool, serve);
        QFETCH(QString, header);
        QFETCH(bool, askWhois);
        QFETCH(bool, serveHeader);
        QFETCH(bool, headerIgnored);
        const TrustPlan p = planTrust(facts(peer, loopbackOnly, serve, header));
        QCOMPARE(p.askWhois, askWhois);
        QCOMPARE(p.serveHeader, serveHeader);
        QCOMPARE(p.headerIgnored, headerIgnored);
    }

    void serveHeaderEmpty()
    {
        // A header without a login is no identity.
        const TrustPlan p = planTrust(facts(u"127.0.0.1"_s, true, true, u"  "_s, true));
        QVERIFY(!p.serveHeader);
        QVERIFY(p.headerIgnored);
    }

    void judgeIdentity_data()
    {
        QTest::addColumn<QString>("login");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<QStringList>("allowed");
        QTest::addColumn<bool>("trusted");
        QTest::newRow("owner") << kOwner << QStringList() << QStringList{kOwner} << true;
        QTest::newRow("owner, letter case") << u"ALA.ŻÓŁĆ@GITHUB"_s << QStringList() << QStringList{kOwner} << true;
        QTest::newRow("another user") << u"ola@github"_s << QStringList() << QStringList{kOwner} << false;
        QTest::newRow("another user allowed") << u"ola@github"_s << QStringList() << QStringList{kOwner, u"ola@github"_s}
                                              << true;
        QTest::newRow("tagged") << u"tagged-devices"_s << QStringList{u"tag:rpi"_s} << QStringList{u"tagged-devices"_s}
                                << false;
        QTest::newRow("owner's tagged node") << kOwner << QStringList{u"tag:server"_s} << QStringList{kOwner} << false;
        QTest::newRow("no login") << QString() << QStringList() << QStringList{QString()} << false;
        QTest::newRow("nobody allowed") << kOwner << QStringList() << QStringList() << false;
    }
    void judgeIdentity()
    {
        QFETCH(QString, login);
        QFETCH(QStringList, tags);
        QFETCH(QStringList, allowed);
        QFETCH(bool, trusted);
        const Judgement j = judge(person(login, u"pixel-9-pro"_s, tags), allowed);
        QCOMPARE(j.trusted, trusted);
        QVERIFY(!j.why.isEmpty());
        QCOMPARE(j.node, u"pixel-9-pro"_s);
    }

    void decideAll()
    {
        const QStringList allowed{kOwner};
        // whois allowed / denied.
        QVERIFY(decide(facts(u"100.98.93.11"_s), person(kOwner), allowed).trusted);
        const Judgement other = decide(facts(u"100.98.93.11"_s), person(u"ola@github"_s), allowed);
        QVERIFY(!other.trusted);
        QVERIFY(other.why.contains(u"ola@github"_s));
        // tailscaled knows nobody there: the code.
        const Judgement unknown = decide(facts(u"100.98.93.11"_s), std::nullopt, allowed);
        QVERIFY(!unknown.trusted);
        QVERIFY(unknown.why.contains(u"tailscaled"_s));
        // Outside the tailnet: the code, whatever was said.
        const Judgement lan = decide(facts(u"192.168.1.20"_s), person(kOwner), allowed);
        QVERIFY(!lan.trusted);
        QVERIFY(lan.why.contains(u"no address of the tailnet"_s));
        // A forged header from the LAN, even to a server behind Serve: refused.
        QVERIFY(!decide(facts(u"192.168.1.20"_s, true, true, kOwner), std::nullopt, allowed).trusted);
        // From a tailnet peer the header is ignored and whois decides.
        QVERIFY(!decide(facts(u"100.98.93.11"_s, false, true, kOwner), person(u"ola@github"_s), allowed).trusted);
        // Behind Serve: whois first (tailscaled knows whom it proxied) …
        QVERIFY(!decide(facts(u"127.0.0.1"_s, true, true, kOwner), person(u"ola@github"_s), allowed).trusted);
        // … else the header.
        const Judgement serve = decide(facts(u"127.0.0.1"_s, true, true, kOwner), std::nullopt, allowed);
        QVERIFY(serve.trusted);
        QCOMPARE(serve.login, kOwner);
        QVERIFY(serve.why.contains(u"Serve"_s));
        QVERIFY(!decide(facts(u"127.0.0.1"_s, true, true, u"ola@github"_s), std::nullopt, allowed).trusted);
        // A local program's header to a server not behind Serve, or one that listens beyond loopback.
        QVERIFY(!decide(facts(u"127.0.0.1"_s, true, false, kOwner), std::nullopt, allowed).trusted);
        QVERIFY(!decide(facts(u"127.0.0.1"_s, false, true, kOwner), std::nullopt, allowed).trusted);
    }

    // ---- the LocalAPI client ---------------------------------------------------------------------

    void httpAnswer()
    {
        LocalApi::Answer a = parseHttpAnswer("HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{}trailing");
        QCOMPARE(a.status, 200);
        QCOMPARE(a.body, QByteArray("{}"));
        a = parseHttpAnswer("HTTP/1.1 404 Not Found\r\n\r\nno match");
        QCOMPARE(a.status, 404);
        QCOMPARE(a.body, QByteArray("no match"));
        a = parseHttpAnswer("HTTP/1.0 200 OK\r\nContent-Length: 10\r\n\r\n{}");
        QCOMPARE(a.status, 0); // cut off
        QVERIFY(!a.error.isEmpty());
        QCOMPARE(parseHttpAnswer("").status, 0);
        QCOMPARE(parseHttpAnswer("SSH-2.0-OpenSSH\r\n\r\n").status, 0);
        QCOMPARE(parseHttpAnswer("HTTP/1.0 abc OK\r\n\r\n").status, 0);
        QCOMPARE(parseHttpAnswer("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n").status, 0);
    }

    void localApi()
    {
        QTemporaryDir dir;
        FakeTailscaled ts(dir.filePath(u"ts.sock"_s));
        QVERIFY(ts.listening());
        ts.status = FakeTailscaled::statusJson(u"nixos.tail1ea20e.ts.net"_s, {u"100.108.227.25"_s}, kOwner);
        ts.whois.insert(u"100.98.93.11:41641"_s, FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s));
        ts.whois.insert(u"fd7a:115c:a1e0::6432:5d0c"_s, FakeTailscaled::whoisJson(u"ola@github"_s, u"telefon-oli"_s));
        LocalApi api;
        api.setSocketPath(ts.path());
        api.setProgram(QString());

        std::optional<Status> status;
        QString error = u"-"_s;
        api.status([&](const std::optional<Status> &s, const QString &e) {
            status = s;
            error = e;
        });
        QTRY_VERIFY_WITH_TIMEOUT(error != u"-", kTimeoutMs);
        QVERIFY2(status, qPrintable(error));
        QCOMPARE(status->dnsName, u"nixos.tail1ea20e.ts.net"_s);
        QCOMPARE(ts.targets.last(), u"/localapi/v0/status"_s);
        QCOMPARE(ts.hosts.last(), u"local-tailscaled.sock"_s);

        const auto ask = [&](const QString &address, quint16 port) {
            std::optional<Identity> who;
            QString whyNot = u"-"_s;
            api.whois(QHostAddress(address), port, [&](const std::optional<Identity> &id, const QString &e) {
                who = id;
                whyNot = e;
            });
            [&] { QTRY_VERIFY_WITH_TIMEOUT(whyNot != u"-", kTimeoutMs); }();
            return std::pair{who, whyNot};
        };
        auto [pixel, e1] = ask(u"100.98.93.11"_s, 41641);
        QVERIFY2(pixel, qPrintable(e1));
        QCOMPARE(pixel->node, u"pixel-9-pro-1"_s);
        QCOMPARE(ts.targets.last(), u"/localapi/v0/whois?addr=100.98.93.11:41641"_s);
        // An IPv4-mapped peer is asked for as IPv4.
        auto [mapped, e2] = ask(u"::ffff:100.98.93.11"_s, 41641);
        QVERIFY(mapped);
        // IPv6 in brackets.
        auto [ola, e3] = ask(u"fd7a:115c:a1e0::6432:5d0c"_s, 5000);
        QVERIFY2(ola, qPrintable(e3));
        QCOMPARE(ola->login, u"ola@github"_s);
        QVERIFY(ts.targets.last().contains(u"%5Bfd7a:115c:a1e0::6432:5d0c%5D:5000"_s)
                || ts.targets.last().contains(u"[fd7a:115c:a1e0::6432:5d0c]:5000"_s));
        // Nobody there: nullopt without an error.
        auto [nobody, e4] = ask(u"100.70.0.1"_s, 1);
        QVERIFY(!nobody);
        QVERIFY(e4.isEmpty());
    }

    void localApiFailures()
    {
        QTemporaryDir dir;
        LocalApi api;
        api.setProgram(QString());
        api.setTimeoutMs(300 * BRULION_TEST_SLOWDOWN);
        const auto status = [&]() {
            std::optional<Status> s;
            QString error = u"-"_s;
            api.status([&](const std::optional<Status> &got, const QString &e) {
                s = got;
                error = e;
            });
            [&] { QTRY_VERIFY_WITH_TIMEOUT(error != u"-", kTimeoutMs); }();
            return std::pair{s, error};
        };
        // No socket at all.
        api.setSocketPath(dir.filePath(u"none.sock"_s));
        auto [none, e1] = status();
        QVERIFY(!none);
        QVERIFY(e1.contains(u"not reached"_s));
        api.setSocketPath(QString());
        auto [empty, e2] = status();
        QVERIFY(!empty);
        QVERIFY(!e2.isEmpty());

        FakeTailscaled ts(dir.filePath(u"ts.sock"_s));
        api.setSocketPath(ts.path());
        // A tailscaled that never answers.
        ts.silent = true;
        auto [hung, e3] = status();
        QVERIFY(!hung);
        QVERIFY(e3.contains(u"in time"_s));
        ts.silent = false;
        // Garbage.
        ts.garbage = "SSH-2.0-OpenSSH_9.9\r\n";
        auto [garbage, e4] = status();
        QVERIFY(!garbage);
        QVERIFY(!e4.isEmpty());
        ts.garbage.clear();
        // An error status.
        auto [failed, e5] = status(); // (no status set: 500)
        QVERIFY(!failed);
        QVERIFY(e5.contains(u"500"_s));
        // Not JSON.
        ts.status = "<html>login</html>";
        auto [html, e6] = status();
        QVERIFY(!html);
        QVERIFY(e6.contains(u"JSON"_s));
    }

    void program()
    {
#ifdef Q_OS_UNIX
        // The socket not reached: the `tailscale` program answers (`status --json`, `whois --json`).
        QTemporaryDir dir;
        const QString script = dir.filePath(u"tailscale"_s);
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n"
                "case \"$1\" in\n"
                "status) cat \"$(dirname \"$0\")/status.json\" ;;\n"
                "whois) if [ \"$3\" = 100.98.93.11:7 ]; then cat \"$(dirname \"$0\")/whois.json\"; "
                "else echo 'no match for IP:port' >&2; exit 1; fi ;;\n"
                "*) exit 2 ;;\n"
                "esac\n");
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QFile s(dir.filePath(u"status.json"_s));
        QVERIFY(s.open(QIODevice::WriteOnly));
        s.write(FakeTailscaled::statusJson(u"laptop.tail1ea20e.ts.net"_s, {u"100.64.0.9"_s}, kOwner));
        s.close();
        QFile w(dir.filePath(u"whois.json"_s));
        QVERIFY(w.open(QIODevice::WriteOnly));
        w.write(FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s));
        w.close();

        LocalApi api;
        api.setSocketPath(dir.filePath(u"none.sock"_s));
        api.setProgram(script);
        std::optional<Status> status;
        QString error = u"-"_s;
        api.status([&](const std::optional<Status> &got, const QString &e) {
            status = got;
            error = e;
        });
        QTRY_VERIFY_WITH_TIMEOUT(error != u"-", kTimeoutMs);
        QVERIFY2(status, qPrintable(error));
        QCOMPARE(status->dnsName, u"laptop.tail1ea20e.ts.net"_s);

        std::optional<Identity> who;
        QString whyNot = u"-"_s;
        api.whois(QHostAddress(u"100.98.93.11"_s), 7, [&](const std::optional<Identity> &id, const QString &e) {
            who = id;
            whyNot = e;
        });
        QTRY_VERIFY_WITH_TIMEOUT(whyNot != u"-", kTimeoutMs);
        QVERIFY(who);
        QCOMPARE(who->node, u"pixel-9-pro-1"_s);
        whyNot = u"-"_s;
        api.whois(QHostAddress(u"100.98.93.12"_s), 7, [&](const std::optional<Identity> &id, const QString &e) {
            who = id;
            whyNot = e;
        });
        QTRY_VERIFY_WITH_TIMEOUT(whyNot != u"-", kTimeoutMs);
        QVERIFY(!who);
        QVERIFY(whyNot.isEmpty()); // (no match: nobody, not a failure)
#else
        QSKIP("a shell script plays the program");
#endif
    }

    void deletedMidQuestion()
    {
        // A callback never comes after the client is gone.
        QTemporaryDir dir;
        FakeTailscaled ts(dir.filePath(u"ts.sock"_s));
        ts.silent = true;
        bool called = false;
        {
            LocalApi api;
            api.setSocketPath(ts.path());
            api.setProgram(QString());
            api.status([&](const std::optional<Status> &, const QString &) { called = true; });
            QTRY_VERIFY_WITH_TIMEOUT(!ts.targets.isEmpty(), kTimeoutMs);
        }
        QTest::qWait(50);
        QVERIFY(!called);
    }
};

QTEST_GUILESS_MAIN(TestTailscale)
#include "tst_tailscale.moc"

// "Share agents with the phone" (src/app/shell/agentshare.h): how brulion-cli is started for each
// address — found by itself (Tailscale, else this computer's address in its network) or typed (an
// address of this computer, a tailnet name with http or behind Serve with https, an https proxy;
// a name over plain http refused) —, the address of the network picked, the offers of other
// computers in the notebook (never this computer's own, never one added already); and the whole of
// it on a real Workspace with the real `brulion-cli acp serve` and a fake tailscaled
// (faketailscaled.h): the switch starts it on the notebook that is open with this device's agents,
// it is named by its tailnet name, the notebook's config.json gets its offer (its other keys kept),
// the owner's device pairs without a code through the app's own client (only the agent picked),
// a pairing code with its brulion://pair link, the devices counted and all unpaired, without the
// trust the code asked for (codeNeeded), not for the documentation notebook nor without agents,
// and turned off it stops and its offer leaves the notebook. Code mode's projects of this device go
// to its projects' file (--projects), written again when they change — the running server reads it
// again: a paired device lists them at once.
#include "agentshare.h"
#include "codeprojects.h"
#include "serverprocess.h"
#include "workspace.h"
#include "workspaceagents.h"
#include "workspacechats.h"

#include "acp/codehost.h"
#include "faketailscaled.h"
#include "sync/http.h"
#include "tailscale/localapi.h"
#include "vault/vaultconfig.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::shell::AgentConfig;
using brulion::shell::AgentShare;
using brulion::shell::ServerProcess;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceAgents;

namespace {

constexpr int kTimeoutMs = 30000 * BRULION_TEST_SLOWDOWN;
const QString kOwner = u"ala.żółć@github"_s;

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

QString readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

AgentShare::Launch launch(const QString &address = {}, bool tailscale = false, const QString &lan = {}, bool trust = true)
{
    AgentShare::Launch l;
    l.root = u"/home/ala/Notatki"_s;
    l.agentsFile = u"/home/ala/.local/share/Brulion/agent-share/agents.json"_s;
    l.address = address;
    l.tailscale = tailscale;
    l.lan = lan;
    l.trust = trust;
    return l;
}

QStringList base()
{
    return {u"--vault"_s, u"/home/ala/Notatki"_s, u"--compact"_s, u"acp"_s, u"serve"_s, u"--agents"_s,
            u"/home/ala/.local/share/Brulion/agent-share/agents.json"_s, u"--stop-on-eof"_s};
}

} // namespace

class TestAgentShare : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QLocale::setDefault(QLocale::c());
        QStandardPaths::setTestModeEnabled(true);
    }

    void arguments_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("tailscale");
        QTest::addColumn<QString>("lan");
        QTest::addColumn<bool>("trust");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("tailscale") << QString() << true << QString() << true
                                   << QStringList{u"--tailscale"_s, u"--port"_s, u"8790"_s};
        QTest::newRow("tailscale, no trust") << QString() << true << QString() << false
                                             << QStringList{u"--tailscale"_s, u"--port"_s, u"8790"_s,
                                                            u"--no-tailnet-trust"_s};
        QTest::newRow("lan") << QString() << false << u"192.168.1.10"_s << true
                             << QStringList{u"--listen"_s, u"192.168.1.10"_s, u"--port"_s, u"8790"_s,
                                            u"--public-url"_s, u"http://192.168.1.10:8790"_s, u"--allow-http"_s};
        QTest::newRow("typed lan") << u"http://192.168.1.20:9000"_s << false << QString() << true
                                   << QStringList{u"--listen"_s, u"192.168.1.20"_s, u"--port"_s, u"9000"_s,
                                                  u"--public-url"_s, u"http://192.168.1.20:9000"_s, u"--allow-http"_s};
        QTest::newRow("typed without a scheme") << u"192.168.1.20"_s << false << QString() << true
                                                << QStringList{u"--listen"_s, u"192.168.1.20"_s, u"--port"_s, u"8790"_s,
                                                               u"--public-url"_s, u"http://192.168.1.20"_s,
                                                               u"--allow-http"_s};
        QTest::newRow("typed loopback") << u"http://127.0.0.1:8800"_s << false << QString() << true
                                        << QStringList{u"--listen"_s, u"127.0.0.1"_s, u"--port"_s, u"8800"_s,
                                                       u"--public-url"_s, u"http://127.0.0.1:8800"_s};
        QTest::newRow("typed tailnet name") << u"http://nixos.tail1ea20e.ts.net:8791"_s << false << QString() << true
                                            << QStringList{u"--tailscale"_s, u"--port"_s, u"8791"_s, u"--public-url"_s,
                                                           u"http://nixos.tail1ea20e.ts.net:8791"_s};
        QTest::newRow("typed tailnet https: serve")
                << u"https://nixos.tail1ea20e.ts.net"_s << false << QString() << false
                << QStringList{u"--tailscale-serve"_s, u"--port"_s, u"8790"_s, u"--public-url"_s,
                               u"https://nixos.tail1ea20e.ts.net"_s, u"--no-tailnet-trust"_s};
        QTest::newRow("typed proxy") << u"https://agents.example.com/brulion"_s << false << QString() << true
                                     << QStringList{u"--listen"_s, u"127.0.0.1"_s, u"--port"_s, u"8790"_s,
                                                    u"--public-url"_s, u"https://agents.example.com/brulion"_s};
    }
    void arguments()
    {
        QFETCH(QString, address);
        QFETCH(bool, tailscale);
        QFETCH(QString, lan);
        QFETCH(bool, trust);
        QFETCH(QStringList, expected);
        QString error;
        const QStringList args = AgentShare::serveArguments(launch(address, tailscale, lan, trust), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(args, base() + expected);
        // With Code mode's projects: their file too.
        AgentShare::Launch l = launch(address, tailscale, lan, trust);
        l.projectsFile = u"/home/ala/.local/share/Brulion/agent-share/projects.json"_s;
        const QStringList withProjects = base() + QStringList{u"--projects"_s, l.projectsFile} + expected;
        QCOMPARE(AgentShare::serveArguments(l, &error), withProjects);
    }

    void argumentsRefused_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("lan");
        QTest::newRow("no network") << QString() << QString();
        QTest::newRow("a name over http") << u"http://komputer-ali.local:8790"_s << QString();
        QTest::newRow("not an address") << u"http://ala:hasło@192.168.1.10"_s << QString();
        QTest::newRow("another scheme") << u"ftp://192.168.1.10"_s << QString();
    }
    void argumentsRefused()
    {
        QFETCH(QString, address);
        QFETCH(QString, lan);
        QString error;
        QVERIFY(AgentShare::serveArguments(launch(address, false, lan), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void lanAddress()
    {
        const auto pick = [](const QStringList &addresses) {
            QList<QHostAddress> list;
            for (const QString &a : addresses)
                list.append(QHostAddress(a));
            return AgentShare::lanAddress(list);
        };
        QCOMPARE(pick({u"127.0.0.1"_s, u"100.108.227.25"_s, u"169.254.3.4"_s, u"fe80::1"_s, u"192.168.1.10"_s}),
                 u"192.168.1.10"_s);
        QCOMPARE(pick({u"83.1.2.3"_s, u"10.0.0.7"_s}), u"10.0.0.7"_s); // (a private one first)
        QCOMPARE(pick({u"172.20.0.5"_s}), u"172.20.0.5"_s);
        QCOMPARE(pick({u"83.1.2.3"_s, u"::1"_s}), u"83.1.2.3"_s);
        QCOMPARE(pick({u"127.0.0.1"_s, u"100.108.227.25"_s}), QString());
        QCOMPARE(pick({}), QString());
    }

    void offers()
    {
        const QJsonObject config{
                {u"agentHosts"_s,
                 QJsonArray{QJsonObject{{u"url"_s, u"http://nixos.tail1ea20e.ts.net:8790"_s},
                                        {u"host"_s, u"nixos"_s},
                                        {u"agents"_s, QJsonArray{QJsonObject{{u"id"_s, u"claude"_s}, {u"name"_s, u"Claude Code"_s}},
                                                                 QJsonObject{{u"id"_s, u"codex"_s}, {u"name"_s, u"Codex"_s}}}}},
                            QJsonObject{{u"url"_s, u"http://laptop-ali.tail1ea20e.ts.net:8790"_s},
                                        {u"host"_s, u"laptop-ali"_s},
                                        {u"agents"_s, QJsonArray{QJsonObject{{u"id"_s, u"claude"_s}, {u"name"_s, u"Claude Code"_s}}}}},
                            QJsonObject{{u"url"_s, u"http://192.168.1.10:8790"_s}, {u"host"_s, u"lan"_s},
                                        {u"agents"_s, QJsonArray{QJsonObject{{u"id"_s, u"x"_s}}}}}}}};
        // On the phone: both computers' agents (the LAN one never).
        QVariantList offers = AgentShare::offersFrom(config, QString(), u"pixel-9-pro"_s, {});
        QCOMPARE(offers.size(), 3);
        QCOMPARE(offers.first().toMap().value(u"name"_s).toString(), u"Claude Code"_s);
        QCOMPARE(offers.first().toMap().value(u"host"_s).toString(), u"nixos"_s);
        QCOMPARE(offers.first().toMap().value(u"agent"_s).toString(), u"claude"_s);
        // Claude Code of nixos added already (its address with another port): left out.
        offers = AgentShare::offersFrom(config, QString(), u"pixel-9-pro"_s,
                                        {{u"http://NixOS.tail1ea20e.ts.net:9000"_s, u"claude"_s}});
        QCOMPARE(offers.size(), 2);
        QCOMPARE(offers.first().toMap().value(u"agent"_s).toString(), u"codex"_s);
        // On nixos itself: its own offer is no offer (by its name, by its address).
        QCOMPARE(AgentShare::offersFrom(config, QString(), u"NIXOS"_s, {}).size(), 1);
        QCOMPARE(AgentShare::offersFrom(config, u"http://nixos.tail1ea20e.ts.net:8790"_s, u"inny"_s, {}).size(), 1);
    }

    // ---- the whole of it, with brulion-cli ------------------------------------------------------

    void sharing()
    {
#ifndef BRULION_CLI
        QSKIP("no brulion-cli in this build");
#else
        if (!WorkspaceAgents::available())
            QSKIP("programs run only in the desktop app");
        // A tailscaled that says this computer is nixos in the tailnet at 127.0.0.1, and that the
        // connection from there is the owner's phone.
        FakeTailscaled ts(m_tmp.filePath(u"ts.sock"_s));
        QVERIFY(ts.listening());
        ts.status = FakeTailscaled::statusJson(u"nixos.tail1ea20e.ts.net"_s, {u"127.0.0.1"_s}, kOwner);
        ts.whois.insert(u"127.0.0.1"_s, FakeTailscaled::whoisJson(kOwner, u"pixel-9-pro-1"_s));
        qputenv("BRULION_TAILSCALE_SOCKET", ts.path().toLocal8Bit());
        qputenv("BRULION_TAILSCALE_PROGRAM", "");
        qputenv("LANGUAGE", "en");

        const QString root = m_tmp.filePath(u"Notatki Ali"_s);
        QVERIFY(writeRaw(root + u"/Dom.md"_s, "- [ ] Zapłacić czynsz\n"));
        QVERIFY(writeRaw(root + u"/.brulion/config.json"_s, R"({"version": 1, "journalsFolder": "Dziennik", "przyszłość": 7})"));
        Workspace ws;
        QString error;
        QVERIFY2(ws.open(root, root + u"-state"_s, &error), qPrintable(error));
        ws.finishIndexing();
        // A project of Code mode on this computer.
        QByteArray kept;
        ws.projects()->setStorage([&kept] { return kept; }, [&kept](const QByteArray &json) { kept = json; });
        const QString project = m_tmp.filePath(u"Sklep żółwia"_s);
        QVERIFY(QDir().mkpath(project));
        QCOMPARE(ws.projects()->add(project), u"sklep-zolwia"_s);
        AgentShare *share = ws.chats()->share();
        share->setCommand({QStringLiteral(BRULION_CLI)});
        share->setPort(0);
        share->server()->setRetryDelays({200});
        QVERIFY(share->available());

        // On, without agents: it waits for one.
        share->setNotebook(root, true);
        share->setSettings(true, QString(), true);
        QTRY_COMPARE_WITH_TIMEOUT(share->state(), u"agents"_s, kTimeoutMs);
        // The documentation notebook is never shared.
        ws.chats()->setAgents({AgentConfig{u"fake"_s, u"Fałszywy agent"_s, u"sh"_s, false}});
        share->setNotebook(root, false);
        QTRY_COMPARE_WITH_TIMEOUT(share->state(), u"notebook"_s, kTimeoutMs);
        // The notebook: it runs, named by its tailnet name.
        share->setNotebook(root, true);
        QTRY_COMPARE_WITH_TIMEOUT(share->state(), u"running"_s, kTimeoutMs);
        const QString url = share->url();
        QVERIFY2(url.startsWith(u"http://nixos.tail1ea20e.ts.net:"_s), qPrintable(url));
        QVERIFY(share->tailnet());
        QCOMPARE(share->owner(), kOwner);
        QVERIFY(!share->unencrypted());
        QVERIFY(share->status().contains(url));
        QCOMPARE(share->automaticAddress(), u"http://nixos.tail1ea20e.ts.net:0"_s);
        const QStringList args = share->server()->arguments();
        QVERIFY(args.contains(u"--tailscale"_s));
        QCOMPARE(args.at(args.indexOf(u"--vault"_s) + 1), QDir::cleanPath(root));
        // Its agents' list: this device's agents.
        const QJsonArray agents = QJsonDocument::fromJson(readRaw(args.at(args.indexOf(u"--agents"_s) + 1)).toUtf8()).array();
        QCOMPARE(agents.size(), 1);
        QCOMPARE(agents.first().toObject().value(u"name"_s).toString(), u"Fałszywy agent"_s);
        // Its projects' list: this device's projects of Code mode, with their worktrees' root.
        QVERIFY(args.contains(u"--projects"_s));
        const QString projectsFile = args.at(args.indexOf(u"--projects"_s) + 1);
        const std::optional<QList<acp::HostProject>> listed = acp::parseHostProjects(readRaw(projectsFile).toUtf8());
        QVERIFY(listed && listed->size() == 1);
        QCOMPARE(listed->first().id, u"sklep-zolwia"_s);
        QCOMPARE(listed->first().path, QDir::cleanPath(project));
        QCOMPARE(listed->first().worktreesRoot, ws.projects()->worktreesRoot());
#ifndef Q_OS_WIN // (no permission bits there: Qt reports what the file's ACL allows the owner)
        QCOMPARE(QFile(projectsFile).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions());
#endif

        // The notebook offers it: its address and the agent's name, the other keys kept.
        const QString configPath = root + u"/.brulion/config.json"_s;
        QTRY_VERIFY_WITH_TIMEOUT(readRaw(configPath).contains(u"agentHosts"_s), kTimeoutMs);
        const QJsonObject config = QJsonDocument::fromJson(readRaw(configPath).toUtf8()).object();
        QCOMPARE(config.value(u"journalsFolder"_s).toString(), u"Dziennik"_s);
        QCOMPARE(config.value(u"przyszłość"_s).toInt(), 7);
        const QJsonObject offer = config.value(u"agentHosts"_s).toArray().first().toObject();
        QCOMPARE(offer.value(u"url"_s).toString(), url);
        QCOMPARE(offer.value(u"agents"_s).toArray().first().toObject().value(u"name"_s).toString(),
                 u"Fałszywy agent"_s);
        QVERIFY(!readRaw(configPath).contains(u"token"_s));
        QVERIFY(share->offers().isEmpty()); // (its own: no offer here)

        // The owner's phone pairs without a code, through the app's own client, just the agent picked.
        const QString port = QString::number(QUrl(url).port());
        const QString reachable = u"http://127.0.0.1:"_s + port;
        WorkspaceAgents *agentsHere = ws.chats()->agents();
        QList<AgentConfig> got;
        connect(agentsHere, &WorkspaceAgents::paired, this,
                [&got](bool, const QString &, const QList<AgentConfig> &agents) { got = agents; });
        QSignalSpy paired(agentsHere, &WorkspaceAgents::paired);
        agentsHere->pair(reachable, QString(), u"fake"_s);
        QTRY_COMPARE_WITH_TIMEOUT(paired.size(), 1, kTimeoutMs);
        QVERIFY2(paired.first().at(0).toBool(), qPrintable(paired.first().at(1).toString()));
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.first().remote, u"fake"_s);
        QVERIFY(got.first().token.startsWith(u"bat_"_s));
        QTRY_COMPARE_WITH_TIMEOUT(share->devices(), 1, kTimeoutMs);
        // That device lists this computer's projects; one added here is there at once (the file
        // written again, read again by the server that runs on).
        sync::NetworkHttpTransport transport;
        const auto projectsThere = [&] {
            QList<acp::RemoteProject> out;
            bool done = false;
            acp::fetchRemoteProjects(&transport, QUrl(reachable), got.first().token,
                                     [&](const QList<acp::RemoteProject> &p, const acp::RemoteError &) {
                                         out = p;
                                         done = true;
                                     });
            (void)QTest::qWaitFor([&done] { return done; }, kTimeoutMs);
            return out;
        };
        QList<acp::RemoteProject> there = projectsThere();
        QCOMPARE(there.size(), 1);
        QCOMPARE(there.first().name, u"Sklep żółwia"_s);
        const qint64 serverPid = share->server()->processId();
        const QString second = m_tmp.filePath(u"Drugi projekt"_s);
        QVERIFY(QDir().mkpath(second));
        QCOMPARE(ws.projects()->add(second), u"drugi-projekt"_s);
        QTRY_COMPARE_WITH_TIMEOUT(projectsThere().size(), 2, kTimeoutMs);
        QCOMPARE(share->server()->processId(), serverPid); // (not started again)

        // A code for a device outside the tailnet: its link for the QR code.
        QSignalSpy pairing(share, &AgentShare::pairingChanged);
        share->newPairing();
        QTRY_VERIFY_WITH_TIMEOUT(!share->pairCode().isEmpty(), kTimeoutMs);
        QCOMPARE(share->pairCode().size(), 9);
        const QUrl link(share->pairLink());
        QCOMPARE(link.scheme(), u"brulion"_s);
        QCOMPARE(QUrlQuery(link).queryItemValue(u"url"_s, QUrl::FullyDecoded), url);
        QVERIFY(!share->pairExpires().isEmpty());
        share->closePairing();
        QVERIFY(share->pairCode().isEmpty());

        // Every device unpaired.
        share->unpairAll();
        QTRY_COMPARE_WITH_TIMEOUT(share->devices(), 0, kTimeoutMs);

        // Without the trust: a device without a code is asked for it.
        share->setSettings(true, QString(), false);
        QTRY_VERIFY_WITH_TIMEOUT(share->server()->arguments().contains(u"--no-tailnet-trust"_s)
                                         && share->state() == u"running", kTimeoutMs);
        QVERIFY(share->owner().isEmpty());
        QSignalSpy codeNeeded(agentsHere, &WorkspaceAgents::codeNeeded);
        paired.clear();
        agentsHere->pair(u"http://127.0.0.1:"_s + QString::number(QUrl(share->url()).port()), QString());
        QTRY_COMPARE_WITH_TIMEOUT(paired.size(), 1, kTimeoutMs);
        QVERIFY(!paired.first().at(0).toBool());
        QVERIFY(paired.first().at(1).toString().isEmpty());
        QCOMPARE(codeNeeded.size(), 1);

        // Off: it stops and its offer leaves the notebook.
        const qint64 pid = share->server()->processId();
        QVERIFY(pid > 0);
        share->setSettings(false, QString(), true);
        QTRY_COMPARE_WITH_TIMEOUT(share->state(), u"off"_s, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!readRaw(configPath).contains(u"agentHosts"_s), kTimeoutMs);
        QCOMPARE(QJsonDocument::fromJson(readRaw(configPath).toUtf8()).object().value(u"przyszłość"_s).toInt(), 7);
#ifdef Q_OS_LINUX
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(u"/proc/%1"_s.arg(pid)), kTimeoutMs);
#endif
#endif
    }

    void offersOnAnotherDevice()
    {
        // A phone's notebook with the computer's offer: listed, and gone once its agent is added. (The
        // computer is not named as this one: its own offer is none here.)
        const QString root = m_tmp.filePath(u"Telefon"_s);
        // (No raw strings with addresses here: moc reads "//" in them as a comment.)
        const auto offerOf = [](const QString &url, const QString &host, const QString &id, const QString &name) {
            return QJsonObject{{u"url"_s, url},
                               {u"host"_s, host},
                               {u"agents"_s, QJsonArray{QJsonObject{{u"id"_s, id}, {u"name"_s, name}}}}};
        };
        const auto configWith = [](const QJsonObject &offer) {
            return QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"agentHosts"_s, QJsonArray{offer}}}).toJson();
        };
        QVERIFY(writeRaw(root + u"/.brulion/config.json"_s,
                         configWith(offerOf(u"http:"_s + u"//komputer-ali.tail1ea20e.ts.net:8790"_s, u"komputer-ali"_s, u"claude"_s,
                                            u"Claude Code"_s))));
        QVERIFY(writeRaw(root + u"/Dom.md"_s, "- x\n"));
        Workspace ws;
        QString error;
        QVERIFY2(ws.open(root, root + u"-state"_s, &error), qPrintable(error));
        ws.finishIndexing();
        AgentShare *share = ws.chats()->share();
        QTRY_COMPARE_WITH_TIMEOUT(share->offers().size(), 1, kTimeoutMs);
        QCOMPARE(share->offers().first().toMap().value(u"url"_s).toString(),
                 u"http:"_s + u"//komputer-ali.tail1ea20e.ts.net:8790"_s);
        QSignalSpy changed(share, &AgentShare::offersChanged);
        AgentConfig added{u"nixos-claude"_s, u"Claude Code"_s, u"npx …"_s, true};
        added.url = u"http:"_s + u"//komputer-ali.tail1ea20e.ts.net:8790"_s;
        added.remote = u"claude"_s;
        added.token = u"bat_x"_s;
        ws.chats()->setAgents({added});
        QTRY_VERIFY_WITH_TIMEOUT(share->offers().isEmpty(), kTimeoutMs);
        // A sync brings another computer's offer: listed.
        QVERIFY(ws.vault()->writeText(
                vaultConfigPath(),
                QString::fromUtf8(configWith(offerOf(u"http:"_s + u"//laptop.tail1ea20e.ts.net:8790"_s, u"laptop"_s,
                                                     u"codex"_s, u"Codex"_s)))));
        QTRY_COMPARE_WITH_TIMEOUT(share->offers().size(), 1, kTimeoutMs);
        QCOMPARE(share->offers().first().toMap().value(u"host"_s).toString(), u"laptop"_s);
    }
};

QTEST_MAIN(TestAgentShare)
#include "tst_agentshare.moc"

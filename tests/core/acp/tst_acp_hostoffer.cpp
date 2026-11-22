// A computer's agents offered in the notebook's .brulion/config.json (core acp/hostoffer.h): only an
// address of a tailnet is offered (a LAN or internet address, a look-alike name, user info, no
// scheme: left out), names cut to one line, agents with odd ids dropped, a computer named twice once,
// the limits, and writing: the same computer replaced (its port or scheme changed), others and the
// other keys kept, the key gone with the last offer.
#include "acp/hostoffer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::acp;

namespace {

QJsonObject entry(const QString &url, const QString &host, const QJsonArray &agents)
{
    return QJsonObject{{u"url"_s, url}, {u"host"_s, host}, {u"agents"_s, agents}};
}

QJsonArray claude()
{
    return QJsonArray{QJsonObject{{u"id"_s, u"claude"_s}, {u"name"_s, u"Claude Code"_s}}};
}

QJsonObject config(const QJsonArray &offers)
{
    return QJsonObject{{u"version"_s, 1}, {u"journalsFolder"_s, u"Dziennik"_s}, {HostOffersKey, offers}};
}

} // namespace

class TestAcpHostOffer : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void read()
    {
        const QList<HostOffer> offers = hostOffers(config(QJsonArray{
                entry(u"http://nixos.tail1ea20e.ts.net:8790"_s, u"nixos"_s,
                      QJsonArray{QJsonObject{{u"id"_s, u"claude"_s}, {u"name"_s, u"Claude Code"_s}},
                                 QJsonObject{{u"id"_s, u"codex"_s}, {u"name"_s, u"Codex\nna\tlaptopie"_s}},
                                 QJsonObject{{u"id"_s, u"zła nazwa"_s}, {u"name"_s, u"X"_s}},
                                 QJsonObject{{u"id"_s, u"claude"_s}, {u"name"_s, u"Twice"_s}},
                                 QJsonObject{{u"id"_s, u"gemini"_s}}}),
                entry(u"https://100.98.93.11/brulion/acp/v1/"_s, QString(), claude())}));
        QCOMPARE(offers.size(), 2);
        QCOMPARE(offers.at(0).url, u"http://nixos.tail1ea20e.ts.net:8790"_s);
        QCOMPARE(offers.at(0).host, u"nixos"_s);
        QCOMPARE(offers.at(0).agents.size(), 3);
        QCOMPARE(offers.at(0).agents.at(1).name, u"Codex na laptopie"_s);
        QCOMPARE(offers.at(0).agents.at(2).name, u"gemini"_s); // (no name: its id)
        QVERIFY(offers.at(0).agents.at(0).command.isEmpty());
        QCOMPARE(offers.at(1).url, u"https://100.98.93.11/brulion"_s);
        QCOMPARE(offers.at(1).host, u"100"_s); // (no name: its address's first part)
    }

    void refused_data()
    {
        QTest::addColumn<QJsonObject>("offer");
        QTest::newRow("lan") << entry(u"http://192.168.1.10:8790"_s, u"pc"_s, claude());
        QTest::newRow("internet") << entry(u"https://agent.example.com"_s, u"pc"_s, claude());
        QTest::newRow("look-alike") << entry(u"https://nixos.tail1.ts.net.example.com"_s, u"pc"_s, claude());
        QTest::newRow("no scheme") << entry(u"nixos.tail1ea20e.ts.net:8790"_s, u"pc"_s, claude());
        QTest::newRow("other scheme") << entry(u"ftp://nixos.tail1ea20e.ts.net"_s, u"pc"_s, claude());
        QTest::newRow("user info") << entry(u"http://ala:haslo@nixos.tail1ea20e.ts.net"_s, u"pc"_s, claude());
        QTest::newRow("query") << entry(u"http://nixos.tail1ea20e.ts.net/?code=K7QM2XRP"_s, u"pc"_s, claude());
        QTest::newRow("no agents") << entry(u"http://nixos.tail1ea20e.ts.net"_s, u"pc"_s, QJsonArray());
        QTest::newRow("not an object") << QJsonObject();
    }
    void refused()
    {
        QFETCH(QJsonObject, offer);
        QVERIFY(hostOffers(config(QJsonArray{offer})).isEmpty());
    }

    void malformed()
    {
        QVERIFY(hostOffers(QJsonObject()).isEmpty());
        QVERIFY(hostOffers(QJsonObject{{HostOffersKey, u"http://nixos.tail1ea20e.ts.net"_s}}).isEmpty());
        QVERIFY(hostOffers(QJsonObject{{HostOffersKey, QJsonArray{1, u"x"_s, QJsonArray()}}}).isEmpty());
    }

    void limits()
    {
        QJsonArray many;
        for (int i = 0; i < MaxHostOffers + 3; ++i)
            many.append(entry(u"http://pc%1.tail1.ts.net"_s.arg(i), u"pc%1"_s.arg(i), claude()));
        // The same computer twice: once (its first).
        many.prepend(entry(u"http://PC0.tail1.ts.net:9000"_s, u"pierwszy"_s, claude()));
        const QList<HostOffer> offers = hostOffers(config(many));
        QCOMPARE(offers.size(), MaxHostOffers);
        QCOMPARE(offers.first().host, u"pierwszy"_s);
        QJsonArray agents;
        for (int i = 0; i < MaxOfferedAgents + 5; ++i)
            agents.append(QJsonObject{{u"id"_s, u"a%1"_s.arg(i)}, {u"name"_s, QString(200, u'ż')}});
        const QList<HostOffer> one = hostOffers(config({entry(u"http://pc.tail1.ts.net"_s, u"pc"_s, agents)}));
        QCOMPARE(one.first().agents.size(), MaxOfferedAgents);
        QCOMPARE(one.first().agents.first().name.size(), 80);
    }

    void write()
    {
        HostOffer offer{u"http://nixos.tail1ea20e.ts.net:8790"_s, u"nixos"_s, {{u"claude"_s, u"Claude Code"_s, {}}}};
        const QJsonObject other = entry(u"http://laptop.tail1ea20e.ts.net:8790"_s, u"laptop"_s, claude());
        QJsonObject c = withHostOffer(config(QJsonArray{other}), offer);
        QCOMPARE(c.value(u"journalsFolder"_s).toString(), u"Dziennik"_s); // (the other keys kept)
        QList<HostOffer> offers = hostOffers(c);
        QCOMPARE(offers.size(), 2);
        QCOMPARE(offers.first(), offer);
        // The same computer moved to another port: replaced, not added.
        offer.url = u"http://nixos.tail1ea20e.ts.net:8791"_s;
        offer.agents.append({u"codex"_s, u"Codex"_s, {}});
        c = withHostOffer(c, offer);
        offers = hostOffers(c);
        QCOMPARE(offers.size(), 2);
        QCOMPARE(offers.first(), offer);
        // Removed: the other stays; with the last the key goes.
        c = withoutHostOffer(c, u"http://NixOS.tail1ea20e.ts.net"_s);
        offers = hostOffers(c);
        QCOMPARE(offers.size(), 1);
        QCOMPARE(offers.first().host, u"laptop"_s);
        c = withoutHostOffer(c, u"http://laptop.tail1ea20e.ts.net:8790"_s);
        QVERIFY(!c.contains(HostOffersKey));
        QCOMPARE(c.value(u"version"_s).toInt(), 1);
    }

    void sameComputer()
    {
        QVERIFY(sameHost(u"http://nixos.tail1.ts.net:8790"_s, u"https://NIXOS.tail1.ts.net"_s));
        QVERIFY(!sameHost(u"http://nixos.tail1.ts.net"_s, u"http://laptop.tail1.ts.net"_s));
        QVERIFY(!sameHost(QString(), QString()));
    }
};

QTEST_GUILESS_MAIN(TestAcpHostOffer)
#include "tst_acp_hostoffer.moc"

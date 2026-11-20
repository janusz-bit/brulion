// The CalDAV client and the task sync against a real Radicale (scripts/caldav-radicale-test.sh
// starts one from nixpkgs and sets BRULION_RADICALE_URL, _USER and _PASSWORD; without them every
// test is skipped — ctest runs it that way). What only a real server tells: its discovery, its XML,
// its ETags and sync tokens, what it keeps of what Brulion writes.
#include "caldav/calendarsync.h"

#include "index/vaultindex.h"
#include "sync/http.h"
#include "vault/vault.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

class TestCalDavRadicale : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void discoveryAndTwoWaySync();

private:
    QString m_url;
    QString m_user;
    QString m_password;
};

void TestCalDavRadicale::initTestCase()
{
    m_url = qEnvironmentVariable("BRULION_RADICALE_URL");
    m_user = qEnvironmentVariable("BRULION_RADICALE_USER");
    m_password = qEnvironmentVariable("BRULION_RADICALE_PASSWORD");
    if (m_url.isEmpty())
        QSKIP("no Radicale (scripts/caldav-radicale-test.sh runs one)");
}

void TestCalDavRadicale::discoveryAndTwoWaySync()
{
    sync::NetworkHttpTransport transport;
    CalDavClient client(&transport, QUrl(m_url), m_user, m_password);
    std::optional<Discovery> found;
    client.discover([&found](const Discovery &d) { found = d; });
    QTRY_VERIFY_WITH_TIMEOUT(found.has_value(), 20'000);
    QVERIFY2(!found->error.isError(), qPrintable(found->error.message));
    const auto calendar = std::ranges::find(found->calendars, u"Zadania"_s, &CalendarInfo::name);
    QVERIFY(calendar != found->calendars.end());
    QVERIFY(calendar->holdsTasks());
    QVERIFY(!calendar->syncToken.isEmpty());

    QTemporaryDir dir;
    QDir().mkpath(dir.filePath(u"notes"_s));
    Vault vault(dir.filePath(u"notes"_s), dir.filePath(u"state"_s));
    QVERIFY(vault.open());
    VaultIndex index(vault.config());
    CalendarStore store(dir.filePath(u"state/calendars"_s));
    CalendarSettings settings;
    Account account;
    account.id = u"a-1"_s;
    account.server = m_url;
    account.user = m_user;
    account.homeUrl = found->homeUrl;
    settings.accounts.append(account);
    CalendarEntry entry;
    entry.id = u"c-1"_s;
    entry.account = account.id;
    entry.url = calendar->url;
    entry.name = calendar->name;
    entry.syncTasks = true;
    entry.taskPage = u"Kalendarze/Zadania.md"_s;
    settings.calendars.append(entry);
    store.saveSettings(settings);
    CalendarSync sync(&store);
    sync.setVault(&vault, &index);
    sync.setTransports(&transport, nullptr);
    const QString password = m_password;
    sync.setPasswords([password](const QString &) { return password; });
    const auto run = [&sync]() {
        std::optional<CalendarRunReport> report;
        sync.run(true, false, [&report](const CalendarRunReport &r) { report = r; });
        for (int i = 0; i < 3000 && !report; ++i)
            QTest::qWait(10);
        if (!report)
            qFatal("the calendar sync did not finish");
        return *report;
    };
    // The first run: what the script put there arrives.
    CalendarRunReport report = run();
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    const QString page = vault.readText(entry.taskPage);
    QVERIFY2(page.contains(u"- [ ] Kupić żółty ser 📅 2026-11-21 ^c"_s), qPrintable(page));
    // A new line and an edit go to the server; a second run changes nothing.
    QString text = page;
    text.replace(u"Kupić żółty ser"_s, u"Kupić żółty ser i masło"_s);
    text += u"- [ ] Zadzwonić do Łucji 📅 2026-11-22 18:00 ⏰ 2026-11-22 17:30\n"_s;
    QVERIFY(vault.writeText(entry.taskPage, text));
    report = run();
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    QCOMPARE(report.uploaded, 2);
    report = run();
    QCOMPARE(report.uploaded + report.downloaded, 0);
    // The server has them as Brulion wrote them (read back through a fresh token).
    std::optional<ChangeSet> got;
    client.changes(QUrl(calendar->url), {}, [&got](const ChangeSet &c) { got = c; });
    QTRY_VERIFY_WITH_TIMEOUT(got.has_value(), 20'000);
    QStringList paths;
    for (const RemoteResource &r : got->changed)
        paths.append(r.path);
    std::optional<QList<RemoteResource>> data;
    client.fetch(QUrl(calendar->url), paths, [&data](const QList<RemoteResource> &r, const sync::WebDavError &) { data = r; });
    QTRY_VERIFY_WITH_TIMEOUT(data.has_value(), 20'000);
    QByteArray joined;
    for (const RemoteResource &r : *data)
        joined += r.data;
    QVERIFY(joined.contains("SUMMARY:Kupić żółty ser i masło"));
    QVERIFY(joined.contains("SUMMARY:Zadzwonić do Łucji"));
    QVERIFY(joined.contains("DUE;TZID="));
}

QTEST_GUILESS_MAIN(TestCalDavRadicale)
#include "tst_caldav_radicale.moc"

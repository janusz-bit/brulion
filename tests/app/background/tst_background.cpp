// The background check of the notes (src/app/background/backgroundcheck.h): the target's JSON, the
// reminders it hands Android (the app's own set), and whole checks of a notebook folder — missing,
// empty, with reminders, with a Nextcloud sync first (in-memory WebDAV server), with the app syncing
// at that moment (the store's sync lock held).
#include "backgroundcheck.h"

#include "fakewebdavserver.h"
#include "synctest.h"

#include "index/vaultindex.h"
#include "reminders/reminders.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace brulion::background;
using namespace Qt::StringLiterals;

namespace {

const QDateTime kNow(QDate(2026, 10, 14), QTime(10, 0));

/// Records what a scheduler hands the platform.
class RecordingSink : public NotificationSink {
public:
    bool firesWhenClosed() const override { return true; } // Android
    void schedule(const QList<ScheduledNotification> &notifications) override { scheduled.append(notifications); }
    void showNow(const ScheduledNotification &) override { }
    QList<QList<ScheduledNotification>> scheduled;
};

QString journal()
{
    return u"- [ ] Odebrać paczkę z paczkomatu ⏰ 2026-10-14 17:30\n"
           u"- ⏰! 2026-10-15 06:30 Pobudka — pociąg do Łodzi\n"
           u"- [x] Już zrobione ⏰ 2026-10-14 12:00 ✅ 2026-10-14\n"_s;
}

/// A notebook folder in `dir` with a journal and a page; its target.
Target notebookIn(const QTemporaryDir &dir)
{
    const QString root = dir.filePath(u"Notatnik"_s);
    synctest::writeFileBehind(root, u"journals/2026-10-14.md"_s, journal().toUtf8());
    synctest::writeFileBehind(root, u"Zdrowie.md"_s,
                              u"- Łyknąć witaminy 🔁 every day ⏰ 2026-10-10 08:00\n"_s.toUtf8());
    Target t;
    t.root = root;
    t.stateDir = dir.filePath(u"stan"_s);
    t.vault = u"notatnik-dom"_s;
    t.ringMinutes = 7;
    return t;
}

/// Run `check` to its end.
QJsonObject run(Check &check)
{
    QSignalSpy spy(&check, &Check::finished);
    check.start();
    if (!spy.wait(20000) && spy.isEmpty())
        return QJsonObject{{u"test"_s, u"no answer"_s}};
    return spy.first().first().toJsonObject();
}

QStringList titles(const QJsonObject &result)
{
    QStringList out;
    for (const QJsonValue &item : result.value(u"reminders"_s).toArray())
        out.append(item.toObject().value(u"title"_s).toString());
    return out;
}

} // namespace

class TestBackground : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void normalizedInterval_data()
    {
        QTest::addColumn<int>("minutes");
        QTest::addColumn<int>("expected");
        QTest::newRow("off") << 0 << 0;
        QTest::newRow("negative") << -5 << 0;
        QTest::newRow("too short") << 1 << 15;
        QTest::newRow("15") << 15 << 15;
        QTest::newRow("20") << 20 << 15;
        QTest::newRow("30") << 30 << 30;
        QTest::newRow("tie goes longer") << 45 << 60;
        QTest::newRow("hour") << 60 << 60;
        QTest::newRow("a day") << 24 * 60 << 60;
    }
    void normalizedInterval()
    {
        QFETCH(int, minutes);
        QFETCH(int, expected);
        QCOMPARE(background::normalizedInterval(minutes), expected);
    }

    void targetJsonRoundTrip()
    {
        Target t;
        t.root = u"/storage/emulated/0/Notatki — żółw"_s;
        t.stateDir = u"/data/user/0/io.github.januszbit.brulion/files/vaults/abc"_s;
        t.vault = u"abc"_s;
        t.ringMinutes = 25;
        t.language = u"en"_s;
        t.intervalMinutes = 60;
        QVERIFY(t.isValid());
        QCOMPARE(Target::fromJson(t.toJson()), std::optional<Target>(t));
        QVERIFY(!t.toJson().contains(u"nextcloud"_s));

        t.nextcloud = Target::Nextcloud{u"https://chmura.example.pl/remote.php/dav/files/jan/Brulion"_s, u"jan"_s,
                                        u"Telefon Łucji"_s};
        const QJsonObject json = t.toJson();
        QCOMPARE(Target::fromJson(json), std::optional<Target>(t));
        // The app password never travels with it.
        QVERIFY(!QJsonDocument(json).toJson().contains("password"));
    }

    void targetJsonIsNormalised()
    {
        const std::optional<Target> t = Target::fromJson(QJsonObject{
                {u"root"_s, u"/n"_s},
                {u"stateDir"_s, u"/s"_s},
                {u"vault"_s, u"v"_s},
                {u"ringMinutes"_s, 500},
                {u"language"_s, u"de"_s},
                {u"intervalMinutes"_s, 20},
                {u"nextcloud"_s, QJsonObject{{u"davUrl"_s, u"https://x"_s}}}, // no user: no sync
        });
        QVERIFY(t);
        QCOMPARE(t->ringMinutes, 60);
        QCOMPARE(t->language, u"pl"_s);
        QCOMPARE(t->intervalMinutes, 15);
        QVERIFY(!t->nextcloud);

        const std::optional<Target> off = Target::fromJson(QJsonObject{
                {u"root"_s, u"/n"_s}, {u"stateDir"_s, u"/s"_s}, {u"vault"_s, u"v"_s}, {u"intervalMinutes"_s, 0}});
        QVERIFY(off);
        QVERIFY(!off->isValid());

        QVERIFY(!Target::fromJson({}));
        QVERIFY(!Target::fromJson(QJsonObject{{u"stateDir"_s, u"/s"_s}, {u"vault"_s, u"v"_s}}));
        QVERIFY(!Target::fromJson(QJsonObject{{u"root"_s, 5}, {u"stateDir"_s, u"/s"_s}, {u"vault"_s, u"v"_s}}));
    }

    void upcomingIsTheSetTheAppHandsAndroid_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("ringMinutes");
        QTest::newRow("pl") << u"pl"_s << 10;
        QTest::newRow("en, 25 min") << u"en"_s << 25;
    }
    void upcomingIsTheSetTheAppHandsAndroid()
    {
        QFETCH(QString, language);
        QFETCH(int, ringMinutes);
        VaultIndex index;
        {
            VaultIndex::Batch batch(index);
            index.update(u"journals/2026-10-14.md"_s, journal());
            index.update(u"Projekty/Dom.md"_s, u"- [ ] Malowanie salonu 📅 2026-10-16 ⏰ 2026-10-15 12:00\n"_s);
            for (int i = 0; i < 80; ++i) // more than the scheduler hands over
                index.update(u"Codzienne/%1.md"_s.arg(i), u"- Krok %1 🔁 every day ⏰ 2026-10-01 07:%2\n"_s.arg(i).arg(
                                                                  i % 60, 2, 10, QLatin1Char('0')));
        }
        // What the app's scheduler (as WorkspaceReminders sets it up) hands the platform.
        RecordingSink sink;
        ReminderScheduler scheduler(&index, &sink, nullptr, [] { return kNow; });
        scheduler.setAlarmRingMinutes(ringMinutes);
        scheduler.setLocale(language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish);
        scheduler.start();
        QCOMPARE(sink.scheduled.size(), 1);
        QList<ScheduledNotification> app = sink.scheduled.first();
        for (ScheduledNotification &n : app)
            n.vault = u"notatnik-dom"_s; // WorkspaceReminders' proxy stamps the notebook

        Target target;
        target.root = u"/n"_s;
        target.stateDir = u"/s"_s;
        target.vault = u"notatnik-dom"_s;
        target.ringMinutes = ringMinutes;
        target.language = language;
        const QList<ScheduledNotification> background = upcomingFor(index, kNow, target);
        QCOMPARE(background.size(), ReminderScheduler::DefaultMaxScheduled);
        QCOMPARE(background, app);
        for (const ScheduledNotification &n : background) {
            QCOMPARE(n.ringMinutes, ringMinutes);
            QCOMPARE(n.vault, u"notatnik-dom"_s);
        }
    }

    void aMissingFolderFailsAndArmsNothing()
    {
        QTemporaryDir dir;
        Target target = notebookIn(dir);
        target.root = dir.filePath(u"Nie ma takiego"_s);
        Check check(target, {}, nullptr, [] { return kNow; });
        const QJsonObject result = run(check);
        QCOMPARE(result.value(u"ok"_s).toBool(true), false);
        QCOMPARE(result.value(u"error"_s).toString(), u"missing"_s);
        QVERIFY(!result.contains(u"reminders"_s));
        QVERIFY(!QFileInfo::exists(target.root)); // never created
    }

    void anEmptyFolderFailsAndArmsNothing()
    {
        // An unmounted card, a storage permission gone: an empty folder must not cancel every alarm.
        QTemporaryDir dir;
        Target target = notebookIn(dir);
        target.root = dir.filePath(u"Pusty"_s);
        QVERIFY(QDir().mkpath(target.root));
        Check check(target, {}, nullptr, [] { return kNow; });
        const QJsonObject result = run(check);
        QCOMPARE(result.value(u"ok"_s).toBool(true), false);
        QCOMPARE(result.value(u"error"_s).toString(), u"empty"_s);
    }

    void anInvalidTargetFails()
    {
        Check check(Target{}, {}, nullptr, [] { return kNow; });
        const QJsonObject result = run(check);
        QCOMPARE(result.value(u"ok"_s).toBool(true), false);
        QCOMPARE(result.value(u"error"_s).toString(), u"invalid"_s);
    }

    void aCheckReadsTheRemindersOfTheNotebook()
    {
        QTemporaryDir dir;
        const Target target = notebookIn(dir);
        Check check(target, {}, nullptr, [] { return kNow; });
        const QJsonObject result = run(check);
        QVERIFY2(result.value(u"ok"_s).toBool(), QJsonDocument(result).toJson().constData());
        QCOMPARE(result.value(u"vault"_s).toString(), u"notatnik-dom"_s);
        QCOMPARE(result.value(u"notes"_s).toInt(), 2);
        QCOMPARE(result.value(u"sync"_s).toString(), u"off"_s);
        const QStringList got = titles(result);
        QVERIFY2(got.contains(u"Odebrać paczkę z paczkomatu"_s), qPrintable(got.join(u", "_s)));
        QVERIFY(got.contains(u"Pobudka — pociąg do Łodzi"_s));
        QVERIFY(got.contains(u"Łyknąć witaminy"_s));
        QVERIFY(!got.contains(u"Już zrobione"_s));
        for (const QJsonValue &v : result.value(u"reminders"_s).toArray()) {
            const QJsonObject item = v.toObject();
            QCOMPARE(item.value(u"vault"_s).toString(), u"notatnik-dom"_s);
            QCOMPARE(item.value(u"ringMinutes"_s).toInt(), 7);
            QVERIFY(item.value(u"id"_s).toInt() > 0);
            QVERIFY(!item.value(u"key"_s).toString().isEmpty());
        }
        // The notes are only read.
        QCOMPARE(synctest::filesBelow(target.root), (QStringList{u"Zdrowie.md"_s, u"journals/2026-10-14.md"_s}));
    }

    void aCheckSyncsWithNextcloudFirst()
    {
        // A reminder written on the computer reaches the phone through the server.
        synctest::FakeWebDavServer server;
        server.putText(synctest::kFolder + u"/Z komputera.md"_s, u"- Spotkanie z Łucją ⏰ 2026-10-14 15:45\n"_s);
        QTemporaryDir dir;
        Target target = notebookIn(dir);
        target.nextcloud = Target::Nextcloud{server.url(synctest::kFolder), server.options().username, u"Telefon"_s};
        Check check(target, server.options().password, &server, [] { return kNow; });
        const QJsonObject result = run(check);
        QVERIFY2(result.value(u"ok"_s).toBool(), QJsonDocument(result).toJson().constData());
        QCOMPARE(result.value(u"sync"_s).toString(), u"ok"_s);
        QCOMPARE(result.value(u"downloaded"_s).toInt(), 1);
        QCOMPARE(result.value(u"uploaded"_s).toInt(), 2);
        QCOMPARE(result.value(u"notes"_s).toInt(), 3);
        QVERIFY(titles(result).contains(u"Spotkanie z Łucją"_s));
        QVERIFY(QFileInfo::exists(target.root + u"/Z komputera.md"_s));
        // The sync bookkeeping is the app's: the same store in the notebook's state folder.
        QVERIFY(QFileInfo::exists(target.stateDir + u"/store.json"_s));
    }

    void aCheckWhileTheAppSyncsOnlySkipsTheSync()
    {
        synctest::FakeWebDavServer server;
        server.putText(synctest::kFolder + u"/Z komputera.md"_s, u"- Spotkanie ⏰ 2026-10-14 15:45\n"_s);
        QTemporaryDir dir;
        Target target = notebookIn(dir);
        target.nextcloud = Target::Nextcloud{server.url(synctest::kFolder), server.options().username, u"Telefon"_s};
        QVERIFY(QDir().mkpath(target.stateDir));
        QLockFile app(target.stateDir + u"/store.json.sync-lock"_s);
        QVERIFY(app.tryLock(0));
        Check check(target, server.options().password, &server, [] { return kNow; });
        const QJsonObject result = run(check);
        QVERIFY2(result.value(u"ok"_s).toBool(), QJsonDocument(result).toJson().constData());
        QCOMPARE(result.value(u"sync"_s).toString(), u"busy"_s);
        QCOMPARE(result.value(u"syncError"_s).toString(), u"other-process"_s);
        QVERIFY(server.requests().isEmpty());
        // What the phone has still counts.
        QVERIFY(titles(result).contains(u"Odebrać paczkę z paczkomatu"_s));
        QVERIFY(!titles(result).contains(u"Spotkanie"_s));
    }

    void aCheckWithoutThePasswordSkipsTheSync()
    {
        synctest::FakeWebDavServer server;
        QTemporaryDir dir;
        Target target = notebookIn(dir);
        target.nextcloud = Target::Nextcloud{server.url(synctest::kFolder), server.options().username, u"Telefon"_s};
        Check check(target, {}, &server, [] { return kNow; });
        const QJsonObject result = run(check);
        QVERIFY(result.value(u"ok"_s).toBool());
        QCOMPARE(result.value(u"sync"_s).toString(), u"skipped"_s);
        QVERIFY(server.requests().isEmpty());
    }

    void aFailingSyncStillArmsWhatThePhoneHas()
    {
        synctest::FakeWebDavServer server;
        QTemporaryDir dir;
        Target target = notebookIn(dir);
        target.nextcloud = Target::Nextcloud{server.url(synctest::kFolder), server.options().username, u"Telefon"_s};
        Check check(target, u"złe hasło"_s, &server, [] { return kNow; });
        const QJsonObject result = run(check);
        QVERIFY(result.value(u"ok"_s).toBool());
        QCOMPARE(result.value(u"sync"_s).toString(), u"failed"_s);
        QCOMPARE(result.value(u"syncError"_s).toString(), u"auth"_s);
        QVERIFY(titles(result).contains(u"Pobudka — pociąg do Łodzi"_s));
    }
};

QTEST_GUILESS_MAIN(TestBackground)
#include "tst_background.moc"

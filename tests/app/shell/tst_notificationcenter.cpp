// NotificationCenter (docs/UI.md "Notifications"): what the app told the user — kept per notebook on
// this device, newest first, read or unread, pruned by count and age; posts of agents, extensions
// and scripts checked against the settings and the hourly limit, titled with who sends them, cleaned.

#include "notificationcenter.h"

#include "vault/localstore.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::LocalStore;
using brulion::shell::NotificationCenter;
using Kind = NotificationCenter::Kind;

class TestNotificationCenter : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    QDateTime m_now{QDate(2026, 11, 25), QTime(9, 0)};
    bool m_active = false;

    std::unique_ptr<NotificationCenter> make()
    {
        auto center = std::make_unique<NotificationCenter>([this] { return m_now; });
        center->setAppActive([this] { return m_active; });
        return center;
    }
    static QVariantMap first(const NotificationCenter &c) { return c.entries().value(0).toMap(); }

private Q_SLOTS:
    void recordsAndKeeps()
    {
        const QString file = m_tmp.filePath(u"store-1.json"_s);
        {
            LocalStore store(file);
            auto center = make();
            center->attach(&store);
            QSignalSpy changed(center.get(), &NotificationCenter::changed);
            const int reminder =
                    center->record(Kind::Reminder, QString(), u"Zadzwonić do mamy"_s, u"dziś 9:00"_s, u"Dom.md"_s);
            m_now = m_now.addSecs(60);
            m_active = true; // (came while the app was in front: seen)
            center->record(Kind::Chat, u"Claude Code"_s, u"Plan wyjazdu"_s, u"Gotowe: trzy dni w Zakopanem"_s,
                           u"AI/2026-11-25 Plan wyjazdu.md"_s);
            m_active = false;
            QCOMPARE(changed.size(), 2);
            QCOMPARE(center->count(), 2);
            QCOMPARE(center->unread(), 1);
            QVariantMap newest = first(*center);
            QCOMPARE(newest.value(u"kind"_s).toString(), u"chat"_s);
            QCOMPARE(newest.value(u"source"_s).toString(), u"Claude Code"_s);
            QVERIFY(newest.value(u"read"_s).toBool());
            QCOMPARE(newest.value(u"time"_s).toString(), u"09:01"_s);
            QCOMPARE(newest.value(u"day"_s).toString(), u"2026-11-25"_s);
            QCOMPARE(center->entries(0, true).size(), 1);
            QCOMPARE(center->entries(0, true).first().toMap().value(u"entryId"_s).toInt(), reminder);
            // Opening what it points to (any letter case) reads it.
            center->markOpened(u"dom.md"_s);
            QCOMPARE(center->unread(), 0);
            store.flush();
        }
        // Kept for the notebook: read back, in order, with their state.
        LocalStore store(file);
        auto center = make();
        center->attach(&store);
        QCOMPARE(center->count(), 2);
        QCOMPARE(first(*center).value(u"title"_s).toString(), u"Plan wyjazdu"_s);
        QCOMPARE(center->entries().last().toMap().value(u"open"_s).toString(), u"Dom.md"_s);
        QCOMPARE(center->unread(), 0);
        // Another notebook (none): nothing.
        center->detach();
        QCOMPARE(center->count(), 0);
    }

    void readRemoveClear()
    {
        LocalStore store{QString()};
        auto center = make();
        center->attach(&store);
        const int a = center->record(Kind::Reminder, QString(), u"Ą"_s, u"ą"_s, QString());
        const int b = center->record(Kind::Timer, QString(), u"Przerwa minęła"_s, QString(), QString());
        center->record(Kind::Reminder, QString(), u"Ć"_s, u"ć"_s, QString());
        QCOMPARE(center->unread(), 3);
        center->markRead(a);
        center->markRead(a);
        QCOMPARE(center->unread(), 2);
        center->remove(b);
        QCOMPARE(center->count(), 2);
        center->remove(12345); // (none such)
        QCOMPARE(center->count(), 2);
        center->markAllRead();
        QCOMPARE(center->unread(), 0);
        QSignalSpy changed(center.get(), &NotificationCenter::changed);
        center->markAllRead(); // (nothing to do: no change)
        QCOMPARE(changed.size(), 0);
        center->clear();
        QCOMPARE(center->count(), 0);
        QVERIFY(store.get(u"notify/log"_s).isUndefined());
    }

    void pruned()
    {
        LocalStore store{QString()};
        auto center = make();
        center->attach(&store);
        center->record(Kind::Reminder, QString(), u"Stare"_s, QString(), QString());
        m_now = m_now.addDays(NotificationCenter::MaxAgeDays + 1);
        for (int i = 0; i < NotificationCenter::MaxEntries + 5; ++i)
            center->record(Kind::Reminder, QString(), u"Nr %1"_s.arg(i), QString(), QString());
        QCOMPARE(center->count(), NotificationCenter::MaxEntries);
        QCOMPARE(first(*center).value(u"title"_s).toString(), u"Nr %1"_s.arg(NotificationCenter::MaxEntries + 4));
        // A broken or later version's entry is left out, the rest read.
        store.set(u"notify/log"_s,
                  QJsonArray{QJsonObject{{u"kind"_s, u"hologram"_s}, {u"at"_s, u"2026-11-25T09:00:00"_s}},
                             QJsonObject{{u"kind"_s, u"reminder"_s}, {u"at"_s, u"nie data"_s}},
                             QJsonObject{{u"kind"_s, u"agent"_s},
                                         {u"title"_s, u"Codex: Gotowe"_s},
                                         {u"at"_s, m_now.toString(Qt::ISODate)}},
                             u"nie obiekt"_s});
        center->attach(&store);
        QCOMPARE(center->count(), 1);
        QCOMPARE(first(*center).value(u"kind"_s).toString(), u"agent"_s);
        QCOMPARE(center->unread(), 1);
    }

    void posts_data()
    {
        QTest::addColumn<QString>("who");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("shownTitle");
        QTest::addColumn<QString>("shownText");
        QTest::newRow("who and title") << u"Claude Code"_s << u"Budowanie"_s << u"Zbudowane"_s
                                       << u"Claude Code: Budowanie"_s << u"Zbudowane"_s;
        QTest::newRow("who alone") << u"Siłownia"_s << QString() << u"Przerwa"_s << u"Siłownia"_s << u"Przerwa"_s;
        QTest::newRow("nobody") << QString() << QString() << u"x"_s << u"Brulion"_s << u"x"_s;
        QTest::newRow("cleaned") << u"Agent\nzły"_s << u"Tytuł\r\nw dwóch"_s
                                 << u"\x1b[31mczerwony\x1b[0m\n\n\n\nkoniec "_s << u"Agent zły: Tytuł w dwóch"_s
                                 << u"[31mczerwony[0m\n\nkoniec"_s;
    }
    void posts()
    {
        QFETCH(QString, who);
        QFETCH(QString, title);
        QFETCH(QString, text);
        QFETCH(QString, shownTitle);
        QFETCH(QString, shownText);
        LocalStore store{QString()};
        auto center = make();
        center->attach(&store);
        QSignalSpy shown(center.get(), &NotificationCenter::shown);
        QCOMPARE(center->post({Kind::Extension, u"silownia"_s, who, title, text, u"Siłownia.md"_s}), QString());
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown.first().at(0).toString(), u"extension"_s);
        QCOMPARE(shown.first().at(1).toString(), u"Siłownia.md"_s);
        QCOMPARE(shown.first().at(2).toString(), shownTitle);
        QCOMPARE(shown.first().at(3).toString(), shownText);
        QCOMPARE(first(*center).value(u"title"_s).toString(), shownTitle);
        QCOMPARE(center->unread(), 1);
    }

    void limitsAndSettings()
    {
        LocalStore store{QString()};
        auto center = make();
        center->attach(&store);
        const NotificationCenter::Post post{Kind::Extension, u"Silownia"_s, u"Siłownia"_s,
                                            QString(),       u"Seria"_s,    QString()};
        for (int i = 0; i < NotificationCenter::MaxPerHour; ++i) {
            QCOMPARE(center->post(post), QString());
            m_now = m_now.addSecs(60);
        }
        qint64 retry = 0;
        QCOMPARE(center->post(post, &retry), u"limit"_s);
        QCOMPARE(retry, qint64(50) * 60 * 1000); // (the first went 10 minutes ago)
        // Its key in any letter case is the same source; another source and another kind count apart.
        QCOMPARE(center->post({Kind::Extension, u"SILOWNIA"_s, QString(), QString(), u"x"_s, QString()}), u"limit"_s);
        QCOMPARE(center->post({Kind::Extension, u"nawyki"_s, QString(), QString(), u"x"_s, QString()}), QString());
        QCOMPARE(center->post({Kind::Agent, u"silownia"_s, QString(), QString(), u"x"_s, QString()}), QString());
        m_now = m_now.addSecs(50 * 60);
        QCOMPARE(center->post(post), QString());
        // Empty once cleaned; turned off.
        QCOMPARE(center->post({Kind::Script, QString(), QString(), QString(), u" \x07 \n "_s, QString()}), u"empty"_s);
        center->setAllowed(Kind::Extension, false);
        QVERIFY(!center->allowed(Kind::Extension));
        QCOMPARE(center->post({Kind::Extension, u"nowe"_s, QString(), QString(), u"x"_s, QString()}), u"off"_s);
        center->setAllowed(Kind::Script, false); // (scripts cannot be turned off: the user's own)
        QCOMPARE(center->post({Kind::Script, QString(), QString(), QString(), u"x"_s, QString()}), QString());
        // While the app is in front: shown as a toast and kept as read.
        m_active = true;
        QSignalSpy shown(center.get(), &NotificationCenter::shown);
        QCOMPARE(center->post({Kind::Agent, u"a"_s, u"Codex"_s, QString(), u"Na wierzchu"_s, QString()}), QString());
        QVERIFY(shown.first().at(4).toBool());
        QVERIFY(first(*center).value(u"read"_s).toBool());
        m_active = false;
    }

    void clean_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("max");
        QTest::addColumn<QString>("expected");
        QTest::newRow("trimmed") << u"  żółw  "_s << 10 << u"żółw"_s;
        QTest::newRow("cut") << u"abcdefghij"_s << 5 << u"abcd…"_s;
        QTest::newRow("not half a character") << u"abc😀😀"_s << 5 << u"abc…"_s;
        QTest::newRow("tabs stay") << u"a\tb"_s << 10 << u"a\tb"_s;
        QTest::newRow("empty lines") << u"a\n\n\n\n\nb"_s << 20 << u"a\n\nb"_s;
        QTest::newRow("controls") << QString(u"a"_s + QChar(0) + u"b"_s + QChar(7) + u"c"_s + QChar(0x7f)) << 20
                                  << u"abc"_s;
    }
    void clean()
    {
        QFETCH(QString, text);
        QFETCH(int, max);
        QFETCH(QString, expected);
        QCOMPARE(NotificationCenter::clean(text, max), expected);
    }
};

QTEST_GUILESS_MAIN(TestNotificationCenter)
#include "tst_notificationcenter.moc"

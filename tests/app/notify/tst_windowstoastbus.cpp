// WindowsToastBus on a real Windows (windows.yml): the bus's facts, a toast shown, replaced, in
// the Action Center's history, closed and cleared — under a test AppUserModelID registered for the
// run and removed afterwards. A machine with notifications turned off (a CI runner may be one)
// skips what needs them; no WinRT error may escape the bus.
#include "appidentity.h"
#include "desktopnotifier.h"
#include "toastxml.h"
#include "windowstoastbus.h"

#include <QSettings>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::notify;

namespace {

const QString TestId = u"JanuszBit.Brulion.test-toastbus"_s;
const QString ClassesKey = u"HKEY_CURRENT_USER\\Software\\Classes\\AppUserModelId"_s;

brulion::ScheduledNotification reminder()
{
    brulion::ScheduledNotification n;
    n.id = u"0123456789abcdef"_s;
    n.title = u"Zadzwonić do pani Łucji — żółć <b>&\"'"_s;
    n.body = u"Test powiadomień Brulionu (CI)"_s;
    n.at = QDateTime(QDate(2026, 10, 27), QTime(9, 0));
    n.path = u"journals/2026-10-27.md"_s;
    n.line = 1;
    return n;
}

} // namespace

class TestWindowsToastBus : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QSettings classes(ClassesKey, QSettings::NativeFormat);
        QVERIFY(brulion::platform::registerAppUserModelId(classes, TestId, u"Brulion (test)"_s, QString()));
    }

    void cleanupTestCase()
    {
        QSettings classes(ClassesKey, QSettings::NativeFormat);
        classes.remove(TestId);
    }

    void facts()
    {
        WindowsToastBus bus(TestId);
        QCOMPARE(bus.name(), u"toast"_s);
        QCOMPARE(bus.appUserModelId(), TestId);
        QVERIFY(!bus.supportsSoundFiles());
        QVERIFY(bus.ringsItself());
        QCOMPARE(bus.themeSounds(), windowsToastSounds());
        QVERIFY(bus.capabilities().contains(u"actions"_s));
        QVERIFY(!bus.capabilities().contains(u"body-markup"_s));
        qInfo("Toast notifications %s for %s%s", bus.isAvailable() ? "allowed" : "not allowed", qPrintable(TestId),
              bus.lastError().isEmpty() ? "" : qPrintable(u" ("_s + bus.lastError() + u')'));
    }

    void showReplaceCloseClear()
    {
        WindowsToastBus bus(TestId);
        if (!bus.isAvailable())
            QSKIP("Toast notifications are turned off on this machine");
        const DesktopNotification message = buildDesktopNotification(
                reminder(), bus.capabilities(), 10, 0, SoundChoice{ SoundChoice::Mode::Silent, {}, {} });
        uint id = 0;
        bool answered = false;
        bus.notify(message, [&](uint shown) {
            answered = true;
            id = shown;
        });
        QVERIFY(answered); // synchronous
        if (id == 0)
            QSKIP(qPrintable(u"Windows refused the toast: "_s + bus.lastError()));

        // The same id again replaces it (an alarm's silent copy, a reminder shown late).
        DesktopNotification again = message;
        again.replacesId = id;
        uint replaced = 0;
        bus.notify(again, [&](uint shown) { replaced = shown; });
        QCOMPARE(replaced, id);

        const QString tag = QString::number(id);
        if (!QTest::qWaitFor([&] { return bus.historyTags().contains(tag); }, 5000))
            QSKIP("The toast is not in the Action Center's history on this machine");
        QCOMPARE(bus.historyTags().count(tag), 1);

        bus.close(id);
        QTRY_VERIFY(!bus.historyTags().contains(tag));

        // At quit nothing stays behind: a click there would reach no Brulion.
        uint second = 0;
        bus.notify(message, [&](uint shown) { second = shown; });
        QVERIFY(second != 0 && second != id);
        QTRY_VERIFY(bus.historyTags().contains(QString::number(second)));
        bus.clearAll();
        QTRY_VERIFY(bus.historyTags().isEmpty());
    }

    void unregisteredIdFailsQuietly()
    {
        // An id nobody registered (and an empty one): no exception, at worst no toast.
        for (const QString &id : { u"JanuszBit.Brulion.niezarejestrowany-żółw"_s, QString() }) {
            WindowsToastBus bus(id);
            bool answered = false;
            bus.notify(buildDesktopNotification(reminder(), bus.capabilities(), 5), [&](uint) { answered = true; });
            QVERIFY(answered);
            bus.close(1);
            bus.clearAll();
            Q_UNUSED(bus.historyTags());
        }
    }
};

QTEST_GUILESS_MAIN(TestWindowsToastBus)
#include "tst_windowstoastbus.moc"

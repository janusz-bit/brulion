// Notifier adapters: the freedesktop Notify message of a reminder or alarm, DesktopNotifier
// against a fake notification bus (snooze / open / OK actions, foreign notifications, replacing,
// ringing alarms, the tray fallback, Wayland activation tokens), the sounds (freedesktop hints of
// every choice, the sample notification), the JSON for Android's alarm scheduler and sound
// channels, and the platform factory.
#include "androidpayload.h"
#include "desktopnotifier.h"
#include "notifier.h"
#include "soundchoice.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ScheduledNotification;
using namespace brulion::notify;

namespace {

ScheduledNotification reminder()
{
    ScheduledNotification n;
    n.id = u"0123456789abcdef"_s;
    n.nativeId = 4242;
    n.title = u"Kupić mleko i żółty ser"_s;
    n.body = u"Dom & ogród <b> · termin: dziś 18:00"_s;
    n.at = QDateTime(QDate(2026, 10, 14), QTime(18, 0));
    n.path = u"journals/2026-10-14.md"_s;
    n.line = 3;
    return n;
}

ScheduledNotification alarm()
{
    ScheduledNotification n = reminder();
    n.id = u"fedcba9876543210"_s;
    n.nativeId = 77;
    n.title = u"Pobudka na lotnisko"_s;
    n.body = u"Podróże"_s;
    n.alarm = true;
    n.ringMinutes = 1;
    return n;
}

/// A notification service recording every call.
class FakeBus : public NotificationBus {
public:
    bool available = true;
    bool fail = false;
    QStringList caps{ u"actions"_s, u"body"_s, u"body-markup"_s };
    QList<DesktopNotification> sent;
    QList<uint> closed;
    uint nextId = 100;

    bool isAvailable() const override { return available; }
    QStringList capabilities() const override { return caps; }
    void notify(const DesktopNotification &n, std::function<void(uint id)> done) override
    {
        sent.append(n);
        done(fail ? 0 : (n.replacesId != 0 ? n.replacesId : ++nextId));
    }
    void close(uint id) override { closed.append(id); }

    void invoke(uint id, const QString &action) { Q_EMIT actionInvoked(id, action); }
    void dismiss(uint id) { Q_EMIT notificationClosed(id, 2); }
    void token(uint id, const QString &t) { Q_EMIT activationTokenReceived(id, t); }
};

} // namespace

class TestNotify : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void messageForReminder()
    {
        const DesktopNotification d =
                buildDesktopNotification(reminder(), { u"actions"_s, u"body-markup"_s }, 10);
        QCOMPARE(d.appName, u"Brulion"_s);
        QCOMPARE(d.appIcon, u"io.github.januszbit.brulion"_s);
        QCOMPARE(d.replacesId, 0u);
        QCOMPARE(d.summary, u"Kupić mleko i żółty ser"_s);
        // The server reads markup: note text is escaped, never markup.
        QCOMPARE(d.body, u"Dom &amp; ogród &lt;b&gt; · termin: dziś 18:00"_s);
        QCOMPARE(d.actions, (QStringList{ u"default"_s, u"Open note"_s, u"snooze"_s, u"Snooze 10 min"_s, u"ok"_s, u"OK"_s }));
        const QVariant urgency = d.hints.value(u"urgency"_s);
        QCOMPARE(urgency.metaType(), QMetaType::fromType<uchar>()); // a D-Bus byte
        QCOMPARE(urgency.value<uchar>(), uchar(1));
        QCOMPARE(d.hints.value(u"desktop-entry"_s).toString(), u"io.github.januszbit.brulion"_s);
        QVERIFY(!d.hints.contains(u"sound-name"_s));
        QVERIFY(!d.hints.contains(u"resident"_s));
        QCOMPARE(d.expireTimeout, -1);
    }

    void messageForAlarm()
    {
        const DesktopNotification d = buildDesktopNotification(alarm(), { u"actions"_s }, 5, 33);
        QCOMPARE(d.summary, u"⏰ Pobudka na lotnisko"_s);
        QCOMPARE(d.replacesId, 33u);
        QCOMPARE(d.hints.value(u"urgency"_s).value<uchar>(), uchar(2)); // critical
        QCOMPARE(d.hints.value(u"sound-name"_s).toString(), u"alarm-clock-elapsed"_s);
        QCOMPARE(d.hints.value(u"resident"_s).toBool(), true);
        QCOMPARE(d.expireTimeout, 0); // waits for the user
        QCOMPARE(d.actions.at(3), u"Snooze 5 min"_s);
    }

    void messageWithoutCapabilities()
    {
        ScheduledNotification n = reminder();
        const DesktopNotification d = buildDesktopNotification(n, {}, 10);
        QVERIFY(d.actions.isEmpty());
        QCOMPARE(d.body, n.body); // plain text servers get the text as it is

        n.title = u"   "_s;
        QCOMPARE(buildDesktopNotification(n, {}, 10).summary, n.body);
        n.body.clear();
        QCOMPARE(buildDesktopNotification(n, {}, 10).summary, u"Reminder"_s);
        n.alarm = true;
        QCOMPARE(buildDesktopNotification(n, {}, 10).summary, u"⏰ Alarm"_s);
        // Snooze lengths are clamped to the settings' range.
        QCOMPARE(buildDesktopNotification(reminder(), { u"actions"_s }, 500).actions.at(3), u"Snooze 120 min"_s);
        QCOMPARE(buildDesktopNotification(reminder(), { u"actions"_s }, 0).actions.at(3), u"Snooze 1 min"_s);
    }

    void showAndActions()
    {
        auto *bus = new FakeBus;
        DesktopNotifier notifier(bus);
        notifier.setSnoozeMinutes(15);
        QVERIFY(!notifier.firesWhenClosed());
        QCOMPARE(notifier.backend(), u"dbus"_s);
        QVERIFY(notifier.canShow());
        QSignalSpy snoozed(&notifier, &Notifier::snoozeRequested);
        QSignalSpy activated(&notifier, &Notifier::activated);

        notifier.showNow(reminder());
        QCOMPARE(bus->sent.size(), 1);
        QCOMPARE(bus->sent.first().actions.at(3), u"Snooze 15 min"_s);
        QCOMPARE(notifier.shownCount(), 1);
        const uint first = bus->nextId;

        // Other programs' notifications share the bus.
        bus->invoke(999, u"default"_s);
        QCOMPARE(activated.size(), 0);

        bus->invoke(first, u"snooze"_s);
        QCOMPARE(snoozed.size(), 1);
        QCOMPARE(qvariant_cast<ScheduledNotification>(snoozed.first().at(0)).id, reminder().id);
        QCOMPARE(snoozed.first().at(1).toInt(), 15);
        QCOMPARE(bus->closed, (QList<uint>{ first }));
        QCOMPARE(notifier.shownCount(), 0);
        bus->invoke(first, u"snooze"_s); // handled once
        QCOMPARE(snoozed.size(), 1);

        notifier.showNow(reminder());
        bus->invoke(bus->nextId, u"default"_s);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.first().at(0).toString(), u"journals/2026-10-14.md"_s);
        QCOMPARE(activated.first().at(1).toInt(), 3);

        notifier.showNow(reminder());
        bus->invoke(bus->nextId, u"ok"_s);
        QCOMPARE(activated.size(), 1);
        QCOMPARE(snoozed.size(), 1);
        QCOMPARE(bus->closed.size(), 3);

        notifier.showNow(reminder());
        bus->dismiss(bus->nextId);
        QCOMPARE(notifier.shownCount(), 0);
    }

    void sameReminderReplacesItsNotification()
    {
        auto *bus = new FakeBus;
        DesktopNotifier notifier(bus);
        notifier.showNow(reminder());
        notifier.showNow(reminder());
        QCOMPARE(bus->sent.size(), 2);
        QCOMPARE(bus->sent.at(1).replacesId, bus->nextId);
        QCOMPARE(notifier.shownCount(), 1);
    }

    void trayFallback()
    {
        auto *bus = new FakeBus;
        bus->fail = true; // no notification daemon answers
        DesktopNotifier notifier(bus);
        QSignalSpy tray(&notifier, &DesktopNotifier::trayMessageRequested);
        notifier.showNow(reminder());
        QCOMPARE(tray.size(), 1);
        QCOMPARE(tray.first().at(0).toString(), u"Kupić mleko i żółty ser"_s);
        QCOMPARE(tray.first().at(1).toString(), reminder().body); // plain text in the tray
        QCOMPARE(notifier.shownCount(), 0);

        DesktopNotifier trayOnly(nullptr);
        QCOMPARE(trayOnly.backend(), u"none"_s);
        QVERIFY(!trayOnly.canShow());
        trayOnly.setTrayAvailable(true);
        QCOMPARE(trayOnly.backend(), u"tray"_s);
        QVERIFY(trayOnly.canShow());
        QSignalSpy tray2(&trayOnly, &DesktopNotifier::trayMessageRequested);
        trayOnly.showNow(alarm());
        QCOMPARE(tray2.size(), 1);
        QCOMPARE(tray2.first().at(0).toString(), u"⏰ Pobudka na lotnisko"_s);
    }

    void alarmsRingUntilHandled()
    {
        auto *bus = new FakeBus;
        DesktopNotifier notifier(bus);
        QDateTime now(QDate(2026, 10, 14), QTime(6, 30));
        notifier.setClock([&now] { return now; });
        notifier.setRingInterval(20);

        notifier.showNow(alarm());
        const uint id = bus->nextId;
        QCOMPARE(notifier.ringingCount(), 1);
        QTRY_VERIFY(bus->sent.size() >= 3); // sent again (sound again) …
        QCOMPARE(bus->sent.last().replacesId, id); // … in place
        QCOMPARE(bus->sent.last().hints.value(u"sound-name"_s).toString(), u"alarm-clock-elapsed"_s);

        // After its ring time it stays on screen, silently.
        now = now.addSecs(61);
        QTRY_COMPARE(notifier.ringingCount(), 0);
        const qsizetype sent = bus->sent.size();
        QTest::qWait(80);
        QCOMPARE(bus->sent.size(), sent);
        QCOMPARE(notifier.shownCount(), 1);

        // An action stops a ringing alarm at once.
        ScheduledNotification other = alarm();
        other.id = u"1111111111111111"_s;
        notifier.showNow(other);
        QCOMPARE(notifier.ringingCount(), 1);
        bus->invoke(bus->nextId, u"ok"_s);
        QCOMPARE(notifier.ringingCount(), 0);

        // Ordinary reminders never ring.
        notifier.showNow(reminder());
        QCOMPARE(notifier.ringingCount(), 0);
    }

    void activationTokenIsHandedOver()
    {
        auto *bus = new FakeBus;
        DesktopNotifier notifier(bus);
        qunsetenv("XDG_ACTIVATION_TOKEN");
        notifier.showNow(reminder());
        bus->token(bus->nextId, u"token-źdźbło"_s);
        bus->invoke(bus->nextId, u"default"_s);
        QCOMPARE(QString::fromUtf8(qgetenv("XDG_ACTIVATION_TOKEN")), u"token-źdźbło"_s);
        qunsetenv("XDG_ACTIVATION_TOKEN");

        // Tokens of other programs' notifications (the signal is broadcast) are not ours to use.
        notifier.showNow(reminder());
        bus->token(4711, u"token-innego-programu"_s);
        bus->invoke(4711, u"default"_s);
        bus->invoke(bus->nextId, u"default"_s);
        QVERIFY(qgetenv("XDG_ACTIVATION_TOKEN").isEmpty());
    }

    void androidPayload()
    {
        ScheduledNotification a = reminder();
        ScheduledNotification b = alarm();
        b.nativeId = 0;     // derived from the id
        b.ringMinutes = 99; // clamped to 60
        b.line = -4;
        ScheduledNotification broken = reminder();
        broken.at = QDateTime();
        const QJsonArray payload = androidReminderPayload({ a, b, broken });
        QCOMPARE(payload.size(), 2);
        const QJsonObject first = payload.at(0).toObject();
        QCOMPARE(first.value(u"id"_s).toInt(), 4242);
        QCOMPARE(first.value(u"title"_s).toString(), u"Kupić mleko i żółty ser"_s);
        QCOMPARE(first.value(u"body"_s).toString(), a.body);
        QCOMPARE(first.value(u"at"_s).toInteger(), a.at.toMSecsSinceEpoch());
        QVERIFY(first.value(u"at"_s).isDouble());
        QCOMPARE(first.value(u"alarm"_s).toBool(), false);
        QCOMPARE(first.value(u"ringMinutes"_s).toInt(), 10);
        QCOMPARE(first.value(u"path"_s).toString(), u"journals/2026-10-14.md"_s);
        QCOMPARE(first.value(u"line"_s).toInt(), 3);
        const QJsonObject second = payload.at(1).toObject();
        QCOMPARE(second.value(u"id"_s).toInt(), brulion::nativeIdFor(b.id));
        QVERIFY(second.value(u"id"_s).toInt() > 0);
        QCOMPARE(second.value(u"alarm"_s).toBool(), true);
        QCOMPARE(second.value(u"ringMinutes"_s).toInt(), 60);
        QCOMPARE(second.value(u"line"_s).toInt(), 0);
        QVERIFY(androidReminderPayload({}).isEmpty());
    }

    void soundChoiceKeys()
    {
        using Mode = SoundChoice::Mode;
        for (Mode mode : {Mode::Default, Mode::Custom, Mode::VibrateOnly, Mode::Silent}) {
            Mode parsed = Mode::Silent;
            QVERIFY(soundModeFromKey(soundModeKey(mode), &parsed));
            QCOMPARE(parsed, mode);
        }
        Mode parsed = Mode::Silent;
        QVERIFY(!soundModeFromKey(u"głośno"_s, &parsed));
        QCOMPARE(parsed, Mode::Default);
        QVERIFY(!soundModeFromKey(QString(), nullptr));

        SoundKind kind = SoundKind::Reminder;
        QVERIFY(soundKindFromKey(u"alarm"_s, &kind));
        QCOMPARE(kind, SoundKind::Alarm);
        QCOMPARE(soundKindKey(SoundKind::Reminder), u"reminder"_s);
        QVERIFY(!soundKindFromKey(u"budzik"_s, &kind));

        // Normalised: trimmed, a custom sound without a uri is the default; another mode keeps
        // the remembered custom sound.
        const SoundChoice padded{Mode::Custom, u"  bell \n"_s, u"  Dzwonek   szkolny "_s};
        QCOMPARE(normalizedSound(padded), (SoundChoice{Mode::Custom, u"bell"_s, u"Dzwonek szkolny"_s}));
        QCOMPARE(normalizedSound(SoundChoice{Mode::Custom, u"   "_s, {}}).mode, Mode::Default);
        const SoundChoice remembered{Mode::Silent, u"content://media/27"_s, u"Żuraw"_s};
        QCOMPARE(normalizedSound(remembered), remembered);
        QVERIFY(!remembered.isCustom());
        QCOMPARE(soundFromVariant(soundToVariant(remembered)), remembered);
        QCOMPARE(soundFromVariant({{u"mode"_s, u"nonsense"_s}}), SoundChoice());
        QCOMPARE(soundFromVariant({}), SoundChoice());
    }

    void soundFiles()
    {
        QVERIFY(isSoundFile(u"file:///home/ja/D%C5%BAwi%C4%99ki/%C4%87ma.oga"_s));
        QVERIFY(isSoundFile(u"/usr/share/sounds/freedesktop/stereo/bell.oga"_s));
        QVERIFY(!isSoundFile(u"bell"_s));
        QVERIFY(!isSoundFile(u"content://media/internal/audio/media/27"_s));
        QCOMPARE(soundFilePath(u"file:///home/ja/D%C5%BAwi%C4%99ki/%C4%87ma%20nocna.oga"_s),
                 u"/home/ja/Dźwięki/ćma nocna.oga"_s);
        QCOMPARE(soundFilePath(u"/home/ja/./Dźwięki//dzwonek.wav"_s), u"/home/ja/Dźwięki/dzwonek.wav"_s);
        QCOMPARE(soundFilePath(u"bell"_s), QString());
        QVERIFY(desktopThemeSounds().contains(defaultAlarmThemeSound()));
        QCOMPARE(desktopThemeSounds().size(), 5);
    }

    void desktopSoundHints_data()
    {
        QTest::addColumn<bool>("isAlarm");
        QTest::addColumn<int>("mode");
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("soundName");
        QTest::addColumn<QString>("soundFile");
        QTest::addColumn<bool>("suppress");

        using Mode = SoundChoice::Mode;
        const QString file = u"file:///home/ja/D%C5%BAwi%C4%99ki/%C5%BCuraw.oga"_s;
        const QString path = u"/home/ja/Dźwięki/żuraw.oga"_s;
        // Reminders: the desktop's own default (no hint), or the chosen sound.
        QTest::newRow("reminder default")
                << false << int(Mode::Default) << QString() << QString() << QString() << false;
        QTest::newRow("reminder theme") << false << int(Mode::Custom) << u"message-new-instant"_s
                                        << u"message-new-instant"_s << QString() << false;
        QTest::newRow("reminder file") << false << int(Mode::Custom) << file << QString() << path << false;
        QTest::newRow("reminder silent") << false << int(Mode::Silent) << u"bell"_s << QString() << QString() << true;
        QTest::newRow("reminder vibrate")
                << false << int(Mode::VibrateOnly) << QString() << QString() << QString() << true;
        // Alarms: an alarm clock by default.
        QTest::newRow("alarm default") << true << int(Mode::Default) << QString() << u"alarm-clock-elapsed"_s
                                       << QString() << false;
        QTest::newRow("alarm theme") << true << int(Mode::Custom) << u"phone-incoming-call"_s
                                     << u"phone-incoming-call"_s << QString() << false;
        QTest::newRow("alarm file") << true << int(Mode::Custom) << path << QString() << path << false;
        QTest::newRow("alarm custom without uri")
                << true << int(Mode::Custom) << QString() << u"alarm-clock-elapsed"_s << QString() << false;
        QTest::newRow("alarm silent") << true << int(Mode::Silent) << QString() << QString() << QString() << true;
    }

    void desktopSoundHints()
    {
        QFETCH(bool, isAlarm);
        QFETCH(int, mode);
        QFETCH(QString, uri);
        QFETCH(QString, soundName);
        QFETCH(QString, soundFile);
        QFETCH(bool, suppress);

        const ScheduledNotification n = isAlarm ? alarm() : reminder();
        const SoundChoice sound{SoundChoice::Mode(mode), uri, {}};
        const DesktopNotification d = buildDesktopNotification(n, {u"actions"_s, u"sound"_s}, 10, 0, sound);
        QCOMPARE(d.hints.value(u"sound-name"_s).toString(), soundName);
        QCOMPARE(d.hints.contains(u"sound-name"_s), !soundName.isEmpty());
        QCOMPARE(d.hints.value(u"sound-file"_s).toString(), soundFile);
        QCOMPARE(d.hints.contains(u"sound-file"_s), !soundFile.isEmpty());
        QCOMPARE(d.hints.value(u"suppress-sound"_s).toBool(), suppress);
        QCOMPARE(d.hints.contains(u"suppress-sound"_s), suppress);
        // The rest of the message does not depend on the sound.
        QCOMPARE(d.hints.value(u"urgency"_s).value<uchar>(), uchar(isAlarm ? 2 : 1));
        QCOMPARE(d.hints.value(u"resident"_s).toBool(), isAlarm);
        QCOMPARE(d.expireTimeout, isAlarm ? 0 : -1);
        QCOMPARE(d.actions.size(), 6);

        // The sample of "Play sample" sounds the same, but passes and offers nothing.
        const DesktopNotification sample =
                buildSoundPreview(isAlarm ? SoundKind::Alarm : SoundKind::Reminder, sound, 7);
        QCOMPARE(sample.hints.value(u"sound-name"_s).toString(), soundName);
        QCOMPARE(sample.hints.value(u"sound-file"_s).toString(), soundFile);
        QCOMPARE(sample.hints.value(u"suppress-sound"_s).toBool(), suppress);
        QCOMPARE(sample.hints.value(u"urgency"_s).value<uchar>(), uchar(1));
        QCOMPARE(sample.hints.value(u"transient"_s).toBool(), true);
        QVERIFY(!sample.hints.contains(u"resident"_s));
        QVERIFY(sample.actions.isEmpty());
        QCOMPARE(sample.replacesId, 7u);
        QVERIFY(sample.expireTimeout > 0);
        QCOMPARE(sample.summary, isAlarm ? u"⏰ Alarm sound"_s : u"Reminder sound"_s);
    }

    void desktopNotifierPlaysTheChosenSounds()
    {
        auto *bus = new FakeBus;
        DesktopNotifier notifier(bus);
        QCOMPARE(notifier.soundModes(), (QList<SoundChoice::Mode>{SoundChoice::Mode::Default, SoundChoice::Mode::Custom,
                                                                  SoundChoice::Mode::Silent}));
        QVERIFY(notifier.supportsSoundFiles());
        QVERIFY(!notifier.hasSystemSoundPicker());
        QVERIFY(!notifier.hasSystemSoundSettings());
        QCOMPARE(notifier.themeSounds(), desktopThemeSounds());
        QVERIFY(!notifier.pickSound(SoundKind::Alarm));
        QVERIFY(!notifier.openSystemSoundSettings(SoundKind::Alarm));

        QDateTime now(QDate(2026, 10, 14), QTime(6, 30));
        notifier.setClock([&now] { return now; });
        notifier.setRingInterval(20);
        notifier.setSounds({SoundChoice::Mode::Silent, u"bell"_s, {}},
                           {SoundChoice::Mode::Custom, u"file:///home/ja/budzik%20%C5%BC.oga"_s, u"budzik ż.oga"_s});
        QCOMPARE(notifier.sound(SoundKind::Reminder).mode, SoundChoice::Mode::Silent);

        notifier.showNow(reminder());
        QCOMPARE(bus->sent.last().hints.value(u"suppress-sound"_s).toBool(), true);
        QVERIFY(!bus->sent.last().hints.contains(u"sound-name"_s));

        notifier.showNow(alarm());
        QCOMPARE(bus->sent.last().hints.value(u"sound-file"_s).toString(), u"/home/ja/budzik ż.oga"_s);
        QVERIFY(!bus->sent.last().hints.contains(u"sound-name"_s));
        // Ringing again plays the chosen sound again, not the default alarm clock.
        const qsizetype before = bus->sent.size();
        QTRY_VERIFY(bus->sent.size() >= before + 2);
        QCOMPARE(bus->sent.last().hints.value(u"sound-file"_s).toString(), u"/home/ja/budzik ż.oga"_s);
        QVERIFY(!bus->sent.last().hints.contains(u"sound-name"_s));
        now = now.addSecs(120);
        QTRY_COMPARE(notifier.ringingCount(), 0);
    }

    void desktopSoundPreview()
    {
        auto *bus = new FakeBus;
        DesktopNotifier notifier(bus);
        QSignalSpy errors(&notifier, &Notifier::errorOccurred);
        QSignalSpy activated(&notifier, &Notifier::activated);
        QSignalSpy snoozed(&notifier, &Notifier::snoozeRequested);

        QVERIFY(notifier.previewSound(SoundKind::Reminder, {SoundChoice::Mode::Custom, u"bell"_s, {}}));
        QCOMPARE(bus->sent.size(), 1);
        QCOMPARE(bus->sent.last().hints.value(u"sound-name"_s).toString(), u"bell"_s);
        QCOMPARE(bus->sent.last().replacesId, 0u);
        const uint sample = bus->nextId;
        // The next sample replaces the first one; samples are no reminders.
        QVERIFY(notifier.previewSound(SoundKind::Alarm, {}));
        QCOMPARE(bus->sent.last().replacesId, sample);
        QCOMPARE(bus->sent.last().hints.value(u"sound-name"_s).toString(), u"alarm-clock-elapsed"_s);
        QCOMPARE(notifier.shownCount(), 0);
        QCOMPARE(notifier.ringingCount(), 0);
        bus->invoke(sample, u"default"_s);
        QCOMPARE(activated.size(), 0);
        QCOMPARE(snoozed.size(), 0);
        QCOMPARE(errors.size(), 0);

        // The service refuses: say so.
        bus->fail = true;
        QVERIFY(notifier.previewSound(SoundKind::Alarm, {}));
        QCOMPARE(errors.size(), 1);

        // No notification service at all: nothing to play the sound.
        DesktopNotifier trayOnly(nullptr);
        QSignalSpy trayErrors(&trayOnly, &Notifier::errorOccurred);
        QVERIFY(!trayOnly.previewSound(SoundKind::Reminder, {}));
        QCOMPARE(trayErrors.size(), 1);
        QVERIFY(!trayErrors.first().at(0).toString().isEmpty());
    }

    void androidSoundPayload()
    {
        using Mode = SoundChoice::Mode;
        const SoundChoice ringtone{Mode::Custom, u" content://media/internal/audio/media/27 "_s, u"Żółta łódź"_s};
        const SoundChoice vibrate{Mode::VibrateOnly, u"content://media/internal/audio/media/5"_s, u"Stary"_s};
        const QJsonObject settings = androidSoundSettings(ringtone, vibrate);
        QCOMPARE(settings.keys(), (QStringList{u"alarm"_s, u"reminder"_s}));
        const QJsonObject r = settings.value(u"reminder"_s).toObject();
        QCOMPARE(r.value(u"mode"_s).toString(), u"custom"_s);
        QCOMPARE(r.value(u"uri"_s).toString(), u"content://media/internal/audio/media/27"_s);
        QCOMPARE(r.value(u"title"_s).toString(), u"Żółta łódź"_s);
        const QJsonObject a = settings.value(u"alarm"_s).toObject();
        QCOMPARE(a.value(u"mode"_s).toString(), u"vibrate"_s);
        // A custom sound without a ringtone is sent as the default.
        QCOMPARE(androidSoundItem({Mode::Custom, {}, {}}).value(u"mode"_s).toString(), u"default"_s);
        QCOMPARE(androidSoundItem({Mode::Silent, {}, {}}).value(u"mode"_s).toString(), u"silent"_s);

        const QJsonObject preview = androidSoundPreview(SoundKind::Alarm, ringtone);
        QCOMPARE(preview.value(u"kind"_s).toString(), u"alarm"_s);
        QCOMPARE(preview.value(u"mode"_s).toString(), u"custom"_s);
        QCOMPARE(androidSoundPreview(SoundKind::Reminder, {}).value(u"kind"_s).toString(), u"reminder"_s);

        // The picker's answers (ReminderSounds.kt: SoundChoice.toJson / an error).
        bool ok = false;
        const SoundChoice picked =
                androidPickedSound(QJsonObject{{u"mode"_s, u"custom"_s},
                                               {u"uri"_s, u"content://media/external/audio/media/9"_s},
                                               {u"title"_s, u"Kukułka ąę"_s}},
                                   &ok);
        QVERIFY(ok);
        QCOMPARE(picked, (SoundChoice{Mode::Custom, u"content://media/external/audio/media/9"_s, u"Kukułka ąę"_s}));
        QCOMPARE(androidPickedSound(QJsonObject{{u"mode"_s, u"default"_s}, {u"uri"_s, u""_s}}, &ok), SoundChoice());
        QVERIFY(ok);
        androidPickedSound(QJsonObject{{u"error"_s, u"no ringtone picker"_s}}, &ok);
        QVERIFY(!ok);
        androidPickedSound(QJsonObject{}, &ok);
        QVERIFY(!ok);
        androidPickedSound(QJsonObject{{u"mode"_s, u"głośny"_s}}, &ok);
        QVERIFY(!ok);
    }

    void platformFactory()
    {
        QObject parent;
        Notifier *notifier = createPlatformNotifier(&parent);
        QVERIFY(notifier);
        QCOMPARE(notifier->parent(), &parent);
        QVERIFY(qobject_cast<DesktopNotifier *>(notifier)); // a desktop build
        QVERIFY(!notifier->firesWhenClosed());
        QVERIFY(QStringList({ u"dbus"_s, u"tray"_s, u"none"_s }).contains(notifier->backend()));
        notifier->setSnoozeMinutes(0);
        QCOMPARE(notifier->snoozeMinutes(), 1);
        notifier->setSnoozeMinutes(1000);
        QCOMPARE(notifier->snoozeMinutes(), 120);
        notifier->schedule({ reminder(), alarm() });
        QCOMPARE(notifier->scheduled().size(), 2);
        notifier->schedule({});
        QVERIFY(notifier->scheduled().isEmpty());
        // A desktop build: no vibration, a theme sound or a file.
        QVERIFY(!notifier->soundModes().contains(SoundChoice::Mode::VibrateOnly));
        QVERIFY(notifier->soundModes().contains(SoundChoice::Mode::Custom));
        notifier->setSounds({SoundChoice::Mode::Custom, {}, {}}, {SoundChoice::Mode::Silent, {}, {}});
        QCOMPARE(notifier->sound(SoundKind::Reminder).mode, SoundChoice::Mode::Default); // normalised
        QCOMPARE(notifier->sound(SoundKind::Alarm).mode, SoundChoice::Mode::Silent);
    }
};

QTEST_GUILESS_MAIN(TestNotify)
#include "tst_notify.moc"

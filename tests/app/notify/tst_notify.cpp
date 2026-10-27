// Notifier adapters: the freedesktop Notify message of a reminder or alarm, DesktopNotifier
// against a fake notification bus (snooze / done / open / OK actions, foreign notifications, replacing,
// ringing alarms, the tray fallback, Wayland activation tokens), the sounds (freedesktop hints of
// every choice, the sample notification), the Windows toast XML (scenarios, buttons, sounds,
// escaping) and its activation arguments, a bus that rings itself (Windows' alarm toasts), the
// JSON for Android's alarm scheduler, its "Zrobione" requests and sound channels, and the platform
// factory.
#include "androidpayload.h"
#include "desktopnotifier.h"
#include "notifier.h"
#include "soundchoice.h"
#include "toastxml.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QXmlStreamReader>

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

/// A task's reminder: its notification offers "Done".
ScheduledNotification taskReminder()
{
    ScheduledNotification n = reminder();
    n.id = u"aaaabbbbccccdddd"_s;
    n.nativeId = 5151;
    n.title = u"Zapłacić za źródło wody"_s;
    n.taskLine = 2;
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
    // A Windows-like bus: its own name and sounds, no sound files, alarms with a looping sound
    // ringing by themselves (as WindowsToastBus decides).
    QString busName = u"dbus"_s;
    QStringList sounds = desktopThemeSounds();
    bool soundFiles = true;
    bool selfRinging = false;

    bool isAvailable() const override { return available; }
    QStringList capabilities() const override { return caps; }
    QString name() const override { return busName; }
    QStringList themeSounds() const override { return sounds; }
    bool supportsSoundFiles() const override { return soundFiles; }
    bool ringsItself(const DesktopNotification &n) const override { return selfRinging && toastLoopsItsSound(n); }
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

/// A toast's XML read back.
struct Toast {
    bool wellFormed = false;
    QString root;
    QHash<QString, QString> attributes; ///< of <toast>
    QStringList texts;
    QStringList labels;    ///< <action content>
    QStringList arguments; ///< <action arguments>
    QStringList activationTypes;
    bool hasAudio = false;
    QHash<QString, QString> audio;
};

Toast readToast(const QString &xml)
{
    Toast toast;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement)
            continue;
        const QString name = r.name().toString();
        QHash<QString, QString> attributes;
        for (const QXmlStreamAttribute &a : r.attributes())
            attributes.insert(a.name().toString(), a.value().toString());
        if (toast.root.isEmpty()) {
            toast.root = name;
            toast.attributes = attributes;
        } else if (name == u"text") {
            toast.texts.append(r.readElementText());
        } else if (name == u"action") {
            toast.labels.append(attributes.value(u"content"_s));
            toast.arguments.append(attributes.value(u"arguments"_s));
            toast.activationTypes.append(attributes.value(u"activationType"_s));
        } else if (name == u"audio") {
            toast.hasAudio = true;
            toast.audio = attributes;
        }
    }
    toast.wellFormed = !r.hasError();
    return toast;
}

const QStringList ToastCaps{u"actions"_s, u"body"_s, u"sound"_s, u"persistence"_s};

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

    void messageForTaskReminder()
    {
        // Done between Snooze and OK (GNOME shows three buttons).
        const DesktopNotification d = buildDesktopNotification(taskReminder(), {u"actions"_s}, 10);
        QCOMPARE(d.actions, (QStringList{u"default"_s, u"Open note"_s, u"snooze"_s, u"Snooze 10 min"_s, u"done"_s,
                                         u"Done"_s, u"ok"_s, u"OK"_s}));
        QVERIFY(buildDesktopNotification(taskReminder(), {}, 10).actions.isEmpty());
        // Not a task: no "Done".
        QVERIFY(!buildDesktopNotification(reminder(), {u"actions"_s}, 10).actions.contains(u"done"_s));

        // The Windows toast: three buttons, "Done" activates action=done.
        const Toast toast = readToast(buildToastXml(buildDesktopNotification(taskReminder(), ToastCaps, 10), 12));
        QVERIFY(toast.wellFormed);
        QCOMPARE(toast.labels, (QStringList{u"Snooze 10 min"_s, u"Done"_s, u"OK"_s}));
        QCOMPARE(toast.arguments.at(1), u"brulion-toast:id=12&action=done"_s);
        QCOMPARE(parseToastArguments(toast.arguments.at(1)).action, u"done"_s);
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

    void doneAction()
    {
        auto *bus = new FakeBus;
        DesktopNotifier notifier(bus);
        QSignalSpy done(&notifier, &Notifier::doneRequested);
        QSignalSpy snoozed(&notifier, &Notifier::snoozeRequested);
        QSignalSpy activated(&notifier, &Notifier::activated);

        notifier.showNow(taskReminder());
        const uint id = bus->nextId;
        bus->invoke(4711, u"done"_s); // another program's notification
        QVERIFY(done.isEmpty());

        bus->invoke(id, u"done"_s);
        QCOMPARE(done.size(), 1);
        const ScheduledNotification n = qvariant_cast<ScheduledNotification>(done.first().at(0));
        QCOMPARE(n.id, taskReminder().id);
        QCOMPARE(n.taskLine, 2);
        QVERIFY(!done.first().at(1).toDate().isValid()); // today's date, chosen by the shell
        QCOMPARE(bus->closed, (QList<uint>{id}));
        QCOMPARE(notifier.shownCount(), 0);
        QVERIFY(snoozed.isEmpty());
        QVERIFY(activated.isEmpty());
        bus->invoke(id, u"done"_s); // handled once
        QCOMPARE(done.size(), 1);

        // "Done" on a ringing alarm of a task stops it.
        ScheduledNotification taskAlarm = alarm();
        taskAlarm.taskLine = 0;
        notifier.showNow(taskAlarm);
        QCOMPARE(notifier.ringingCount(), 1);
        QVERIFY(bus->sent.last().actions.contains(u"done"_s));
        bus->invoke(bus->nextId, u"done"_s);
        QCOMPARE(notifier.ringingCount(), 0);
        QCOMPARE(done.size(), 2);
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

    void toastXml_data()
    {
        QTest::addColumn<ScheduledNotification>("notification");
        QTest::addColumn<QStringList>("caps");
        QTest::addColumn<SoundChoice>("sound");
        QTest::addColumn<QString>("scenario");
        QTest::addColumn<QString>("duration");
        QTest::addColumn<QStringList>("texts");
        QTest::addColumn<QStringList>("labels");
        QTest::addColumn<QString>("audioSrc"); ///< "" none, "silent"
        QTest::addColumn<bool>("loop");

        const QStringList buttons{u"Snooze 10 min"_s, u"OK"_s};
        const QStringList reminderTexts{u"Kupić mleko i żółty ser"_s, u"Dom & ogród <b> · termin: dziś 18:00"_s};
        const QStringList alarmTexts{u"⏰ Pobudka na lotnisko"_s, u"Podróże"_s};
        const auto custom = [](const QString &uri) { return SoundChoice{SoundChoice::Mode::Custom, uri, {}}; };
        const QString event = u"ms-winsoundevent:Notification."_s;

        QTest::newRow("reminder") << reminder() << ToastCaps << SoundChoice{} << QString() << u"long"_s << reminderTexts
                                  << buttons << QString() << false;
        QTest::newRow("alarm") << alarm() << ToastCaps << SoundChoice{} << u"alarm"_s << QString() << alarmTexts
                               << buttons << event + u"Looping.Alarm"_s << true;
        QTest::newRow("alarm, silent") << alarm() << ToastCaps << SoundChoice{SoundChoice::Mode::Silent, {}, {}}
                                       << u"alarm"_s << QString() << alarmTexts << buttons << u"silent"_s << false;
        QTest::newRow("alarm, Looping.Alarm3")
                << alarm() << ToastCaps << custom(u"Looping.Alarm3"_s) << u"alarm"_s << QString() << alarmTexts
                << buttons << event + u"Looping.Alarm3"_s << true;
        QTest::newRow("alarm, a short sound") << alarm() << ToastCaps << custom(u"Reminder"_s) << u"alarm"_s
                                              << QString() << alarmTexts << buttons << event + u"Reminder"_s << false;
        QTest::newRow("alarm, a sound file")
                << alarm() << ToastCaps << custom(u"file:///C:/D%C5%BAwi%C4%99ki/budzik.wav"_s) << u"alarm"_s
                << QString() << alarmTexts << buttons << QString() << false;
        QTest::newRow("reminder, Looping.Call2: once")
                << reminder() << ToastCaps << custom(u"Looping.Call2"_s) << QString() << u"long"_s << reminderTexts
                << buttons << event + u"Looping.Call2"_s << false;
        QTest::newRow("reminder, Linux theme name") << reminder() << ToastCaps << custom(u"bell"_s) << QString()
                                                    << u"long"_s << reminderTexts << buttons << QString() << false;
        QTest::newRow("reminder, Linux name with a Windows sound")
                << reminder() << ToastCaps << custom(u"message-new-instant"_s) << QString() << u"long"_s
                << reminderTexts << buttons << event + u"IM"_s << false;
        QTest::newRow("reminder, unknown name") << reminder() << ToastCaps << custom(u"Looping.Alarm11"_s) << QString()
                                                << u"long"_s << reminderTexts << buttons << QString() << false;
        QTest::newRow("reminder, silent") << reminder() << ToastCaps << SoundChoice{SoundChoice::Mode::Silent, {}, {}}
                                          << QString() << u"long"_s << reminderTexts << buttons << u"silent"_s << false;

        ScheduledNotification markup = reminder();
        markup.title = u"Zadzwonić do pani Łucji — żółć, ęą <b>&\"'"_s;
        markup.body = u"<i>&amp; \"cytat\" 'apostrof'</i> ]]> <!-- x -->"_s;
        QTest::newRow("markup is text") << markup << ToastCaps << SoundChoice{} << QString() << u"long"_s
                                        << QStringList{markup.title, markup.body} << buttons << QString() << false;

        ScheduledNotification control = reminder();
        control.title = u"Linia"_s + QChar(0x01) + u" dwa"_s + QChar(0x07);
        control.body = u"a"_s + QChar(0) + u"b"_s + QChar(0xD800) + u"c 😀 \t"_s + QChar(0xFFFE);
        QTest::newRow("characters XML cannot carry")
                << control << ToastCaps << SoundChoice{} << QString() << u"long"_s
                << QStringList{u"Linia dwa"_s, u"abc 😀 \t"_s} << buttons << QString() << false;

        ScheduledNotification untitled = alarm();
        untitled.title.clear();
        untitled.body.clear();
        QTest::newRow("alarm without actions or text")
                << untitled << QStringList{} << SoundChoice{} << QString() << u"long"_s << QStringList{u"⏰ Alarm"_s}
                << QStringList{} << event + u"Looping.Alarm"_s << false;
    }

    void toastXml()
    {
        QFETCH(ScheduledNotification, notification);
        QFETCH(QStringList, caps);
        QFETCH(SoundChoice, sound);
        QFETCH(QString, scenario);
        QFETCH(QString, duration);
        QFETCH(QStringList, texts);
        QFETCH(QStringList, labels);
        QFETCH(QString, audioSrc);
        QFETCH(bool, loop);

        const DesktopNotification message = buildDesktopNotification(notification, caps, 10, 0, sound);
        const QString xml = buildToastXml(message, 42);
        const Toast toast = readToast(xml);
        QVERIFY2(toast.wellFormed, qPrintable(xml));
        QCOMPARE(toast.root, u"toast"_s);
        QCOMPARE(toast.attributes.value(u"launch"_s), u"brulion-toast:id=42&action=default"_s);
        QCOMPARE(toast.attributes.value(u"scenario"_s), scenario);
        QCOMPARE(toast.attributes.value(u"duration"_s), duration);
        QCOMPARE(toast.texts, texts);
        QCOMPARE(toast.labels, labels);
        if (!labels.isEmpty()) {
            QCOMPARE(toast.arguments,
                     (QStringList{u"brulion-toast:id=42&action=snooze"_s, u"brulion-toast:id=42&action=ok"_s}));
            QCOMPARE(toast.activationTypes, (QStringList{u"foreground"_s, u"foreground"_s}));
        }
        if (audioSrc.isEmpty()) {
            QVERIFY2(!toast.hasAudio, qPrintable(xml));
        } else if (audioSrc == u"silent") {
            QCOMPARE(toast.audio.value(u"silent"_s), u"true"_s);
            QVERIFY(!toast.audio.contains(u"src"_s));
        } else {
            QCOMPARE(toast.audio.value(u"src"_s), audioSrc);
            QCOMPARE(toast.audio.value(u"loop"_s), loop ? u"true"_s : QString());
        }
        // The bus rings exactly the alarms whose toast loops (DesktopNotifier re-sends the others).
        QCOMPARE(toastLoopsItsSound(message), loop);
        // Markup in a note never becomes elements of the toast.
        QVERIFY(!xml.contains(u"<b>"_s));
        QVERIFY(!xml.contains(u"<i>"_s));
    }

    void toastXmlActionsAndSamples()
    {
        // Every known action becomes a button; unknown ones do not.
        DesktopNotification d = buildDesktopNotification(reminder(), ToastCaps, 5);
        d.actions << u"done"_s << u"Zrobione"_s << u"rm -rf"_s << u"Zło"_s;
        Toast toast = readToast(buildToastXml(d, 4294967295u));
        QVERIFY(toast.wellFormed);
        QCOMPARE(toast.labels, (QStringList{u"Snooze 5 min"_s, u"OK"_s, u"Zrobione"_s}));
        QCOMPARE(toast.arguments.last(), u"brulion-toast:id=4294967295&action=done"_s);
        const ToastActivation done = parseToastArguments(toast.arguments.last());
        QVERIFY(done.valid);
        QCOMPARE(done.id, 4294967295u);
        QCOMPARE(done.action, u"done"_s);

        // A reminder that waits for the user (expire timeout 0) stays on screen too.
        d = buildDesktopNotification(reminder(), ToastCaps, 5);
        d.expireTimeout = 0;
        QCOMPARE(readToast(buildToastXml(d, 1)).attributes.value(u"scenario"_s), u"reminder"_s);

        // "Play sample": a passing toast without buttons; the alarm's sample sounds like an alarm.
        for (const SoundKind kind : {SoundKind::Reminder, SoundKind::Alarm}) {
            toast = readToast(buildToastXml(buildSoundPreview(kind, SoundChoice{}), 9));
            QVERIFY(toast.wellFormed);
            QVERIFY(!toast.attributes.contains(u"scenario"_s));
            QVERIFY(!toast.attributes.contains(u"duration"_s));
            QVERIFY(toast.labels.isEmpty());
            QCOMPARE(toast.hasAudio, kind == SoundKind::Alarm);
            if (kind == SoundKind::Alarm) {
                QCOMPARE(toast.audio.value(u"src"_s), u"ms-winsoundevent:Notification.Looping.Alarm"_s);
                QVERIFY(!toast.audio.contains(u"loop"_s));
            }
        }
        toast = readToast(buildToastXml(
                buildSoundPreview(SoundKind::Reminder, SoundChoice{SoundChoice::Mode::Custom, u"Mail"_s, {}}), 9));
        QCOMPARE(toast.audio.value(u"src"_s), u"ms-winsoundevent:Notification.Mail"_s);
    }

    void toastSounds()
    {
        const QStringList sounds = windowsToastSounds();
        QCOMPARE(sounds.size(), 25);
        QCOMPARE(sounds.first(), u"Default"_s);
        QVERIFY(sounds.contains(u"Looping.Alarm"_s));
        QVERIFY(sounds.contains(u"Looping.Alarm10"_s));
        QVERIFY(sounds.contains(u"Looping.Call10"_s));
        QVERIFY(!sounds.contains(u"Looping.Alarm1"_s)); // the first one has no number
        for (const QString &sound : sounds) {
            QCOMPARE(windowsToastSoundFor(sound), sound);
            QVERIFY(!isSoundFile(sound)); // stored as a "sound-name"
        }
        QCOMPARE(windowsToastSoundFor(defaultAlarmThemeSound()), u"Looping.Alarm"_s);
        QCOMPARE(windowsToastSoundFor(u"phone-incoming-call"_s), u"Looping.Call"_s);
        QVERIFY(windowsToastSoundFor(u"bell"_s).isEmpty());
        QVERIFY(windowsToastSoundFor(u"default"_s).isEmpty()); // case matters
        QVERIFY(windowsToastSoundFor(QString()).isEmpty());
    }

    void toastArguments_data()
    {
        QTest::addColumn<QString>("arguments");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<uint>("id");
        QTest::addColumn<QString>("action");
        QTest::newRow("click") << u"brulion-toast:id=7&action=default"_s << true << 7u << u"default"_s;
        QTest::newRow("snooze") << u"brulion-toast:id=101&action=snooze"_s << true << 101u << u"snooze"_s;
        QTest::newRow("ok") << u"brulion-toast:id=1&action=ok"_s << true << 1u << u"ok"_s;
        QTest::newRow("done") << u"brulion-toast:id=3&action=done"_s << true << 3u << u"done"_s;
        QTest::newRow("largest id") << u"brulion-toast:id=4294967295&action=ok"_s << true << 4294967295u << u"ok"_s;
        QTest::newRow("leading zeros") << u"brulion-toast:id=0007&action=ok"_s << true << 7u << u"ok"_s;
        QTest::newRow("empty") << QString() << false << 0u << QString();
        QTest::newRow("prefix only") << u"brulion-toast:"_s << false << 0u << QString();
        QTest::newRow("another prefix") << u"toast:id=7&action=ok"_s << false << 0u << QString();
        QTest::newRow("upper-case prefix") << u"BRULION-TOAST:id=7&action=ok"_s << false << 0u << QString();
        QTest::newRow("id 0") << u"brulion-toast:id=0&action=ok"_s << false << 0u << QString();
        QTest::newRow("negative id") << u"brulion-toast:id=-1&action=ok"_s << false << 0u << QString();
        QTest::newRow("plus sign") << u"brulion-toast:id=+5&action=ok"_s << false << 0u << QString();
        QTest::newRow("space") << u"brulion-toast:id= 5&action=ok"_s << false << 0u << QString();
        QTest::newRow("too large") << u"brulion-toast:id=4294967296&action=ok"_s << false << 0u << QString();
        QTest::newRow("far too large") << u"brulion-toast:id=99999999999&action=ok"_s << false << 0u << QString();
        QTest::newRow("hex") << u"brulion-toast:id=0x1f&action=ok"_s << false << 0u << QString();
        QTest::newRow("other digits") << u"brulion-toast:id=٣&action=ok"_s << false << 0u << QString();
        QTest::newRow("no id") << u"brulion-toast:id=&action=ok"_s << false << 0u << QString();
        QTest::newRow("unknown action") << u"brulion-toast:id=7&action=drzemka"_s << false << 0u << QString();
        QTest::newRow("empty action") << u"brulion-toast:id=7&action="_s << false << 0u << QString();
        QTest::newRow("no action") << u"brulion-toast:id=7"_s << false << 0u << QString();
        QTest::newRow("swapped") << u"brulion-toast:action=ok&id=7"_s << false << 0u << QString();
        QTest::newRow("extra &") << u"brulion-toast:id=7&action=ok&path=/etc"_s << false << 0u << QString();
        QTest::newRow("injected quote") << u"brulion-toast:id=7&action=ok\""_s << false << 0u << QString();
        QTest::newRow("trailing space") << u"brulion-toast:id=7&action=ok "_s << false << 0u << QString();
    }

    void toastArguments()
    {
        QFETCH(QString, arguments);
        QFETCH(bool, valid);
        QFETCH(uint, id);
        QFETCH(QString, action);
        const ToastActivation activation = parseToastArguments(arguments);
        QCOMPARE(activation.valid, valid);
        QCOMPARE(activation.id, id);
        QCOMPARE(activation.action, action);
        if (valid) {
            const ToastActivation again = parseToastArguments(brulion::notify::toastArguments(id, action));
            QVERIFY(again.valid);
            QCOMPARE(again.id, id);
            QCOMPARE(again.action, action);
        }
    }

    void toastPermission_data()
    {
        QTest::addColumn<bool>("registered");
        QTest::addColumn<ToastSetting>("setting");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("allowed") << true << ToastSetting::Enabled << true;
        // ERROR_NOT_FOUND before the first toast of an app without a package: try it.
        QTest::newRow("not known yet") << true << ToastSetting::Unknown << true;
        QTest::newRow("off for Brulion") << true << ToastSetting::DisabledForApplication << false;
        QTest::newRow("off for all apps") << true << ToastSetting::DisabledForUser << false;
        QTest::newRow("group policy") << true << ToastSetting::DisabledByGroupPolicy << false;
        QTest::newRow("manifest") << true << ToastSetting::DisabledByManifest << false;
        // Nobody registered the id (tests): its toasts would never appear — the tray shows them.
        QTest::newRow("unregistered") << false << ToastSetting::Enabled << false;
        QTest::newRow("unregistered, not known") << false << ToastSetting::Unknown << false;
    }
    void toastPermission()
    {
        QFETCH(bool, registered);
        QFETCH(ToastSetting, setting);
        QFETCH(bool, allowed);
        QCOMPARE(toastsAllowed(registered, setting), allowed);
    }

    // Windows' alarm toasts with a looping sound loop it themselves: not sent again every ring
    // interval, once silently when the ring time is over (it stays on screen).
    void busThatRingsItself()
    {
        auto *bus = new FakeBus;
        bus->busName = u"toast"_s;
        bus->sounds = windowsToastSounds();
        bus->soundFiles = false;
        bus->selfRinging = true;
        bus->caps = ToastCaps;
        DesktopNotifier notifier(bus);
        QCOMPARE(notifier.backend(), u"toast"_s);
        QCOMPARE(notifier.themeSounds(), windowsToastSounds());
        QVERIFY(!notifier.supportsSoundFiles());
        QVERIFY(notifier.soundModes().contains(SoundChoice::Mode::Custom));

        QDateTime now(QDate(2026, 10, 14), QTime(6, 30));
        notifier.setClock([&now] { return now; });
        notifier.setRingInterval(20);
        notifier.setSounds({}, {SoundChoice::Mode::Custom, u"Looping.Alarm3"_s, {}});
        notifier.showNow(alarm());
        const uint id = bus->nextId;
        QCOMPARE(bus->sent.size(), 1);
        QCOMPARE(bus->sent.first().hints.value(u"sound-name"_s).toString(), u"Looping.Alarm3"_s);
        QCOMPARE(notifier.ringingCount(), 1);
        QTest::qWait(120);
        QCOMPARE(bus->sent.size(), 1); // it rings by itself
        QCOMPARE(notifier.ringingCount(), 1);

        now = now.addSecs(61);
        QTRY_COMPARE(notifier.ringingCount(), 0);
        QCOMPARE(bus->sent.size(), 2);
        QCOMPARE(bus->sent.last().replacesId, id);
        QCOMPARE(bus->sent.last().hints.value(u"suppress-sound"_s).toBool(), true);
        QVERIFY(!bus->sent.last().hints.contains(u"sound-name"_s));
        QCOMPARE(bus->sent.last().actions, bus->sent.first().actions); // the buttons stay
        QTest::qWait(80);
        QCOMPARE(bus->sent.size(), 2);
        QCOMPARE(notifier.shownCount(), 1);

        // An action before the time is over: no silent copy.
        ScheduledNotification other = alarm();
        other.id = u"2222222222222222"_s;
        notifier.showNow(other);
        QCOMPARE(notifier.ringingCount(), 1);
        bus->invoke(bus->nextId, u"snooze"_s);
        QCOMPARE(notifier.ringingCount(), 0);
        now = now.addSecs(600);
        QTest::qWait(80);
        QCOMPARE(bus->sent.size(), 3);
    }

    // An alarm with a short Windows sound ("Poczta") plays it once per toast: it is sent again
    // every ring interval, as on Linux, and stays on screen silently when the time is over.
    void busThatRingsItselfShortSound()
    {
        auto *bus = new FakeBus;
        bus->busName = u"toast"_s;
        bus->sounds = windowsToastSounds();
        bus->soundFiles = false;
        bus->selfRinging = true;
        bus->caps = ToastCaps;
        DesktopNotifier notifier(bus);

        QDateTime now(QDate(2026, 10, 14), QTime(6, 30));
        notifier.setClock([&now] { return now; });
        notifier.setRingInterval(20);
        notifier.setSounds({}, {SoundChoice::Mode::Custom, u"Mail"_s, {}});
        notifier.showNow(alarm());
        const uint id = bus->nextId;
        QCOMPARE(notifier.ringingCount(), 1);
        QVERIFY(!toastLoopsItsSound(bus->sent.first()));
        QTRY_VERIFY(bus->sent.size() >= 3);
        for (const DesktopNotification &sent : std::as_const(bus->sent)) {
            QCOMPARE(sent.hints.value(u"sound-name"_s).toString(), u"Mail"_s);
            QVERIFY(!sent.hints.value(u"suppress-sound"_s).toBool());
        }
        QCOMPARE(bus->sent.last().replacesId, id);

        now = now.addSecs(61);
        QTRY_COMPARE(notifier.ringingCount(), 0);
        const qsizetype sent = bus->sent.size();
        QTest::qWait(80);
        QCOMPARE(bus->sent.size(), sent); // nothing more, not even a silent copy
        QVERIFY(!bus->sent.last().hints.value(u"suppress-sound"_s).toBool());
        QCOMPARE(notifier.shownCount(), 1);

        // Switched to a looping sound while it rings: sent once more (looping), then left alone,
        // and a silent copy when the time is over.
        ScheduledNotification other = alarm();
        other.id = u"3333333333333333"_s;
        notifier.showNow(other);
        const uint otherId = bus->nextId;
        notifier.setSounds({}, {SoundChoice::Mode::Custom, u"Looping.Alarm2"_s, {}});
        QTRY_COMPARE(bus->sent.last().hints.value(u"sound-name"_s).toString(), u"Looping.Alarm2"_s);
        QCOMPARE(bus->sent.last().replacesId, otherId);
        const qsizetype looped = bus->sent.size();
        QTest::qWait(120);
        QCOMPARE(bus->sent.size(), looped);
        QCOMPARE(notifier.ringingCount(), 1);
        now = now.addSecs(61);
        QTRY_COMPARE(notifier.ringingCount(), 0);
        QCOMPARE(bus->sent.size(), looped + 1);
        QVERIFY(bus->sent.last().hints.value(u"suppress-sound"_s).toBool());
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

        // The occurrence ("Zrobione" names it): its id and wall-clock time, and whether it is on
        // a task.
        QCOMPARE(first.value(u"key"_s).toString(), a.id);
        QCOMPARE(first.value(u"local"_s).toString(), u"2026-10-14T18:00"_s);
        QCOMPARE(first.value(u"task"_s).toBool(), false);
        ScheduledNotification task = taskReminder();
        task.at = QDateTime(QDate(2026, 10, 25), QTime(2, 30)); // the night the clocks go back
        const QJsonObject item = androidReminderItem(task);
        QCOMPARE(item.value(u"task"_s).toBool(), true);
        QCOMPARE(item.value(u"local"_s).toString(), u"2026-10-25T02:30"_s);
    }

    void androidDoneRequestsParsed()
    {
        const QJsonArray array{
                QJsonObject{{u"key"_s, u"aaaabbbbccccdddd"_s},
                            {u"local"_s, u"2026-10-25T02:30"_s},
                            {u"path"_s, u"dziennik/Łódź.md"_s},
                            {u"line"_s, 4},
                            {u"title"_s, u"Zapłacić za źródło wody"_s},
                            {u"day"_s, u"2026-10-26"_s}},
                // No day (or a broken one): completed with today's date.
                QJsonObject{{u"key"_s, u"k2"_s},
                            {u"local"_s, u"2026-10-14T09:00"_s},
                            {u"path"_s, u"a.md"_s},
                            {u"line"_s, -3},
                            {u"day"_s, u"wczoraj"_s}},
                // Broken: no key, no note, no or a malformed time, not an object.
                QJsonObject{{u"local"_s, u"2026-10-14T09:00"_s}, {u"path"_s, u"a.md"_s}},
                QJsonObject{{u"key"_s, u"k"_s}, {u"local"_s, u"2026-10-14T09:00"_s}},
                QJsonObject{{u"key"_s, u"k"_s}, {u"path"_s, u"a.md"_s}},
                QJsonObject{{u"key"_s, u"k"_s}, {u"local"_s, u"2026-13-14T09:00"_s}, {u"path"_s, u"a.md"_s}},
                7,
        };
        const QList<AndroidDoneRequest> requests = androidDoneRequests(array);
        QCOMPARE(requests.size(), 2);
        const ScheduledNotification &n = requests.at(0).notification;
        QCOMPARE(n.id, u"aaaabbbbccccdddd"_s);
        QCOMPARE(n.at, QDateTime(QDate(2026, 10, 25), QTime(2, 30))); // local wall-clock time
        QCOMPARE(n.at.timeSpec(), Qt::LocalTime);
        QCOMPARE(n.path, u"dziennik/Łódź.md"_s);
        QCOMPARE(n.line, 4);
        QCOMPARE(n.title, u"Zapłacić za źródło wody"_s);
        QCOMPARE(requests.at(0).day, QDate(2026, 10, 26));
        QCOMPARE(requests.at(1).notification.line, 0);
        QVERIFY(!requests.at(1).day.isValid());
        QVERIFY(androidDoneRequests({}).isEmpty());

        // What the payload sends comes back as the same occurrence.
        const ScheduledNotification task = taskReminder();
        QJsonObject tapped = androidReminderItem(task);
        tapped.insert(u"day"_s, u"2026-10-14"_s);
        const AndroidDoneRequest back = androidDoneRequests({tapped}).value(0);
        QCOMPARE(back.notification.id, task.id);
        QCOMPARE(back.notification.at, task.at);
        QCOMPARE(back.notification.path, task.path);
        QCOMPARE(back.notification.line, task.line);
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
        QVERIFY(QStringList({u"dbus"_s, u"toast"_s, u"tray"_s, u"none"_s}).contains(notifier->backend()));
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

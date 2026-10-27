// ReminderSounds — the sound settings of reminders and alarms: persistence through the storage
// seam (AppSettings in the app; a map here) and a second instance reading it back, the modes a
// platform offers (Android with vibration, the desktop with theme sounds and files, Windows with
// its notification sounds and no files, no notifier),
// "Ringtone" without a ringtone chosen yet, the remembered ringtone, the system picker's answer,
// previews and the system settings, the choices reaching the notifier.
#include "notifier.h"
#include "remindersounds.h"
#include "soundchoice.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::notify;
using Mode = SoundChoice::Mode;

namespace {

/// A notifier recording the sounds it gets; the platform is configurable.
class FakeNotifier : public Notifier {
public:
    QList<Mode> modes{Mode::Default, Mode::Custom, Mode::VibrateOnly, Mode::Silent};
    bool picker = true;
    bool files = true; ///< without a picker: sound files can be played (not Windows' toasts)
    QStringList themes;
    int applied = 0;
    QList<SoundKind> picks;
    QList<QPair<SoundKind, SoundChoice>> previews;
    QList<SoundKind> settingsOpened;

    QString backend() const override { return u"fake"_s; }
    bool canShow() const override { return true; }
    bool firesWhenClosed() const override { return true; }
    void showNow(const brulion::ScheduledNotification &) override { }

    QList<Mode> soundModes() const override { return modes; }
    bool hasSystemSoundPicker() const override { return picker; }
    bool hasSystemSoundSettings() const override { return picker; }
    bool supportsSoundFiles() const override { return !picker && files; }
    QStringList themeSounds() const override { return themes; }
    bool pickSound(SoundKind kind) override
    {
        picks.append(kind);
        return true;
    }
    bool previewSound(SoundKind kind, const SoundChoice &choice) override
    {
        previews.append({kind, choice});
        return choice.mode != Mode::Silent;
    }
    bool openSystemSoundSettings(SoundKind kind) override
    {
        settingsOpened.append(kind);
        return true;
    }

    void answer(SoundKind kind, const SoundChoice &choice) { Q_EMIT soundPicked(kind, choice); }

protected:
    void soundsChanged() override { ++applied; }
};

/// The storage seam over a map (what AppSettings::value / setValue are in the app).
struct MapStore {
    QVariantMap values;
    ReminderSounds::Load load()
    {
        return [this](const QString &key) { return values.value(key, QString()); };
    }
    ReminderSounds::Store store()
    {
        return [this](const QString &key, const QVariant &value) { values.insert(key, value); };
    }
};

const QString kRingtone = u"content://media/internal/audio/media/27"_s;

} // namespace

class TestNotifySounds : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void defaultsWithoutAnything()
    {
        ReminderSounds sounds;
        QCOMPARE(ReminderSounds::instance(), &sounds);
        QCOMPARE(sounds.choice(SoundKind::Reminder), SoundChoice());
        QCOMPARE(sounds.reminder().value(u"mode"_s).toString(), u"default"_s);
        QCOMPARE(sounds.alarm().value(u"mode"_s).toString(), u"default"_s);
        QCOMPARE(sounds.modes(), (QStringList{u"default"_s, u"silent"_s}));
        QVERIFY(!sounds.systemPicker());
        QVERIFY(!sounds.systemSettings());
        QVERIFY(!sounds.soundFiles());
        QVERIFY(sounds.themeSounds().isEmpty());
        QVERIFY(!sounds.pick(u"alarm"_s));
        QVERIFY(!sounds.openSystemSettings(u"alarm"_s));
        QSignalSpy previewed(&sounds, &ReminderSounds::previewRequested);
        QVERIFY(!sounds.preview(u"alarm"_s)); // nothing plays it
        QCOMPARE(previewed.size(), 1);
        // Without a notifier only the default and silence exist; no storage: kept in memory.
        QVERIFY(sounds.setMode(u"alarm"_s, u"silent"_s));
        QCOMPARE(sounds.choice(SoundKind::Alarm).mode, Mode::Silent);
        QVERIFY(!sounds.setMode(u"alarm"_s, u"vibrate"_s));
        QVERIFY(!sounds.setCustom(u"alarm"_s, kRingtone));
    }

    void settingsKeys()
    {
        QCOMPARE(ReminderSounds::settingsKey(SoundKind::Reminder, u"mode"_s), u"reminders/sound/reminder/mode"_s);
        QCOMPARE(ReminderSounds::settingsKey(SoundKind::Alarm, u"uri"_s), u"reminders/sound/alarm/uri"_s);
    }

    void choicesAreStoredAndReadBack()
    {
        MapStore map;
        FakeNotifier notifier;
        {
            ReminderSounds sounds;
            sounds.setStorage(map.load(), map.store());
            sounds.setNotifier(&notifier);
            QSignalSpy changed(&sounds, &ReminderSounds::changed);
            QVERIFY(sounds.setCustom(u"alarm"_s, kRingtone, u"  Żółta   łódź "_s));
            QCOMPARE(changed.size(), 1);
            QVERIFY(sounds.setMode(u"reminder"_s, u"vibrate"_s));
            QCOMPARE(changed.size(), 2);
            // The same choice again changes nothing.
            QVERIFY(sounds.setMode(u"reminder"_s, u"vibrate"_s));
            QCOMPARE(changed.size(), 2);
        }
        QCOMPARE(map.values.value(u"reminders/sound/alarm/mode"_s).toString(), u"custom"_s);
        QCOMPARE(map.values.value(u"reminders/sound/alarm/uri"_s).toString(), kRingtone);
        QCOMPARE(map.values.value(u"reminders/sound/alarm/title"_s).toString(), u"Żółta łódź"_s);
        QCOMPARE(map.values.value(u"reminders/sound/reminder/mode"_s).toString(), u"vibrate"_s);

        // The next start reads them back and hands them to the notifier.
        FakeNotifier next;
        ReminderSounds again;
        again.setStorage(map.load(), map.store());
        again.setNotifier(&next);
        QCOMPARE(again.choice(SoundKind::Alarm), (SoundChoice{Mode::Custom, kRingtone, u"Żółta łódź"_s}));
        QCOMPARE(again.choice(SoundKind::Reminder).mode, Mode::VibrateOnly);
        QCOMPARE(next.sound(SoundKind::Alarm).uri, kRingtone);
        QCOMPARE(next.sound(SoundKind::Reminder).mode, Mode::VibrateOnly);
        QVERIFY(next.applied >= 1);
    }

    void realSettingsFile()
    {
        // The app keeps them in an INI file (AppSettings over QSettings): text survives as text.
        QTemporaryDir dir;
        const QString file = dir.filePath(u"Brulion.ini"_s);
        FakeNotifier notifier;
        {
            QSettings ini(file, QSettings::IniFormat);
            ReminderSounds sounds;
            sounds.setStorage([&ini](const QString &key) { return ini.value(key, QString()); },
                              [&ini](const QString &key, const QVariant &value) { ini.setValue(key, value); });
            sounds.setNotifier(&notifier);
            QVERIFY(sounds.setCustom(u"reminder"_s, u"content://media/external/audio/media/ćma"_s, u"Ćma, ąę"_s));
            QVERIFY(sounds.setMode(u"alarm"_s, u"silent"_s));
        }
        QSettings ini(file, QSettings::IniFormat);
        ReminderSounds sounds;
        sounds.setStorage([&ini](const QString &key) { return ini.value(key, QString()); },
                          [&ini](const QString &key, const QVariant &value) { ini.setValue(key, value); });
        sounds.setNotifier(&notifier);
        QCOMPARE(sounds.choice(SoundKind::Reminder),
                 (SoundChoice{Mode::Custom, u"content://media/external/audio/media/ćma"_s, u"Ćma, ąę"_s}));
        QCOMPARE(sounds.choice(SoundKind::Alarm).mode, Mode::Silent);
    }

    void brokenStoredValuesAreTheDefault()
    {
        MapStore map;
        map.values = {
                {u"reminders/sound/reminder/mode"_s, u"głośno"_s},
                {u"reminders/sound/alarm/mode"_s, u"custom"_s}, // no uri
        };
        FakeNotifier notifier;
        ReminderSounds sounds;
        sounds.setStorage(map.load(), map.store());
        sounds.setNotifier(&notifier);
        QCOMPARE(sounds.choice(SoundKind::Reminder), SoundChoice());
        QCOMPARE(sounds.choice(SoundKind::Alarm), SoundChoice());
    }

    void modesOfThePlatform()
    {
        MapStore map;
        map.values.insert(u"reminders/sound/alarm/mode"_s, u"vibrate"_s); // copied from a phone
        FakeNotifier desktop;
        desktop.modes = {Mode::Default, Mode::Custom, Mode::Silent};
        desktop.picker = false;
        desktop.themes = desktopThemeSounds();
        ReminderSounds sounds;
        QSignalSpy capabilities(&sounds, &ReminderSounds::capabilitiesChanged);
        sounds.setStorage(map.load(), map.store());
        sounds.setNotifier(&desktop);
        QCOMPARE(capabilities.size(), 1);
        QCOMPARE(sounds.modes(), (QStringList{u"default"_s, u"custom"_s, u"silent"_s}));
        QVERIFY(!sounds.systemPicker());
        QVERIFY(sounds.soundFiles());
        QCOMPARE(sounds.themeSounds(), desktopThemeSounds());
        // Vibration does not exist here: the default plays.
        QCOMPARE(sounds.choice(SoundKind::Alarm).mode, Mode::Default);
        QCOMPARE(desktop.sound(SoundKind::Alarm).mode, Mode::Default);
        QVERIFY(!sounds.setMode(u"alarm"_s, u"vibrate"_s));

        // Theme sounds and files are the desktop's custom sounds; anything else is refused.
        QVERIFY(sounds.setCustom(u"alarm"_s, u"phone-incoming-call"_s));
        QCOMPARE(sounds.choice(SoundKind::Alarm), (SoundChoice{Mode::Custom, u"phone-incoming-call"_s, {}}));
        QVERIFY(!sounds.setCustom(u"alarm"_s, u"rm -rf"_s));
        QVERIFY(sounds.setCustom(u"reminder"_s, u"file:///home/ja/D%C5%BAwi%C4%99ki/%C5%BCuraw%20nocny.oga"_s));
        QCOMPARE(sounds.choice(SoundKind::Reminder).title, u"żuraw nocny.oga"_s);
        QCOMPARE(desktop.sound(SoundKind::Reminder).uri, u"file:///home/ja/D%C5%BAwi%C4%99ki/%C5%BCuraw%20nocny.oga"_s);
        QVERIFY(!sounds.setCustom(u"reminder"_s, u"   "_s));
        QVERIFY(!sounds.setCustom(u"budzik"_s, u"bell"_s));
        QVERIFY(!sounds.pick(u"alarm"_s)); // no system picker: the page shows its menu
        QVERIFY(desktop.picks.isEmpty());
    }

    // Windows: its notification sounds (toastxml.h), no sound files (toasts cannot play them).
    void windowsSounds()
    {
        MapStore map;
        FakeNotifier windows;
        windows.modes = {Mode::Default, Mode::Custom, Mode::Silent};
        windows.picker = false;
        windows.files = false;
        windows.themes = {u"Default"_s, u"Mail"_s, u"Looping.Alarm"_s, u"Looping.Alarm3"_s};
        ReminderSounds sounds;
        sounds.setStorage(map.load(), map.store());
        sounds.setNotifier(&windows);
        QVERIFY(!sounds.soundFiles());
        QCOMPARE(sounds.themeSounds(), windows.themes);
        QVERIFY(sounds.setCustom(u"alarm"_s, u"Looping.Alarm3"_s));
        QCOMPARE(windows.sound(SoundKind::Alarm), (SoundChoice{Mode::Custom, u"Looping.Alarm3"_s, {}}));
        QVERIFY(!sounds.setCustom(u"reminder"_s, u"file:///C:/D%C5%BAwi%C4%99ki/dzwonek.wav"_s));
        QVERIFY(!sounds.setCustom(u"reminder"_s, u"C:/Dźwięki/dzwonek.wav"_s));
        QVERIFY(!sounds.setCustom(u"reminder"_s, u"bell"_s)); // a Linux name is not offered here
        QCOMPARE(sounds.choice(SoundKind::Reminder).mode, Mode::Default);
    }

    void ringtoneNeedsAChoiceFirst()
    {
        MapStore map;
        FakeNotifier android;
        ReminderSounds sounds;
        sounds.setStorage(map.load(), map.store());
        sounds.setNotifier(&android);
        // "Ringtone" with none chosen yet: nothing changes (the page opens the picker).
        QVERIFY(!sounds.setMode(u"reminder"_s, u"custom"_s));
        QCOMPARE(sounds.choice(SoundKind::Reminder).mode, Mode::Default);
        QVERIFY(sounds.pick(u"reminder"_s));
        QCOMPARE(android.picks, (QList<SoundKind>{SoundKind::Reminder}));

        // The picker answers: stored and played.
        android.answer(SoundKind::Reminder, {Mode::Custom, kRingtone, u"Kukułka"_s});
        QCOMPARE(sounds.choice(SoundKind::Reminder), (SoundChoice{Mode::Custom, kRingtone, u"Kukułka"_s}));
        QCOMPARE(map.values.value(u"reminders/sound/reminder/uri"_s).toString(), kRingtone);
        QCOMPARE(android.sound(SoundKind::Reminder).uri, kRingtone);

        // Another mode keeps the ringtone; "Ringtone" brings it back without the picker.
        QVERIFY(sounds.setMode(u"reminder"_s, u"silent"_s));
        QCOMPARE(sounds.reminder().value(u"uri"_s).toString(), kRingtone);
        QCOMPARE(android.sound(SoundKind::Reminder).mode, Mode::Silent);
        QVERIFY(sounds.setMode(u"reminder"_s, u"custom"_s));
        QCOMPARE(sounds.choice(SoundKind::Reminder).mode, Mode::Custom);
        QCOMPARE(android.picks.size(), 1);

        // "Default" picked in the system picker: the default sound.
        android.answer(SoundKind::Alarm, SoundChoice());
        QCOMPARE(sounds.choice(SoundKind::Alarm), SoundChoice());
        QVERIFY(!sounds.setMode(u"alarm"_s, u"nonsense"_s));
        QVERIFY(!sounds.setMode(u"budzik"_s, u"silent"_s));
    }

    void previewsAndSystemSettings()
    {
        FakeNotifier android;
        ReminderSounds sounds;
        sounds.setNotifier(&android);
        QSignalSpy previewed(&sounds, &ReminderSounds::previewRequested);
        QVERIFY(sounds.setMode(u"alarm"_s, u"vibrate"_s));
        QVERIFY(sounds.preview(u"alarm"_s));
        QCOMPARE(previewed.size(), 1);
        QCOMPARE(previewed.first().at(0).toString(), u"alarm"_s);
        QCOMPARE(android.previews.size(), 1);
        QCOMPARE(android.previews.first().first, SoundKind::Alarm);
        QCOMPARE(android.previews.first().second.mode, Mode::VibrateOnly);
        QVERIFY(sounds.setMode(u"reminder"_s, u"silent"_s));
        QVERIFY(!sounds.preview(u"reminder"_s)); // nothing to hear
        QVERIFY(!sounds.preview(u"budzik"_s));
        QCOMPARE(previewed.size(), 2);

        QVERIFY(sounds.systemSettings());
        QVERIFY(sounds.openSystemSettings(u"reminder"_s));
        QVERIFY(!sounds.openSystemSettings(u"x"_s));
        QCOMPARE(android.settingsOpened, (QList<SoundKind>{SoundKind::Reminder}));
    }

    void notifierChangesAreFollowed()
    {
        FakeNotifier first;
        FakeNotifier second;
        second.modes = {Mode::Default, Mode::Silent};
        ReminderSounds sounds;
        sounds.setNotifier(&first);
        QVERIFY(sounds.setCustom(u"alarm"_s, kRingtone));
        // A notifier of a platform without ringtones: the default plays; the first one's picker
        // answers do not count any more.
        sounds.setNotifier(&second);
        QCOMPARE(second.sound(SoundKind::Alarm).mode, Mode::Default);
        first.answer(SoundKind::Reminder, {Mode::Silent, {}, {}});
        QCOMPARE(sounds.choice(SoundKind::Reminder).mode, Mode::Default);
        sounds.setNotifier(nullptr);
        QCOMPARE(sounds.modes(), (QStringList{u"default"_s, u"silent"_s}));
    }
};

QTEST_GUILESS_MAIN(TestNotifySounds)
#include "tst_notify_sounds.moc"

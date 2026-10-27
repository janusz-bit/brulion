// AndroidNotifier — reminders as native Android alarms (Android builds only): schedule() hands
// the whole set to AndroidBridge::scheduleReminders() (AlarmManager exact alarms, notification
// channels, the alarm screen over the lock screen, snooze inside the notification, re-armed after
// a reboot — android/src/io/github/januszbit/brulion/). They fire while Brulion is closed, so
// firesWhenClosed() is true and the ReminderScheduler never times them in-app.
//
// showNow() (the settings page's test notification) arms the notification two seconds ahead
// together with the current set — scheduleReminders() replaces the armed set, so it must never
// go alone. A tapped notification arrives as AndroidBridge::activated() and is forwarded as
// activated() (main.cpp also hands it to AppController::openNotification()).
//
// "Zrobione" in a task reminder's notification or on the alarm screen: the Kotlin side queues
// the tap (DoneQueue.kt; the app may be closed, the tap is persisted at once) and tells
// AndroidBridge (doneRequestsQueued(), also on every return to the front). The queue is taken
// only while a notebook is ready (setDoneTarget(): indexed, reminders running) and only that
// notebook's taps (and those of reminders armed before notebooks were told apart); the others
// stay queued for their notebook. They are forwarded as doneRequested() with the day of the tap
// and written by WorkspaceReminders right away — a tap is never held only in memory.
//
// Sounds: every mode (default, a ringtone from the system picker, vibration only, silent);
// setSounds() goes to AndroidBridge::setReminderSounds() — the Kotlin side recreates the
// notification channel of a kind whose sound changed (a channel's sound is fixed once created)
// and keeps the choice for alarms that ring while Brulion is closed.
#pragma once

#include "notifier.h"

namespace brulion::notify {

class AndroidNotifier : public Notifier {
    Q_OBJECT
public:
    explicit AndroidNotifier(QObject *parent = nullptr);
    ~AndroidNotifier() override;

    QString backend() const override;
    bool canShow() const override;

    bool firesWhenClosed() const override { return true; }
    void schedule(const QList<ScheduledNotification> &notifications) override;
    void showNow(const ScheduledNotification &notification) override;

    QList<SoundChoice::Mode> soundModes() const override;
    bool hasSystemSoundPicker() const override { return true; }
    bool hasSystemSoundSettings() const override { return true; }
    bool pickSound(SoundKind kind) override;
    bool previewSound(SoundKind kind, const SoundChoice &choice) override;
    bool openSystemSoundSettings(SoundKind kind) override;
    void setDoneTarget(const QString &vault) override;

protected:
    void soundsChanged() override;

private:
    void takeDoneRequests();
    void arm(const QList<ScheduledNotification> &notifications);

    /// setDoneTarget(): the queue is taken only while a notebook is ready for it.
    QString m_doneTarget;
};

} // namespace brulion::notify

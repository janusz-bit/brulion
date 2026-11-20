// brulion::applock — the app lock (docs/UI.md "App lock", docs/ARCHITECTURE.md "Security"): when the
// app hides its content behind a lock screen, how a PIN is kept and checked, and how many wrong ones
// are allowed. GUI-free and pure — no clock, no platform: the app feeds the state machine with its
// events and times; the device's own check (Android's biometrics or screen lock) is the platform's,
// this only hears that it succeeded. Any thread, one owner.
//
//   Policy     what the user chose (Settings → Privacy): on or off, PIN or the device's check, lock
//              on start, after how long in the background, after how long without input (desktop)
//   LockState  locked or not, from: started(), wentToBackground() / cameToForeground(), activity()
//              and tick() (idle), lockNow(), unlocked(); wrong PINs: failedAttempt(), retryAt() —
//              after 5 wrong ones a wait of 30 s, growing to 15 minutes, kept across restarts
//              (attempts() / restoreAttempts(): wall-clock times the app stores)
//   PIN        isValidPin() (4–16 digits), hashPin() / verifyPin() — PBKDF2-SHA512, 210 000 rounds,
//              a random salt (crypto/secrethash.h), stored per device, never synced
#pragma once

#include <QString>
#include <QStringView>

namespace brulion::applock {

/// How the lock is opened.
enum class Method {
    Pin,    ///< the app's own PIN (every platform)
    Device, ///< the device's check: Android's BiometricPrompt — a fingerprint, the face or the
            ///< phone's own PIN, pattern or password (Android only; elsewhere it falls back to Pin)
};

/// What the user chose (AppSettings, per device).
struct Policy {
    bool enabled = false;
    Method method = Method::Pin;
    bool lockOnStart = true;
    /// Lock when the app comes back after at least this long in the background; 0: every time.
    int backgroundMinutes = 0;
    /// Lock after this long without input while the app is in front (the desktop); 0: never.
    int idleMinutes = 0;
};

/// Wrong PINs so far, for the wait before the next try (kept across restarts by the app).
struct Attempts {
    int failures = 0;
    qint64 lastFailure = 0; ///< wall-clock ms (since the epoch) of the last wrong PIN
};

/// The wait after `failures` wrong PINs in a row: none for the first four, then 30 s, 1, 2, 5 and
/// 15 minutes (and 15 from then on).
qint64 retryDelayMs(int failures);

/// The lock's state. Times of the foreground/background/idle events are a monotonic clock's (ms);
/// times of PIN attempts are wall-clock ms (they must survive a restart).
class LockState {
public:
    void setPolicy(const Policy &policy);
    const Policy &policy() const { return m_policy; }

    bool locked() const { return m_locked; }

    /// The app started (its window is about to show): locked when the lock is on and lockOnStart.
    void started(qint64 now);
    /// The app went to the background (another app in front, the screen off, the window hidden).
    void wentToBackground(qint64 now);
    /// The app came back: locked when the lock is on and it was away at least backgroundMinutes.
    /// True when this locked it.
    bool cameToForeground(qint64 now);
    /// The user did something (a key, a click, a touch): the idle time starts again.
    void activity(qint64 now);
    /// Idle check (call it at nextDeadline()): locked after idleMinutes without activity() while in
    /// front. True when this locked it.
    bool tick(qint64 now);
    /// When tick() should run next (monotonic ms), -1 when nothing is due (no idle lock, locked, in
    /// the background).
    qint64 nextDeadline() const;
    /// "Lock now" (the shell's command, the tray).
    void lockNow();
    /// The PIN or the device's check succeeded: unlocked, wrong PINs forgotten.
    void unlocked(qint64 now);
    /// The lock was switched off: never locked.
    void disable();

    /// A wrong PIN at wall-clock `wallNow`.
    void failedAttempt(qint64 wallNow);
    /// May a PIN be tried at `wallNow` (the wait after wrong ones is over)? A clock set back before
    /// the last failure counts as over (else a wrong clock would lock the user out for good).
    bool attemptAllowed(qint64 wallNow) const;
    /// When the next PIN may be tried (wall-clock ms), 0 when at once.
    qint64 retryAt() const;
    Attempts attempts() const { return m_attempts; }
    void restoreAttempts(const Attempts &attempts) { m_attempts = attempts; }

private:
    Policy m_policy;
    bool m_locked = false;
    bool m_inBackground = false;
    qint64 m_backgroundSince = 0;
    qint64 m_lastActivity = 0;
    Attempts m_attempts;
};

/// A PIN Brulion accepts: 4 to 16 ASCII digits.
bool isValidPin(QStringView pin);
/// The stored form of `pin` (crypto::hashSecret: a salted PBKDF2-SHA512 hash); "" for an invalid PIN.
QString hashPin(QStringView pin);
/// Does `pin` match the stored form? Constant-time comparison; a malformed stored form matches nothing.
bool verifyPin(QStringView stored, QStringView pin);

} // namespace brulion::applock

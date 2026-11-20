#include "applockcontroller.h"

#include "appsettings.h"
#include "platform.h" // src/platform

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#define BRULION_APPLOCK_THREADS 1
#endif

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

constexpr auto kPin = "privacy/lock/pin"_L1;
constexpr auto kMethod = "privacy/lock/method"_L1;
constexpr auto kOnStart = "privacy/lock/onStart"_L1;
constexpr auto kBackground = "privacy/lock/backgroundMinutes"_L1;
constexpr auto kIdle = "privacy/lock/idleMinutes"_L1;
constexpr auto kSecureWindow = "privacy/lock/secureWindow"_L1;
constexpr auto kHideNotifications = "privacy/lock/hideNotificationText"_L1;
constexpr auto kHideWidgets = "privacy/lock/hideWidgets"_L1;
constexpr auto kFailures = "privacy/lock/failures"_L1;
constexpr auto kLastFailure = "privacy/lock/lastFailure"_L1;

/// A phone is left and picked up again all day: one minute; a computer's window loses the focus to
/// other windows: a quarter of an hour.
#ifdef Q_OS_ANDROID
constexpr int DefaultBackgroundMinutes = 1;
#else
constexpr int DefaultBackgroundMinutes = 15;
#endif

QVariantMap result(bool ok, const QString &error = {})
{
    return {{u"ok"_s, ok}, {u"error"_s, error}};
}

bool isInput(QEvent::Type type)
{
    switch (type) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TabletPress: return true;
    default: return false;
    }
}

} // namespace

AppLockController::AppLockController(QObject *parent) : QObject(parent)
{
    m_elapsed.start();
    m_tick.setSingleShot(true);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_state.tick(monotonicNow()))
            setLocked(true);
        else
            scheduleTick();
    });
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        connect(app, &QGuiApplication::applicationStateChanged, this, &AppLockController::applicationStateChanged);
#ifdef Q_OS_ANDROID
    connect(platform::AndroidBridge::instance(), &platform::AndroidBridge::deviceAuthFinished, this,
            [this](bool ok, const QString &error) {
                if (ok) {
                    m_state.unlocked(monotonicNow());
                    storeAttempts();
                    setLocked(false);
                }
                Q_EMIT unlockFinished(ok, error);
            });
#endif
}

AppLockController::~AppLockController()
{
    if (m_filterInstalled && QCoreApplication::instance())
        QCoreApplication::instance()->removeEventFilter(this);
}

void AppLockController::setClocks(std::function<qint64()> monotonic, std::function<qint64()> wall)
{
    m_monotonic = std::move(monotonic);
    m_wall = std::move(wall);
}

qint64 AppLockController::monotonicNow() const
{
    return m_monotonic ? m_monotonic() : m_elapsed.elapsed();
}

qint64 AppLockController::wallNow() const
{
    return m_wall ? m_wall() : QDateTime::currentMSecsSinceEpoch();
}

QString AppLockController::stored(const QString &key, const QString &fallback) const
{
    return m_settings ? m_settings->value(key, fallback).toString() : fallback;
}

void AppLockController::store(const QString &key, const QVariant &value)
{
    if (m_settings)
        m_settings->setValue(key, value);
}

void AppLockController::setSettings(AppSettings *settings)
{
    m_settings = settings;
    applock::Attempts attempts;
    if (m_settings) {
        attempts.failures = std::max(0, m_settings->value(kFailures, 0).toInt());
        attempts.lastFailure = m_settings->value(kLastFailure, qint64(0)).toLongLong();
    }
    m_state.restoreAttempts(attempts);
    applyPolicy();
    start();
}

void AppLockController::start()
{
    if (m_started)
        return;
    m_started = true;
    const bool was = m_state.locked();
    m_state.started(monotonicNow());
    if (was != m_state.locked())
        Q_EMIT lockedChanged();
    applyWindowFlags();
    scheduleTick();
}

applock::Policy AppLockController::policy() const
{
    applock::Policy p;
    p.enabled = enabled();
    p.method = method() == u"device" ? applock::Method::Device : applock::Method::Pin;
    p.lockOnStart = lockOnStart();
    p.backgroundMinutes = backgroundMinutes();
    p.idleMinutes = idleMinutes();
    return p;
}

void AppLockController::applyPolicy()
{
    const bool was = m_state.locked();
    m_state.setPolicy(policy());
    // The idle check listens to input only while it is wanted (every event of the app passes).
    const bool wantFilter = enabled() && idleMinutes() > 0;
    if (wantFilter != m_filterInstalled && QCoreApplication::instance()) {
        if (wantFilter)
            QCoreApplication::instance()->installEventFilter(this);
        else
            QCoreApplication::instance()->removeEventFilter(this);
        m_filterInstalled = wantFilter;
    }
    if (was != m_state.locked())
        Q_EMIT lockedChanged();
    applyWindowFlags();
    scheduleTick();
    Q_EMIT changed();
}

bool AppLockController::eventFilter(QObject *watched, QEvent *event)
{
    if (isInput(event->type()))
        m_state.activity(monotonicNow());
    return QObject::eventFilter(watched, event);
}

void AppLockController::scheduleTick()
{
    const qint64 deadline = m_state.nextDeadline();
    if (deadline < 0) {
        m_tick.stop();
        return;
    }
    // At most a minute ahead: input meanwhile moves the deadline, checked when the timer fires.
    m_tick.start(int(std::clamp<qint64>(deadline - monotonicNow(), 1000, 60 * 1000)));
}

void AppLockController::applicationStateChanged(Qt::ApplicationState state)
{
    const qint64 now = monotonicNow();
    if (state == Qt::ApplicationActive) {
        if (m_state.cameToForeground(now))
            setLocked(true);
    } else {
        m_state.wentToBackground(now);
    }
    scheduleTick();
}

void AppLockController::setLocked(bool locked)
{
    // (m_state decides; this tells the others.)
    Q_UNUSED(locked);
    applyWindowFlags();
    scheduleTick();
    Q_EMIT lockedChanged();
}

void AppLockController::applyWindowFlags()
{
    const bool on = enabled();
    const bool secure = on && (secureWindow() == u"always" || m_state.locked());
    platform::setSecureWindow(secure, on);
}

void AppLockController::storeAttempts()
{
    const applock::Attempts a = m_state.attempts();
    store(kFailures, a.failures);
    store(kLastFailure, a.lastFailure);
    Q_EMIT attemptsChanged();
}

// ---- Settings ---------------------------------------------------------------------------------

bool AppLockController::enabled() const
{
    if (method() == u"device")
        return true;
    return hasPin();
}

QString AppLockController::method() const
{
    const QString m = stored(kMethod, u"none"_s);
    return m == u"device" || m == u"pin" ? m : u"none"_s;
}

bool AppLockController::hasPin() const
{
    return !stored(kPin).isEmpty();
}

bool AppLockController::deviceAvailable() const
{
    return platform::deviceAuthAvailable();
}

bool AppLockController::lockOnStart() const
{
    return m_settings ? m_settings->value(kOnStart, true).toBool() : true;
}

void AppLockController::setLockOnStart(bool on)
{
    if (on == lockOnStart())
        return;
    store(kOnStart, on);
    applyPolicy();
}

int AppLockController::backgroundMinutes() const
{
    const int m =
            m_settings ? m_settings->value(kBackground, DefaultBackgroundMinutes).toInt() : DefaultBackgroundMinutes;
    return std::clamp(m, 0, 24 * 60);
}

void AppLockController::setBackgroundMinutes(int minutes)
{
    minutes = std::clamp(minutes, 0, 24 * 60);
    if (minutes == backgroundMinutes())
        return;
    store(kBackground, minutes);
    applyPolicy();
}

int AppLockController::idleMinutes() const
{
    const int m = m_settings ? m_settings->value(kIdle, 0).toInt() : 0;
    return std::clamp(m, 0, 24 * 60);
}

void AppLockController::setIdleMinutes(int minutes)
{
    minutes = std::clamp(minutes, 0, 24 * 60);
    if (minutes == idleMinutes())
        return;
    store(kIdle, minutes);
    m_state.activity(monotonicNow()); // counted from now
    applyPolicy();
}

QString AppLockController::secureWindow() const
{
    return stored(kSecureWindow, u"whileLocked"_s) == u"always" ? u"always"_s : u"whileLocked"_s;
}

void AppLockController::setSecureWindow(const QString &mode)
{
    const QString m = mode == u"always" ? u"always"_s : u"whileLocked"_s;
    if (m == secureWindow())
        return;
    store(kSecureWindow, m);
    applyPolicy();
}

bool AppLockController::hideNotificationText() const
{
    return m_settings ? m_settings->value(kHideNotifications, true).toBool() : true;
}

void AppLockController::setHideNotificationText(bool on)
{
    if (on == hideNotificationText())
        return;
    store(kHideNotifications, on);
    Q_EMIT changed();
}

bool AppLockController::hideWidgets() const
{
    return m_settings ? m_settings->value(kHideWidgets, true).toBool() : true;
}

void AppLockController::setHideWidgets(bool on)
{
    if (on == hideWidgets())
        return;
    store(kHideWidgets, on);
    Q_EMIT changed();
}

int AppLockController::retrySeconds() const
{
    const qint64 at = m_state.retryAt();
    const qint64 now = wallNow();
    if (at == 0 || m_state.attemptAllowed(now))
        return 0;
    return int((at - now + 999) / 1000);
}

bool AppLockController::validPin(const QString &pin) const
{
    return applock::isValidPin(pin);
}

QVariantMap AppLockController::setPin(const QString &current, const QString &pin)
{
    if (hasPin() && !applock::verifyPin(stored(kPin), current))
        return result(false, tr("The current PIN is wrong."));
    if (!applock::isValidPin(pin))
        return result(false, tr("A PIN is 4 to 16 digits."));
    const QString hash = applock::hashPin(pin);
    if (hash.isEmpty())
        return result(false, tr("The PIN could not be stored."));
    store(kPin, hash);
    store(kMethod, u"pin"_s);
    m_state.unlocked(monotonicNow());
    storeAttempts();
    applyPolicy();
    return result(true);
}

QVariantMap AppLockController::useDevice(const QString &current)
{
    if (!deviceAvailable())
        return result(false, tr("This device has no screen lock to use. Set one in the system settings first."));
    if (method() == u"pin" && hasPin() && !applock::verifyPin(stored(kPin), current))
        return result(false, tr("The current PIN is wrong."));
    store(kMethod, u"device"_s);
    applyPolicy();
    return result(true);
}

QVariantMap AppLockController::turnOff(const QString &current)
{
    if (method() == u"pin" && hasPin() && !applock::verifyPin(stored(kPin), current))
        return result(false, tr("The current PIN is wrong."));
    store(kPin, QString());
    store(kMethod, u"none"_s);
    m_state.disable();
    storeAttempts();
    applyPolicy();
    Q_EMIT lockedChanged();
    return result(true);
}

void AppLockController::unlock(const QString &pin)
{
    if (!m_state.locked()) {
        Q_EMIT unlockFinished(true, QString());
        return;
    }
    if (!m_state.attemptAllowed(wallNow())) {
        Q_EMIT unlockFinished(false, tr("Too many wrong PINs. Try again in %n second(s).", "", retrySeconds()));
        return;
    }
    if (m_checking)
        return;
    const QString hash = stored(kPin);
    m_checking = true;
    Q_EMIT checkingChanged();
    auto ok = std::make_shared<bool>(false);
    QPointer<AppLockController> self(this);
    auto done = [self, ok] {
        if (!self)
            return;
        self->m_checking = false;
        Q_EMIT self->checkingChanged();
        if (*ok) {
            self->m_state.unlocked(self->monotonicNow());
            self->storeAttempts();
            self->setLocked(false);
            Q_EMIT self->unlockFinished(true, QString());
            return;
        }
        self->m_state.failedAttempt(self->wallNow());
        self->storeAttempts();
        const int wait = self->retrySeconds();
        Q_EMIT self->unlockFinished(false, wait > 0 ? tr("Wrong PIN. Try again in %n second(s).", "", wait)
                                                    : tr("Wrong PIN."));
    };
#ifdef BRULION_APPLOCK_THREADS
    if (!m_synchronous) {
        QThreadPool::globalInstance()->start([ok, hash, pin, done = std::move(done)]() mutable {
            *ok = applock::verifyPin(hash, pin);
            QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(done), Qt::QueuedConnection);
        });
        return;
    }
#endif
    QTimer::singleShot(0, this, [ok, hash, pin, done = std::move(done)]() mutable {
        *ok = applock::verifyPin(hash, pin);
        done();
    });
}

void AppLockController::unlockWithDevice()
{
    if (!m_state.locked()) {
        Q_EMIT unlockFinished(true, QString());
        return;
    }
    if (method() != u"device")
        return;
    if (!deviceAvailable()) {
        // The phone's screen lock was removed: there is nothing left to ask — the lock opens
        // (docs/UI.md "App lock").
        m_state.unlocked(monotonicNow());
        storeAttempts();
        setLocked(false);
        Q_EMIT unlockFinished(true, tr("This phone has no screen lock any more, so Brulion cannot ask for it. Set a "
                                       "screen lock or a PIN for Brulion."));
        return;
    }
    if (!platform::requestDeviceAuth(tr("Unlock Brulion"), tr("Your notes are locked."), tr("Cancel")))
        Q_EMIT unlockFinished(false, tr("The phone's unlock could not be shown."));
}

void AppLockController::lockNow()
{
    if (!enabled() || m_state.locked())
        return;
    m_state.lockNow();
    setLocked(true);
}

} // namespace brulion::shell

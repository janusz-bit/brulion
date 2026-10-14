// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

#include "android/androidbridge.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>

#include <atomic>
#include <iterator>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

Q_STATIC_LOGGING_CATEGORY(lcAndroid, "brulion.android")

// android/src/io/github/januszbit/brulion/android/BrulionAndroid.kt (a Kotlin object: every
// function called here is @JvmStatic). Keep names and signatures in sync with it.
constexpr char kClass[] = "io/github/januszbit/brulion/android/BrulionAndroid";
constexpr char kContextToBool[] = "(Landroid/content/Context;)Z";
constexpr char kContextToVoid[] = "(Landroid/content/Context;)V";

/// The live bridge for the native callbacks (they run on Android's main thread). Cleared after
/// BrulionAndroid.detach(), which waits until no callback runs any more.
std::atomic<AndroidBridge *> s_bridge{nullptr};

QJniObject appContext()
{
    return QNativeInterface::QAndroidApplication::context();
}

/// Clears a pending Java exception and reports it. A safety net only: Qt 6's QJniObject call
/// functions already check, log (with the stack trace) and clear exceptions themselves and then
/// return a default value (false / an invalid object), so callers must also treat those as
/// failures. A method missing from the Kotlin class (renamed, wrong signature) ends up the same
/// way: a NoSuchMethodError in logcat and the default value.
bool failed(const char *method)
{
    QJniEnvironment env;
    if (!env.checkAndClearExceptions())
        return false;
    qCWarning(lcAndroid, "BrulionAndroid.%s threw an exception", method);
    return true;
}

bool callBool(const char *method)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const jboolean result =
        QJniObject::callStaticMethod<jboolean>(kClass, method, kContextToBool, context.object());
    if (failed(method))
        return false;
    return result != JNI_FALSE;
}

void callVoid(const char *method)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kClass, method, kContextToVoid, context.object());
    failed(method);
}

QString fromJString(JNIEnv *env, jstring string)
{
    if (!string)
        return {};
    const jsize length = env->GetStringLength(string);
    const jchar *chars = env->GetStringChars(string, nullptr);
    if (!chars)
        return {};
    QString result = QString::fromUtf16(reinterpret_cast<const char16_t *>(chars), length);
    env->ReleaseStringChars(string, chars);
    return result;
}

// --- native methods of BrulionAndroid (called on Android's main thread) --------------------

void JNICALL nativeActivated(JNIEnv *env, jclass, jstring path, jint line)
{
    AndroidBridge *bridge = s_bridge.load();
    if (!bridge)
        return;
    const QString notePath = fromJString(env, path);
    const int noteLine = static_cast<int>(line);
    QMetaObject::invokeMethod(
        bridge, [bridge, notePath, noteLine] { Q_EMIT bridge->activated(notePath, noteLine); },
        Qt::QueuedConnection);
}

void JNICALL nativePermissionsChanged(JNIEnv *, jclass)
{
    AndroidBridge *bridge = s_bridge.load();
    if (!bridge)
        return;
    QMetaObject::invokeMethod(
        bridge, [bridge] { Q_EMIT bridge->permissionsChanged(); }, Qt::QueuedConnection);
}

} // namespace

AndroidBridge *AndroidBridge::instance()
{
    static QPointer<AndroidBridge> bridge;
    if (!bridge) {
        Q_ASSERT(QCoreApplication::instance());
        bridge = new AndroidBridge(QCoreApplication::instance());
    }
    return bridge;
}

AndroidBridge::AndroidBridge(QObject *parent)
    : QObject(parent)
{
    s_bridge.store(this);

    const JNINativeMethod methods[] = {
        {"nativeActivated", "(Ljava/lang/String;I)V", reinterpret_cast<void *>(&nativeActivated)},
        {"nativePermissionsChanged", "()V", reinterpret_cast<void *>(&nativePermissionsChanged)},
    };
    QJniEnvironment env;
    if (env.registerNativeMethods(kClass, methods, int(std::size(methods))))
        callVoid("attach");
    else
        qCWarning(lcAndroid, "cannot register the native methods of %s", kClass);

    // Returning to the app: a permission may have been granted in the settings, and a tap may
    // be waiting (the native callback is the normal path; this is the safety net).
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (state != Qt::ApplicationActive)
                        return;
                    Q_EMIT permissionsChanged();
                    deliverPendingActivation();
                });
    }
    // Cold start from a notification: delivered once the event loop runs (after the UI
    // connected to activated()).
    QMetaObject::invokeMethod(this, &AndroidBridge::deliverPendingActivation, Qt::QueuedConnection);
}

AndroidBridge::~AndroidBridge()
{
    // After detach() returns no native callback runs or starts (Java holds a lock around them).
    QJniObject::callStaticMethod<void>(kClass, "detach", "()V");
    failed("detach");
    s_bridge.store(nullptr);
}

bool AndroidBridge::hasAllFilesAccess() const
{
    return callBool("hasAllFilesAccess");
}

void AndroidBridge::requestAllFilesAccess()
{
    callVoid("requestAllFilesAccess");
}

bool AndroidBridge::notificationPermission() const
{
    return callBool("notificationPermission");
}

bool AndroidBridge::requestNotificationPermission()
{
    return callBool("requestNotificationPermission");
}

bool AndroidBridge::canScheduleExact() const
{
    return callBool("canScheduleExact");
}

void AndroidBridge::requestExactAlarms()
{
    callVoid("requestExactAlarms");
}

bool AndroidBridge::canUseFullScreenIntent() const
{
    return callBool("canUseFullScreenIntent");
}

void AndroidBridge::requestFullScreenIntent()
{
    callVoid("requestFullScreenIntent");
}

ScheduleResult AndroidBridge::scheduleReminders(const QJsonArray &items)
{
    ScheduleResult result;
    const QJniObject context = appContext();
    if (!context.isValid()) {
        result.error = u"no Android context"_s;
        return result;
    }
    const QString json = QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact));
    const QJniObject javaJson = QJniObject::fromString(json);
    const QJniObject answer = QJniObject::callStaticObjectMethod(
        kClass, "scheduleReminders", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        context.object(), javaJson.object<jstring>());
    if (failed("scheduleReminders") || !answer.isValid()) {
        result.error = u"scheduleReminders failed"_s;
        return result;
    }
    const QJsonObject object = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
    if (object.contains("error"_L1)) {
        result.error = object.value("error"_L1).toString();
        qCWarning(lcAndroid) << "scheduleReminders:" << result.error;
        return result;
    }
    result.ok = true;
    result.scheduled = object.value("scheduled"_L1).toInt();
    result.exact = object.value("exact"_L1).toBool();
    return result;
}

Activation AndroidBridge::takeActivation()
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return {};
    const QJniObject answer = QJniObject::callStaticObjectMethod(
        kClass, "takeActivation", "(Landroid/content/Context;)Ljava/lang/String;", context.object());
    if (failed("takeActivation") || !answer.isValid())
        return {};
    const QJsonObject object = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
    return {object.value("path"_L1).toString(), object.value("line"_L1).toInt()};
}

void AndroidBridge::deliverPendingActivation()
{
    const Activation activation = takeActivation();
    if (activation.isValid())
        Q_EMIT activated(activation.path, activation.line);
}

} // namespace brulion::platform

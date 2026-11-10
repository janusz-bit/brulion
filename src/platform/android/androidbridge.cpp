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

#include <algorithm>
#include <atomic>
#include <iterator>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

Q_STATIC_LOGGING_CATEGORY(lcAndroid, "brulion.android")

// android/src/io/github/januszbit/brulion/android/BrulionAndroid.kt (a Kotlin object: every
// function called here is @JvmStatic). Keep names and signatures in sync with it.
constexpr char kClass[] = "io/github/januszbit/brulion/android/BrulionAndroid";
constexpr char kContextToBool[] = "(Landroid/content/Context;)Z";
constexpr char kContextToVoid[] = "(Landroid/content/Context;)V";
constexpr char kContextStringToVoid[] = "(Landroid/content/Context;Ljava/lang/String;)V";
constexpr char kContextToString[] = "(Landroid/content/Context;)Ljava/lang/String;";
constexpr char kContextStringToString[] = "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;";

/// The live bridge for the native callbacks (they run on Android's main thread). Cleared after
/// BrulionAndroid.detach(), which waits until no callback runs any more.
std::atomic<AndroidBridge *> s_bridge{nullptr};

QJniObject appContext()
{
    return QNativeInterface::QAndroidApplication::context();
}

/// Clears a pending Java exception and reports it. After a QJniObject call a safety net only:
/// Qt 6's call functions already check, log (with the stack trace) and clear exceptions
/// themselves and then return a default value (false, 0, an invalid object), so callers must
/// also treat those as failures. A method missing from the Kotlin class (renamed, wrong
/// signature) ends up the same way: a NoSuchMethodError in logcat and the default value. Where
/// that default is a valid answer and the outcome matters (viewFile()'s 0 = started, a void call
/// such as startActivity or OutputStream.write), the call goes through plain JNI and this check.
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
    const jboolean result = QJniObject::callStaticMethod<jboolean>(kClass, method, kContextToBool, context.object());
    return !failed(method) && result != JNI_FALSE;
}

/// BrulionAndroid.<method>(Context): void, with @p argument <method>(Context, String): void.
void callVoid(const char *method, const std::optional<QString> &argument = std::nullopt)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return;
    if (argument) {
        QJniObject::callStaticMethod<void>(kClass, method, kContextStringToVoid, context.object(),
                                           QJniObject::fromString(*argument).object<jstring>());
    } else {
        QJniObject::callStaticMethod<void>(kClass, method, kContextToVoid, context.object());
    }
    failed(method);
}

/// The answer of BrulionAndroid.<method>(Context): String, with @p argument of <method>(Context,
/// String): String, as UTF-8; nullopt when there is no context or the call failed.
std::optional<QByteArray> callString(const char *method, const std::optional<QString> &argument = std::nullopt)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return std::nullopt;
    const QJniObject answer =
            argument ? QJniObject::callStaticObjectMethod(kClass, method, kContextStringToString, context.object(),
                                                          QJniObject::fromString(*argument).object<jstring>())
                     : QJniObject::callStaticObjectMethod(kClass, method, kContextToString, context.object());
    if (failed(method) || !answer.isValid())
        return std::nullopt;
    return answer.toString().toUtf8();
}

/// callString() as JSON: a null document when the call failed or did not answer JSON.
QJsonDocument callJson(const char *method, const std::optional<QString> &argument = std::nullopt)
{
    const std::optional<QByteArray> answer = callString(method, argument);
    return answer ? QJsonDocument::fromJson(*answer) : QJsonDocument();
}

/// @p json (a QJsonObject or QJsonArray) as compact text: the String argument of the Kotlin side.
template<typename Json>
QString compactJson(const Json &json)
{
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

/// True (and logged) when the answer of BrulionAndroid.<method> is `{"error": …}`.
bool answeredError(const char *method, const QJsonObject &answer)
{
    if (!answer.contains("error"_L1))
        return false;
    qCWarning(lcAndroid).nospace() << method << ": " << answer.value("error"_L1).toString();
    return true;
}

/// BrulionAndroid.<method>(Context, String): String with JSON in and out, an `{"error": …}`
/// answer logged; {} when the call failed.
QJsonObject callLogged(const char *method, const QJsonObject &argument)
{
    const QJsonObject answer = callJson(method, compactJson(argument)).object();
    answeredError(method, answer);
    return answer;
}

QJniObject stringField(const char *className, const char *field)
{
    return QJniObject::getStaticObjectField(className, field, "Ljava/lang/String;");
}

/// BrulionAndroid.documentInfo / listDocuments: `(Context, String uri): String` answering
/// `{uri, name, dir, size, virtual}`, `{"documents": [those]}` or `{"error": "…"}`. Any thread.
DocumentsResult documentsCall(const char *method, const QString &uri)
{
    DocumentsResult result;
    const std::optional<QByteArray> answer = callString(method, uri);
    if (!answer) {
        result.bridgeFailed = true;
        result.error = QString::fromLatin1(method) + u" failed"_s;
        return result;
    }
    const QJsonObject object = QJsonDocument::fromJson(*answer).object();
    if (object.isEmpty() || object.contains("error"_L1)) {
        result.error = object.value("error"_L1).toString(u"no answer"_s);
        qCWarning(lcAndroid) << method << result.error;
        return result;
    }
    const auto document = [](const QJsonObject &o) {
        return DocumentInfo{.uri = o.value("uri"_L1).toString(),
                            .name = o.value("name"_L1).toString(),
                            .isDir = o.value("dir"_L1).toBool(),
                            .size = o.value("size"_L1).toInteger(-1),
                            .isVirtual = o.value("virtual"_L1).toBool()};
    };
    if (object.contains("documents"_L1)) {
        const QJsonArray documents = object.value("documents"_L1).toArray();
        result.documents.reserve(documents.size());
        for (const QJsonValue &value : documents) {
            const DocumentInfo d = document(value.toObject());
            if (!d.uri.isEmpty())
                result.documents.append(d);
        }
    } else {
        result.documents.append(document(object));
    }
    result.ok = true;
    return result;
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

/// Hands @p deliver the live bridge on Qt's thread (the native callbacks run on Android's main
/// thread). Queued to the bridge: nothing runs once it is gone.
template<typename Deliver>
void postToBridge(Deliver deliver)
{
    if (AndroidBridge *bridge = s_bridge.load())
        QMetaObject::invokeMethod(bridge, [bridge, deliver] { deliver(bridge); }, Qt::QueuedConnection);
}

// --- native methods of BrulionAndroid (called on Android's main thread) --------------------

void JNICALL nativeActivated(JNIEnv *env, jclass, jstring path, jint line)
{
    postToBridge([notePath = fromJString(env, path), line](AndroidBridge *bridge) {
        Q_EMIT bridge->activated(notePath, line);
    });
}

/// ShareActivity received a share while Qt runs (BrulionAndroid.deliverShare).
void JNICALL nativeShared(JNIEnv *env, jclass, jstring json)
{
    postToBridge([share = fromJString(env, json).toUtf8()](AndroidBridge *bridge) { Q_EMIT bridge->shared(share); });
}

void JNICALL nativePermissionsChanged(JNIEnv *, jclass)
{
    postToBridge([](AndroidBridge *bridge) { Q_EMIT bridge->permissionsChanged(); });
}

/// "Done" was tapped (DoneQueue.kt queued it): C++ takes it with takeDoneRequests().
void JNICALL nativeDoneQueued(JNIEnv *, jclass)
{
    postToBridge([](AndroidBridge *bridge) { Q_EMIT bridge->doneRequestsQueued(); });
}

/// Quick add of a widget queued an entry (WidgetInbox.kt): C++ takes it with takeWidgetActions().
void JNICALL nativeWidgetQueued(JNIEnv *, jclass)
{
    postToBridge([](AndroidBridge *bridge) { Q_EMIT bridge->widgetActionsQueued(); });
}

/// Quick add of a widget wrote a note itself (JournalAppend.kt) while Qt runs.
void JNICALL nativeWidgetWrote(JNIEnv *env, jclass, jstring path)
{
    postToBridge([file = fromJString(env, path)](AndroidBridge *bridge) { Q_EMIT bridge->widgetNoteWritten(file); });
}

/// The ringtone picker answered (BrulionAndroid.onActivityResult): `json` is the choice. Not an
/// object (never sent): {}, which the receiver rejects like a missing picker.
void JNICALL nativeSoundPicked(JNIEnv *env, jclass, jint kind, jstring json)
{
    const QJsonObject choice = QJsonDocument::fromJson(fromJString(env, json).toUtf8()).object();
    postToBridge([kind, choice](AndroidBridge *bridge) { Q_EMIT bridge->soundPicked(kind, choice); });
}

/// The keyboard observer (ImeInsets.kt) reports, also every frame of the keyboard's slide: queued
/// to Qt's thread like the other callbacks. Malformed values (negative) count as 0.
void JNICALL nativeImeChanged(JNIEnv *, jclass, jint overlap, jboolean shown, jboolean animating)
{
    const int pixels = std::max(0, static_cast<int>(overlap));
    const bool isShown = shown != JNI_FALSE;
    const bool isAnimating = animating != JNI_FALSE;
    postToBridge([pixels, isShown, isAnimating](AndroidBridge *bridge) {
        Q_EMIT bridge->imeChanged(pixels, isShown, isAnimating);
    });
}

/// The system gesture insets (ImeInsets.kt), queued to Qt's thread. Negative values count as 0.
void JNICALL nativeGestureInsetsChanged(JNIEnv *, jclass, jint left, jint right)
{
    const int l = std::max(0, static_cast<int>(left));
    const int r = std::max(0, static_cast<int>(right));
    postToBridge([l, r](AndroidBridge *bridge) { Q_EMIT bridge->systemGesturesChanged(l, r); });
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
            {"nativeShared", "(Ljava/lang/String;)V", reinterpret_cast<void *>(&nativeShared)},
            {"nativePermissionsChanged", "()V", reinterpret_cast<void *>(&nativePermissionsChanged)},
            {"nativeSoundPicked", "(ILjava/lang/String;)V", reinterpret_cast<void *>(&nativeSoundPicked)},
            {"nativeDoneQueued", "()V", reinterpret_cast<void *>(&nativeDoneQueued)},
            {"nativeWidgetQueued", "()V", reinterpret_cast<void *>(&nativeWidgetQueued)},
            {"nativeWidgetWrote", "(Ljava/lang/String;)V", reinterpret_cast<void *>(&nativeWidgetWrote)},
            {"nativeImeChanged", "(IZZ)V", reinterpret_cast<void *>(&nativeImeChanged)},
            {"nativeGestureInsetsChanged", "(II)V", reinterpret_cast<void *>(&nativeGestureInsetsChanged)},
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
                    deliverPendingShare();
                    Q_EMIT doneRequestsQueued();
                    Q_EMIT widgetActionsQueued();
                });
    }
    // Cold start from a notification or a share: delivered once the event loop runs (after the
    // UI connected to activated() and shared()). "Done" taps queued while Brulion was closed
    // likewise.
    QMetaObject::invokeMethod(this, &AndroidBridge::deliverPendingActivation, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &AndroidBridge::deliverPendingShare, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &AndroidBridge::doneRequestsQueued, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &AndroidBridge::widgetActionsQueued, Qt::QueuedConnection);
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
    if (!appContext().isValid()) {
        result.error = u"no Android context"_s;
        return result;
    }
    // An answer that is no JSON object is a failure too, not "nothing armed".
    const QJsonDocument answer = callJson("scheduleReminders", compactJson(items));
    const QJsonObject object = answer.object();
    if (!answer.isObject() || answeredError("scheduleReminders", object)) {
        result.error = object.value("error"_L1).toString(u"scheduleReminders failed"_s);
        return result;
    }
    result.ok = true;
    result.scheduled = object.value("scheduled"_L1).toInt();
    result.exact = object.value("exact"_L1).toBool();
    return result;
}

Activation AndroidBridge::takeActivation()
{
    const QJsonObject object = callJson("takeActivation").object();
    return {object.value("path"_L1).toString(), object.value("line"_L1).toInt()};
}

QByteArray AndroidBridge::takeShare()
{
    const QByteArray json = callString("takeShare").value_or(QByteArray());
    return json == "{}" ? QByteArray() : json;
}

QJsonArray AndroidBridge::takeDoneRequests(const QString &vault)
{
    return callJson("takeDoneRequests", vault).array();
}

int AndroidBridge::updateWidgets(const QJsonObject &snapshot)
{
    const QJsonDocument answer = callJson("updateWidgets", compactJson(snapshot));
    const QJsonObject object = answer.object();
    if (!answer.isObject() || answeredError("updateWidgets", object))
        return -1;
    return object.value("widgets"_L1).toInt();
}

QJsonObject AndroidBridge::setBackgroundCheck(const QJsonObject &target)
{
    return callLogged("setBackgroundCheck", target);
}

QJsonObject AndroidBridge::backgroundCheckStatus()
{
    return callJson("backgroundCheckStatus").object();
}

QJsonObject AndroidBridge::backgroundCheckTarget()
{
    return callJson("backgroundCheckTarget").object();
}

void AndroidBridge::backgroundCheckFinished(const QJsonObject &result)
{
    callVoid("backgroundCheckFinished", compactJson(result));
}

QJsonArray AndroidBridge::takeWidgetActions(const QString &vault)
{
    return callJson("takeWidgetActions", vault).array();
}

void AndroidBridge::requeueWidgetActions(const QJsonArray &actions)
{
    if (!actions.isEmpty())
        callVoid("requeueWidgetActions", compactJson(actions));
}

bool AndroidBridge::shareFile(const QString &path, const QString &mimeType, const QString &subject,
                              const QString &chooserTitle)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const QString authority =
            context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString() + u".qtprovider"_s;
    const QJniObject file("java/io/File", "(Ljava/lang/String;)V", QJniObject::fromString(path).object<jstring>());
    const QJniObject uri = QJniObject::callStaticObjectMethod(
            "androidx/core/content/FileProvider", "getUriForFile",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;", context.object(),
            QJniObject::fromString(authority).object<jstring>(), file.object());
    if (failed("FileProvider.getUriForFile") || !uri.isValid())
        return false;
    constexpr char kIntent[] = "android/content/Intent";
    const QJniObject javaSubject = QJniObject::fromString(subject);
    QJniObject intent(kIntent, "(Ljava/lang/String;)V", stringField(kIntent, "ACTION_SEND").object<jstring>());
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(mimeType).object<jstring>());
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                            stringField(kIntent, "EXTRA_STREAM").object<jstring>(), uri.object());
    // The URI also as ClipData: createChooser() hands it and the read grant to the share sheet,
    // which can then show the file's preview (without it the sheet is denied access).
    const QJniObject clip =
            QJniObject::callStaticObjectMethod("android/content/ClipData", "newRawUri",
                                               "(Ljava/lang/CharSequence;Landroid/net/Uri;)Landroid/content/ClipData;",
                                               javaSubject.object<jstring>(), uri.object());
    if (!failed("ClipData.newRawUri") && clip.isValid())
        intent.callMethod<void>("setClipData", "(Landroid/content/ClipData;)V", clip.object());
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                            stringField(kIntent, "EXTRA_SUBJECT").object<jstring>(), javaSubject.object<jstring>());
    const jint grantRead = QJniObject::getStaticField<jint>(kIntent, "FLAG_GRANT_READ_URI_PERMISSION");
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", grantRead);
    QJniObject chooser = QJniObject::callStaticObjectMethod(
            kIntent, "createChooser", "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
            intent.object(), QJniObject::fromString(chooserTitle).object<jstring>());
    if (failed("Intent.createChooser") || !chooser.isValid())
        return false;
    chooser.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", grantRead);
    // The context is the activity while the app is visible; anything else needs a new task.
    QJniEnvironment env;
    const jclass activityClass = env.findClass("android/app/Activity");
    if (!activityClass || !env->IsInstanceOf(context.object(), activityClass)) {
        const jint newTask = QJniObject::getStaticField<jint>(kIntent, "FLAG_ACTIVITY_NEW_TASK");
        chooser.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", newTask);
    }
    // Plain JNI: QJniObject's void calls swallow the exception of a start that failed.
    const jclass contextClass = env.findClass("android/content/Context");
    const jmethodID startActivity =
            contextClass ? env->GetMethodID(contextClass, "startActivity", "(Landroid/content/Intent;)V") : nullptr;
    if (startActivity)
        env->CallVoidMethod(context.object(), startActivity, chooser.object());
    return !failed("startActivity") && startActivity != nullptr;
}

AndroidBridge::ViewResult AndroidBridge::viewFile(const QString &path, const QString &mimeType)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return ViewResult::Failed;
    // Plain JNI: QJniObject answers 0 (= started) for a call that threw or a missing method.
    QJniEnvironment env;
    const jclass bridgeClass = env.findClass(kClass);
    const jmethodID view =
            bridgeClass ? env->GetStaticMethodID(bridgeClass, "viewFile",
                                                 "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)I")
                        : nullptr;
    if (failed("viewFile") || !view)
        return ViewResult::Failed;
    const jint code = env->CallStaticIntMethod(bridgeClass, view, context.object(),
                                               QJniObject::fromString(path).object<jstring>(),
                                               QJniObject::fromString(mimeType).object<jstring>());
    if (failed("viewFile"))
        return ViewResult::Failed;
    switch (code) {
    case 0: return ViewResult::Started;
    case 1: return ViewResult::NoApp;
    case 3: return ViewResult::Copying;
    default: return ViewResult::Failed;
    }
}

bool AndroidBridge::printPdf(const QString &path, const QString &jobName)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const jboolean ok = QJniObject::callStaticMethod<jboolean>(
            kClass, "printPdf", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z", context.object(),
            QJniObject::fromString(path).object<jstring>(), QJniObject::fromString(jobName).object<jstring>());
    return !failed("printPdf") && ok;
}

DownloadResult AndroidBridge::saveToDownloads(const QString &name, const QString &mimeType, const QByteArray &data)
{
    DownloadResult result;
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 29) {
        result.unsupported = true;
        return result;
    }
    const QJniObject context = appContext();
    const QJniObject resolver =
            context.isValid() ? context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;")
                              : QJniObject();
    if (!resolver.isValid()) {
        result.error = u"no content resolver"_s;
        return result;
    }
    constexpr char kColumns[] = "android/provider/MediaStore$MediaColumns";
    const QJniObject displayName = stringField(kColumns, "DISPLAY_NAME");
    const QJniObject pendingKey = stringField(kColumns, "IS_PENDING");
    const auto putString = [](QJniObject &values, const QJniObject &key, const QString &value) {
        values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V", key.object<jstring>(),
                                QJniObject::fromString(value).object<jstring>());
    };
    const auto putInt = [](QJniObject &values, const QJniObject &key, int value) {
        const QJniObject boxed = QJniObject::callStaticObjectMethod("java/lang/Integer", "valueOf",
                                                                    "(I)Ljava/lang/Integer;", jint(value));
        values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/Integer;)V", key.object<jstring>(),
                                boxed.object());
    };
    // A pending entry first: other apps see the file only once it is complete.
    QJniObject values("android/content/ContentValues");
    putString(values, displayName, name);
    putString(values, stringField(kColumns, "MIME_TYPE"), mimeType);
    putString(values, stringField(kColumns, "RELATIVE_PATH"),
              stringField("android/os/Environment", "DIRECTORY_DOWNLOADS").toString());
    putInt(values, pendingKey, 1);
    const QJniObject collection = QJniObject::getStaticObjectField("android/provider/MediaStore$Downloads",
                                                                   "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
    const QJniObject uri =
            resolver.callObjectMethod("insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
                                      collection.object(), values.object());
    if (failed("MediaStore insert") || !uri.isValid()) {
        result.error = u"the Download folder refused the file"_s;
        return result;
    }
    const auto discard = [&] {
        resolver.callMethod<jint>("delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I", uri.object(),
                                  jstring(nullptr), jobjectArray(nullptr));
        failed("MediaStore delete");
    };
    const QJniObject out =
            resolver.callObjectMethod("openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;", uri.object());
    if (failed("openOutputStream") || !out.isValid()) {
        discard();
        result.error = u"cannot write to the Download folder"_s;
        return result;
    }
    // In chunks (an export may be large), through plain JNI: QJniObject's void calls swallow
    // exceptions, and a failed write (storage full) must not leave a broken file behind.
    QJniEnvironment env;
    const jclass stream = env->GetObjectClass(out.object());
    const jmethodID write = env->GetMethodID(stream, "write", "([BII)V");
    const jmethodID close = env->GetMethodID(stream, "close", "()V");
    constexpr qsizetype Chunk = 1 << 20;
    const jbyteArray buffer = env->NewByteArray(jsize(std::clamp<qsizetype>(data.size(), 1, Chunk)));
    bool ok = !failed("OutputStream") && write && close && buffer;
    for (qsizetype offset = 0; ok && offset < data.size(); offset += Chunk) {
        const jsize n = jsize(std::min<qsizetype>(Chunk, data.size() - offset));
        env->SetByteArrayRegion(buffer, 0, n, reinterpret_cast<const jbyte *>(data.constData() + offset));
        env->CallVoidMethod(out.object(), write, buffer, jint(0), n);
        ok = !failed("OutputStream.write");
    }
    if (buffer)
        env->DeleteLocalRef(buffer);
    if (close)
        env->CallVoidMethod(out.object(), close);
    ok = !failed("OutputStream.close") && ok;
    env->DeleteLocalRef(stream);
    if (!ok) {
        discard();
        result.error = u"writing to the Download folder failed"_s;
        return result;
    }
    QJniObject done("android/content/ContentValues");
    putInt(done, pendingKey, 0);
    resolver.callMethod<jint>(
            "update", "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I",
            uri.object(), done.object(), jstring(nullptr), jobjectArray(nullptr));
    failed("MediaStore update");
    result.ok = true;
    result.name = name;
    // The name the system gave it (" (1)" when taken).
    const jobjectArray projection = env->NewObjectArray(1, env.findClass("java/lang/String"), displayName.object());
    const QJniObject cursor = resolver.callObjectMethod("query",
                                                        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/"
                                                        "lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
                                                        uri.object(), projection, jstring(nullptr),
                                                        jobjectArray(nullptr), jstring(nullptr));
    env->DeleteLocalRef(projection);
    if (!failed("MediaStore query") && cursor.isValid()) {
        if (cursor.callMethod<jboolean>("moveToFirst") && !failed("Cursor.moveToFirst")) {
            const QString shown = cursor.callObjectMethod("getString", "(I)Ljava/lang/String;", jint(0)).toString();
            if (!failed("Cursor.getString") && !shown.isEmpty())
                result.name = shown;
        }
        cursor.callMethod<void>("close");
        failed("Cursor.close");
    }
    return result;
}

DocumentsResult AndroidBridge::documentInfo(const QString &uri)
{
    return documentsCall("documentInfo", uri);
}

DocumentsResult AndroidBridge::listDocuments(const QString &folderUri)
{
    return documentsCall("listDocuments", folderUri);
}

QJsonObject AndroidBridge::setReminderSounds(const QJsonObject &sounds)
{
    return callLogged("setReminderSounds", sounds);
}

bool AndroidBridge::pickSound(int kind, const QJsonObject &current)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const jboolean asked = QJniObject::callStaticMethod<jboolean>(
            kClass, "pickSound", "(Landroid/content/Context;ILjava/lang/String;)Z", context.object(), jint(kind),
            QJniObject::fromString(compactJson(current)).object<jstring>());
    return !failed("pickSound") && asked != JNI_FALSE;
}

bool AndroidBridge::previewSound(const QJsonObject &choice)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const jboolean played = QJniObject::callStaticMethod<jboolean>(
            kClass, "previewSound", "(Landroid/content/Context;Ljava/lang/String;)Z", context.object(),
            QJniObject::fromString(compactJson(choice)).object<jstring>());
    return !failed("previewSound") && played != JNI_FALSE;
}

void AndroidBridge::openSoundSettings(int kind)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kClass, "openSoundSettings", "(Landroid/content/Context;I)V", context.object(),
                                       jint(kind));
    failed("openSoundSettings");
}

void AndroidBridge::requestImeState()
{
    QJniObject::callStaticMethod<void>(kClass, "requestImeState", "()V");
    failed("requestImeState");
}

void AndroidBridge::setSystemBarsLight(bool light)
{
    QJniObject::callStaticMethod<void>(kClass, "setSystemBarsLight", "(Z)V", jboolean(light));
    failed("setSystemBarsLight");
}

void AndroidBridge::setGestureExclusion(const QList<QRect> &rects)
{
    QList<jint> values;
    values.reserve(rects.size() * 4);
    for (const QRect &r : rects)
        values << r.x() << r.y() << r.width() << r.height();
    QJniEnvironment env;
    const jsize size = jsize(values.size());
    jintArray array = env->NewIntArray(size);
    if (array) {
        env->SetIntArrayRegion(array, 0, size, values.constData());
        QJniObject::callStaticMethod<void>(kClass, "setGestureExclusion", "([I)V", array);
        env->DeleteLocalRef(array);
    }
    failed("setGestureExclusion"); // also clears the OutOfMemoryError of a failed NewIntArray()
}

void AndroidBridge::deliverPendingActivation()
{
    const Activation activation = takeActivation();
    if (activation.isValid())
        Q_EMIT activated(activation.path, activation.line);
}

void AndroidBridge::deliverPendingShare()
{
    const QByteArray share = takeShare();
    if (!share.isEmpty())
        Q_EMIT shared(share);
}

} // namespace brulion::platform

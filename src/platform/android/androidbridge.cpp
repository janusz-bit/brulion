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

/// ShareActivity received a share while Qt runs (BrulionAndroid.deliverShare).
void JNICALL nativeShared(JNIEnv *env, jclass, jstring json)
{
    AndroidBridge *bridge = s_bridge.load();
    if (!bridge)
        return;
    const QByteArray share = fromJString(env, json).toUtf8();
    QMetaObject::invokeMethod(bridge, [bridge, share] { Q_EMIT bridge->shared(share); }, Qt::QueuedConnection);
}

void JNICALL nativePermissionsChanged(JNIEnv *, jclass)
{
    AndroidBridge *bridge = s_bridge.load();
    if (!bridge)
        return;
    QMetaObject::invokeMethod(
        bridge, [bridge] { Q_EMIT bridge->permissionsChanged(); }, Qt::QueuedConnection);
}

/// The ringtone picker answered (BrulionAndroid.onActivityResult): `json` is the choice.
void JNICALL nativeSoundPicked(JNIEnv *env, jclass, jint kind, jstring json)
{
    AndroidBridge *bridge = s_bridge.load();
    if (!bridge)
        return;
    const int soundKind = static_cast<int>(kind);
    const QJsonObject choice = QJsonDocument::fromJson(fromJString(env, json).toUtf8()).object();
    QMetaObject::invokeMethod(
            bridge, [bridge, soundKind, choice] { Q_EMIT bridge->soundPicked(soundKind, choice); },
            Qt::QueuedConnection);
}

/// The keyboard observer (ImeInsets.kt) reports, also every frame of the keyboard's slide: queued
/// to Qt's thread like the other callbacks. Malformed values (negative) count as 0.
void JNICALL nativeImeChanged(JNIEnv *, jclass, jint overlap, jboolean shown, jboolean animating)
{
    AndroidBridge *bridge = s_bridge.load();
    if (!bridge)
        return;
    const int pixels = std::max(0, static_cast<int>(overlap));
    const bool isShown = shown != JNI_FALSE;
    const bool isAnimating = animating != JNI_FALSE;
    QMetaObject::invokeMethod(
            bridge, [bridge, pixels, isShown, isAnimating] { Q_EMIT bridge->imeChanged(pixels, isShown, isAnimating); },
            Qt::QueuedConnection);
}

/// The system gesture insets (ImeInsets.kt), queued to Qt's thread. Negative values count as 0.
void JNICALL nativeGestureInsetsChanged(JNIEnv *, jclass, jint left, jint right)
{
    AndroidBridge *bridge = s_bridge.load();
    if (!bridge)
        return;
    const int l = std::max(0, static_cast<int>(left));
    const int r = std::max(0, static_cast<int>(right));
    QMetaObject::invokeMethod(
            bridge, [bridge, l, r] { Q_EMIT bridge->systemGesturesChanged(l, r); }, Qt::QueuedConnection);
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
                });
    }
    // Cold start from a notification or a share: delivered once the event loop runs (after the
    // UI connected to activated() and shared()).
    QMetaObject::invokeMethod(this, &AndroidBridge::deliverPendingActivation, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &AndroidBridge::deliverPendingShare, Qt::QueuedConnection);
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

QByteArray AndroidBridge::takeShare()
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return {};
    const QJniObject answer = QJniObject::callStaticObjectMethod(
            kClass, "takeShare", "(Landroid/content/Context;)Ljava/lang/String;", context.object());
    if (failed("takeShare") || !answer.isValid())
        return {};
    const QByteArray json = answer.toString().toUtf8();
    return json == "{}" ? QByteArray() : json;
}

namespace {

QJniObject stringField(const char *className, const char *field)
{
    return QJniObject::getStaticObjectField(className, field, "Ljava/lang/String;");
}

} // namespace

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
    const auto stringConstant = [](const char *cls, const char *name) {
        return QJniObject::getStaticObjectField(cls, name, "Ljava/lang/String;");
    };
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                      stringConstant("android/content/Intent", "ACTION_SEND").object<jstring>());
    intent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;",
                            QJniObject::fromString(mimeType).object<jstring>());
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;",
                            stringConstant("android/content/Intent", "EXTRA_STREAM").object<jstring>(), uri.object());
    // The URI also as ClipData: createChooser() hands it and the read grant to the share sheet,
    // which can then show the file's preview (without it the sheet is denied access).
    const QJniObject clip =
            QJniObject::callStaticObjectMethod("android/content/ClipData", "newRawUri",
                                               "(Ljava/lang/CharSequence;Landroid/net/Uri;)Landroid/content/ClipData;",
                                               QJniObject::fromString(subject).object<jstring>(), uri.object());
    if (!failed("ClipData.newRawUri") && clip.isValid())
        intent.callMethod<void>("setClipData", "(Landroid/content/ClipData;)V", clip.object());
    intent.callObjectMethod("putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                            stringConstant("android/content/Intent", "EXTRA_SUBJECT").object<jstring>(),
                            QJniObject::fromString(subject).object<jstring>());
    const jint grantRead = QJniObject::getStaticField<jint>("android/content/Intent", "FLAG_GRANT_READ_URI_PERMISSION");
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", grantRead);
    QJniObject chooser = QJniObject::callStaticObjectMethod(
            "android/content/Intent", "createChooser",
            "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;", intent.object(),
            QJniObject::fromString(chooserTitle).object<jstring>());
    if (failed("Intent.createChooser") || !chooser.isValid())
        return false;
    chooser.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", grantRead);
    // The context is the activity while the app is visible; anything else needs a new task.
    QJniEnvironment env;
    const jclass activityClass = env.findClass("android/app/Activity");
    if (!activityClass || !env->IsInstanceOf(context.object(), activityClass)) {
        const jint newTask = QJniObject::getStaticField<jint>("android/content/Intent", "FLAG_ACTIVITY_NEW_TASK");
        chooser.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", newTask);
    }
    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", chooser.object());
    return !failed("startActivity");
}

DownloadResult AndroidBridge::saveToDownloads(const QString &name, const QString &mimeType, const QByteArray &data)
{
    DownloadResult result;
    if (QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT") < 29) {
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
              QJniObject::getStaticObjectField("android/os/Environment", "DIRECTORY_DOWNLOADS", "Ljava/lang/String;")
                      .toString());
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
    {
        QJniEnvironment env;
        const auto threw = [&env] {
            if (!env->ExceptionCheck())
                return false;
            env->ExceptionDescribe();
            env->ExceptionClear();
            return true;
        };
        const jclass stream = env->GetObjectClass(out.object());
        const jmethodID write = env->GetMethodID(stream, "write", "([BII)V");
        const jmethodID close = env->GetMethodID(stream, "close", "()V");
        constexpr qsizetype Chunk = 1 << 20;
        const jbyteArray buffer =
                env->NewByteArray(jsize(std::min<qsizetype>(Chunk, std::max<qsizetype>(1, data.size()))));
        bool ok = !threw() && write && close && buffer;
        for (qsizetype offset = 0; ok && offset < data.size(); offset += Chunk) {
            const jsize n = jsize(std::min<qsizetype>(Chunk, data.size() - offset));
            env->SetByteArrayRegion(buffer, 0, n, reinterpret_cast<const jbyte *>(data.constData() + offset));
            env->CallVoidMethod(out.object(), write, buffer, jint(0), n);
            ok = !threw();
        }
        if (buffer)
            env->DeleteLocalRef(buffer);
        if (close)
            env->CallVoidMethod(out.object(), close);
        ok = !threw() && ok;
        env->DeleteLocalRef(stream);
        if (!ok) {
            discard();
            result.error = u"writing to the Download folder failed"_s;
            return result;
        }
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
    QJniEnvironment env;
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

namespace {

/// BrulionAndroid.documentInfo / listDocuments: `(Context, String uri): String` answering
/// `{uri, name, dir, size, virtual}`, `{"documents": [those]}` or `{"error": "…"}`. Any thread.
DocumentsResult documentsCall(const char *method, const QString &uri)
{
    DocumentsResult result;
    const QJniObject context = appContext();
    if (!context.isValid()) {
        result.bridgeFailed = true;
        result.error = u"no Android context"_s;
        return result;
    }
    const QJniObject javaUri = QJniObject::fromString(uri);
    const QJniObject answer = QJniObject::callStaticObjectMethod(
            kClass, method, "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;", context.object(),
            javaUri.object<jstring>());
    if (failed(method) || !answer.isValid()) {
        result.bridgeFailed = true;
        result.error = QString::fromLatin1(method) + u" failed"_s;
        return result;
    }
    const QJsonObject object = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
    if (object.isEmpty() || object.contains("error"_L1)) {
        result.error = object.value("error"_L1).toString(u"no answer"_s);
        qCWarning(lcAndroid) << method << result.error;
        return result;
    }
    const auto document = [](const QJsonObject &o) {
        DocumentInfo d;
        d.uri = o.value("uri"_L1).toString();
        d.name = o.value("name"_L1).toString();
        d.isDir = o.value("dir"_L1).toBool();
        d.size = o.value("size"_L1).toInteger(-1);
        d.isVirtual = o.value("virtual"_L1).toBool();
        return d;
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

} // namespace

DocumentsResult AndroidBridge::documentInfo(const QString &uri)
{
    return documentsCall("documentInfo", uri);
}

DocumentsResult AndroidBridge::listDocuments(const QString &folderUri)
{
    return documentsCall("listDocuments", folderUri);
}

namespace {

QJniObject jsonString(const QJsonObject &object)
{
    return QJniObject::fromString(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

} // namespace

QJsonObject AndroidBridge::setReminderSounds(const QJsonObject &sounds)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return {};
    const QJniObject json = jsonString(sounds);
    const QJniObject answer = QJniObject::callStaticObjectMethod(
            kClass, "setReminderSounds", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
            context.object(), json.object<jstring>());
    if (failed("setReminderSounds") || !answer.isValid())
        return {};
    const QJsonObject object = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
    if (object.contains("error"_L1))
        qCWarning(lcAndroid) << "setReminderSounds:" << object.value("error"_L1).toString();
    return object;
}

bool AndroidBridge::pickSound(int kind, const QJsonObject &current)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const QJniObject json = jsonString(current);
    const jboolean asked = QJniObject::callStaticMethod<jboolean>(kClass, "pickSound",
                                                                  "(Landroid/content/Context;ILjava/lang/String;)Z",
                                                                  context.object(), jint(kind), json.object<jstring>());
    return !failed("pickSound") && asked != JNI_FALSE;
}

bool AndroidBridge::previewSound(const QJsonObject &choice)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const QJniObject json = jsonString(choice);
    const jboolean played = QJniObject::callStaticMethod<jboolean>(kClass, "previewSound",
                                                                   "(Landroid/content/Context;Ljava/lang/String;)Z",
                                                                   context.object(), json.object<jstring>());
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

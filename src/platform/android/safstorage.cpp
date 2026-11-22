// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

#include "android/safstorage.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace brulion::platform::android {

namespace {

Q_STATIC_LOGGING_CATEGORY(lcFolder, "brulion.android")

// android/src/io/github/januszbit/brulion/android/TreeFolder.kt (a Kotlin object: every function
// called here is @JvmStatic). Keep names and signatures in sync with it.
constexpr char kClass[] = "io/github/januszbit/brulion/android/TreeFolder";
constexpr char kPathCall[] = "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;";
constexpr char kTreeCall[] = "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;";

QJniObject appContext()
{
    return QNativeInterface::QAndroidApplication::context();
}

/// A pending Java exception, cleared (Qt's calls clear and log them already: a safety net).
bool threw()
{
    QJniEnvironment env;
    return env.checkAndClearExceptions();
}

sync::FolderErrorCode codeOf(const QString &name)
{
    using sync::FolderErrorCode;
    if (name == u"not-found")
        return FolderErrorCode::NotFound;
    if (name == u"exists")
        return FolderErrorCode::Exists;
    if (name == u"not-allowed")
        return FolderErrorCode::NotAllowed;
    if (name == u"unavailable")
        return FolderErrorCode::Unavailable;
    if (name == u"unsupported")
        return FolderErrorCode::Unsupported;
    return FolderErrorCode::Failed;
}

/// The error of an answer, none when it has no "error".
sync::FolderError errorOf(const QJsonObject &answer)
{
    if (!answer.contains("error"_L1))
        return {};
    return sync::FolderError::make(codeOf(answer.value("error"_L1).toString()), answer.value("message"_L1).toString());
}

/// TreeFolder.<method>(Context, tree, path): String as JSON; a Failed error when JNI failed.
QJsonObject callPath(const char *method, const QString &tree, const QString &path)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return {{"error"_L1, u"unavailable"_s}, {"message"_L1, u"no Android context"_s}};
    const QJniObject answer =
            QJniObject::callStaticObjectMethod(kClass, method, kPathCall, context.object(),
                                               QJniObject::fromString(tree).object<jstring>(),
                                               QJniObject::fromString(path).object<jstring>());
    if (threw() || !answer.isValid())
        return {{"error"_L1, u"failed"_s}, {"message"_L1, u"TreeFolder.%1 failed"_s.arg(QLatin1StringView(method))}};
    const QJsonDocument json = QJsonDocument::fromJson(answer.toString().toUtf8());
    if (!json.isObject())
        return {{"error"_L1, u"failed"_s}, {"message"_L1, u"TreeFolder.%1: not JSON"_s.arg(QLatin1StringView(method))}};
    return json.object();
}

sync::FolderEntry entryOf(const QJsonObject &json)
{
    sync::FolderEntry entry;
    entry.name = json.value("name"_L1).toString();
    entry.isDir = json.value("dir"_L1).toBool();
    entry.size = json.value("size"_L1).toInteger();
    entry.mtime = json.value("mtime"_L1).toInteger();
    return entry;
}

sync::FolderStatResult statOf(const QJsonObject &answer)
{
    if (const sync::FolderError error = errorOf(answer); error.isError())
        return {std::nullopt, error};
    if (!answer.value("exists"_L1).toBool())
        return {};
    return {entryOf(answer), {}};
}

} // namespace

SafFolderStorage::SafFolderStorage(const QString &treeUri)
    : m_tree(treeUri)
{
}

sync::FolderStatResult SafFolderStorage::stat(const QString &path)
{
    return statOf(callPath("stat", m_tree, path));
}

sync::FolderListResult SafFolderStorage::list(const QString &folder)
{
    const QJsonObject answer = callPath("list", m_tree, folder);
    sync::FolderListResult result;
    result.error = errorOf(answer);
    if (result.error.isError())
        return result;
    if (!answer.value("entries"_L1).isArray()) {
        result.error = sync::FolderError::make(sync::FolderErrorCode::Failed, u"TreeFolder.list: no entries"_s);
        return result;
    }
    const QJsonArray entries = answer.value("entries"_L1).toArray();
    result.entries.reserve(entries.size());
    for (const QJsonValue &value : entries)
        result.entries.append(entryOf(value.toObject()));
    return result;
}

sync::FolderReadResult SafFolderStorage::read(const QString &path, qint64 maxBytes)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return {{}, sync::FolderError::make(sync::FolderErrorCode::Unavailable, u"no Android context"_s)};
    const QJniObject bytes = QJniObject::callStaticObjectMethod(
            kClass, "read", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;J)[B", context.object(),
            QJniObject::fromString(m_tree).object<jstring>(), QJniObject::fromString(path).object<jstring>(),
            jlong(maxBytes));
    if (threw())
        return {{}, sync::FolderError::make(sync::FolderErrorCode::Failed, u"TreeFolder.read failed"_s)};
    if (!bytes.isValid()) {
        const QJniObject why = QJniObject::callStaticObjectMethod(kClass, "lastError", "()Ljava/lang/String;");
        const QJsonObject answer = why.isValid() ? QJsonDocument::fromJson(why.toString().toUtf8()).object() : QJsonObject();
        sync::FolderError error = errorOf(answer);
        if (!error.isError())
            error = sync::FolderError::make(sync::FolderErrorCode::Failed, u"TreeFolder.read: no bytes"_s);
        return {{}, error};
    }
    QJniEnvironment env;
    const auto array = bytes.object<jbyteArray>();
    const jsize size = env->GetArrayLength(array);
    QByteArray data(qsizetype(size), Qt::Uninitialized);
    env->GetByteArrayRegion(array, 0, size, reinterpret_cast<jbyte *>(data.data()));
    if (env.checkAndClearExceptions())
        return {{}, sync::FolderError::make(sync::FolderErrorCode::Failed, u"TreeFolder.read: copy failed"_s)};
    return {std::move(data), {}};
}

sync::FolderStatResult SafFolderStorage::write(const QString &path, const QByteArray &data)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return {std::nullopt, sync::FolderError::make(sync::FolderErrorCode::Unavailable, u"no Android context"_s)};
    QJniEnvironment env;
    jbyteArray array = env->NewByteArray(jsize(data.size()));
    if (!array || env.checkAndClearExceptions())
        return {std::nullopt, sync::FolderError::make(sync::FolderErrorCode::Failed, u"no memory for %1"_s.arg(path))};
    env->SetByteArrayRegion(array, 0, jsize(data.size()), reinterpret_cast<const jbyte *>(data.constData()));
    const QJniObject answer = QJniObject::callStaticObjectMethod(
            kClass, "write", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;[B)Ljava/lang/String;",
            context.object(), QJniObject::fromString(m_tree).object<jstring>(),
            QJniObject::fromString(path).object<jstring>(), array);
    env->DeleteLocalRef(array);
    if (threw() || !answer.isValid())
        return {std::nullopt, sync::FolderError::make(sync::FolderErrorCode::Failed, u"TreeFolder.write failed"_s)};
    return statOf(QJsonDocument::fromJson(answer.toString().toUtf8()).object());
}

sync::FolderError SafFolderStorage::makeFolder(const QString &path)
{
    return errorOf(callPath("makeFolder", m_tree, path));
}

sync::FolderError SafFolderStorage::remove(const QString &path)
{
    return errorOf(callPath("remove", m_tree, path));
}

DeviceFolderInfo adoptTree(const QString &treeUri)
{
    DeviceFolderInfo info;
    const QJniObject context = appContext();
    if (!context.isValid()) {
        info.code = u"unavailable"_s;
        info.error = u"no Android context"_s;
        return info;
    }
    const QJniObject answer = QJniObject::callStaticObjectMethod(kClass, "take", kTreeCall, context.object(),
                                                                 QJniObject::fromString(treeUri).object<jstring>());
    if (threw() || !answer.isValid()) {
        info.code = u"failed"_s;
        info.error = u"TreeFolder.take failed"_s;
        return info;
    }
    const QJsonObject json = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
    if (const sync::FolderError error = errorOf(json); error.isError() || json.isEmpty()) {
        info.code = error.isError() ? sync::folderErrorName(error.code) : u"failed"_s;
        info.error = error.isError() ? error.message : u"TreeFolder.take: not JSON"_s;
        qCWarning(lcFolder).noquote() << u"Picked folder not usable: %1"_s.arg(info.error);
        return info;
    }
    info.ok = true;
    info.address = json.value("uri"_L1).toString(treeUri);
    info.name = json.value("name"_L1).toString();
    info.where = json.value("where"_L1).toString();
    return info;
}

void releaseTree(const QString &treeUri)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kClass, "release", "(Landroid/content/Context;Ljava/lang/String;)V",
                                       context.object(), QJniObject::fromString(treeUri).object<jstring>());
    threw();
}

bool treeAccessible(const QString &treeUri)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return false;
    const jboolean held = QJniObject::callStaticMethod<jboolean>(kClass, "hasAccess",
                                                                 "(Landroid/content/Context;Ljava/lang/String;)Z",
                                                                 context.object(),
                                                                 QJniObject::fromString(treeUri).object<jstring>());
    return !threw() && held != JNI_FALSE;
}

} // namespace brulion::platform::android

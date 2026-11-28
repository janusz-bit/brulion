// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

#include "android/safstorage.h"

#include <QCoreApplication>
#include <QFile>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <unistd.h>

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

namespace {

/// Why the last TreeFolder call on this thread failed (its lastError()), or `fallback`.
sync::FolderError lastFolderError(const QString &fallback)
{
    const QJniObject why = QJniObject::callStaticObjectMethod(kClass, "lastError", "()Ljava/lang/String;");
    const QJsonObject answer = why.isValid() ? QJsonDocument::fromJson(why.toString().toUtf8()).object() : QJsonObject();
    sync::FolderError error = errorOf(answer);
    return error.isError() ? error : sync::FolderError::make(sync::FolderErrorCode::Failed, fallback);
}

} // namespace

sync::FolderError SafFolderStorage::readInto(const QString &path, qint64 maxBytes, const sync::PieceSink &sink)
{
    const QJniObject context = appContext();
    if (!context.isValid())
        return sync::FolderError::make(sync::FolderErrorCode::Unavailable, u"no Android context"_s);
    const jint fd = QJniObject::callStaticMethod<jint>(
            kClass, "openRead", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;J)I", context.object(),
            QJniObject::fromString(m_tree).object<jstring>(), QJniObject::fromString(path).object<jstring>(),
            jlong(maxBytes));
    if (threw())
        return sync::FolderError::make(sync::FolderErrorCode::Failed, u"TreeFolder.openRead failed"_s);
    if (fd < 0)
        return lastFolderError(u"TreeFolder.openRead: no file"_s);
    QFile file;
    if (!file.open(int(fd), QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return sync::FolderError::make(sync::FolderErrorCode::Failed, u"%1: %2"_s.arg(path, file.errorString()));
    }
    return sync::readPieces(file, path, maxBytes, sink);
}

namespace {

/// `piece`s of `source` written to `file` until the end; the bytes written, or nullopt when the
/// source failed or the file could not take them (`why` says).
std::optional<qint64> writePieces(QFile &file, const sync::PieceSource &source, QString *why)
{
    qint64 total = 0;
    for (std::optional<QByteArray> piece = source(); !piece || !piece->isEmpty(); piece = source()) {
        if (!piece) {
            *why = u"its body could not be read"_s;
            return std::nullopt;
        }
        if (file.write(*piece) != piece->size()) {
            *why = file.errorString();
            return std::nullopt;
        }
        total += piece->size();
    }
    if (!file.flush()) {
        *why = file.errorString();
        return std::nullopt;
    }
    return total;
}

} // namespace

sync::FolderStatResult SafFolderStorage::writeFrom(const QString &path, const sync::PieceSource &source)
{
    const QJsonObject begun = callPath("beginWrite", m_tree, path);
    if (const sync::FolderError error = errorOf(begun); error.isError())
        return {std::nullopt, error};
    const QString part = begun.value("part"_L1).toString();
    const int fd = begun.value("fd"_L1).toInt(-1);
    QFile file;
    QString why;
    std::optional<qint64> written;
    if (fd >= 0 && file.open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle))
        written = writePieces(file, source, &why);
    else
        why = fd < 0 ? u"no descriptor"_s : file.errorString();
    file.close();
    if (!written) {
        callPath("abandonWrite", m_tree, part); // (nothing of it left behind)
        return {std::nullopt, sync::FolderError::make(sync::FolderErrorCode::Failed, u"Write %1: %2"_s.arg(path, why))};
    }
    const QJniObject context = appContext();
    const QJniObject answer = QJniObject::callStaticObjectMethod(
            kClass, "finishWrite", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;J)Ljava/lang/String;",
            context.object(), QJniObject::fromString(m_tree).object<jstring>(), QJniObject::fromString(path).object<jstring>(),
            QJniObject::fromString(part).object<jstring>(), jlong(*written));
    if (threw() || !answer.isValid()) {
        callPath("abandonWrite", m_tree, part);
        return {std::nullopt, sync::FolderError::make(sync::FolderErrorCode::Failed, u"TreeFolder.finishWrite failed"_s)};
    }
    const sync::FolderStatResult result = statOf(QJsonDocument::fromJson(answer.toString().toUtf8()).object());
    if (result.error.isError())
        callPath("abandonWrite", m_tree, part);
    return result;
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

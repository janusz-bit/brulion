#include "devicefolder.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#ifdef Q_OS_ANDROID
#include "android/safstorage.h"
#endif

using namespace Qt::StringLiterals;

namespace brulion::platform {

bool isTreeUri(const QString &address)
{
    return address.startsWith(u"content://"_s) && address.contains(u"/tree/"_s);
}

#ifdef Q_OS_ANDROID

DeviceFolderInfo adoptDeviceFolder(const QString &picked)
{
    return android::adoptTree(picked);
}

void releaseDeviceFolder(const QString &address)
{
    if (isTreeUri(address))
        android::releaseTree(address);
}

bool deviceFolderAccessible(const QString &address)
{
    return isTreeUri(address) && android::treeAccessible(address);
}

std::shared_ptr<sync::FolderStorage> openDeviceFolder(const QString &address)
{
    if (!isTreeUri(address))
        return nullptr;
    return std::make_shared<android::SafFolderStorage>(address);
}

#else

namespace {

/// A local path from a picked URL (file://) or a path.
QString localFolder(const QString &picked)
{
    const QUrl url(picked);
    const QString path = url.isLocalFile() ? url.toLocalFile() : picked;
    return path.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

DeviceFolderInfo adoptDeviceFolder(const QString &picked)
{
    DeviceFolderInfo info;
    if (isTreeUri(picked)) {
        info.code = u"unsupported"_s;
        info.error = u"A folder of Android's system dialog on another platform"_s;
        return info;
    }
    const QString path = localFolder(picked);
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        info.code = u"not-found"_s;
        info.error = u"No folder %1"_s.arg(picked);
        return info;
    }
    info.ok = true;
    info.address = path;
    info.name = QFileInfo(path).fileName();
    info.where = QDir::toNativeSeparators(path);
    return info;
}

void releaseDeviceFolder(const QString &address)
{
    Q_UNUSED(address);
}

bool deviceFolderAccessible(const QString &address)
{
    return !isTreeUri(address) && !address.isEmpty() && QFileInfo(address).isDir();
}

std::shared_ptr<sync::FolderStorage> openDeviceFolder(const QString &address)
{
    if (isTreeUri(address) || address.isEmpty())
        return nullptr;
    return std::make_shared<sync::LocalFolderStorage>(address);
}

#endif

} // namespace brulion::platform

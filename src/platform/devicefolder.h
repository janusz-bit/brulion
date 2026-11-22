// A folder of this device that a notebook in the app's own storage is kept in sync with
// (docs/ANDROID.md "A folder picked in the system's dialog"): on Android a folder picked in the
// system's dialog — a tree URI of the Storage Access Framework (android/safstorage.h), reached with
// the grant the app keeps for it, no storage permission (the app for Google Play has none) —, on
// other platforms a local folder by its path (LocalFolderStorage: the desktop's tests and builds).
// The sync itself is sync::SyncEngine over sync::FolderDavTransport (src/core/sync/foldertransport.h).
#pragma once

#include "sync/folderstorage.h"

#include <QString>

#include <memory>

namespace brulion::platform {

/// What adoptDeviceFolder() found.
struct DeviceFolderInfo {
    bool ok = false;
    QString address; ///< what the link keeps: a tree URI ("content://…/tree/…"), or a clean path
    QString name;    ///< the folder's name ("Notatki")
    QString where;   ///< where it is, for the settings: "primary:Notatki" (Android's storage), the path
    QString error;   ///< why not (English, for logs) when !ok
    /// The error code of sync::folderErrorName() ("not-allowed", …) when !ok.
    QString code;
};

/// A folder just picked (a URL from the folder dialog: content:// on Android, file:// or a path
/// elsewhere): Android keeps its grant (across restarts); the folder is checked to be one.
DeviceFolderInfo adoptDeviceFolder(const QString &picked);
/// The app no longer syncs with the folder: Android gives its grant back. Nothing elsewhere.
void releaseDeviceFolder(const QString &address);
/// The app may still read and write the folder (Android: it holds the grant, which can be taken
/// back in the system's settings; elsewhere: the folder exists).
bool deviceFolderAccessible(const QString &address);
/// The storage the sync reaches the folder through (std::nullptr for an address this platform
/// cannot reach: a tree URI outside Android).
std::shared_ptr<sync::FolderStorage> openDeviceFolder(const QString &address);
/// The address is a tree URI of Android's Storage Access Framework.
bool isTreeUri(const QString &address);

} // namespace brulion::platform

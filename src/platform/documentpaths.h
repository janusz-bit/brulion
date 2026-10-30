#pragma once

// Android documents that say which file they are. The system's file picker (ACTION_OPEN_DOCUMENT,
// FilePicker) hands out content:// URIs even for files on the phone's own storage — also for one
// inside the notebook, which Brulion reads directly with its all-files access. The storage's
// document provider and the Downloads provider name the file in the URI itself; with that a picked
// file of the notebook is used where it is instead of being copied (Workspace::importAttachment(),
// a drawing's backdrop). Pure (no JNI), built everywhere: tests/platform checks it on the desktop.

#include <QString>
#include <QUrl>

namespace brulion::platform {

/// The file system path of an Android document whose URI names it:
///   content://com.android.externalstorage.documents/document/primary%3ANotatki%2Fa.pdf
///       → <primaryStorage>/Notatki/a.pdf (the phone's storage; tree URIs "…/tree/<id>/document/<id>"
///       the same)
///   …/document/home%3Aa.pdf → <primaryStorage>/Documents/a.pdf
///   …/document/1234-ABCD%3AWyk%C5%82ady%2Fa.pdf → /storage/1234-ABCD/Wykłady/a.pdf (a memory card)
///   content://com.android.providers.downloads.documents/document/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2Fa.pdf
///       → /storage/emulated/0/Download/a.pdf
/// "" for other URIs (MediaStore, cloud drives, opaque ids such as "msf:123"), for "primary" and
/// "home" without `primaryStorage`, and for a path that leaves its volume ("..").
QString localPathOfDocument(const QUrl &url, const QString &primaryStorage);

/// The phone's own storage that the absolute path `path` lies on (/storage/emulated/<user>), ""
/// when it lies elsewhere (a memory card, the app's private folder, a desktop path). Pass a
/// canonical path: /sdcard is a link to it.
QString primaryStorageOf(const QString &path);

} // namespace brulion::platform

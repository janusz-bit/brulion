// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later
//
// A folder picked in Android's system dialog (a tree URI of the Storage Access Framework) as a
// sync::FolderStorage: every operation goes through JNI to TreeFolder.kt, which reaches the folder
// through its document provider (the phone's storage and memory cards: by path; other providers
// folder by folder) with the grant the app keeps for it — no storage permission. Called from
// sync::FolderDavTransport's worker thread (QJniObject attaches it to the JVM).
#pragma once

#include "devicefolder.h"
#include "sync/folderstorage.h"

#include <QString>

namespace brulion::platform::android {

class SafFolderStorage final : public sync::FolderStorage {
public:
    explicit SafFolderStorage(const QString &treeUri);

    QString describe() const override { return m_tree; }
    sync::FolderStatResult stat(const QString &path) override;
    sync::FolderListResult list(const QString &folder) override;
    sync::FolderReadResult read(const QString &path, qint64 maxBytes) override;
    sync::FolderStatResult write(const QString &path, const QByteArray &data) override;
    sync::FolderError makeFolder(const QString &path) override;
    sync::FolderError remove(const QString &path) override;
    /// Reads and writes stream: a document's file descriptor read in pieces (TreeFolder.openRead);
    /// a write goes in pieces into a part file beside it, renamed into place — or copied there by
    /// the JVM in pieces where the provider cannot rename (TreeFolder.beginWrite, finishWrite).
    bool streams() const override { return true; }
    sync::FolderError readInto(const QString &path, qint64 maxBytes, const sync::PieceSink &sink) override;
    sync::FolderStatResult writeFrom(const QString &path, const sync::PieceSource &source) override;

private:
    QString m_tree;
};

/// TreeFolder.take(): keeps the grant of a picked folder and names it.
DeviceFolderInfo adoptTree(const QString &treeUri);
/// TreeFolder.release().
void releaseTree(const QString &treeUri);
/// TreeFolder.hasAccess().
bool treeAccessible(const QString &treeUri);

} // namespace brulion::platform::android

// The plan of a publish — the dry run: every file of the site and of the last publish with what
// happens to it (upload, update, unchanged, delete) or why it cannot (a conflict: changed at the
// target since the last publish, or there without having been published from here). Pure.
// Part of the publish module — see publish/publish.h.
#pragma once

#include "publish/manifest.h"
#include "publish/site.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::publish {

enum class FileAction {
    Upload,    ///< new at the target
    Update,    ///< changed since the last publish
    Unchanged, ///< the target has this very version
    Delete,    ///< published before, not in the site any more
    Conflict,  ///< changed at the target (or there, not published from here): left alone
};
/// "upload" | "update" | "unchanged" | "delete" | "conflict".
QString actionName(FileAction action);

struct PlannedFile {
    QString path;
    FileAction action = FileAction::Upload;
    qint64 size = 0;
    /// A conflict of a file that would be deleted (else of one that would be written).
    bool deletion = false;
    /// The version the target has ("" none): what an upload or a delete expects to find.
    QString remote;
    friend bool operator==(const PlannedFile &, const PlannedFile &) = default;
};

/// A file at the target.
struct RemoteFile {
    /// Its version: GitHub — the blob id, WebDAV — the ETag, a folder — the SHA-256.
    QString version;
    qint64 size = -1;
};
using RemoteState = QHash<QString, RemoteFile>;

/// How a target's versions compare with a site file's content.
enum class VersionKind {
    GitSha, ///< RemoteFile::version is Git's blob id (SiteFile::gitSha)
    Sha256, ///< … the SHA-256 (SiteFile::sha256)
    Opaque, ///< an ETag: equal only to what an upload of ours left (ManifestFile::remote)
};

struct PublishPlan {
    QList<PlannedFile> files; ///< sorted by path
    /// The target's files were looked at (else the plan comes from the manifest alone: offline).
    bool remoteChecked = false;

    int count(FileAction action) const;
    qint64 bytes(FileAction action) const;
    /// Anything to upload or delete.
    bool hasChanges() const;
    QList<PlannedFile> conflicts() const;
};

/// The plan of publishing `site` where `manifest` says the last publish went, the target holding
/// `remote` (nullopt: unknown — from the manifest alone). `force`: conflicts are written or
/// deleted anyway (the user chose "Replace them").
///   site file, at the target:  the same content (by kind) → unchanged; the version the manifest
///                              recorded → unchanged or update by the SHA-256; else conflict
///   site file, not there:      upload
///   in the manifest only:      delete when the target still has the recorded version (or the
///                              same content), conflict when it changed, nothing when it is gone
PublishPlan planPublish(const SiteBuild &site, const PublishManifest &manifest,
                        const std::optional<RemoteState> &remote, VersionKind kind, bool force);

/// The plan of removing what the manifest published (every file a delete, or a conflict when it
/// changed at the target since).
PublishPlan planUnpublish(const PublishManifest &manifest, const std::optional<RemoteState> &remote, VersionKind kind,
                          bool force);

} // namespace brulion::publish

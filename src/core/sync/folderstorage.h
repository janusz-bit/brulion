// A folder of this device as the other side of a sync (docs/ANDROID.md "A folder picked in the
// system's dialog"): the notebook lives in the app's own storage, where files are fast, watched
// and written atomically, and SyncEngine keeps it in sync with a folder the app reaches only
// through an API of the system — Android's Storage Access Framework (a folder picked in the
// system's dialog, src/platform/android/safstorage.h), or a plain folder (LocalFolderStorage: the
// tests, and any platform). FolderDavTransport (foldertransport.h) puts such a folder behind the
// HttpTransport of a WebDavClient, so the engine, its three-way merge and its guards are the same
// as with Nextcloud.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::sync {

enum class FolderErrorCode {
    None,
    NotFound,    ///< the path (or the folder it should be in) does not exist
    Exists,      ///< a folder to create exists already, or a move's destination
    NotAllowed,  ///< no access any more (Android: the picked folder's permission was taken back)
    Unavailable, ///< the folder itself cannot be reached (storage removed, provider gone)
    Unsupported, ///< the storage cannot do this (a move): the caller has another way
    Failed,      ///< reading or writing failed
};

struct FolderError {
    FolderErrorCode code = FolderErrorCode::None;
    QString message; ///< technical details (English), for logs

    bool isError() const { return code != FolderErrorCode::None; }
    bool is(FolderErrorCode c) const { return code == c; }
    static FolderError make(FolderErrorCode code, const QString &message) { return {code, message}; }
};

/// "not-found", "exists", "not-allowed", "unavailable", "unsupported", "failed" ("" for None).
QString folderErrorName(FolderErrorCode code);

/// A file or folder of the storage.
struct FolderEntry {
    QString name;      ///< the name as the storage spells it (list()); "" for stat() of the folder itself
    bool isDir = false;
    qint64 size = 0;   ///< bytes (files)
    qint64 mtime = 0;  ///< last modification, ms since the epoch; 0 when the storage does not say
    friend bool operator==(const FolderEntry &, const FolderEntry &) = default;
};

struct FolderStatResult {
    std::optional<FolderEntry> entry; ///< std::nullopt (and no error) when the path does not exist
    FolderError error;
};

struct FolderListResult {
    QList<FolderEntry> entries; ///< the folder's children (any order)
    FolderError error;          ///< NotFound: the folder does not exist; never an empty list instead
};

struct FolderReadResult {
    QByteArray data;
    FolderError error;
};

/// The folder, reached by relative paths: "Dziennik/2026-11-22.md", "" for the folder itself;
/// '/' between names, never "." or ".." (FolderDavTransport refuses them before asking).
/// Synchronous; FolderDavTransport calls it from one worker thread at a time, never from two at
/// once. Implementations tell a folder that cannot be read from an empty one: a failed listing is
/// an error, never an empty list — the engine would take every file for deleted there.
class FolderStorage {
public:
    virtual ~FolderStorage() = default;
    /// For log lines: what the folder is ("/home/jan/Notatki", "primary:Brulion").
    virtual QString describe() const = 0;
    /// A file or folder; no entry when it does not exist.
    virtual FolderStatResult stat(const QString &path) = 0;
    /// The children of a folder.
    virtual FolderListResult list(const QString &folder) = 0;
    /// The whole file; Failed when it is larger than `maxBytes`.
    virtual FolderReadResult read(const QString &path, qint64 maxBytes) = 0;
    /// Writes the whole file, created or replaced (its folder exists: else NotFound); the entry
    /// it has afterwards.
    virtual FolderStatResult write(const QString &path, const QByteArray &data) = 0;
    /// Creates a folder in an existing one (else NotFound); Exists when the name is taken. "" is
    /// the folder itself: created where the storage can (else NotAllowed).
    virtual FolderError makeFolder(const QString &path) = 0;
    /// Deletes a file, or a folder with everything in it; NotFound when missing.
    virtual FolderError remove(const QString &path) = 0;
    /// Moves or renames a file or folder into an existing folder; Exists when `to` is taken and
    /// not `overwrite`. Unsupported by default: the engine then copies and deletes.
    virtual FolderError move(const QString &from, const QString &to, bool overwrite);
};

/// A plain folder of the file system (QFile/QDir; files written through QSaveFile).
class LocalFolderStorage final : public FolderStorage {
public:
    explicit LocalFolderStorage(const QString &root);

    QString root() const { return m_root; }
    QString describe() const override { return m_root; }
    FolderStatResult stat(const QString &path) override;
    FolderListResult list(const QString &folder) override;
    FolderReadResult read(const QString &path, qint64 maxBytes) override;
    FolderStatResult write(const QString &path, const QByteArray &data) override;
    FolderError makeFolder(const QString &path) override;
    FolderError remove(const QString &path) override;
    FolderError move(const QString &from, const QString &to, bool overwrite) override;

private:
    QString absolute(const QString &path) const;
    QString m_root;
};

} // namespace brulion::sync

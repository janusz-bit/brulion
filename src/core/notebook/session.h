// Notebook — a vault opened for headless use (brulion-cli, its MCP server): the Vault without the
// app around it, a VaultIndex built on first use and kept up to date with the vault's changes, a
// lock that serialises the writers of the device, and edits written safely next to the app,
// Syncthing and Nextcloud. It never creates a ReminderScheduler, WorkspaceSync or AppSettings:
// reminder state ("handled", "missed") and the app's settings stay the app's. Part of the notebook
// module — see notebook/notebook.h.
//
// Writing (edit()): the edit is an intent applied to the file's current text (append a block, set a
// task's status), never a stale copy of the whole file:
//   1. read the bytes and hash them (FNV-1a 64, hex — the `hash` brulion-cli shows);
//   2. apply the edit (pure functions: line endings, a BOM and every other byte stay);
//   3. write with Vault::writeTextIfUnchanged(): if another program changed the file since step 1,
//      start again at 1 (at most `attempts` times), else report a conflict;
//   4. the replaced text goes to the device's History first, with the reason given (brulion-cli:
//      "cli", not throttled, so every agent edit can be undone in the app's version history).
// A dry run stops after step 2. lock() (QLockFile in the device state folder, stale locks of dead
// processes taken over) keeps two command-line processes from interleaving their steps.
#pragma once

#include "notebook/indexloader.h"
#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <functional>
#include <memory>

class QLockFile;

namespace brulion {
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::notebook {

/// The device-local state folder of a vault (History, LocalStore, the command line's lock):
/// "<appDataDir>/vaults/<Vault::idForPath(vaultRoot)>" — shared by the app and brulion-cli.
QString deviceStatePath(const QString &appDataDir, const QString &vaultRoot);

/// FNV-1a 64 of a file's bytes as 16 hex digits: the `hash` of `page read`, `--expect-hash`.
QString contentHash(const QByteArray &bytes);
/// Text of a note's bytes as the vault reads it (UTF-8, a leading BOM dropped).
QString decodeText(const QByteArray &bytes);

class Notebook {
public:
    struct Options {
        /// Device state folder (History, LocalStore, lock); "" = none (no History).
        QString stateDir;
        /// Watch the folder and rescan it every 2 minutes (a long-running process: MCP).
        bool watch = false;
        /// History reason of the notebook's own writes ("cli"); "" = the ChangeSource's name.
        QString historyReason = QStringLiteral("cli");
        /// Clock of History and the trash (default: the current time).
        std::function<QDateTime()> clock;
        /// Threads the index loader may use (none: everything on the calling thread).
        WorkerPool workers;
        /// Keep the index cache in "<stateDir>/index-cache.bin" (index/indexcache.h): read when
        /// the index is loaded, written when the notebook closes after a load that parsed many
        /// notes (and in watch mode after quiet changes). Off without a state folder.
        bool indexCache = true;
    };

    enum class OpenError { None, Missing, NotAFolder, Failed };

    Notebook();
    ~Notebook();
    Notebook(const Notebook &) = delete;
    Notebook &operator=(const Notebook &) = delete;

    /// Open the vault folder `root`. Unlike Vault::open() a missing folder is never created
    /// (Missing), and a file is refused (NotAFolder).
    bool open(const QString &root, const Options &options, QString *error = nullptr, OpenError *kind = nullptr);
    bool isOpen() const;
    Vault &vault() const;
    QString root() const;
    QString stateDir() const;

    /// The index of every note and attachment, read from the disk on first use and kept up to
    /// date with the vault's changes from then on.
    VaultIndex &index();
    bool hasIndex() const;
    /// How index() loaded the index (IndexLoader::Stats; empty before the first index()).
    IndexLoader::Stats indexStats() const;
    /// The index cache file ("" when none is kept: no state folder, or Options::indexCache off).
    QString indexCachePath() const;

    /// Take the device's write lock ("<stateDir>/cli.lock", else one in the temp folder named by the
    /// vault id), waiting at most `timeoutMs`; nullptr (and `error`) when it cannot be had. The lock
    /// is released when the returned object is destroyed.
    std::unique_ptr<QLockFile> lock(int timeoutMs, QString *error = nullptr) const;

    /// The file as it is on disk now.
    struct Current {
        bool exists = false;
        QByteArray bytes;
        QString text; ///< decodeText(bytes)
        QString hash; ///< contentHash(bytes); "" when missing
        bool validUtf8 = true;
    };
    Current read(const VaultPath &path) const;

    struct WriteOptions {
        QString expectHash;     ///< refuse unless the file's hash is this (HashMismatch)
        bool dryRun = false;    ///< compute, write nothing
        bool mayCreate = false; ///< a missing file is created (else NotFound)
        /// What a missing file starts from (mayCreate): the edit is applied to it (a daily note's
        /// template, newJournalText()), so a dry run shows the file as it would be created.
        QString initialText;
        int attempts = 3; ///< tries when other programs keep changing the file
    };
    struct WriteResult {
        enum Status {
            Written,      ///< the new text is in the file
            Unchanged,    ///< the edit changed nothing: nothing written
            DryRun,       ///< computed only
            Refused,      ///< the edit refused (its own reason)
            NotFound,     ///< the file does not exist (and mayCreate is off)
            HashMismatch, ///< expectHash differs from the file's hash
            Conflict,     ///< other programs changed the file on every attempt
            Encoding,     ///< not valid UTF-8: rewriting it would change bytes the edit never touched
            Failed,       ///< I/O error (error)
        };
        Status status = Failed;
        VaultPath path;
        bool created = false;
        QString hashBefore; ///< "" for a missing file
        QString hashAfter;
        QString before; ///< the file's text the edit was applied to ("" for a new file)
        QString after;  ///< what the edit made of it
        QString error;
        bool ok() const { return status == Written || status == Unchanged || status == DryRun; }
    };
    /// Edit: the current text (initialText for a missing file) and whether the file exists -> the
    /// new text in `next`; false refuses.
    using Edit = std::function<bool(const QString &current, bool exists, QString *next)>;
    /// Apply `edit` to `path` as described in the file comment.
    WriteResult edit(const VaultPath &path, const Edit &edit, const WriteOptions &options);

    /// Tests: called right before each write attempt (another writer may change the file there).
    void setBeforeWrite(std::function<void(const VaultPath &path, int attempt)> hook);
    /// Tests: the smallest notebook that gets an index cache (default 200 notes).
    void setIndexCacheMinNotes(qsizetype notes) { m_cacheMinNotes = notes; }

private:
    void applyChanges(const QList<VaultChange> &changes);
    void indexFile(const VaultPath &path);

    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    QString m_stateDir;
    WorkerPool m_workers;
    IndexLoader::Stats m_indexStats;
    bool m_indexCache = true;
    qsizetype m_cacheMinNotes = 200;
    bool m_watch = false;
    std::unique_ptr<class IndexCacheKeeper> m_cacheKeeper;
    std::function<void(const VaultPath &, int)> m_beforeWrite;
};

} // namespace brulion::notebook

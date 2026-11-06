// WorkspaceBackups — automatic backups of the open notebook (QML: Workspace.backups), over the core
// module backup: the settings and state of this device (the notebook's LocalStore, keys "backup/…",
// never in the vault), the schedule (checked soon after the notebook is ready, hourly while it is
// open and when the app returns to the foreground), the backups in the folder, "create now", pruning
// after a complete new backup, and restoring: a plan shown as counts, open editors saved and sync
// held, a safety backup first (a failed one, or one missing a file the plan touches, aborts), then the
// plan applied, one sync after it. A restore interrupted by closing or a kill is remembered
// ("backup/restoring"): told again on the next open. Never blocks: the work runs on the worker pool it
// is given, or in slices on the GUI thread (the browser). docs/UI.md "Backups", docs/ARCHITECTURE.md
// "Data safety".
//
// Whose copies: the manifest in each ZIP names its notebook by id. A copy of the same name pattern
// from another notebook (or this one when it lay elsewhere) is listed as foreign: never pruned, and
// restoring or deleting it takes an explicit `allowForeign` after a confirmation of its own. The id
// comes from the notebook's path, so copies made before a lost device state are still its own.
//
// The folder: by default <device state>/backups (in the browser a mount of its own, docs/WEB.md); on
// the desktop and Android the user may choose another. One inside the notebook is refused (sync would
// spread it and it would grow by itself), an Android document URI that names no file too.
//
// In the browser a copy is only safe once IndexedDB has it (setDeferUntilStored()): pruning, and a
// restore after its safety backup, wait for storageFlushed(); storageRefused() removes the new copy
// again and keeps the older ones.
#pragma once

#include "backup/archive.h"
#include "backup/restore.h"
#include "notebook/indexloader.h"

#include <QDateTime>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion {
class Vault;
} // namespace brulion

namespace brulion::shell {

class WorkspaceBackups : public QObject {
    Q_OBJECT

    /// A notebook with a device state is open (not the documentation, which needs no backups).
    Q_PROPERTY(bool available READ available NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged FINAL)
    /// "daily" | "weekly"
    Q_PROPERTY(QString interval READ interval WRITE setInterval NOTIFY stateChanged FINAL)
    /// How many backups are kept.
    Q_PROPERTY(int keep READ keep WRITE setKeep NOTIFY stateChanged FINAL)
    /// [{ value, text }] for the "keep" control: fewer choices in the browser.
    Q_PROPERTY(QVariantList keepChoices READ keepChoices NOTIFY stateChanged FINAL)
    /// The folder in use, as the file system knows it.
    Q_PROPERTY(QString folder READ folder NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool usingDefaultFolder READ usingDefaultFolder NOTIFY stateChanged FINAL)
    /// The browser: copies live in its storage, no folder to choose, "download" on each.
    Q_PROPERTY(bool browser READ browser NOTIFY stateChanged FINAL)
    /// Android: the phone's system share sheet offers the default folder's copies (they lie in the
    /// app's private folder, no other app or file manager sees them).
    Q_PROPERTY(bool canShare READ canShare NOTIFY stateChanged FINAL)
    /// A desktop: the folder can be opened in the file manager.
    Q_PROPERTY(bool canOpenFolder READ canOpenFolder CONSTANT FINAL)
    /// A backup or a restore is under way.
    Q_PROPERTY(bool running READ running NOTIFY stateChanged FINAL)
    /// "" | "backup" | "check" | "sync" (waiting for a running sync) | "safety" | "restore"
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged FINAL)
    /// "none" (no backup yet) | "ok" | "warning" (some files were left out) | "failed"
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    /// What status means, translated; "" when there is nothing to say.
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged FINAL)
    /// "today 14:30 · 12.3 MB" of the last backup; "" before the first.
    Q_PROPERTY(QString lastBackupText READ lastBackupText NOTIFY stateChanged FINAL)
    /// A restore did not finish (closed, killed): statusText says what to do; dismissInterrupted().
    Q_PROPERTY(bool interrupted READ interrupted NOTIFY stateChanged FINAL)
    /// [{ name, time, when, size, sizeText, foreign (another notebook's, or unknown), complete }],
    /// newest first.
    Q_PROPERTY(QVariantList backups READ backups NOTIFY backupsChanged FINAL)

public:
    explicit WorkspaceBackups(std::function<QDateTime()> clock, QObject *parent = nullptr);
    ~WorkspaceBackups() override;

    struct Attachment {
        QString notebookName;
        QString defaultFolder; ///< this notebook's folder of the device ("" : no backups)
        notebook::WorkerPool pool;
        /// The browser: where the temporary file is written (outside the storage's mounts) and the
        /// largest archive the storage takes.
        QString workFolder;
        qint64 maxBytes = 0;
    };
    /// A notebook was opened. Nothing runs before start().
    void attach(Vault *vault, const Attachment &attachment);
    /// The notebook closes: whatever runs is cancelled, what a restore did stays done.
    void detach();
    /// The notebook is ready: the scheduled checks begin.
    void start();
    /// The documentation notebook has no backups (AppController::finishOpening()).
    void setPaused(bool paused);
    /// Delays of the scheduled checks (tests; 30 s after ready and hourly by default).
    void setCheckTimings(int firstMs, int periodMs);
    /// A scheduled check now: creates a backup when one is due and something changed.
    Q_INVOKABLE void check();
    /// The app returned to the foreground (Android stops the timers in the background).
    void applicationActive();

    void setBrowser(bool browser);
    /// See the file comment.
    void setDeferUntilStored(bool defer) { m_deferUntilStored = defer; }
    /// The browser's mount of the backups could not be loaded: nothing written to it would be
    /// stored, whatever a later write-back reports. No backups or restores; a persistent status.
    void setStorageUnavailable(const QString &why);
    /// How a restore holds Nextcloud's sync: `hold(ready)` — no run starts from now on, `ready` once
    /// none is in flight —, `release` — one run follows. Without these nothing is held (tests).
    void setSyncControl(std::function<void(std::function<void()>)> hold, std::function<void()> release);
    /// Tests: called before each read of a file by a backup, on the writing thread.
    void setReadHook(std::function<void(const QString &, qint64)> hook) { m_readHook = std::move(hook); }
    /// Another language: the texts follow.
    void setLanguage(const QString &language);

    bool available() const;
    bool enabled() const;
    void setEnabled(bool on);
    QString interval() const;
    void setInterval(const QString &interval);
    int keep() const;
    void setKeep(int keep);
    QVariantList keepChoices() const;
    QString folder() const;
    bool usingDefaultFolder() const;
    bool browser() const { return m_browser; }
    bool canShare() const;
    bool canOpenFolder() const;
    bool running() const { return m_op != Op::None; }
    QString phase() const;
    qreal progress() const;
    QString status() const;
    QString statusText() const;
    QString lastBackupText() const;
    bool interrupted() const;
    QVariantList backups() const { return m_backups; }

    /// A backup now, also when nothing changed. False (with a message) when it cannot start.
    Q_INVOKABLE bool createNow();
    /// Stop what runs (a restore: what was done stays done).
    Q_INVOKABLE void cancel();
    /// Use a folder the user picked: a local URL or an Android document naming a file. Refused with a
    /// message: inside the notebook, a document that names no file, a folder that takes no file.
    Q_INVOKABLE void chooseFolder(const QUrl &folder);
    Q_INVOKABLE void useDefaultFolder();
    /// Read the folder again (the view opens); backupsChanged follows.
    Q_INVOKABLE void refresh();
    /// Delete one of the listed backups (by its file name; nothing else in the folder). A foreign
    /// one only with `allowForeign`, after the user confirmed exactly that.
    Q_INVOKABLE bool remove(const QString &name, bool allowForeign = false);
    /// Check a listed backup against the notebook; restorePlanReady() (or a message) follows. A
    /// foreign one only with `allowForeign`, after the user confirmed exactly that.
    Q_INVOKABLE bool prepareRestore(const QString &name, bool allowForeign = false);
    /// Apply the plan restorePlanReady() described: the safety backup first.
    Q_INVOKABLE void confirmRestore();
    Q_INVOKABLE void dismissRestore();
    /// The user has read about the interrupted restore; its safety backup may be pruned again.
    Q_INVOKABLE void dismissInterrupted();
    /// The browser: the backup's bytes to the save picker or a download. Android: the share sheet.
    Q_INVOKABLE bool exportBackup(const QString &name);
    /// A desktop: the folder in the file manager.
    Q_INVOKABLE void openFolder();

    /// The browser's write-back finished with nothing refused (main.cpp).
    void storageFlushed();
    /// The browser's storage refused the backups' mount (quota; main.cpp): the new copy goes.
    void storageRefused(const QString &why);

Q_SIGNALS:
    void stateChanged();
    void progressChanged();
    void backupsChanged();
    /// { name, when, changed, recreated, trashed, identical, tooLarge, hasChanges, incomplete,
    /// foreign }
    void restorePlanReady(const QVariantMap &plan);
    /// A message for a toast (translated); kind "info" | "success" | "warning" | "error".
    void messageRequested(const QString &text, const QString &kind);
    /// A backup was written (tests, the event log has it too).
    void backupWritten(const QString &name);
    /// Whatever ran has finished (tests).
    void idle();
    /// Before a restore's safety backup: editors write what they hold, so the backup has it.
    void saveOpenEditors();
    /// `path` (a file or a folder) goes to the trash in a restore: an editor drops its edits.
    void aboutToRemove(const QString &path);

private:
    enum class Op { None, Backup, Check, WaitForSync, Safety, Restore };
    enum class Failure {
        None,
        FolderMissing,
        FolderUnwritable,
        WriteFailed,
        TooLarge,
        StorageRefused,
        NothingArchived,
        NotebookUnavailable, ///< the notebook's storage went during the run
        InsideNotebook,
    };

    void setOp(Op op);
    void startWrite(Op op);
    void onWritten(const backup::WriteResult &result);
    void afterWrite(const backup::WriteResult &result);
    void onChecked(const backup::RestoreCheck &check);
    void startApply();
    void holdSyncThenSafetyBackup();
    void releaseSync();
    /// False (with a message and a status) when nothing may be written now.
    bool canWrite();
    bool isForeign(const QString &name, bool *listed = nullptr) const;
    void onApplied(const backup::RestoreResult &result);
    void abortRestore(const QString &message);
    void setFailure(Failure failure, const QString &detail);
    void clearFailure(int skipped);
    /// Delete the oldest beyond the limit, then read the folder. `pruneComplete` false: the new
    /// backup left files out, so no complete one goes.
    void pruneThenRefresh(bool pruneComplete);
    QString lastFile() const;
    bool notebookStorageOk() const;
    /// Run `work` off the GUI thread (`done` on it afterwards), or on a later turn without threads.
    void inBackground(std::function<void()> work, std::function<void()> done);
    QString failureMessage(Failure failure) const;

    std::function<QDateTime()> m_clock;
    Vault *m_vault = nullptr;
    Attachment m_attachment;
    bool m_browser = false;
    bool m_paused = false;
    bool m_started = false;
    bool m_deferUntilStored = false;
    QString m_language = QStringLiteral("pl");
    Op m_op = Op::None;
    std::unique_ptr<backup::BackupWriter> m_writer;
    std::unique_ptr<backup::RestoreChecker> m_checker;
    std::unique_ptr<backup::RestoreApplier> m_applier;
    std::unique_ptr<backup::FolderWork> m_work;
    QTimer m_firstCheck;
    QTimer m_periodicCheck;
    QTimer m_progressTimer;
    QTimer m_storedTimeout;
    QVariantList m_backups;
    int m_listGeneration = 0;
    // A run in progress.
    QDateTime m_runTime;
    QString m_runFingerprint;
    bool m_runAutomatic = false;
    // A restore in progress.
    QString m_restoreName;
    backup::RestorePlan m_plan;
    bool m_planReady = false;
    bool m_waitingForStored = false; ///< its safety backup must reach the browser's storage first
    QString m_safetyName;            ///< its safety backup ("" : the notebook was empty)
    std::function<void(std::function<void()>)> m_holdSync;
    std::function<void()> m_releaseSync;
    bool m_syncHeld = false;
    int m_holdGeneration = 0; ///< a hold's `ready` that comes after its restore ended is dropped
    QString m_storageUnavailable;
    bool m_unconfirmedComplete = true;
    std::function<void(const QString &, qint64)> m_readHook;
    QString m_unconfirmed;              ///< the newest backup, not known to be in the browser's storage
    QJsonValue m_lastBeforeUnconfirmed; ///< what the state said before it
    /// Failures already told in a toast this session (the settings' status always says them).
    QSet<int> m_toldFailures;
};

} // namespace brulion::shell

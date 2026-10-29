#pragma once

// WebAssembly only (compiled when EMSCRIPTEN is set, see src/platform/CMakeLists.txt).

#include <QObject>
#include <QString>
#include <QStringList>

namespace brulion::platform {

struct WasmStorageJs;

/// Persistent browser storage for the WebAssembly build.
///
/// Emscripten's file system lives in memory. WasmStorage mounts Emscripten's IDBFS (a file
/// system mirrored to IndexedDB) on a few directories — by default the vault root
/// (`vaultRoot()`) and the app data directory — and keeps them in sync:
///
/// * `start()` mounts the directories and loads their content from IndexedDB. Loading is
///   asynchronous: open the vault and read app data only after `ready()`. Files written to a
///   mounted directory before `ready()` are lost (the loaded state replaces them).
/// * Every change under a mounted directory (file closed after writing, rename, delete, mkdir,
///   utime, …) is noticed at the Emscripten FS layer and schedules a debounced write-back
///   (`FS.syncfs(false)`), so callers normally need not do anything. `scheduleFlush()` and
///   `flushNow()` exist for explicit control.
/// * Pending changes are also written back immediately when the page is hidden or closed
///   (`visibilitychange`, `pagehide`); `beforeunload` asks the user to stay while a write-back
///   is still running.
/// * Write-back is never started before the initial load succeeded — an empty in-memory tree
///   would otherwise delete the stored notes. If IndexedDB is unavailable (some private modes,
///   blocked site data), `ready()` is still emitted, `isBackedByIndexedDb()` is false and the
///   files live only in memory for this page session.
///
/// * Cache mounts (`cacheMountPoints()`, by default `indexCacheRoot()`) hold files the app can
///   rebuild (the index cache). Each mount is its own IndexedDB database written back in its own
///   transaction, so a cache the browser's quota cannot take never takes History, sync state or
///   settings down with it: a cache mount that fails is no storage error (no `error()`, nothing
///   left pending); its files are deleted from memory — the next write-back removes the stored
///   copies too, freeing their space — and `cacheNotSaved()` tells the app to stop writing them.
///
/// One instance per process (the mounts are global). The JavaScript side keeps running after a
/// crash of the WebAssembly instance, so the HTML shell can still save the in-memory files.
class WasmStorage : public QObject
{
    Q_OBJECT

public:
    explicit WasmStorage(QObject *parent = nullptr);
    ~WasmStorage() override;

    /// The vault folder of the browser build: `$HOME/Brulion` (`/home/web_user/Brulion`).
    static QString vaultRoot();
    /// `vaultRoot()` and the writable `QStandardPaths::AppDataLocation` (history, sync state,
    /// local settings). Call after the application name/organisation are set.
    static QStringList defaultMountPoints();
    /// The folder of the index caches (`<QStandardPaths::CacheLocation>/index`), a cache mount.
    static QString indexCacheRoot();
    /// `indexCacheRoot()`.
    static QStringList defaultCacheMountPoints();
    /// Absolute, cleaned, de-duplicated paths without nesting (a path inside another one is
    /// dropped — IDBFS mounts must not nest). "/" and relative paths are rejected.
    static QStringList normalizeMountPoints(const QStringList &paths);

    /// Mounts IDBFS on `mountPoints` and the cache mounts `cacheMountPoints` (a cache mount
    /// nested in another mount is dropped) and starts loading them from IndexedDB; emits
    /// `ready()` once `mountPoints` are loaded (a cache mount that cannot load stays empty).
    /// Only the first call has an effect.
    void start(const QStringList &mountPoints = defaultMountPoints(),
               const QStringList &cacheMountPoints = defaultCacheMountPoints());

    bool isStarted() const { return m_started; }
    /// The initial load has finished (successfully or not); `ready()` was emitted.
    bool isReady() const { return m_ready; }
    /// Changes are being written to IndexedDB (false before `ready()` and when loading failed).
    bool isBackedByIndexedDb() const { return m_backed; }
    QStringList mountPoints() const { return m_mountPoints; }
    QStringList cacheMountPoints() const { return m_cacheMountPoints; }
    QString lastError() const { return m_lastError; }

    /// `navigator.storage.estimate()` of this site, asked when loading started (bytes; -1 until
    /// the browser answered or where it cannot tell). Process-wide, for the index cache's budget.
    struct Estimate {
        qint64 usage = -1;
        qint64 quota = -1;
    };
    static Estimate estimate();

    /// Changes not yet written to IndexedDB (dirty, debounce pending or write-back running).
    Q_INVOKABLE bool hasPendingChanges() const;

    /// Debounce delay after the last change before writing back (default 400 ms).
    int flushDelay() const { return m_flushDelay; }
    void setFlushDelay(int ms);
    /// Upper bound for postponing a write-back while changes keep coming (default 3000 ms).
    int maxFlushDelay() const { return m_maxFlushDelay; }
    void setMaxFlushDelay(int ms);

public Q_SLOTS:
    /// Marks the mounted directories as changed and writes them back after `flushDelay()`.
    void scheduleFlush();
    /// Writes pending changes back now (queued behind a running write-back).
    void flushNow();
    /// Asks the browser to exempt this site's storage from eviction under storage pressure
    /// (`navigator.storage.persist()`); the answer arrives as `persistentStorageResult()`.
    /// Firefox shows a permission prompt — call it from a user action.
    void requestPersistentStorage();

Q_SIGNALS:
    /// The initial load from IndexedDB has finished (also on failure: see `lastError()`).
    void ready();
    /// A write-back to IndexedDB has finished.
    void flushed(bool ok);
    /// Loading or saving failed (technical message, e.g. a quota error).
    void error(const QString &message);
    /// The cache mount `mountPoint` could not be written back (`message`, e.g. a quota error):
    /// its files were deleted; do not write new ones in this session.
    void cacheNotSaved(const QString &mountPoint, const QString &message);
    void persistentStorageResult(bool persisted);

private:
    friend struct WasmStorageJs;
    void handleJsEvent(int op, bool ok, const QString &message);
    void pushDelays();
    void dropCacheFiles(const QString &mountPoint);

    QStringList m_mountPoints;
    QStringList m_cacheMountPoints;
    QString m_lastError;
    int m_flushDelay = 400;
    int m_maxFlushDelay = 3000;
    bool m_started = false;
    bool m_ready = false;
    bool m_backed = false;
};

} // namespace brulion::platform

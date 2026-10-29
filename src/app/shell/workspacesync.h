// WorkspaceSync — sync of the open vault (QML: `Workspace.sync`): Nextcloud/WebDAV through the
// core's WebDavClient + SyncEngine + SyncScheduler over a NetworkHttpTransport, the local
// Syncthing daemon's status, and the conflict copies both leave in the vault.
//
//   Nextcloud  set up from the app settings (setNextcloud(); the shell's integration passes
//              AppSettings' server, user, folder, interval and the app password from its
//              owner-only file). With a vault attached and complete settings the scheduler
//              syncs at once, every intervalMinutes (0: about once a day), ~20 s after edits,
//              when the network comes back and when the app returns to the foreground.
//              Mass deletions wait for confirmDeletions() (needsConfirmation, pendingDeletions).
//   Syncthing  status of the Syncthing folder holding the vault (REST API with an API key from
//              the settings, else — on the desktop — read from Syncthing's own config.xml),
//              polled every minute while the app is in the foreground.
//   Conflicts  Syncthing ".sync-conflict-" and Brulion ".conflict-" copies (sync::findConflicts),
//              kept up to date on vault changes; resolveConflict() writes the merged text into
//              the original (its old text stays in History, also within History's throttle) and
//              moves the copy to the trash; resolveConflictBytes() the same with bytes (a merged
//              drawing).
//   Secrets    the stored app password is only ever sent to the server and user it was saved
//              for (sameNextcloudAccount()); nothing here logs URLs with credentials or keys.
//
// QML values (all plain data):
//   state        "disabled" | "idle" | "syncing" | "error" | "offline" (not set up: "disabled")
//   statusText   one translated line for the status bar ("Zsynchronizowano o 10:32", …)
//   lastReport   { uploaded, downloaded, merged, deletedLocal, deletedRemote, foldersCreated,
//                  foldersDeleted, foldersMoved, foldersMovedHere, filesMoved, filesMovedHere,
//                  conflicts, conflictPaths,
//                  errors: [{ path, message }], skipped, changes, started, finished, cancelled,
//                  failed, failure }
//   pendingDeletions  [{ path, action: "delete-local" | "delete-remote" }]
//   syncthing    { enabled, folder (the vault has a .stfolder), state, completion, needBytes,
//                  globalBytes, connected (-1 unknown), errors, folderId, folderLabel, error,
//                  text }
//   conflicts    [{ original, copy, title, time ("YYYY-MM-DDTHH:mm:ss"), when, device,
//                  source: "syncthing" | "brulion", text (a text file that can be merged),
//                  drawing (the copy or the original is a drawing — an SVG with an Excalidraw
//                  scene, `*.excalidraw.md`, `.excalidraw`: merged by its elements, never by
//                  lines) }]
//   live         the running sync, updated at most 4 times a second (and once a second for the
//                elapsed time) — {} when idle (workspacesyncruns.cpp):
//                { running, number, trigger, triggerText, phase ("connecting" | "listing" |
//                  "listing-local" | "comparing" | "syncing" | "saving" | "done"), phaseText,
//                  detailText, done, total, progress (0…1, -1 = unknown), active: [titles],
//                  activeText, elapsedMs, elapsedText, bytesText, changes, errors, stalled,
//                  stallText, waitingFor: [..], abandoning }
//   history      the last runs, newest first (SyncHistory in <device state>/sync-history.json):
//                [{ number, started, startedText, durationMs, durationText, trigger, triggerText,
//                   outcome, outcomeText, tone ("success" | "warning" | "danger" | "info"),
//                   summary, errorCode, errorText, needsUser, repeats, quiet }]
//   runDetails(n) one run for the "Szczegóły" dialog (phases, requests, errors, conflicts,
//                counts with the folders created, deleted and moved)
#pragma once

#include "types.h"

#include <QDateTime>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class LocalStore;
class Vault;
}

namespace brulion::sync {
class HttpTransport;
class NetworkHttpTransport;
class SyncEngine;
class SyncScheduler;
class SyncHistory;
class SyncthingClient;
class WebDavClient;
struct SyncReport;
struct SyncRunRecord;
} // namespace brulion::sync

namespace brulion::shell {

/// Nextcloud account and schedule (from AppSettings).
struct NextcloudSettings {
    bool enabled = false;
    QString url;
    QString user;
    QString password;
    QString folder;          ///< relative to the user's files; empty = "Brulion"
    int intervalMinutes = 15; ///< 0 = after edits and about once a day only
    /// Another notebook on this device whose sync is on with this server folder, one inside it or
    /// around it (AppSettings::nextcloudFolderUser(); 0.3.1–0.3.5 could set that up): its path.
    /// Nothing syncs then, neither by itself nor by "Sync now" — the two notebooks' notes would
    /// mix in the folder — until one of them gets another folder or its sync is turned off.
    QString sharedWith;

    /// Enabled, with a server, a user and a password.
    bool isComplete() const;
    friend bool operator==(const NextcloudSettings &, const NextcloudSettings &) = default;
};

/// Syncthing REST access (from AppSettings "sync/syncthingApiKey" / "sync/syncthingUrl").
struct SyncthingSettings {
    QString apiKey; ///< empty: discovered from Syncthing's config.xml on the desktop
    QString url;    ///< empty: http://127.0.0.1:8384
    friend bool operator==(const SyncthingSettings &, const SyncthingSettings &) = default;
};

/// Both describe the same Nextcloud account: the same user on the same server (scheme, host,
/// port and path compared; "https://" is assumed without a scheme). A stored app password is only
/// ever sent to the account it was saved for.
bool sameNextcloudAccount(const QString &url, const QString &user, const QString &otherUrl, const QString &otherUser);

/// What the sync bookkeeping of a notebook (its LocalStore's "sync/meta" "remote", the
/// WebDavClient's identity) records after syncing with this account and server folder:
/// "<user>@<WebDAV address of the folder>"; "" without a server or user.
QString nextcloudRemote(const QString &url, const QString &user, const QString &folder);
/// The server folder such a record names on this account ("" when it names another account).
QString nextcloudRemoteFolder(const QString &url, const QString &user, const QString &remote);

/// The GUI address and API key of a Syncthing config.xml; std::nullopt without an API key.
/// "0.0.0.0:8384" / "[::]:8384" mean this machine; tls="true" gives https.
std::optional<SyncthingSettings> parseSyncthingConfig(const QByteArray &xml);

/// Syncthing's config.xml of this user (desktop: $XDG_STATE_HOME or ~/.local/state, then
/// $XDG_CONFIG_HOME or ~/.config), parsed; std::nullopt when there is none (or on Android/web).
std::optional<SyncthingSettings> discoverSyncthingConfig();

class WorkspaceSync : public QObject {
    Q_OBJECT

    // ---- Nextcloud ----------------------------------------------------------------------
    /// The settings are complete (enabled, server, user, password).
    Q_PROPERTY(bool configured READ configured NOTIFY configurationChanged FINAL)
    /// A sync engine runs for the open vault ("Sync now" works).
    Q_PROPERTY(bool active READ active NOTIFY configurationChanged FINAL)
    /// WebDAV address the settings lead to ("" when incomplete).
    Q_PROPERTY(QString davUrl READ currentDavUrl NOTIFY configurationChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged FINAL)
    /// Finish of the last successful sync, "YYYY-MM-DDTHH:mm:ss" ("" before the first).
    Q_PROPERTY(QString lastSync READ lastSync NOTIFY statusChanged FINAL)
    /// Human form of lastSync ("dziś 10:32").
    Q_PROPERTY(QString lastSyncText READ lastSyncText NOTIFY statusChanged FINAL)
    /// Translated reason of the last failure ("" after a clean run) and its code ("auth", …).
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY statusChanged FINAL)
    /// Progress of the running sync 0…1, -1 when not syncing or unknown.
    Q_PROPERTY(qreal progress READ progress NOTIFY statusChanged FINAL)
    /// Planned next automatic sync "YYYY-MM-DDTHH:mm:ss" ("" when none).
    Q_PROPERTY(QString nextSync READ nextSync NOTIFY statusChanged FINAL)
    Q_PROPERTY(QVariantMap lastReport READ lastReport NOTIFY statusChanged FINAL)
    /// The mass-deletion guard held deletions back: confirmDeletions() applies them.
    Q_PROPERTY(bool needsConfirmation READ needsConfirmation NOTIFY statusChanged FINAL)
    Q_PROPERTY(QVariantList pendingDeletions READ pendingDeletions NOTIFY statusChanged FINAL)
    /// testConnection() is running.
    Q_PROPERTY(bool testing READ testing NOTIFY testingChanged FINAL)
    /// Name of this device in conflict copies.
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY configurationChanged FINAL)
    /// The notebook (its folder's name) that syncs with this notebook's server folder too, while
    /// that stops the sync (NextcloudSettings::sharedWith); "" otherwise.
    Q_PROPERTY(QString folderSharedWith READ folderSharedWith NOTIFY configurationChanged FINAL)

    /// The running sync, throttled (see the top of this file); {} when idle.
    Q_PROPERTY(QVariantMap live READ live NOTIFY liveChanged FINAL)
    /// The last runs, newest first (see the top of this file).
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged FINAL)
    /// The newest history row ({} before the first run).
    Q_PROPERTY(QVariantMap lastRun READ lastRun NOTIFY historyChanged FINAL)
    /// Epoch ms of the planned next attempt (a QML countdown), 0 = none.
    Q_PROPERTY(qint64 nextSyncMs READ nextSyncMs NOTIFY statusChanged FINAL)
    /// The next attempt is a retry after failed runs (back-off).
    Q_PROPERTY(bool retrying READ retrying NOTIFY statusChanged FINAL)

    // ---- Syncthing ----------------------------------------------------------------------
    Q_PROPERTY(QVariantMap syncthing READ syncthingStatus NOTIFY syncthingChanged FINAL)

    // ---- Conflicts ----------------------------------------------------------------------
    Q_PROPERTY(QVariantList conflicts READ conflicts NOTIFY conflictsChanged FINAL)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY conflictsChanged FINAL)

public:
    static constexpr int SyncthingPollMs = 60 * 1000;
    /// Automatic syncing starts this long after the settings last changed (typing a server
    /// address must not start a sync per keystroke).
    static constexpr int StartDelayMs = 1500;

    explicit WorkspaceSync(QObject *parent = nullptr);
    ~WorkspaceSync() override;

    // ---- wiring -------------------------------------------------------------------------
    /// HTTP for WebDAV and Syncthing (tests: a fake server); default: a NetworkHttpTransport.
    /// Not owned; set before configuring.
    void setTransport(sync::HttpTransport *transport);
    /// Name of this device in conflict copies (default: the host name).
    void setDeviceName(const QString &name);
    QString deviceName() const;
    /// Clock for the engine's reports and conflict names (default: current local time).
    void setClock(std::function<QDateTime()> clock);
    /// "pl" | "en" for statusText and human times.
    void setLanguage(const QString &language);
    /// Automatic syncing (default on). Off: only syncNow() runs (tests).
    void setAutomatic(bool on);
    /// Online check instead of the system's network information (tests).
    void setOnlineCheck(std::function<bool()> isOnline);
    /// Discover Syncthing's API key from its config.xml (default on; tests turn it off).
    void setSyncthingDiscovery(bool on);

    void setNextcloud(const NextcloudSettings &settings);
    NextcloudSettings nextcloud() const { return m_nextcloud; }
    void setSyncthing(const SyncthingSettings &settings);

    void attach(Vault *vault);
    void detach();
    /// Foreground (true) / background (false), from the application state.
    void setAppActive(bool active);
    /// The user changed folders (created, deleted, renamed): no Vault change tells the scheduler,
    /// so sync after the usual debounce, as after an edit.
    void notifyLocalChange();

    sync::SyncEngine *engine() const { return m_engine.get(); }
    sync::SyncScheduler *scheduler() const { return m_scheduler.get(); }

    // ---- state --------------------------------------------------------------------------
    bool configured() const { return m_nextcloud.isComplete(); }
    bool active() const { return m_scheduler != nullptr; }
    /// Complete settings whose server folder another notebook syncs with: nothing syncs.
    bool folderShared() const { return m_nextcloud.isComplete() && !m_nextcloud.sharedWith.isEmpty(); }
    QString folderSharedWith() const;
    QString currentDavUrl() const;
    QString state() const;
    QString statusText() const;
    QString lastSync() const;
    QString lastSyncText() const;
    QString lastError() const;
    QString lastErrorCode() const;
    qreal progress() const;
    QString nextSync() const;
    QVariantMap lastReport() const;
    bool needsConfirmation() const;
    QVariantList pendingDeletions() const;
    bool testing() const { return m_testing > 0; }
    QVariantMap syncthingStatus() const { return m_syncthingStatus; }
    QVariantList conflicts() const { return m_conflicts; }
    int conflictCount() const { return int(m_conflicts.size()); }
    /// { state, text, lastSync } for AppController.syncStatus.
    QVariantMap appStatus() const;
    QVariantMap live() const { return m_live; }
    QVariantList history() const;
    QVariantMap lastRun() const;
    qint64 nextSyncMs() const;
    bool retrying() const;
    sync::SyncHistory *syncHistory() const { return m_history.get(); }
    /// The newest `max` runs as one line each for the exported log's header
    /// ("#43 2026-10-18 14:03 · zaplanowana · bez zmian · 1,1 s").
    QStringList historyLines(int max = 5) const;
    /// Translated texts (also used by the tests): a trigger, a phase, a per-file error code.
    QString triggerText(const QString &trigger) const;
    QString fileErrorText(const QString &code, int status = 0) const;

    // ---- actions ------------------------------------------------------------------------
    /// Sync now (also between automatic runs; joins a running sync).
    Q_INVOKABLE void syncNow();
    /// Sync now applying the deletions the mass-deletion guard held back. `expectedPaths`: the
    /// pendingDeletions paths the user confirmed — when the list has changed meanwhile nothing
    /// is confirmed (errorOccurred; the guard shows the new list). Empty: no check.
    Q_INVOKABLE void confirmDeletions(const QStringList &expectedPaths = {});
    /// Stop a running sync between two files (what is done stays synced; the rest follows with
    /// the next run).
    Q_INVOKABLE void cancelSync();
    /// Check a server, user, app password and folder without saving them (an empty password
    /// uses the stored one, but only for the saved server and user). Answers with
    /// connectionTested().
    Q_INVOKABLE void testConnection(const QString &url, const QString &user, const QString &password,
                                    const QString &folder);
    /// The WebDAV address for these settings ("" when the server or user is missing).
    Q_INVOKABLE QString davUrlFor(const QString &url, const QString &user, const QString &folder) const;
    /// Ask the Syncthing daemon now.
    Q_INVOKABLE void refreshSyncthing();
    /// Texts of a conflict for BMergeView: { original, copy, title, ours (the original's text),
    /// theirs (the copy's), device, when, text (both are text files) }; {} when unknown.
    Q_INVOKABLE QVariantMap conflictDetails(const QString &copy);
    /// Write `mergedText` into `original` and move the conflict copy `copy` to the trash.
    Q_INVOKABLE bool resolveConflict(const QString &original, const QString &copy, const QString &mergedText);
    /// Write `bytes` into `original` (what they replace kept in History, unthrottled; nothing
    /// written when they are there already) and move the conflict copy `copy` to the trash — a
    /// merged drawing, the copy's drawing; `removeCopy` false: the copy stays (a merged drawing
    /// whose copy has notes still to carry over). False (errorOccurred()) when either step fails.
    bool resolveConflictBytes(const QString &original, const QString &copy, const QByteArray &bytes,
                              bool removeCopy = true);
    /// Keep the original as it is: move the conflict copy to the trash.
    Q_INVOKABLE bool discardConflictCopy(const QString &copy);

    /// One run for the details dialog: the history fields plus phases [{ id, text, durationText,
    /// share (0…1 of the run), items, detailText }], stats { requests, requestsText, averageText,
    /// bytesText, protocol, slowest: [{ method, title, path, status, msText }] }, errors
    /// [{ path, title, code, text (translated), message (technical) }], conflicts [{ path,
    /// title }], fromMs / toMs (the run's time window for the event log); {} when unknown.
    Q_INVOKABLE QVariantMap runDetails(int number) const;
    /// The same as plain text ("Kopiuj szczegóły"): translated labels, technical values.
    Q_INVOKABLE QString runDetailsText(int number) const;
    /// "Foldery: 2 nowe, 1 usunięty, 1 przeniesiony na serwerze, 1 przeniesiony tutaj" (the
    /// counts > 0 only; "" when none). `moved`: renamed here, moved on the server; `movedHere`:
    /// renamed on the server (or another device), renamed here too.
    Q_INVOKABLE QString folderCountsText(int created, int deleted, int moved, int movedHere = 0) const;
    /// "Pliki: 1 przeniesiony na serwerze, 2 przeniesione tutaj" — the pages and files renamed or
    /// moved (the counts > 0 only; "" when none).
    Q_INVOKABLE QString fileMovesText(int moved, int movedHere) const;
    /// Forget the kept runs.
    Q_INVOKABLE void clearHistory();

Q_SIGNALS:
    void configurationChanged();
    void statusChanged();
    void syncthingChanged();
    void conflictsChanged();
    void testingChanged();
    /// testConnection() result: ok, or a translated reason.
    void connectionTested(bool ok, const QString &message);
    /// A sync created new conflict copies (count of conflicts of that run).
    void conflictsFound(int count);
    /// A sync created or removed folders in the vault (folders produce no Vault change of their
    /// own): the tree and the lists refresh.
    void foldersChanged(const QStringList &paths);
    /// A sync renamed a folder of the vault as it had been renamed on the server (or on another
    /// device): its pages followed with their own Vault changes, the folder tree and the routes
    /// showing the folder follow this (Workspace::folderRenamed).
    void folderMovedHere(const QString &from, const QString &to);
    /// Something failed that the user should know about (translated).
    void errorOccurred(const QString &message);
    /// The open notebook's server folder is another notebook's too, so it does not sync (a
    /// warning, translated): once per notebook opened and folder.
    void folderSharingFound(const QString &message);
    void liveChanged();
    void historyChanged();

private:
    sync::HttpTransport *transport();
    void rebuild();
    void teardown();
    void rebuildSyncthing();
    void refreshConflicts();
    void onVaultChanged(const QList<brulion::VaultChange> &changes);
    void onSynced(const sync::SyncReport &report);
    QString errorText(const QString &code, const QString &fallback) const;
    QString human(const QDateTime &time) const;
    // workspacesyncruns.cpp: live progress, history, run details.
    void scheduleLive();
    void updateLive();
    void onRunFinished(const sync::SyncReport &report, bool stalled);
    void openHistory();
    QVariantMap historyRow(const sync::SyncRunRecord &record) const;
    QString phaseText(const QString &phase) const;
    QString outcomeText(const sync::SyncRunRecord &record) const;
    QString shortErrorText(const QString &code) const;
    QString durationText(qint64 ms) const;
    QString bytesText(qint64 bytes) const;
    QDateTime now() const;
    QString effectiveDeviceName() const;
    /// Why nothing syncs while the folder is shared (translated, names the other notebook).
    QString sharedFolderText() const;
    std::optional<QVariantMap> conflictFor(const QString &copy) const;

    QPointer<Vault> m_vault;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::NetworkHttpTransport> m_ownTransport;
    std::unique_ptr<sync::WebDavClient> m_client;
    std::unique_ptr<sync::SyncEngine> m_engine;
    std::unique_ptr<sync::SyncScheduler> m_scheduler;
    std::unique_ptr<sync::SyncthingClient> m_syncthing;
    QMetaObject::Connection m_vaultConnection;
    NextcloudSettings m_nextcloud;
    SyncthingSettings m_syncthingSettings;
    QString m_deviceName;
    QString m_language = QStringLiteral("pl");
    std::function<QDateTime()> m_clock;
    std::function<bool()> m_onlineCheck;
    bool m_automatic = true;
    bool m_discovery = true;
    bool m_appActive = true;
    int m_testing = 0;
    QVariantMap m_syncthingStatus;
    QString m_syncthingFolderId;
    QVariantList m_conflicts;
    QTimer m_syncthingTimer;
    QTimer m_startTimer;
    int m_syncthingGeneration = 0;
    std::unique_ptr<LocalStore> m_historyStore;
    std::unique_ptr<sync::SyncHistory> m_history;
    QVariantMap m_live;
    QString m_lastStatusText;
    QString m_sharedNotice; ///< the shared folder last warned about (folderSharingFound())
    QTimer m_liveTimer; ///< coalesces progress into ≤ 4 updates per second
    QTimer m_liveTick;  ///< once a second while syncing (elapsed time, stall countdown)
};

} // namespace brulion::shell

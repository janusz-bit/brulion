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
//              the original (its old text stays in History) and moves the copy to the trash.
//
// QML values (all plain data):
//   state        "disabled" | "idle" | "syncing" | "error" | "offline" (not set up: "disabled")
//   statusText   one translated line for the status bar ("Zsynchronizowano o 10:32", …)
//   lastReport   { uploaded, downloaded, merged, deletedLocal, deletedRemote, conflicts,
//                  conflictPaths, errors: [{ path, message }], skipped, changes, started,
//                  finished, cancelled, failed, failure }
//   pendingDeletions  [{ path, action: "delete-local" | "delete-remote" }]
//   syncthing    { enabled, folder (the vault has a .stfolder), state, completion, needBytes,
//                  globalBytes, connected (-1 unknown), errors, folderId, folderLabel, error,
//                  text }
//   conflicts    [{ original, copy, title, time ("YYYY-MM-DDTHH:mm:ss"), when, device,
//                  source: "syncthing" | "brulion", text (a text file that can be merged) }]
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
class Vault;
}

namespace brulion::sync {
class HttpTransport;
class NetworkHttpTransport;
class SyncEngine;
class SyncScheduler;
class SyncthingClient;
class WebDavClient;
struct SyncReport;
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

    sync::SyncEngine *engine() const { return m_engine.get(); }
    sync::SyncScheduler *scheduler() const { return m_scheduler.get(); }

    // ---- state --------------------------------------------------------------------------
    bool configured() const { return m_nextcloud.isComplete(); }
    bool active() const { return m_scheduler != nullptr; }
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

    // ---- actions ------------------------------------------------------------------------
    /// Sync now (also between automatic runs; joins a running sync).
    Q_INVOKABLE void syncNow();
    /// Sync now applying the deletions the mass-deletion guard held back.
    Q_INVOKABLE void confirmDeletions();
    /// Check a server, user, app password and folder without saving them (an empty password
    /// uses the stored one). Answers with connectionTested().
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
    /// Keep the original as it is: move the conflict copy to the trash.
    Q_INVOKABLE bool discardConflictCopy(const QString &copy);

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
    /// Something failed that the user should know about (translated).
    void errorOccurred(const QString &message);

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
    QString effectiveDeviceName() const;
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
};

} // namespace brulion::shell

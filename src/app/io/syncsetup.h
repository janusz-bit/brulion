// Sync settings forms (the Nextcloud and Syncthing parts of SyncView and of the settings'
// "Synchronizacja" section). Both edit drafts and write AppSettings only on save(), so typing a
// server address never starts syncs halfway; the shell's integration (workspaceintegration.cpp)
// applies saved settings to Workspace.sync.
//
// NextcloudSetup (QML: `NextcloudSetup { settings: AppSettings }`)
//   drafts      enabled, url, user, password ("" keeps the stored app password), folder (the
//               notebook's; for one without: "Notatki" for a new account, else the notebook's
//               name — AppSettings::proposedNextcloudFolder()), interval (minutes, 0 = after
//               changes only), deviceName (names conflict copies)
//   validation  urlError / userError / passwordError / folderError (shown once `showErrors`;
//               a folder another notebook on this device syncs with at once, and saving is
//               refused while sync is on), urlWarning (plain http), davUrl (the WebDAV address the
//               form leads to)
//   account     the account is shared by the notebooks: signing out, or saving another server or
//               user, switches sync off in every other notebook
//   test        testConnection() with the drafts — its own WebDavClient, nothing saved — gives
//               testResult { ok, code, title, message, hint, details, canCreateFolder,
//               securityUrl }: 401 → how to create an app password (Nextcloud: Settings →
//               Security), 404 → the account works but the folder is missing (createFolder()), or
//               the user does not exist; TLS / DNS / refused / timeout explained; in the browser
//               a network failure is explained as CORS with the self-hosting options (docs/WEB.md)
//
// SyncthingSetup (QML: `SyncthingSetup { settings: AppSettings; workspace: Workspace }`)
//   apiKey, url drafts for the REST status ("sync/syncthingApiKey", "sync/syncthingUrl"), the
//   recommended .stignore (sync::stignoreTemplate()) — stignoreState "missing" | "current" |
//   "outdated" | "unavailable", writeStignore() creates it or appends the missing rules after the
//   user's own (their order and meaning stay).
#pragma once

#include "appsettings.h"
#include "workspace.h"

#include "sync/errors.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace brulion::sync {
class HttpTransport;
class NetworkHttpTransport;
} // namespace brulion::sync

namespace brulion::ioviews {

/// Error text for a server address ("" when usable): empty, spaces, other schemes, no host.
QString validateServerUrl(const QString &url);
/// The address would send the password unencrypted (http:// to a host that is not local).
bool isUnencryptedUrl(const QString &url);
/// Error text for a Nextcloud user name ("" when usable).
QString validateUserName(const QString &user);
/// Error text for the server folder ("" when usable; empty is refused — it would sync the whole
/// account).
QString validateServerFolder(const QString &folder);
/// "https://cloud.example.com/index.php/settings/user/security" for a server address (also a
/// pasted DAV or web UI address), "" when unusable.
QString nextcloudSecurityUrl(const QString &server);
/// A failed connection test explained: { ok: false, code, title, message, hint, details }.
/// `browser`: the app runs in a web browser (network failures are CORS there).
QVariantMap describeConnectionError(const sync::WebDavError &error, bool browser, const QString &server);

/// The .stignore of a vault compared with the recommendation: "missing", "current" (every
/// recommended rule is there) or "outdated".
QString stignoreState(const QString &existing, bool exists, const QString &recommended);
/// `existing` with the recommended rules it lacks appended (after the user's own rules, which keep
/// their order and precedence); the recommendation itself when `existing` is empty.
QString mergeStignore(const QString &existing, const QString &recommended);

class NextcloudSetup : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::shell::AppSettings *settings READ settings WRITE setSettings NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString user READ user WRITE setUser NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY draftChanged FINAL)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY draftChanged FINAL)
    /// An app password is stored (the draft may stay empty).
    Q_PROPERTY(bool hasStoredPassword READ hasStoredPassword NOTIFY settingsChanged FINAL)
    /// The stored app password is used for the draft: it is stored and the draft names the same
    /// server and user it was saved for. Another server or user needs the password typed again
    /// (the stored one is never sent to an address it was not saved for).
    Q_PROPERTY(bool storedPasswordApplies READ storedPasswordApplies NOTIFY draftChanged FINAL)
    /// The saved settings describe a complete, enabled account.
    Q_PROPERTY(bool configured READ configured NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged FINAL)
    Q_PROPERTY(bool showErrors READ showErrors WRITE setShowErrors NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString urlError READ urlError NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString urlWarning READ urlWarning NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString userError READ userError NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString passwordError READ passwordError NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString folderError READ folderError NOTIFY draftChanged FINAL)
    Q_PROPERTY(bool valid READ valid NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString davUrl READ davUrl NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString securityUrl READ securityUrl NOTIFY draftChanged FINAL)
    Q_PROPERTY(QVariantList intervalOptions READ intervalOptions CONSTANT FINAL)
    Q_PROPERTY(bool testing READ testing NOTIFY testingChanged FINAL)
    Q_PROPERTY(QVariantMap testResult READ testResult NOTIFY testResultChanged FINAL)
    /// Explanations for the browser (CORS); default: running as WebAssembly.
    Q_PROPERTY(bool browser READ browser WRITE setBrowser NOTIFY settingsChanged FINAL)

public:
    explicit NextcloudSetup(QObject *parent = nullptr);
    ~NextcloudSetup() override;

    shell::AppSettings *settings() const { return m_settings; }
    void setSettings(shell::AppSettings *settings);
    /// HTTP for the connection test (tests: a fake server). Not owned.
    void setTransport(sync::HttpTransport *transport) { m_transport = transport; }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString url() const { return m_url; }
    void setUrl(const QString &url);
    QString user() const { return m_user; }
    void setUser(const QString &user);
    QString password() const { return m_password; }
    void setPassword(const QString &password);
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    int interval() const { return m_interval; }
    void setInterval(int minutes);
    QString deviceName() const { return m_deviceName; }
    void setDeviceName(const QString &name);
    bool hasStoredPassword() const;
    bool storedPasswordApplies() const;
    bool configured() const;
    bool dirty() const;
    bool showErrors() const { return m_showErrors; }
    void setShowErrors(bool on);
    QString urlError() const;
    QString urlWarning() const;
    QString userError() const;
    QString passwordError() const;
    QString folderError() const;
    bool valid() const;
    QString davUrl() const;
    QString securityUrl() const;
    QVariantList intervalOptions() const;
    bool testing() const { return m_testing > 0; }
    QVariantMap testResult() const { return m_testResult; }
    bool browser() const { return m_browser; }
    void setBrowser(bool on);

    /// Drafts from the saved settings (drops unsaved edits and the test result).
    Q_INVOKABLE void reload();
    /// Validate (shows the errors) and write the settings; false when something is invalid.
    Q_INVOKABLE bool save();
    /// Check the drafts against the server (async: testResult).
    Q_INVOKABLE void testConnection();
    /// Create the missing server folder (after testResult.canCreateFolder).
    Q_INVOKABLE void createFolder();
    /// Stop syncing with this account: off in every notebook (they share it), the app password
    /// forgotten (address and user stay).
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void clearTestResult();

Q_SIGNALS:
    void settingsChanged();
    void draftChanged();
    void testingChanged();
    void testResultChanged();
    /// save() wrote the settings.
    void saved();

private:
    sync::HttpTransport *transport();
    QString effectivePassword() const;
    /// The drafts keep the saved account (no other server or user).
    bool sameAccount() const;
    /// Another notebook syncs with the drafted folder (while switched on, on the same account).
    QString folderConflict() const;
    void setTestResult(const QVariantMap &result);
    void finishTest(const QVariantMap &result);

    QPointer<shell::AppSettings> m_settings;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::NetworkHttpTransport> m_ownTransport;
    bool m_enabled = false;
    QString m_url;
    QString m_user;
    QString m_password;
    QString m_folder;
    int m_interval = 15;
    QString m_deviceName;
    bool m_showErrors = false;
    bool m_browser = false;
    int m_testing = 0;
    int m_testGeneration = 0;
    QVariantMap m_testResult;
};

class SyncthingSetup : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::shell::AppSettings *settings READ settings WRITE setSettings NOTIFY settingsChanged FINAL)
    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString urlError READ urlError NOTIFY draftChanged FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged FINAL)
    Q_PROPERTY(QString defaultUrl READ defaultUrl CONSTANT FINAL)
    Q_PROPERTY(QString stignoreState READ stignoreState NOTIFY stignoreChanged FINAL)
    Q_PROPERTY(QString stignoreTemplate READ stignoreTemplate CONSTANT FINAL)
    /// Where the vault's .stignore is (native separators).
    Q_PROPERTY(QString stignorePath READ stignorePath NOTIFY stignoreChanged FINAL)

public:
    explicit SyncthingSetup(QObject *parent = nullptr);

    shell::AppSettings *settings() const { return m_settings; }
    void setSettings(shell::AppSettings *settings);
    shell::Workspace *workspace() const { return m_workspace; }
    void setWorkspace(shell::Workspace *workspace);

    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &key);
    QString url() const { return m_url; }
    void setUrl(const QString &url);
    QString urlError() const;
    bool dirty() const;
    QString defaultUrl() const;
    QString stignoreState() const { return m_stignoreState; }
    QString stignoreTemplate() const;
    QString stignorePath() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool save();
    /// Create the vault's .stignore, or append the recommended rules it lacks.
    Q_INVOKABLE bool writeStignore();
    Q_INVOKABLE void refreshStignore();

Q_SIGNALS:
    void settingsChanged();
    void workspaceChanged();
    void draftChanged();
    void stignoreChanged();
    /// Something failed (translated).
    void errorOccurred(const QString &message);

private:
    QPointer<shell::AppSettings> m_settings;
    QPointer<shell::Workspace> m_workspace;
    QString m_apiKey;
    QString m_url;
    QString m_stignoreState = QStringLiteral("unavailable");
};

} // namespace brulion::ioviews

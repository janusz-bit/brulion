// AppSettings — device-local preferences of the app (QML singleton `AppSettings`, also
// `AppController.settings`). Stored with QSettings (INI format): on Linux
// ~/.config/Brulion/Brulion.ini, on Android in the app's private files, in the browser a file
// in the IndexedDB-backed app data folder. Vault settings shared between devices are NOT here —
// they live in the vault's .brulion/config.json (Workspace.config / Workspace.setConfig).
//
// Every property is validated (unknown theme -> "system", sizes clamped) and written at once;
// QML binds to them directly: `Theme.mode: AppSettings.themeMode`.
//
// The Nextcloud app password is deliberately not kept in QSettings: it is stored in its own
// file `<app data>/secrets/nextcloud-password` with owner-only permissions (0600; the folder
// 0700). Limitation: that is plain text protected only by file permissions (and the app's
// private storage on Android, the browser's site storage on the web) — Brulion does not depend
// on an OS keychain (libsecret/KWallet) so it runs the same on every target. Use a Nextcloud
// *app password* (revocable per device), never the account password.
//
// Other app areas can keep simple preferences with value()/setValue() under their own key
// prefix ("views/tasks/showDone", "editor/sourceMode", ...).
#pragma once

#include "log/redact.h"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <memory>

class QSettings;

namespace brulion::shell {

class AppSettings : public QObject {
    Q_OBJECT

    // ---- General ------------------------------------------------------------------------
    /// "system" | "light" | "dark" (Theme.mode).
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged FINAL)
    /// UI language "pl" | "en". Default: English when the system locale is English, else Polish.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    /// UI text size factor 0.85 … 1.4 (Theme.fontScale).
    Q_PROPERTY(qreal fontScale READ fontScale WRITE setFontScale NOTIFY fontScaleChanged FINAL)
    /// Reduce animations (Theme.reducedMotion).
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged FINAL)

    // ---- Editor -------------------------------------------------------------------------
    /// "serif" | "sans" (Theme.editorFont).
    Q_PROPERTY(QString editorFont READ editorFont WRITE setEditorFont NOTIFY editorFontChanged FINAL)
    /// Editor text size in px at 100 % (12 … 28, default 16).
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorFontSizeChanged FINAL)
    /// Maximum width of the editor's text column in px (480 … 1600, default 760); 0 = full width.
    Q_PROPERTY(int editorWidth READ editorWidth WRITE setEditorWidth NOTIFY editorWidthChanged FINAL)
    /// Show the whole file as Markdown source by default (Ctrl+E switches per page).
    Q_PROPERTY(bool editorSourceMode READ editorSourceMode WRITE setEditorSourceMode NOTIFY editorSourceModeChanged FINAL)

    // ---- Vaults -------------------------------------------------------------------------
    /// Absolute path of the vault opened last ("" before the first one).
    Q_PROPERTY(QString currentVault READ currentVault NOTIFY currentVaultChanged FINAL)
    /// [{ path, name, lastOpened (ISO date-time) }], most recent first, at most 8.
    Q_PROPERTY(QVariantList recentVaults READ recentVaults NOTIFY recentVaultsChanged FINAL)

    // ---- Application --------------------------------------------------------------------
    /// Desktop: closing the window keeps Brulion running in the system tray (reminders fire).
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged FINAL)

    // ---- Reminders ----------------------------------------------------------------------
    Q_PROPERTY(bool remindersEnabled READ remindersEnabled WRITE setRemindersEnabled NOTIFY remindersEnabledChanged FINAL)
    /// Snooze length offered by notifications, minutes (1 … 120, default 10).
    Q_PROPERTY(int snoozeMinutes READ snoozeMinutes WRITE setSnoozeMinutes NOTIFY snoozeMinutesChanged FINAL)
    /// How long an alarm rings before it stops by itself, minutes (1 … 60, default 10).
    Q_PROPERTY(int alarmRingMinutes READ alarmRingMinutes WRITE setAlarmRingMinutes NOTIFY alarmRingMinutesChanged FINAL)
    /// Show reminders that were due while the app/device was off.
    Q_PROPERTY(bool notifyMissed READ notifyMissed WRITE setNotifyMissed NOTIFY notifyMissedChanged FINAL)

    // ---- Nextcloud (WebDAV) sync --------------------------------------------------------
    // Whether it syncs and the server folder are per notebook (the current one); the server,
    // the user and the password are shared.
    Q_PROPERTY(bool nextcloudEnabled READ nextcloudEnabled WRITE setNextcloudEnabled NOTIFY nextcloudChanged FINAL)
    /// Server address, e.g. "https://cloud.example.com".
    Q_PROPERTY(QString nextcloudUrl READ nextcloudUrl WRITE setNextcloudUrl NOTIFY nextcloudChanged FINAL)
    Q_PROPERTY(QString nextcloudUser READ nextcloudUser WRITE setNextcloudUser NOTIFY nextcloudChanged FINAL)
    /// Folder on the server, relative to the user's files (default "Brulion").
    Q_PROPERTY(QString nextcloudFolder READ nextcloudFolder WRITE setNextcloudFolder NOTIFY nextcloudChanged FINAL)
    /// Automatic sync interval in minutes (0 = only manually / after edits; default 15).
    Q_PROPERTY(int syncIntervalMinutes READ syncIntervalMinutes WRITE setSyncIntervalMinutes NOTIFY nextcloudChanged FINAL)
    /// An app password is stored (its value is never exposed to QML).
    Q_PROPERTY(bool hasNextcloudPassword READ hasNextcloudPassword NOTIFY nextcloudPasswordChanged FINAL)

public:
    /// `settingsFile`: INI file (empty = the platform's default location for the app).
    /// `dataDir`: app data folder for the secrets file (empty = QStandardPaths::AppDataLocation).
    explicit AppSettings(const QString &settingsFile = {}, const QString &dataDir = {}, QObject *parent = nullptr);
    ~AppSettings() override;

    static constexpr int MaxRecentVaults = 8;

    QString themeMode() const;
    void setThemeMode(const QString &mode);
    QString language() const;
    void setLanguage(const QString &language);
    qreal fontScale() const;
    void setFontScale(qreal scale);
    bool reducedMotion() const;
    void setReducedMotion(bool on);

    QString editorFont() const;
    void setEditorFont(const QString &font);
    int editorFontSize() const;
    void setEditorFontSize(int px);
    int editorWidth() const;
    void setEditorWidth(int px);
    bool editorSourceMode() const;
    void setEditorSourceMode(bool on);

    QString currentVault() const;
    void setCurrentVault(const QString &path);
    QVariantList recentVaults() const;
    /// Put `path` first in the recent list (and make it current).
    void addRecentVault(const QString &path);
    Q_INVOKABLE void forgetRecentVault(const QString &path);

    bool closeToTray() const;
    void setCloseToTray(bool on);

    bool remindersEnabled() const;
    void setRemindersEnabled(bool on);
    int snoozeMinutes() const;
    void setSnoozeMinutes(int minutes);
    int alarmRingMinutes() const;
    void setAlarmRingMinutes(int minutes);
    bool notifyMissed() const;
    void setNotifyMissed(bool on);

    bool nextcloudEnabled() const;
    void setNextcloudEnabled(bool on);
    QString nextcloudUrl() const;
    void setNextcloudUrl(const QString &url);
    QString nextcloudUser() const;
    void setNextcloudUser(const QString &user);
    QString nextcloudFolder() const;
    void setNextcloudFolder(const QString &folder);
    /// Move the pre-0.3.1 shared switch and folder (one for all notebooks) to `notebook`, once.
    void claimSharedNextcloud(const QString &notebook);
    int syncIntervalMinutes() const;
    void setSyncIntervalMinutes(int minutes);

    bool hasNextcloudPassword() const;
    /// Store the app password (owner-only file); "" deletes it. Returns false on I/O errors.
    Q_INVOKABLE bool setNextcloudPassword(const QString &password);
    /// The stored password (C++ only: the sync integration reads it; never exposed to QML).
    QString nextcloudPassword() const;
    /// File that holds the password.
    QString passwordFile() const;

    /// Generic preference for other app areas (key with an area prefix, e.g. "views/tasks/x").
    /// The value comes back with the type of `defaultValue` (bool, int, double, …) — the INI
    /// file stores only text; one that does not convert gives `defaultValue`.
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);

    /// Write pending changes to disk now (QSettings also does it by itself).
    void sync();
    /// The settings file in use.
    QString fileName() const;

Q_SIGNALS:
    void themeModeChanged();
    void languageChanged();
    void fontScaleChanged();
    void reducedMotionChanged();
    void editorFontChanged();
    void editorFontSizeChanged();
    void editorWidthChanged();
    void editorSourceModeChanged();
    void currentVaultChanged();
    void recentVaultsChanged();
    void closeToTrayChanged();
    void remindersEnabledChanged();
    void snoozeMinutesChanged();
    void alarmRingMinutesChanged();
    void notifyMissedChanged();
    void nextcloudChanged();
    void nextcloudPasswordChanged();
    void valueChanged(const QString &key);

private:
    template<typename T>
    bool store(const QString &key, const T &value, const T &current);

    std::unique_ptr<QSettings> m_settings;
    QString m_dataDir;
    /// The app password is masked in every log line (logging::redact) while it is stored.
    mutable logging::ScopedSecret m_passwordSecret;
};

} // namespace brulion::shell

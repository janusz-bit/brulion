// AppUpdates — the message that a newer Brulion is released (`AppController.updates`,
// ShellUpdateBanner.qml): only where nothing updates the app by itself (Android, Windows; `supported`).
//
// While `enabled` (the device setting "updates/check", on by default) GitHub is asked once a day:
// shortly after the start and then whenever a day has passed since the last answer (a failure is
// only logged and asked again a few hours later). The last answer is kept, so the message also
// shows offline. The message stays until the app is updated or dismiss() — a later release shows
// it again. Opening the release's page is the user's: QML passes `releaseUrl` to Workspace.openUrl.
// A build with the CMake option BRULION_UPDATE_CHECK=OFF (store builds: F-Droid, IzzyOnDroid) has
// none of it: checkBuiltIn() is false, so it is never `supported` and never asks.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <functional>

namespace brulion::appupdate {
class UpdateChecker;
}

namespace brulion::shell {

class AppSettings;

class AppUpdates : public QObject {
    Q_OBJECT

    /// This build shows the message at all (Settings → Application offers the switch).
    Q_PROPERTY(bool supported READ supported CONSTANT FINAL)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    /// The newer version to tell about ("" = no message).
    Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY availableChanged FINAL)
    /// The page of that release on GitHub (empty without one).
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY availableChanged FINAL)

public:
    struct Options {
        AppSettings *settings = nullptr; ///< not owned
        QString currentVersion;
        bool supported = false;
        /// False: never asks GitHub (--update-available shows a version given by hand).
        bool network = true;
        QUrl apiUrl; ///< empty: GitHub's (tests: a local server)
        int firstCheckDelayMs = 10 * 1000;
        std::function<QDateTime()> clock; ///< required
    };

    explicit AppUpdates(const Options &options, QObject *parent = nullptr);
    ~AppUpdates() override;

    /// This build may tell about new versions at all: the CMake option BRULION_UPDATE_CHECK (OFF
    /// for builds a store updates, e.g. F-Droid). False: never supported, whatever Options say —
    /// no message, no switch in the settings, no request to GitHub.
    static bool checkBuiltIn();

    /// Starts asking (AppController::start()).
    void start();

    bool supported() const { return m_supported; }
    bool enabled() const;
    void setEnabled(bool enabled);
    QString availableVersion() const;
    QUrl releaseUrl() const;

    /// No more message about this version.
    Q_INVOKABLE void dismiss();
    /// Asks GitHub when supported, enabled and a day has passed since the last answer.
    void checkIfDue();
    /// Pretends the latest release is `version`, for this run only (--update-available).
    void setLatestForThisRun(const QString &version);

Q_SIGNALS:
    void enabledChanged();
    void availableChanged();

private:
    void onChecked(const QString &version);

    AppSettings *m_settings;
    QString m_current;
    bool m_supported;
    int m_firstCheckDelayMs;
    std::function<QDateTime()> m_clock;
    appupdate::UpdateChecker *m_checker = nullptr;
    QString m_latest;
    QString m_dismissed;
    QTimer m_timer;
};

} // namespace brulion::shell

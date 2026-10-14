// Syncthing helpers: a small client for the local Syncthing daemon's REST API (status shown in
// the app, finding the folder that contains the vault) and the recommended .stignore. Conflict
// copies made by Syncthing are found by findConflicts() (sync/conflicts.h).
#pragma once

#include "sync/http.h"

#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QJsonDocument;

namespace brulion::sync {

/// "http://127.0.0.1:8384" — the default address of the Syncthing GUI / REST API.
QString syncthingDefaultUrl();

/// A Syncthing folder (from /rest/config/folders).
struct SyncthingFolder {
    QString id;
    QString label; ///< falls back to the id
    QString path;  ///< as configured (may start with "~")
    std::optional<bool> paused;

    friend bool operator==(const SyncthingFolder &, const SyncthingFolder &) = default;
};

/// A failed Syncthing REST call.
struct SyncthingError {
    int status = 0;  ///< HTTP status; 0 = not reachable
    QString message; ///< empty = no error

    bool isError() const { return !message.isEmpty(); }
};

/// Status of a Syncthing folder (or of the daemon when no folder is selected).
struct SyncthingStatus {
    QString state;         ///< folder state ("idle", "scanning", "syncing", "error", …) or "offline"
    qint64 needBytes = 0;
    qint64 globalBytes = 0;
    double completion = 0; ///< 0-100 (one decimal) of the folder that is in sync
    int errors = 0;        ///< folder errors including pull errors
    QString folderId;
    std::optional<int> connected; ///< connected remote devices, when known
    QString error;         ///< why the state is "offline"

    friend bool operator==(const SyncthingStatus &, const SyncthingStatus &) = default;
};

/// Percentage (0-100, rounded down to one decimal) of a folder that is in sync.
double completionPercent(qint64 needBytes, qint64 globalBytes);

/// Normalise a local folder path for comparison: backslashes -> '/', duplicate and trailing
/// slashes removed, "~" expanded with `home` when given, Windows drive letters lower-cased, NFC.
QString normalizeFsPath(const QString &path, const QString &home = {});

/// The home folder contained in an absolute path ("/home/jan/…" -> "/home/jan"; also
/// "/Users/x", "/root", "/data/data/<app>", "C:/Users/x").
std::optional<QString> guessHome(const QString &absPath);

/// The folder containing `absPath` (the longest matching folder path, segment boundaries
/// respected; case-insensitive for Windows paths). Folder paths starting with "~" are expanded
/// with `home`, or the home guessed from `absPath`.
std::optional<SyncthingFolder> findFolderForPath(const QList<SyncthingFolder> &folders, const QString &absPath,
                                                 const QString &home = {});

/// Recommended .stignore for a vault shared with Syncthing (comments in Polish). Syncthing does
/// not sync .stignore itself: it belongs on every device.
QString stignoreTemplate();

/// Client for the Syncthing REST API (header X-API-Key). Asynchronous like WebDavClient: every
/// callback runs exactly once, from the event loop, while the client lives.
class SyncthingClient : public QObject {
    Q_OBJECT
public:
    struct FoldersResult {
        QList<SyncthingFolder> folders;
        SyncthingError error;
    };
    struct ConnectionsResult {
        int connected = 0;
        SyncthingError error;
    };

    /// `apiKey` from Syncthing's settings (Actions -> Settings -> API Key); `url` empty =
    /// syncthingDefaultUrl(). `transport` is not owned and must outlive the client.
    SyncthingClient(const QString &apiKey, HttpTransport *transport, const QString &url = {}, QObject *parent = nullptr);
    ~SyncthingClient() override;
    Q_DISABLE_COPY_MOVE(SyncthingClient)

    QString url() const { return m_url; }
    /// Folder used by status() when called without an id.
    void setFolderId(const QString &folderId) { m_folderId = folderId; }
    QString folderId() const { return m_folderId; }
    /// Home folder used to expand "~" in folder paths (else guessed from the path searched for).
    void setHome(const QString &home) { m_home = home; }
    /// Time limit per request in ms (default 10 000).
    void setTimeout(int msec) { m_timeoutMs = qMax(0, msec); }

    /// True when the daemon answers /rest/system/ping with this API key.
    void ping(std::function<void(bool)> done);
    /// Configured folders (error when unreachable, unauthorised or malformed).
    void folders(std::function<void(const FoldersResult &)> done);
    /// Number of connected remote devices.
    void connections(std::function<void(const ConnectionsResult &)> done);
    /// Status of `folderId` (empty: the configured folder). Without any folder: whether the
    /// daemon is reachable plus the connection count. Never fails: problems give state
    /// "offline" with `error`.
    void status(const QString &folderId, std::function<void(const SyncthingStatus &)> done);
    /// The configured folder that contains `absPath` (e.g. the vault folder), if any.
    void findFolderForPath(const QString &absPath,
                           std::function<void(const std::optional<SyncthingFolder> &, const SyncthingError &)> done);

private:
    using JsonCallback = std::function<void(const QJsonDocument &, const SyncthingError &)>;
    void getJson(const QString &path, JsonCallback done);

    QString m_apiKey;
    HttpTransport *m_transport;
    QString m_url;
    QString m_folderId;
    QString m_home;
    int m_timeoutMs = 10'000;
};

} // namespace brulion::sync

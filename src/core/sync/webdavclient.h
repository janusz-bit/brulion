// WebDAV client (Nextcloud, ownCloud, any RFC 4918 server) on top of an HttpTransport.
// Paths are vault paths relative to the base URL; every segment is percent-encoded.
#pragma once

#include "log/redact.h"
#include "sync/davxml.h"
#include "sync/errors.h"
#include "sync/http.h"
#include "sync/synctypes.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

namespace brulion::sync {

struct WebDavTestResult {
    bool ok = false;
    WebDavError error; ///< when !ok: Auth (401), Forbidden, NotFound, Network, Timeout, Parse, …
};

struct ListOptions {
    /// Return true to not descend into a folder (it is still listed itself).
    std::function<bool(const VaultPath &)> skipDir;
    /// Polled before each folder request; true stops listing (error Cancelled).
    std::function<bool()> cancelled;
    /// After each folder answered: folders answered, folders found so far, entries found so far.
    std::function<void(int done, int known, int entries)> onProgress;
};

struct ListResult {
    QList<RemoteEntry> entries; ///< everything below the base folder (not the base), sorted by path
    WebDavError error;
};

struct StatResult {
    std::optional<RemoteEntry> entry; ///< std::nullopt when the path does not exist
    WebDavError error;
};

struct GetOptions {
    /// Expected size in bytes (e.g. from the listing): the time limit grows with it. Without
    /// it a download gets ten times the base timeout.
    std::optional<qint64> size;
};

struct GetResult {
    QByteArray data;
    QString etag; ///< normalised ETag of the downloaded version ("" when not sent)
    WebDavError error;
};

struct PutOptions {
    QString ifMatch;          ///< replace only this version (412 Precondition otherwise)
    bool ifNoneMatch = false; ///< create only, never replace ("If-None-Match: *")
    std::optional<qint64> mtime; ///< ms since epoch, stored by Nextcloud (X-OC-MTime)
};

struct PutResult {
    QString etag; ///< new ETag when the server reports one, else ""
    WebDavError error;
};

struct RemoveOptions {
    QString ifMatch; ///< delete only this version (412 Precondition otherwise)
};

struct MoveOptions {
    bool folder = false;    ///< the source is a folder (URLs with a trailing slash)
    bool overwrite = false; ///< replace an existing destination; otherwise it fails (412)
};

/// WebDAV client with HTTP Basic auth. Every operation is asynchronous: its callback runs
/// exactly once, from the event loop (never inside the call), and only while the client lives.
/// Failures are reported as WebDavError values, never thrown.
///
/// Requests carry "X-Requested-With: XMLHttpRequest" (Nextcloud then answers 401 without a
/// browser login prompt) and a time limit: the base timeout (default 60 s) plus, for transfers,
/// the time the payload takes at minBytesPerSecond (default 16 KiB/s) — a stalled connection
/// (a phone switching networks) fails as Timeout instead of hanging the sync.
class WebDavClient : public QObject {
    Q_OBJECT
public:
    using TestCallback = std::function<void(const WebDavTestResult &)>;
    using ListCallback = std::function<void(const ListResult &)>;
    using StatCallback = std::function<void(const StatResult &)>;
    using GetCallback = std::function<void(const GetResult &)>;
    using PutCallback = std::function<void(const PutResult &)>;
    using DoneCallback = std::function<void(const WebDavError &)>;

    /// `baseUrl`: the folder all paths are relative to (e.g. from nextcloudDavUrl()).
    /// `transport` is not owned and must outlive the client.
    WebDavClient(const QString &baseUrl, const QString &username, const QString &password,
                 HttpTransport *transport, QObject *parent = nullptr);
    ~WebDavClient() override;
    Q_DISABLE_COPY_MOVE(WebDavClient)

    QString baseUrl() const { return m_baseUrl; }
    QString username() const { return m_username; }
    /// "user@baseUrl" — identifies account + folder (the engine starts over when it changes).
    QString identity() const;

    /// Base time limit per request in ms (default 60 000; 0 = none).
    void setTimeout(int msec);
    int timeout() const { return m_timeoutMs; }
    /// Slowest accepted transfer rate for the size-based extra time (default 16 KiB/s).
    void setMinBytesPerSecond(qint64 bytesPerSecond);
    /// Parallel PROPFIND requests while listing (default 4).
    void setListConcurrency(int count);

    /// Prefix of this client's log lines ("#42" during sync run 42, "test" for a connection
    /// test; "" = none). Every finished request is logged to brulion.net: a debug line per
    /// request (method, path relative to the base, status, ms, bytes — never headers or
    /// bodies; only in the detailed mode), an info line for answers slower than 5 s, a warning
    /// for timeouts and network failures.
    void setLogTag(const QString &tag);
    QString logTag() const { return m_logTag; }
    /// Requests sent and not answered yet.
    int requestsInFlight() const { return int(m_inFlight.size()); }
    /// ms since the last answer or transfer progress while requests are open (0 when none is).
    qint64 idleMs() const;
    /// The open requests as "PROPFIND Dziennik/", "GET Projekty/Plan.md", oldest first.
    QStringList inFlight(int max = 4) const;
    /// `url` relative to the base folder ("" = the base; the full URL, user info removed, when
    /// it lies elsewhere) — for log lines and statistics.
    QString relativePath(const QUrl &url) const;

Q_SIGNALS:
    /// Every finished request (the engine collects SyncStats from it).
    void exchanged(const brulion::sync::HttpExchange &exchange);

public:
    /// Check that the base folder is reachable with these credentials (PROPFIND Depth 0).
    void test(TestCallback done);
    /// Recursive listing of everything below the base folder, one PROPFIND Depth 1 per folder
    /// (Nextcloud refuses Depth: infinity). NotFound when the base folder does not exist; a
    /// sub-folder deleted while listing is skipped.
    void list(const ListOptions &options, ListCallback done);
    /// Properties of one file or folder (PROPFIND Depth 0); entry is empty when it does not exist.
    void stat(const VaultPath &path, StatCallback done);
    /// Download a file (NotFound when missing).
    void get(const VaultPath &path, const GetOptions &options, GetCallback done);
    /// Upload a file, creating missing parent folders first (MKCOL; 405 = exists). A parent
    /// deleted behind the client's back (409) is recreated and the upload retried once.
    void put(const VaultPath &path, const QByteArray &data, const PutOptions &options, PutCallback done);
    /// Delete a file or folder; a missing path is not an error. Refuses the base folder.
    /// Deleting a folder deletes everything in it (WebDAV has no other DELETE).
    void remove(const VaultPath &path, const RemoveOptions &options, DoneCallback done);
    /// Rename or move a file or folder on the server (WebDAV MOVE, "Overwrite: F" unless
    /// `options.overwrite`), creating the missing parent folders of `to` first. An existing
    /// destination fails with Precondition (412), a missing source with NotFound; servers that
    /// do not move answer 405 / 501 (NotAllowed / Server). Refuses the base folder and moves
    /// into itself. The server's spelling of names inside a moved folder (NFD) is kept.
    void move(const VaultPath &from, const VaultPath &to, const MoveOptions &options, DoneCallback done);
    /// The direct children of a folder (PROPFIND Depth 1), sorted by path; NotFound when the
    /// folder does not exist. An entry that cannot be named as a child of the folder fails the
    /// listing (Parse) instead of being left out: an empty answer means an empty folder.
    void listFolder(const VaultPath &path, ListCallback done);
    /// Create a folder ("" = the base folder); an existing one (405) is fine.
    void mkcol(const VaultPath &path, DoneCallback done);
    /// Create a folder and its missing parent folders; existing ones are fine.
    void mkdirs(const VaultPath &path, DoneCallback done);
    /// Make sure the base folder exists, creating missing ancestors of the base URL as needed.
    void ensureBase(DoneCallback done);

private:
    struct ListRun;
    using ResponseCallback = std::function<void(const HttpResponse &)>;
    using PropfindCallback = std::function<void(const WebDavError &, const QList<DavResource> &)>;

    /// Relative path as the server spells it: the listed name, or for a path below a listed
    /// folder that folder's spelling plus the rest (NFD folders created by macOS clients).
    QString serverName(const VaultPath &normalizedPath) const;
    QString urlFor(const VaultPath &path, bool dir = false) const;
    std::optional<QString> relativeRaw(const QString &href, const QString &requestUrl) const;
    int timeoutFor(qint64 bytes) const;
    int unknownSizeTimeout() const;
    void request(const QByteArray &method, const QString &url, QList<QPair<QByteArray, QByteArray>> headers,
                 const QByteArray &body, int timeoutMs, ResponseCallback done);
    WebDavError check(const HttpResponse &response, const QByteArray &method, const QString &url,
                      std::initializer_list<int> ok) const;
    void propfindRaw(const QString &rawPath, int depth, PropfindCallback done);
    void propfind(const VaultPath &path, int depth, PropfindCallback done);
    void sendPut(const VaultPath &path, const QByteArray &data, const PutOptions &options, ResponseCallback done);
    void ensureParents(const VaultPath &path, bool force, DoneCallback done);
    /// After a MOVE: the known folders and the server's spellings follow the moved path.
    void rebaseKnown(const VaultPath &from, const VaultPath &to, const QString &spelledTo);
    /// After a DELETE: forget the known folders at and below `path`.
    void forgetKnown(const VaultPath &path);
    void ensureParentsFrom(const QStringList &segments, int index, QString dir, bool force, const VaultPath &path,
                           DoneCallback done);
    void mkcolUrl(const QString &url, int depth, DoneCallback done);
    void fail(const WebDavError &error, DoneCallback done);
    void logExchange(const HttpExchange &exchange) const;

    QString m_baseUrl;
    QString m_username;
    QByteArray m_authorization;
    /// The password and the Basic token are masked in every log line and shown error.
    logging::ScopedSecret m_secretPassword;
    logging::ScopedSecret m_secretToken;
    QString m_logTag;
    /// Open requests: id -> "METHOD path", in sending order.
    QMap<quint64, QString> m_inFlight;
    quint64 m_nextRequestId = 0;
    QElapsedTimer m_lastActivity;
    HttpTransport *m_transport;
    QString m_basePathname;
    int m_timeoutMs = 60'000;
    qint64 m_minBytesPerSecond = 16 * 1024;
    int m_listConcurrency = 4;
    /// Folders known to exist on the server ("" = base).
    QSet<QString> m_knownDirs;
    /// Normalised path -> path as spelled on the server, when they differ (NFD names); see
    /// serverName().
    QHash<VaultPath, QString> m_remoteNames;
};

} // namespace brulion::sync

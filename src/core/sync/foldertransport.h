// FolderDavTransport — a folder of this device (FolderStorage) answering a WebDavClient's
// requests as a WebDAV server would, so that SyncEngine keeps a notebook in sync with it exactly
// as with Nextcloud: three-way merges, conflict copies, the guards against mass deletions and empty
// sides, moves, history (docs/ANDROID.md "A folder picked in the system's dialog").
//
// The client's base URL is deviceFolderUrl(id) ("brulion-folder://device/<id>"): no request ever
// leaves the process. Supported: PROPFIND (Depth 0 and 1), GET, PUT (If-Match, If-None-Match; 409
// without the parent folder), DELETE (If-Match), MKCOL (405 when it exists, 409 without the
// parent), MOVE (Destination, Overwrite; 405 when the storage cannot move — the engine then copies
// and deletes). A file's ETag is its modification time and size ("<ms hex>-<size hex>"): a change
// that keeps both (the same size within one tick of a coarse clock) goes unseen until the next
// one, as with rsync's quick check.
//
// The storage runs on a worker thread of the transport, one operation at a time (Android's
// document providers block, sometimes long); the answers come back on the thread that owns the
// transport, from its event loop. A sync over it should send one request at a time
// (SyncEngine::setConcurrency(1), WebDavClient::setListConcurrency(1)): the queue behind one slow
// operation would otherwise count against the next requests' time limits.
//
// Storage failures become HTTP statuses: NotFound 404 (409 for a missing parent), NotAllowed 403,
// Unavailable 503, Unsupported 405, Exists 405 / 412, Failed 500 — the message in the body.
#pragma once

#include "sync/folderstorage.h"
#include "sync/http.h"

#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace brulion::sync {

/// The scheme of deviceFolderUrl(): no network transport answers it.
inline constexpr QLatin1StringView DeviceFolderScheme{"brulion-folder"};

/// The base URL of the device folder `id` for a WebDavClient: "brulion-folder://device/<id>"
/// (`id` is percent-encoded; deviceFolderId() makes one from the folder's address).
QString deviceFolderUrl(const QString &id);
/// A short id for a folder's address (an Android tree URI, a path): 16 hex digits of its SHA-256.
QString deviceFolderId(const QString &address);
/// `url` is a device folder's (its scheme).
bool isDeviceFolderUrl(const QString &url);

class FolderDavTransport : public QObject, public HttpTransport {
    Q_OBJECT
public:
    /// `baseUrl`: the WebDavClient's (deviceFolderUrl()); requests outside it are refused (403).
    FolderDavTransport(std::shared_ptr<FolderStorage> storage, const QString &baseUrl, QObject *parent = nullptr);
    /// Waits for the operation in progress; queued ones are dropped (their callbacks never run).
    ~FolderDavTransport() override;
    Q_DISABLE_COPY_MOVE(FolderDavTransport)

    void send(const HttpRequest &request, HttpCallback done) override;
    /// The storage streams() (LocalFolderStorage does): a GET with a bodySink and a PUT with a
    /// bodyDevice hold no more than a piece of the file. The worker hands each piece over to the
    /// thread that owns the transport, where the sink is called and the device read, and waits
    /// for it — one piece in flight.
    bool streams() const override { return m_storage && m_storage->streams(); }

    /// Answer on the calling thread instead of the worker (still from the event loop): tests.
    void setSynchronous(bool on) { m_synchronous = on; }
    FolderStorage *storage() const { return m_storage.get(); }
    QString baseUrl() const { return m_baseUrl; }

    /// The answer to one request (what the worker runs).
    static HttpResponse answer(FolderStorage &storage, const QString &basePathname, const HttpRequest &request);
    /// "<ms hex>-<size hex>" for a file, "d<ms hex>" for a folder.
    static QString etagOf(const FolderEntry &entry);

private:
    std::shared_ptr<FolderStorage> m_storage;
    QString m_baseUrl;
    QString m_basePathname;
    QThreadPool m_pool;
    bool m_synchronous = false;
    /// Set by the destructor: a worker waiting for the owner's turn stops (OwnerSide::run).
    std::shared_ptr<std::atomic<bool>> m_stopping = std::make_shared<std::atomic<bool>>(false);
};

} // namespace brulion::sync

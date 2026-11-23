// DeviceFolderFirstSync — the first sync of a notebook made in the app's storage for a folder of
// this device (AppController::openDeviceFolder(); docs/ANDROID.md "A folder picked in the system's
// dialog"), run before the notebook opens: the folder's notes arrive first, so nothing the app
// writes into a new, empty notebook (the introduction's settings, a journal page) is merged into
// the notes the folder already has. The same engine and bookkeeping as WorkspaceSync's (the
// notebook's device state folder), so the app's next sync knows every file.
#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace brulion {
class Vault;
namespace sync {
class FolderDavTransport;
class FolderStorage;
class SyncEngine;
class WebDavClient;
struct SyncReport;
} // namespace sync
} // namespace brulion

namespace brulion::shell {

class DeviceFolderFirstSync : public QObject {
    Q_OBJECT
public:
    /// `notebook`: the new notebook's folder (exists); `stateDir`: its device state folder;
    /// `address`: the folder's (its bookkeeping's name); `device`: this device in conflict copies.
    DeviceFolderFirstSync(const QString &notebook, const QString &stateDir, std::shared_ptr<sync::FolderStorage> storage,
                          const QString &address, const QString &device, QObject *parent = nullptr);
    ~DeviceFolderFirstSync() override;
    Q_DISABLE_COPY_MOVE(DeviceFolderFirstSync)

    /// Answer the folder's requests on this thread (tests).
    void setSynchronous(bool on);
    /// Starts the sync; finished() follows once, from the event loop.
    void start();
    QString notebook() const { return m_notebook; }

Q_SIGNALS:
    /// Files compared so far of `total` (0 while the folder is listed).
    void progress(int done, int total);
    /// `ok` false: the sync failed (`code`: sync::SyncReport::failureCode(), `message`: technical).
    void finished(bool ok, int downloaded, const QString &code, const QString &message);

private:
    void done(const sync::SyncReport &report);

    QString m_notebook;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<sync::FolderDavTransport> m_transport;
    std::unique_ptr<sync::WebDavClient> m_client;
    std::unique_ptr<sync::SyncEngine> m_engine;
    bool m_started = false;
};

} // namespace brulion::shell

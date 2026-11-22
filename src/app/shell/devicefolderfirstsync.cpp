#include "devicefolderfirstsync.h"

#include "log/categories.h"
#include "sync/foldertransport.h"
#include "sync/syncengine.h"
#include "sync/webdavclient.h"
#include "vault/vault.h"

#include <QMetaObject>

using namespace Qt::StringLiterals;

namespace brulion::shell {

DeviceFolderFirstSync::DeviceFolderFirstSync(const QString &notebook, const QString &stateDir,
                                             std::shared_ptr<sync::FolderStorage> storage, const QString &address,
                                             const QString &device, QObject *parent)
    : QObject(parent)
    , m_notebook(notebook)
{
    m_vault = std::make_unique<Vault>(notebook, stateDir);
    const QString base = sync::deviceFolderUrl(sync::deviceFolderId(address));
    m_transport = std::make_unique<sync::FolderDavTransport>(std::move(storage), base);
    m_client = std::make_unique<sync::WebDavClient>(base, u"device"_s, QString(), m_transport.get());
    m_client->setListConcurrency(1);
    m_engine = std::make_unique<sync::SyncEngine>(m_vault.get(), m_client.get(), m_vault->store(), device);
    m_engine->setConcurrency(1);
    connect(m_engine.get(), &sync::SyncEngine::progress, this,
            [this](const sync::SyncProgress &p) { Q_EMIT progress(p.done, p.total); });
}

DeviceFolderFirstSync::~DeviceFolderFirstSync()
{
    // The engine (it may still run) before the client, the client before the transport.
    m_engine.reset();
    m_client.reset();
    m_transport.reset();
}

void DeviceFolderFirstSync::setSynchronous(bool on)
{
    m_transport->setSynchronous(on);
}

void DeviceFolderFirstSync::start()
{
    if (std::exchange(m_started, true))
        return;
    m_vault->open();
    qCInfo(logging::lcSync).noquote() << u"Folder sync: the first copy of %1 into %2"_s.arg(
            m_transport->storage() ? m_transport->storage()->describe() : QString(), m_notebook);
    sync::SyncRunOptions options;
    options.trigger = sync::SyncTrigger::Manual;
    m_engine->sync(options, [this](const sync::SyncReport &report) {
        // After the engine's finished(): leave its call stack before anyone tears it down.
        QMetaObject::invokeMethod(this, [this, report] { done(report); }, Qt::QueuedConnection);
    });
}

void DeviceFolderFirstSync::done(const sync::SyncReport &report)
{
    const bool ok = !report.failed() && !report.cancelled;
    if (ok) {
        qCInfo(logging::lcSync).noquote()
                << u"Folder sync: %1 file(s) copied from the folder, %2 sent to it"_s.arg(report.downloaded).arg(report.uploaded);
    } else {
        qCWarning(logging::lcSync).noquote() << u"Folder sync: the first copy failed (%1): %2"_s.arg(
                report.failureCode(), report.failureMessage());
    }
    // The notebook opens next with a store of its own over the same file: this one goes first.
    m_engine.reset();
    m_client.reset();
    m_vault.reset();
    Q_EMIT finished(ok, report.downloaded, report.failureCode(), report.failureMessage());
}

} // namespace brulion::shell

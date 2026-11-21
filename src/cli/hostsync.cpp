// The notebook of `acp serve` kept in sync with Nextcloud (hostsync.h).
#include "hostsync.h"

#include "notebook/session.h"
#include "sync/http.h"
#include "sync/syncengine.h"
#include "sync/url.h"
#include "sync/webdavclient.h"
#include "vault/vault.h"

#include <QTimer>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::cli {

HostSync::HostSync(notebook::Notebook *notebook, HostSyncOptions options, sync::HttpTransport *transport,
                   QObject *parent)
    : QObject(parent), m_notebook(notebook), m_options(std::move(options)), m_transport(transport)
{
    if (!m_options.server.trimmed().isEmpty())
        m_davUrl =
                sync::nextcloudDavUrl(m_options.server.trimmed(), m_options.user.trimmed(), m_options.folder.trimmed());
}

HostSync::~HostSync()
{
    if (m_engine && m_running)
        m_engine->cancel();
}

bool HostSync::start(QString *error)
{
    if (m_davUrl.isEmpty() || m_options.user.trimmed().isEmpty()) {
        *error = u"--nextcloud needs the server's address and --nextcloud-user"_s;
        return false;
    }
    if (m_options.password.isEmpty()) {
        *error = u"--nextcloud needs an app password (--nextcloud-password-file)"_s;
        return false;
    }
    if (!m_transport) {
        auto *network = new sync::NetworkHttpTransport;
        m_ownTransport.reset(network);
        m_transport = network;
    }
    m_client =
            std::make_unique<sync::WebDavClient>(m_davUrl, m_options.user.trimmed(), m_options.password, m_transport);
    Vault &vault = m_notebook->vault();
    m_engine = std::make_unique<sync::SyncEngine>(&vault, m_client.get(), vault.store(),
                                                  m_options.device.isEmpty() ? u"brulion-cli"_s : m_options.device);
    m_interval = new QTimer(this);
    connect(m_interval, &QTimer::timeout, this, [this] { syncNow(); });
    m_interval->start(std::max(30, m_options.intervalSeconds) * 1000);
    m_afterChange = new QTimer(this);
    m_afterChange->setSingleShot(true);
    connect(m_afterChange, &QTimer::timeout, this, [this] { syncNow(); });
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_running && m_engine) {
            if (log)
                log(u"sync: cancelled after %1 s"_s.arg(m_options.timeoutMs / 1000));
            m_engine->cancel();
        }
    });
    syncNow();
    return true;
}

void HostSync::syncNow(std::function<void(bool)> done)
{
    if (!m_engine) {
        if (done)
            done(false);
        return;
    }
    if (m_running) {
        // After the run that is running: a change made meanwhile is in the next one.
        m_again = true;
        if (done)
            m_next.append(std::move(done));
        return;
    }
    if (done)
        m_current.append(std::move(done));
    run();
}

void HostSync::changed()
{
    if (m_afterChange && !m_afterChange->isActive())
        m_afterChange->start(m_options.afterChangeMs);
}

void HostSync::run()
{
    m_running = true;
    m_again = false;
    m_timeout->start(m_options.timeoutMs);
    sync::SyncRunOptions options;
    options.trigger = sync::SyncTrigger::Timer;
    m_engine->sync(options, [this](const sync::SyncReport &report) {
        // After the engine's finished(): leave its call stack first.
        QMetaObject::invokeMethod(this, [this, report] { finished(report); }, Qt::QueuedConnection);
    });
}

void HostSync::finished(const sync::SyncReport &report)
{
    m_timeout->stop();
    m_running = false;
    m_last = QDateTime::currentDateTime();
    m_lastOk = !report.failed() && !report.cancelled;
    m_lastError = report.failed() ? report.failureCode() : report.cancelled ? u"cancelled"_s : QString();
    m_uploaded = report.uploaded;
    m_downloaded = report.downloaded + report.merged;
    if (log) {
        QString line = u"sync: %1 up, %2 down, %3 conflicts in %4 ms"_s.arg(report.uploaded)
                               .arg(report.downloaded + report.merged)
                               .arg(report.conflicts.size())
                               .arg(report.durationMs);
        if (report.failed())
            line += u", failed: "_s + report.failureCode() + u" ("_s + report.failureMessage() + u')';
        if (report.needsConfirmation)
            line += u", deletions held back (more than the guard allows: confirm them in the app)"_s;
        log(line);
    }
    const bool ok = m_lastOk;
    for (const auto &done : std::exchange(m_current, {}))
        done(ok);
    m_current = std::exchange(m_next, {});
    if (m_again || !m_current.isEmpty())
        run();
}

QJsonObject HostSync::status() const
{
    QJsonObject o{{u"enabled"_s, m_engine != nullptr}, {u"running"_s, m_running}};
    if (m_last.isValid()) {
        o.insert(u"last"_s, m_last.toString(Qt::ISODate));
        o.insert(u"ok"_s, m_lastOk);
        o.insert(u"uploaded"_s, m_uploaded);
        o.insert(u"downloaded"_s, m_downloaded);
        if (!m_lastError.isEmpty())
            o.insert(u"error"_s, m_lastError);
    }
    return o;
}

} // namespace brulion::cli

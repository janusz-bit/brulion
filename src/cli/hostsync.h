// The notebook of `acp serve` kept in sync with Nextcloud (docs/CLI.md "AI agents for other
// devices"): the agents work in this computer's copy of a notebook the user's devices sync through
// Nextcloud, with the app's own sync engine (core sync: three-way merges, conflict copies, the
// mass-deletion guard) as one more device of it. The one place brulion-cli syncs; only with
// --nextcloud, never the app's settings or password.
//
// A run when the server starts, every interval, a few seconds after the agent wrote files
// (changed()), and on request (syncNow(): before a turn, so the device's latest notes are here,
// and after it). Runs never overlap: one asked for while another runs follows it, and every caller
// of the runs meanwhile hears when one that started after its call ended.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QTimer;

namespace brulion::sync {
class HttpTransport;
class SyncEngine;
class WebDavClient;
struct SyncReport;
} // namespace brulion::sync

namespace brulion::notebook {
class Notebook;
}

namespace brulion::cli {

struct HostSyncOptions {
    QString server; ///< Nextcloud's address (as the app takes it), or a WebDAV folder's URL
    QString user;
    QString folder;   ///< the notebook's folder on Nextcloud ("Brulion")
    QString password; ///< an app password (read from --nextcloud-password-file)
    QString device;   ///< the device's name in conflict copies and the server's log
    int intervalSeconds = 300;
    int afterChangeMs = 3000;
    int timeoutMs = 10 * 60 * 1000; ///< a run that takes longer is cancelled
};

class HostSync : public QObject {
public:
    /// `transport`: tests (an in-memory WebDAV server); else one over the network of its own.
    HostSync(notebook::Notebook *notebook, HostSyncOptions options, sync::HttpTransport *transport = nullptr,
             QObject *parent = nullptr);
    ~HostSync() override;

    /// The WebDAV folder's URL ("" when the options name none: then start() fails).
    QString davUrl() const { return m_davUrl; }
    /// Checks the options, starts the first run and the interval.
    bool start(QString *error);
    /// A run now (or after the one running); `done(ok)` once one that started after this call ended.
    void syncNow(std::function<void(bool ok)> done = {});
    /// Files were written here: a run within afterChangeMs.
    void changed();
    bool running() const { return m_running; }
    /// {enabled, running, last (ISO time), ok, error, uploaded, downloaded}.
    QJsonObject status() const;
    std::function<void(const QString &line)> log;

private:
    void run();
    void finished(const sync::SyncReport &report);

    notebook::Notebook *m_notebook = nullptr;
    HostSyncOptions m_options;
    QString m_davUrl;
    std::unique_ptr<sync::HttpTransport> m_ownTransport;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::WebDavClient> m_client;
    std::unique_ptr<sync::SyncEngine> m_engine;
    QTimer *m_interval = nullptr;
    QTimer *m_afterChange = nullptr;
    QTimer *m_timeout = nullptr;
    bool m_running = false;
    bool m_again = false;
    /// Callers waiting for the run that is running (m_current) or the next one (m_next).
    QList<std::function<void(bool)>> m_current;
    QList<std::function<void(bool)>> m_next;
    QDateTime m_last;
    bool m_lastOk = false;
    QString m_lastError;
    int m_uploaded = 0;
    int m_downloaded = 0;
};

} // namespace brulion::cli

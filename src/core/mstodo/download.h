// Download of the user's Microsoft To Do lists from Microsoft Graph into the JSON bundle that the
// io module's "mstodo" importer reads. Part of the mstodo module — see mstodo/mstodo.h for the
// overview.
#pragma once

#include "log/redact.h"
#include "sync/http.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace brulion::mstodo {

/// Microsoft Graph.
inline constexpr QStringView GraphHost = u"graph.microsoft.com";

struct DownloadLimits {
    /// Attachments larger than this are left out (To Do itself takes up to 25 MB per file).
    qint64 maxAttachmentBytes = 25LL * 1024 * 1024;
    /// All attachments together; the rest are left out (with a warning).
    qint64 maxTotalAttachmentBytes = 100LL * 1024 * 1024;
    /// Tasks of all lists together; more are left out (the importer says the export is incomplete).
    int maxTasks = 100000;
    /// Retries of one request after 429 / 503 / 504 or a dropped connection.
    int maxRetries = 4;
    /// Longest wait for one Retry-After, in seconds.
    int maxRetryAfterSeconds = 60;
    /// Time limit of one request in ms.
    int timeoutMs = 60000;
};

/// Downloads everything: lists, tasks (with steps and links), attachments. Requests run one after
/// another. Emits exactly one of finished / failed per start() — none after cancel().
class TodoDownload : public QObject {
    Q_OBJECT
public:
    /// `transport` is not owned and must outlive the download.
    explicit TodoDownload(sync::HttpTransport *transport, QObject *parent = nullptr);
    ~TodoDownload() override;

    void setLimits(const DownloadLimits &limits) { m_limits = limits; }
    /// Length of one second of Retry-After in ms (default 1000; tests).
    void setTimeUnitMs(int ms) { m_unitMs = qMax(1, ms); }

    /// Start with an access token from DeviceCodeLogin (cancels a running download). The token
    /// is kept in memory until the download ends.
    void start(const QString &accessToken);
    void cancel();
    bool active() const { return m_active; }

Q_SIGNALS:
    /// `listsDone` of `lists` downloaded, `tasks` so far.
    void progress(int listsDone, int lists, int tasks);
    /// The bundle (UTF-8 JSON) and what was left out (translated).
    void finished(const QByteArray &bundle, const QStringList &warnings);
    /// "unauthorized" (sign-in expired or not accepted), "forbidden" (no Tasks.Read),
    /// "network", "throttled", "server", "no-mailbox"; `message` translated.
    void failed(const QString &code, const QString &message);

private:
    struct ListData {
        QJsonObject list;
        QList<QJsonObject> tasks;
        bool incomplete = false;
    };
    using Json = std::function<void(const QJsonObject &)>;
    using Failure = std::function<void(const sync::HttpResponse &)>;

    void get(const QUrl &url, Json ok, Failure failure = {}, int attempt = 0);
    void collect(const QUrl &url, QJsonArray items, std::function<void(QJsonArray, bool incomplete)> done,
                 Failure failure = {});
    void fetchList(qsizetype index);
    void fetchTasks(qsizetype index, bool expand);
    void queueTaskDetails(qsizetype index);
    void runJobs(std::function<void()> then);
    void finish();
    void fail(const QString &code, const QString &message);
    void failWith(const sync::HttpResponse &response);
    void warn(const QString &message);
    QUrl graph(const QString &path, const QString &query = {}) const;
    bool isGraphUrl(const QUrl &url) const;

    sync::HttpTransport *m_transport;
    DownloadLimits m_limits;
    int m_unitMs = 1000;
    QString m_token;
    logging::ScopedSecret m_secret;
    QList<ListData> m_lists;
    QList<std::function<void(std::function<void()>)>> m_jobs;
    QStringList m_warnings;
    int m_taskCount = 0;
    qint64 m_attachmentBytes = 0;
    int m_generation = 0;
    bool m_active = false;
    bool m_expand = true; ///< Graph accepts $expand=checklistItems,linkedResources on tasks
};

} // namespace brulion::mstodo

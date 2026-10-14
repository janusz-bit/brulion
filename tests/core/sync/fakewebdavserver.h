// In-memory WebDAV server (Nextcloud flavour) implementing HttpTransport, for tests.
// Supports PROPFIND (Depth 0/1; "infinity" is refused like Nextcloud does), GET, PUT
// (If-Match / If-None-Match, 409 when the parent is missing), DELETE (If-Match), MKCOL
// (405 if it exists, 409 if the parent is missing) and Basic auth. Responses arrive from the
// event loop (optionally after a per-method latency). Hooks inject failures, stalls and
// concurrent changes; every request is logged.
#pragma once

#include "sync/http.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace synctest {

using brulion::sync::HttpCallback;
using brulion::sync::HttpRequest;
using brulion::sync::HttpResponse;

class FakeWebDavServer : public QObject, public brulion::sync::HttpTransport {
    Q_OBJECT
public:
    struct Options {
        QString root = QStringLiteral("/remote.php/dav/files/jan"); ///< DAV root of the user
        QString origin = QStringLiteral("https://chmura.example.pl");
        QString username = QStringLiteral("jan");
        QString password = QStringLiteral("hasło-źdźbło");
        QString prefix = QStringLiteral("d"); ///< namespace prefix: "d" (Nextcloud), "D" (Apache), "" (default ns)
        bool fullUrlHrefs = false;            ///< hrefs as full URLs instead of absolute paths
        bool putEtag = true;                  ///< send ETag headers on PUT responses
        bool noEtags = false;                 ///< no ETags at all (getetag, ETag headers)
        std::function<qint64()> now;          ///< clock (ms); default 2026-10-14 08:00:00 UTC
    };

    struct Node {
        bool isDir = false;
        QByteArray data;
        qint64 mtime = 0;
        QString etag;
    };

    struct LoggedRequest {
        QByteArray method;
        QString path; ///< decoded absolute server path, no trailing slash
        QList<QPair<QByteArray, QByteArray>> headers;
        QByteArray header(QByteArrayView name) const;
    };

    /// A hook may return a response to replace the normal handling (noResponse(): never answer).
    using Hook = std::function<std::optional<HttpResponse>(const HttpRequest &, const QString &path)>;

    FakeWebDavServer();
    explicit FakeWebDavServer(Options options, QObject *parent = nullptr);
    ~FakeWebDavServer() override;

    void send(const HttpRequest &request, HttpCallback done) override;

    /// A response for hooks meaning "never answer" (a stalled connection).
    static HttpResponse noResponse();
    static HttpResponse status(int code);

    const Options &options() const { return m_options; }
    /// URL of a folder below the user root (for WebDavClient's base URL).
    QString url(const QString &folder = {}) const;
    /// Absolute decoded server path of a path relative to the user root.
    QString abs(const QString &rel) const;

    /// Create a file directly on the server (as another client would), parents included.
    void putFile(const QString &rel, const QByteArray &content);
    void putText(const QString &rel, const QString &text) { putFile(rel, text.toUtf8()); }
    void mkdir(const QString &rel);
    /// Remove a file or folder directly on the server.
    void remove(const QString &rel);
    std::optional<QString> text(const QString &rel) const;
    std::optional<QByteArray> bytes(const QString &rel) const;
    QString etag(const QString &rel) const;
    bool isDir(const QString &rel) const;
    /// New ETag without a content change (Nextcloud does that sometimes).
    void touchEtag(const QString &rel);
    /// Files below `folder` (relative to the user root): path -> text.
    QMap<QString, QString> files(const QString &folder = {}) const;

    QList<LoggedRequest> requests(const QByteArray &method = {}) const;
    void clearLog() { m_log.clear(); }
    qsizetype logSize() const { return m_log.size(); }

    QList<Hook> hooks;

    /// Delay responses to `method` by `ms` (0 = next event loop iteration).
    void setLatency(const QByteArray &method, int ms) { m_latency.insert(method, ms); }
    /// Highest number of requests of `method` in flight at once.
    int peakInFlight(const QByteArray &method) const { return m_peak.value(method); }

private:
    HttpResponse handle(const HttpRequest &request, const QString &path);
    HttpResponse propfind(const HttpRequest &request, const QString &path) const;
    HttpResponse get(const QString &path) const;
    HttpResponse put(const HttpRequest &request, const QString &path);
    HttpResponse del(const HttpRequest &request, const QString &path);
    HttpResponse mkcol(const QString &path);
    QString nextEtag();
    void mkdirs(const QString &absPath);
    qint64 now() const;

    Options m_options;
    QMap<QString, Node> m_nodes;
    QList<LoggedRequest> m_log;
    QHash<QByteArray, int> m_latency;
    QHash<QByteArray, int> m_inFlight;
    QHash<QByteArray, int> m_peak;
    int m_etagCounter = 0;
};

} // namespace synctest

// Requests of the import downloads (cloudimport.h): one service's HTTP through sync::HttpTransport
// with its access token, retries after 429 / 503 / 504 and dropped connections (Retry-After in
// seconds or as an HTTP date, else 1, 2, 4 … s), only to the service's own hosts over https, and
// a body size limit. Single-threaded; callbacks never run after cancel(). Part of the cloudimport
// module.
#pragma once

#include "log/redact.h"
#include "sync/http.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <functional>

namespace brulion::cloudimport {

struct ApiLimits {
    int maxRetries = 4;
    int maxRetryAfterSeconds = 60;
    int timeoutMs = 60000;
    /// The largest answer accepted (a file, a page of records).
    qint64 maxBodyBytes = 64LL * 1024 * 1024;
};

class ApiClient : public QObject {
    Q_OBJECT
public:
    /// `transport` is not owned and must outlive the client.
    explicit ApiClient(sync::HttpTransport *transport, QObject *parent = nullptr);

    void setLimits(const ApiLimits &limits) { m_limits = limits; }
    const ApiLimits &limits() const { return m_limits; }
    /// Length of one second of Retry-After and of spacing in ms (default 1000; tests).
    void setTimeUnitMs(int ms) { m_unitMs = std::max(1, ms); }
    int timeUnitMs() const { return m_unitMs; }
    /// The pause between two requests in "seconds" × 1000 (Airtable allows 5 a second).
    void setSpacingMs(int ms) { m_spacingMs = std::max(0, ms); }

    /// The token sent as "Authorization: Bearer …" to the hosts `authorized` accepts (registered
    /// with log::redact while set).
    void setToken(const QString &token);
    /// Which hosts may be asked at all (https only), and which of them get the token.
    void setHosts(std::function<bool(const QUrl &)> allowed, std::function<bool(const QUrl &)> authorized);

    using Done = std::function<void(const sync::HttpResponse &)>;
    /// Send `request` (GET when no method): a 2xx answer, or the last failed one after the retries,
    /// reaches `done`. A URL the hosts refuse is answered at once with status 0 and networkError
    /// "refused".
    void send(sync::HttpRequest request, Done done);
    /// Forget the token and stop: no callback of an earlier request runs.
    void cancel();
    bool allowed(const QUrl &url) const;

private:
    void attempt(const sync::HttpRequest &request, const Done &done, int attempt, int generation);

    sync::HttpTransport *m_transport;
    ApiLimits m_limits;
    QString m_token;
    logging::ScopedSecret m_secret;
    std::function<bool(const QUrl &)> m_allowed;
    std::function<bool(const QUrl &)> m_authorized;
    int m_unitMs = 1000;
    int m_spacingMs = 0;
    int m_generation = 0;
};

/// "https://graph.microsoft.com/…" and nothing else: no user info, the default port.
bool isHttpsHost(const QUrl &url, QStringView host);
/// The host or any subdomain of it ("files.todoist.com" for "todoist.com").
bool isHttpsDomain(const QUrl &url, QStringView domain);

} // namespace brulion::cloudimport

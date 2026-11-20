// ICS subscriptions (docs/UI.md "Calendars"): a calendar's address as people paste it (http(s),
// webcal://, webcals://), fetched read-only with conditional requests (If-None-Match /
// If-Modified-Since: an unchanged feed answers 304 and costs nothing), redirects followed by hand
// (never from https down to http; no credentials ever travel), a size limit and a check that the
// answer is a calendar (an HTML error page is none). Part of the caldav module — see
// caldav/caldav.h.
#pragma once

#include "sync/errors.h"
#include "sync/http.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

namespace brulion::caldav {

/// The address a subscription fetches: webcal:// and webcals:// become https:// (Apple's scheme
/// for "subscribe"; a calendar server answers https today), http and https stay; user info is
/// dropped (a feed sends no credentials). Invalid (`error` says why, translated) for anything
/// else: no host, another scheme, spaces.
QUrl feedUrl(const QString &typed, QString *error = nullptr);

/// Does `data` look like iCalendar (BEGIN:VCALENDAR after a BOM and blank lines)?
bool looksLikeCalendar(QByteArrayView data);

/// What one fetch brought.
struct FeedResult {
    bool notModified = false; ///< 304: the cached copy is current
    QByteArray data;          ///< the calendar (200)
    QString etag;             ///< the answer's ETag as sent ("" none)
    QString lastModified;     ///< its Last-Modified as sent
    QUrl url;                 ///< where it came from after redirects
    sync::WebDavError error;  ///< failures: network, timeout, auth (a private feed), not found, parse
    /// The browser refused the answer: no status at all, typically CORS (docs/WEB.md "Calendars
    /// in the browser").
    bool maybeCors = false;

    bool ok() const { return !error.isError(); }
};

/// Fetches subscriptions through an HttpTransport. One fetch at a time per call; callbacks come
/// from the event loop.
class FeedFetcher {
public:
    static constexpr qint64 DefaultMaxBytes = 16LL * 1024 * 1024;
    static constexpr int DefaultTimeoutMs = 60'000;
    static constexpr int MaxRedirects = 5;

    explicit FeedFetcher(sync::HttpTransport *transport);

    /// The largest feed accepted (default 16 MB): a larger answer fails with Oversized.
    void setMaxBytes(qint64 bytes) { m_maxBytes = bytes; }
    void setTimeout(int msec) { m_timeoutMs = msec; }
    /// In the browser the page cannot set a User-Agent (the browser sends its own).
    void setBrowser(bool browser) { m_browser = browser; }

    /// GET `url` (already a feedUrl()); `etag` / `lastModified`: the cached copy's validators
    /// ("" none).
    void fetch(const QUrl &url, const QString &etag, const QString &lastModified,
               std::function<void(const FeedResult &)> done);

private:
    void step(const QUrl &url, const QString &etag, const QString &lastModified, int redirects,
              std::function<void(const FeedResult &)> done);

    sync::HttpTransport *m_transport;
    qint64 m_maxBytes = DefaultMaxBytes;
    int m_timeoutMs = DefaultTimeoutMs;
    bool m_browser = false;
};

/// An HttpTransport over QNetworkAccessManager for subscriptions: it follows redirects to other
/// servers (a feed carries no credentials) but never from https to http, keeps no cookies and
/// stops an answer above the request's limit. The app's NetworkHttpTransport refuses other
/// servers, which CalDAV and Nextcloud need (their requests carry the password).
class FeedHttpTransport : public QObject, public sync::HttpTransport {
    Q_OBJECT
public:
    explicit FeedHttpTransport(QObject *parent = nullptr);
    ~FeedHttpTransport() override;
    void send(const sync::HttpRequest &request, sync::HttpCallback done) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace brulion::caldav

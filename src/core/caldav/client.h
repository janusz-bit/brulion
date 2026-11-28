// The CalDAV client (RFC 4791, RFC 6578, RFC 6764) over sync::HttpTransport: discovery (the
// address given, its /.well-known/caldav, current-user-principal, calendar-home-set, the
// calendars with their names, colours, components, privileges, ctag and sync token), the changes
// of a calendar since a sync token (sync-collection; a refused token or a server without it:
// every member's ETag, compared by the caller), calendar-multiget (else GET one by one), and
// writes with If-Match / If-None-Match. Basic authentication, and Digest when the server asks for
// it (Baïkal, some Radicale setups). The password travels only to the account's server, or to
// another host of the same site over https (iCloud keeps calendars on p…-caldav.icloud.com).
// Part of the caldav module — see caldav/caldav.h.
#pragma once

#include "sync/errors.h"
#include "sync/http.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>

namespace brulion::caldav {

/// A calendar collection of an account.
struct CalendarInfo {
    QString url;            ///< absolute address, ending in "/"
    QString name;           ///< displayname ("" none)
    QString color;          ///< "#rrggbb" ("" none)
    QString description;
    QStringList components; ///< "VEVENT", "VTODO" — empty: the server does not say (both)
    bool readOnly = false;  ///< no write privilege
    QString ctag;           ///< calendarserver getctag ("" none)
    QString syncToken;      ///< DAV:sync-token ("" none: no sync-collection)
    int order = 0;          ///< apple calendar-order

    bool holdsEvents() const { return components.isEmpty() || components.contains(QStringLiteral("VEVENT")); }
    bool holdsTasks() const { return components.isEmpty() || components.contains(QStringLiteral("VTODO")); }
    friend bool operator==(const CalendarInfo &, const CalendarInfo &) = default;
};

struct Discovery {
    QString principalUrl;
    QString homeUrl;
    /// The principal's calendar-user-address-set (RFC 6638): how invitations name the account —
    /// participantAddress() form ("jan@example.pl"; a URI as written).
    QStringList addresses;
    QList<CalendarInfo> calendars; ///< by order, then name
    sync::WebDavError error;
};

/// A member of a calendar: its address (an absolute path, decoded), ETag and — fetched — data.
struct RemoteResource {
    QString path;
    QString etag;
    QByteArray data;
    friend bool operator==(const RemoteResource &, const RemoteResource &) = default;
};

/// What changed in a calendar.
struct ChangeSet {
    QList<RemoteResource> changed; ///< new or changed members (ETags; no data yet)
    QStringList removed;           ///< members gone since the token (sync-collection only)
    QString syncToken;             ///< the token to ask with next time ("" none)
    /// The server listed every member (no token, a refused one, no sync-collection): `changed`
    /// holds all of them and `removed` is empty — whatever the caller knew and is not listed is gone.
    bool complete = false;
    sync::WebDavError error;
};

struct WriteResult {
    QString etag; ///< the new ETag ("" when the server sent none: fetch it)
    QString path; ///< where it was written (a server may choose another name: Location)
    sync::WebDavError error;
};

/// Credentials may go from `server` (the account's address) to `target`: the same scheme, host and
/// port, or https to another host of the same site (the last two labels of the name, three for
/// "co.uk"-like ones).
bool mayCarryCredentials(const QUrl &server, const QUrl &target);

class CalDavClient {
public:
    static constexpr int DefaultTimeoutMs = 60'000;
    static constexpr int MultigetBatch = 50;
    static constexpr qint64 MaxAnswerBytes = 64LL * 1024 * 1024;

    /// `server`: the account's address as given (where the password may go, mayCarryCredentials()).
    CalDavClient(sync::HttpTransport *transport, const QUrl &server, const QString &user, const QString &password);
    ~CalDavClient();
    CalDavClient(const CalDavClient &) = delete;
    CalDavClient &operator=(const CalDavClient &) = delete;

    void setTimeout(int msec);
    /// In the browser the page sends no User-Agent of its own.
    void setBrowser(bool browser);
    QUrl server() const;
    QString user() const;
    /// Requests sent so far, answered or not (statistics).
    int requestCount() const;

    /// Find the account's calendars: the address itself, then its /.well-known/caldav (a server
    /// that redirects is followed), then the principal and its calendar home set.
    void discover(std::function<void(const Discovery &)> done);
    /// The calendars of a calendar home.
    void listCalendars(const QUrl &home, std::function<void(const QList<CalendarInfo> &, const sync::WebDavError &)> done);
    /// One calendar's ctag and sync token (cheap: Depth 0).
    void calendarState(const QUrl &calendar, std::function<void(const CalendarInfo &, const sync::WebDavError &)> done);
    /// What changed since `syncToken` ("" or refused: everything, `complete`).
    void changes(const QUrl &calendar, const QString &syncToken, std::function<void(const ChangeSet &)> done);
    /// The data of `paths` (members of `calendar`): calendar-multiget in batches, else GET each.
    /// Members the server no longer has are left out.
    void fetch(const QUrl &calendar, const QStringList &paths,
               std::function<void(const QList<RemoteResource> &, const sync::WebDavError &)> done);
    /// PUT `data` (text/calendar) at `path`: `ifMatch` the ETag it must still have; "" = a new
    /// member (If-None-Match: *). 412: changed meanwhile (Precondition).
    void put(const QString &path, const QByteArray &data, const QString &ifMatch,
             std::function<void(const WriteResult &)> done);
    /// DELETE `path` if it still has `ifMatch` ("" = whatever it is). A member already gone is fine.
    void remove(const QString &path, const QString &ifMatch, std::function<void(const sync::WebDavError &)> done);

    /// The absolute URL of a member's path on the calendar's server.
    QUrl urlOf(const QUrl &calendar, const QString &path) const;
    /// A server's href (relative, absolute path or full URL, percent-encoded) as a member path.
    static QString pathOf(const QUrl &base, const QString &href);

private:
    struct Private;
    std::shared_ptr<Private> d;
};

} // namespace brulion::caldav

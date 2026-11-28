// An in-memory CalDAV server for the caldav module's tests (and the app's): discovery
// (/.well-known/caldav redirecting to the DAV root, current-user-principal, calendar-home-set, the
// calendars with displayname, calendar-color, supported-calendar-component-set, getctag,
// sync-token and privileges), PROPFIND Depth 0 / 1, REPORT sync-collection (RFC 6578: changes
// since a token, removed members as 404, an unknown token refused with valid-sync-token) and
// calendar-multiget, GET, PUT (If-Match / If-None-Match: *), DELETE (If-Match), Basic or Digest
// authentication. Answers come from the event loop, like a real transport's. Options turn
// features off (no sync-collection, no multiget, no ETag on PUT, another namespace prefix) and
// hooks inject failures; every request is logged.
#pragma once

#include "sync/http.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace caldavtest {

class FakeCalDav : public QObject, public brulion::sync::HttpTransport {
    Q_OBJECT
public:
    struct Options {
        QString origin = QStringLiteral("https://dav.example.pl");
        QString user = QStringLiteral("jan");
        QString password = QStringLiteral("hasło-źdźbło 1");
        QString root = QStringLiteral("/dav/");
        bool digest = false;          ///< Digest authentication instead of Basic
        bool wellKnown = true;        ///< /.well-known/caldav redirects to the root
        bool syncCollection = true;   ///< REPORT sync-collection
        bool multiget = true;         ///< REPORT calendar-multiget
        bool putEtag = true;          ///< ETag header on PUT answers
        bool privileges = true;       ///< current-user-privilege-set
        QString prefix = QStringLiteral("d"); ///< DAV: prefix in answers ("" = default namespace)
        /// calendar-user-address-set of the principal (its own path is added after these)
        QStringList addresses = {QStringLiteral("mailto:Jan.Kowalski@example.pl"), QStringLiteral("MAILTO:jan@example.pl"),
                                 QStringLiteral("mailto:JAN@example.pl")};
    };
    struct Member {
        QByteArray data;
        QString etag;
    };
    struct Calendar {
        QString name;
        QString color;
        QStringList components{QStringLiteral("VEVENT"), QStringLiteral("VTODO")};
        bool readOnly = false;
        QMap<QString, Member> members; ///< file name -> member
        int ctag = 1;
    };
    struct LoggedRequest {
        QByteArray method;
        QString path;
        QByteArray depth;
        QByteArray ifMatch;
        QByteArray ifNoneMatch;
        QByteArray body;
        bool authorized = false;
    };
    using Hook = std::function<std::optional<brulion::sync::HttpResponse>(const brulion::sync::HttpRequest &,
                                                                          const QString &path)>;

    FakeCalDav();
    explicit FakeCalDav(Options options, QObject *parent = nullptr);
    ~FakeCalDav() override;

    void send(const brulion::sync::HttpRequest &request, brulion::sync::HttpCallback done) override;

    Options &options() { return m_options; }
    QString principalPath() const;
    QString homePath() const;
    /// The absolute URL of a path ("https://dav.example.pl/dav/calendars/jan/praca/").
    QString url(const QString &path) const;
    QString calendarUrl(const QString &slug) const;

    // ---- the server's contents (what another client would change) -----------------------------
    void addCalendar(const QString &slug, const Calendar &calendar);
    void removeCalendar(const QString &slug);
    /// Write a member as another client would (new ETag, logged for sync-collection).
    QString putMember(const QString &slug, const QString &name, const QByteArray &data);
    void removeMember(const QString &slug, const QString &name);
    std::optional<Member> member(const QString &slug, const QString &name) const;
    QStringList memberNames(const QString &slug) const;
    const Calendar *calendar(const QString &slug) const;
    /// Forget the change log (sync tokens before now become invalid).
    void expireTokens();

    QList<LoggedRequest> requests;
    void clearLog() { requests.clear(); }
    int count(const QByteArray &method) const;
    Hook hook;

private:
    brulion::sync::HttpResponse handle(const brulion::sync::HttpRequest &request, const QString &path, LoggedRequest &log);
    bool authorized(const brulion::sync::HttpRequest &request);
    brulion::sync::HttpResponse unauthorized() const;
    QString tag(const QString &name) const;
    QString nsDecl() const;
    QByteArray multistatus(const QString &inner, const QString &extra = {}) const;
    QString calendarProps(const QString &slug, const Calendar &c) const;
    void logChange(const QString &slug, const QString &name);
    QString newEtag();

    Options m_options;
    QMap<QString, Calendar> m_calendars;
    struct Change {
        int token;
        QString slug;
        QString name;
    };
    QList<Change> m_changes;
    int m_token = 100;
    int m_firstToken = 100;
    int m_etag = 0;
    QString m_nonce = QStringLiteral("bm9uY2UtYnJ1bGlvbg");
};

} // namespace caldavtest

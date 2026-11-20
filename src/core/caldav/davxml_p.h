// WebDAV / CalDAV XML of the client (client.h): request bodies and a tolerant, namespace-aware
// parser of 207 Multi-Status answers keeping every property of every <response> (the sync module's
// parser keeps only what file sync needs). Internal to the caldav module.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::caldav::detail {

inline constexpr QStringView NsDav = u"DAV:";
inline constexpr QStringView NsCalDav = u"urn:ietf:params:xml:ns:caldav";
inline constexpr QStringView NsCalendarServer = u"http://calendarserver.org/ns/";
inline constexpr QStringView NsApple = u"http://apple.com/ns/ical/";

/// One property's value: its text (calendar-data, getetag, displayname …), the <href>s inside it
/// (current-user-principal, calendar-home-set), the local names of its child elements
/// (resourcetype: collection, calendar; current-user-privilege-set's privileges) and the `name`
/// attributes of <comp> children (supported-calendar-component-set).
struct PropValue {
    QString text;
    QStringList hrefs;
    QStringList children;
    QStringList components;
};

struct DavResponse {
    QString href;    ///< as sent (percent-encoded)
    int status = 0;  ///< the response's own <status> (sync-collection: 404 for a removed member); 0 none
    /// Properties of successful propstats, by "namespace|local-name".
    QHash<QString, PropValue> props;

    const PropValue *prop(QStringView ns, QStringView name) const;
    QString text(QStringView ns, QStringView name) const;
};

struct Multistatus {
    QList<DavResponse> responses;
    QString syncToken; ///< <sync-token> of a sync-collection answer
    QString error;     ///< not a multistatus (an HTML page, broken XML): why
    /// An answer of <error> (a refused sync token: valid-sync-token).
    QStringList errorConditions;
    bool ok() const { return error.isEmpty(); }
};

/// Parse a multistatus (at most `maxResponses` responses; more is an error).
Multistatus parseMultistatus(QByteArrayView xml, int maxResponses = 100'000);
/// The precondition names of an <error> body ("valid-sync-token", "number-of-matches-within-limits").
QStringList parseErrorConditions(QByteArrayView xml);

/// PROPFIND bodies.
QByteArray propfindPrincipal();     ///< current-user-principal (+ resourcetype)
QByteArray propfindHomeSet();       ///< calendar-home-set, displayname
QByteArray propfindCalendars();     ///< the calendars of a home: names, colours, components, tags
QByteArray propfindEtags();         ///< getetag, resourcetype (the ETag fallback)
QByteArray propfindCalendarState(); ///< getctag, sync-token of one calendar
/// REPORT bodies.
QByteArray syncCollection(const QString &token);
QByteArray calendarMultiget(const QStringList &hrefs);
/// PROPPATCH body setting the display name and colour of a calendar ("" leaves one out).
QByteArray proppatchCalendar(const QString &name, const QString &color);

} // namespace brulion::caldav::detail

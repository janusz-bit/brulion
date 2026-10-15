// iCalendar (RFC 5545) reading and writing: content lines with folding and TEXT escaping,
// components, DATE / DATE-TIME values with time zones, durations and the RRULE subset that maps
// to Brulion's recurrence rules. Part of the io module — see io/io.h for the overview.
#pragma once

#include "dates/recurrence.h"
#include "types.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTimeZone>

#include <optional>
#include <utility>

namespace brulion::io::ical {

/// A content line: `NAME;PARAM=value:VALUE`. Names and parameter names are upper-cased;
/// `value` is raw (TEXT values are still escaped — see unescapeText()).
struct Property {
    QString name;
    QHash<QString, QString> params;
    QString value;

    QString param(const QString &key) const { return params.value(key); }
};

/// A component (`BEGIN:VEVENT` … `END:VEVENT`) with its properties and sub-components.
struct Component {
    QString name;
    QList<Property> properties;
    QList<Component> children;

    /// First property called `key` (upper case), or nullptr.
    const Property *property(QStringView key) const;
    /// Unescaped TEXT value of the first property called `key` ("" when missing).
    QString text(QStringView key) const;
    /// Sub-components called `componentName`.
    QList<const Component *> childrenNamed(QStringView componentName) const;
};

/// Parse iCalendar data: lines unfolded (CRLF/LF + space or tab, on bytes, so a fold inside a
/// UTF-8 sequence is harmless), decoded as UTF-8 (BOM skipped; data that is not UTF-8 is read as
/// windows-1250 / windows-1252 like other text files), parsed into components. Lines outside any
/// component, unmatched END lines and malformed lines are ignored (with a warning);
/// unterminated components are closed at the end. Returns the top-level components (VCALENDAR).
QList<Component> parse(QByteArrayView data, QStringList *warnings = nullptr);

/// TEXT unescaping: `\\` `\;` `\,` `\n` `\N` (a lone backslash is kept).
QString unescapeText(QStringView value);
/// TEXT escaping: `\` `;` `,` and line breaks (CRLF, CR and LF become `\n`); other control
/// characters (not allowed in TEXT) are dropped, tabs kept.
QString escapeText(QStringView text);

/// Fold one content line (without terminator) to lines of at most 75 octets joined by
/// "\r\n " — never inside a UTF-8 sequence.
QByteArray foldLine(const QString &line);

/// A DATE or DATE-TIME value as written.
struct DateTimeValue {
    QDate date;
    std::optional<QTime> time; ///< nullopt for DATE values (all-day)
    bool utc = false;          ///< trailing "Z"
    QString tzid;              ///< TZID parameter, empty for UTC and floating times
};

/// Parse the value of a DTSTART / DTEND / DUE / COMPLETED / RECURRENCE-ID / TRIGGER property
/// (`VALUE=DATE` or an 8-digit value gives a date; seconds are dropped). Lenient about "-" and
/// ":" separators. nullopt for anything else.
std::optional<DateTimeValue> parseDateTime(const Property &property);

/// Local wall-clock date(-time) in `zone`: UTC and TZID times are converted, floating times and
/// dates are kept. An unknown TZID is treated as floating (`unknownZone` is set when given).
DateSpec toWallClock(const DateTimeValue &value, const QTimeZone &zone, bool *unknownZone = nullptr);

/// A duration as RFC 5545 3.3.6 counts it: `days` (weeks and days — nominal: the same wall-clock
/// time on another day, whatever DST does) and `seconds` (hours, minutes, seconds — exact).
/// Both carry the sign of the duration.
struct Duration {
    qint64 days = 0;
    qint64 seconds = 0;
};

/// A duration (`-PT15M`, `P1DT2H`, `P2W`) split into nominal days and exact seconds; nullopt
/// when malformed.
std::optional<Duration> parseDurationParts(QStringView value);

/// A duration in seconds (days counted as 86400 s); nullopt when malformed.
std::optional<qint64> parseDuration(QStringView value);

/// RRULE -> recurrence rule. Supported: FREQ=DAILY/WEEKLY/MONTHLY/YEARLY, INTERVAL, BYDAY
/// (weekdays for WEEKLY; MO–FR for DAILY = "every weekday"), BYMONTHDAY (one day, -1 = last).
/// Parts that cannot be represented but leave a usable rule (COUNT, UNTIL, BYMONTH, WKST, …)
/// are listed in `dropped`. nullopt when the rule cannot be represented (hourly rules, "second
/// Tuesday", several month days, …).
std::optional<dates::RecurrenceRule> parseRRule(QStringView value, QStringList *dropped = nullptr);

/// Recurrence rule -> RRULE value ("FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,FR"). "when done" cannot be
/// expressed and is ignored.
QString formatRRule(const dates::RecurrenceRule &rule);

/// "20261015" / "20261015T090000" (floating local time) / "20261015T070000Z".
QString formatDate(QDate date);
QString formatLocalDateTime(QDate date, QTime time);
QString formatUtc(const QDateTime &dateTime);

/// Writes iCalendar data: CRLF line endings, folded lines, escaped TEXT values.
class Writer {
public:
    void begin(QStringView component);
    void end(QStringView component);
    /// A property with a raw (already formatted) value. Parameter values containing `:` `;`
    /// or `,` are quoted (double quotes inside are dropped).
    void property(QStringView name, QStringView value, const QList<std::pair<QString, QString>> &params = {});
    /// A TEXT property (value escaped).
    void text(QStringView name, QStringView value, const QList<std::pair<QString, QString>> &params = {});

    const QByteArray &data() const { return m_data; }

private:
    QByteArray m_data;
};

} // namespace brulion::io::ical

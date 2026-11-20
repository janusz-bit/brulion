// Time zones of iCalendar data: a TZID is resolved to a zone of the system's database (IANA ids,
// "/mozilla.org/…" prefixes, Windows names), else to the calendar's own VTIMEZONE definition
// (STANDARD / DAYLIGHT observances with their DTSTART, RRULE, RDATE and offsets — the only way
// on builds without a time zone database: WebAssembly), else the time is floating (taken as
// written). Converted to the device's wall clock only for display: the notes keep wall-clock
// times (DateSpec) and never go through UTC. Part of the caldav module — see caldav/caldav.h.
#pragma once

#include "caldav/rrule.h"
#include "io/ical.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QTimeZone>

#include <optional>

namespace brulion::caldav {

/// One VTIMEZONE: its observances.
struct ZoneDefinition {
    struct Observance {
        bool daylight = false;
        NaiveDateTime start; ///< DTSTART: local time in the offset before (offsetFrom)
        int offsetFrom = 0;  ///< seconds east of UTC
        int offsetTo = 0;
        std::optional<RRule> rule;
        QList<NaiveDateTime> rdates; ///< local times, like DTSTART
    };
    QString tzid;
    QList<Observance> observances;

    /// The UTC offset (seconds) in effect at the UTC instant `utcMs`; nullopt without observances.
    std::optional<int> offsetAtUtc(qint64 utcMs) const;
    /// The UTC instant (ms) of the local time `local`: a time in a gap (spring forward) is taken
    /// in the offset before it (so it lands after the gap), a repeated one (fall back) is the
    /// first of the two. nullopt without observances.
    std::optional<qint64> toUtc(const NaiveDateTime &local) const;
};

/// A VTIMEZONE component -> its definition (observances without DTSTART or offsets skipped;
/// nullopt when nothing usable is left).
std::optional<ZoneDefinition> parseVTimezone(const io::ical::Component &component);

/// "+0100", "-0530", "+013045" -> seconds east of UTC; nullopt when malformed.
std::optional<int> parseUtcOffset(QStringView value);

/// The zone of the system's database a TZID names (IANA id, a "/mozilla.org/…/Europe/Warsaw"
/// style prefix, a Windows name); invalid when unknown — and always on builds without a time
/// zone database.
QTimeZone systemZoneFor(const QString &tzid);

/// The TZIDs of one calendar and how to turn their local times into instants.
class ZoneTable {
public:
    /// Add the VTIMEZONE components of a VCALENDAR (a later definition of a TZID replaces one).
    void addCalendar(const io::ical::Component &vcalendar);
    void add(const ZoneDefinition &zone);
    void merge(const ZoneTable &other);
    bool isEmpty() const { return m_zones.isEmpty(); }
    const ZoneDefinition *definition(const QString &tzid) const;

    /// The instant (ms since epoch) of a DATE-TIME: UTC as written, a TZID through the system's
    /// zone or the calendar's definition. nullopt for floating times, dates and unknown TZIDs
    /// (`unknownZone` set for the last).
    std::optional<qint64> instant(const io::ical::DateTimeValue &value, bool *unknownZone = nullptr) const;
    /// The local time of `value` in `target` (the device's zone): dates and floating times as
    /// written, the others converted. Seconds dropped.
    NaiveDateTime toTarget(const io::ical::DateTimeValue &value, const QTimeZone &target,
                           bool *unknownZone = nullptr) const;
    /// The wall-clock time an instant has in the zone of `like` (its TZID's, UTC's, or —
    /// floating — `target`'s): what UNTIL and RECURRENCE-ID values in UTC are compared as.
    NaiveDateTime inZoneOf(qint64 utcMs, const io::ical::DateTimeValue &like, const QTimeZone &target) const;

private:
    QHash<QString, ZoneDefinition> m_zones;
};

} // namespace brulion::caldav

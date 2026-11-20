#include "caldav/zones.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringTokenizer>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 MsPerSec = 1000;

qint64 naiveUtcMs(const NaiveDateTime &t)
{
    return QDateTime(t.date, t.time, QTimeZone::UTC).toMSecsSinceEpoch();
}

NaiveDateTime naiveFromUtcMs(qint64 ms)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC);
    return {t.date(), t.time()};
}

std::optional<NaiveDateTime> naiveOf(const io::ical::Property &p)
{
    const auto v = io::ical::parseDateTime(p);
    if (!v)
        return std::nullopt;
    return NaiveDateTime{v->date, v->time.value_or(QTime(0, 0))};
}

/// The onsets (UTC ms, offset after) of `zone`'s observances within the years [fromYear, toYear].
QList<std::pair<qint64, int>> transitions(const ZoneDefinition &zone, int fromYear, int toYear)
{
    QList<std::pair<qint64, int>> out;
    const NaiveDateTime windowFrom{QDate(fromYear, 1, 1), QTime(0, 0)};
    const NaiveDateTime windowTo{QDate(toYear + 1, 1, 1), QTime(0, 0)};
    ExpandLimits limits;
    limits.maxOccurrences = 400;
    limits.maxPeriods = 2000;
    limits.maxCandidates = 20'000;
    for (const ZoneDefinition::Observance &o : zone.observances) {
        QList<NaiveDateTime> onsets;
        if (o.rule) {
            std::optional<NaiveDateTime> until;
            if (o.rule->until) {
                NaiveDateTime u{o.rule->until->date, o.rule->until->time.value_or(QTime(23, 59, 59))};
                if (o.rule->until->utc)
                    u = u.addSecs(o.offsetFrom);
                until = u;
            }
            if (o.start < windowTo)
                onsets = expandRRule(*o.rule, o.start, false, windowFrom, windowTo, until, limits);
        } else if (o.start >= windowFrom && o.start < windowTo) {
            onsets.append(o.start);
        }
        for (const NaiveDateTime &r : o.rdates) {
            if (r >= windowFrom && r < windowTo)
                onsets.append(r);
        }
        for (const NaiveDateTime &local : std::as_const(onsets))
            out.append({naiveUtcMs(local) - qint64(o.offsetFrom) * MsPerSec, o.offsetTo});
    }
    std::ranges::sort(out);
    return out;
}

} // namespace

std::optional<int> parseUtcOffset(QStringView value)
{
    static const QRegularExpression re(u"^([+-])(\\d{2})(\\d{2})(\\d{2})?$"_s);
    const auto m = re.matchView(value.trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    const int h = m.captured(2).toInt();
    const int min = m.captured(3).toInt();
    const int s = m.capturedLength(4) ? m.captured(4).toInt() : 0;
    if (h > 23 || min > 59 || s > 59)
        return std::nullopt;
    const int total = h * 3600 + min * 60 + s;
    return m.captured(1) == u"-" ? -total : total;
}

std::optional<ZoneDefinition> parseVTimezone(const io::ical::Component &component)
{
    ZoneDefinition zone;
    zone.tzid = component.text(u"TZID").trimmed();
    if (zone.tzid.isEmpty())
        return std::nullopt;
    for (const io::ical::Component &child : component.children) {
        if (child.name != u"STANDARD" && child.name != u"DAYLIGHT")
            continue;
        ZoneDefinition::Observance o;
        o.daylight = child.name == u"DAYLIGHT";
        const io::ical::Property *start = child.property(u"DTSTART");
        const io::ical::Property *from = child.property(u"TZOFFSETFROM");
        const io::ical::Property *to = child.property(u"TZOFFSETTO");
        if (!start || !to)
            continue;
        const auto s = naiveOf(*start);
        const auto offTo = parseUtcOffset(to->value);
        const auto offFrom = from ? parseUtcOffset(from->value) : offTo;
        if (!s || !offTo || !offFrom)
            continue;
        o.start = *s;
        o.offsetFrom = *offFrom;
        o.offsetTo = *offTo;
        if (const io::ical::Property *rrule = child.property(u"RRULE"))
            o.rule = parseRRule(rrule->value);
        for (const io::ical::Property &p : child.properties) {
            if (p.name != u"RDATE")
                continue;
            for (const QStringView part : qTokenize(QStringView(p.value), u',', Qt::SkipEmptyParts)) {
                io::ical::Property single = p;
                single.value = part.toString();
                if (const auto r = naiveOf(single))
                    o.rdates.append(*r);
            }
        }
        zone.observances.append(o);
    }
    if (zone.observances.isEmpty())
        return std::nullopt;
    return zone;
}

std::optional<int> ZoneDefinition::offsetAtUtc(qint64 utcMs) const
{
    if (observances.isEmpty())
        return std::nullopt;
    const int year = naiveFromUtcMs(utcMs).date.year();
    const QList<std::pair<qint64, int>> list = transitions(*this, year - 1, year);
    int offset = 0;
    bool found = false;
    for (const auto &[at, off] : list) {
        if (at > utcMs)
            break;
        offset = off;
        found = true;
    }
    if (found)
        return offset;
    // Before every onset we know of: the earliest observance's offset, or — no onset within the
    // last two years, a zone that stopped changing — the latest one's.
    const auto earliest = std::ranges::min_element(observances, {}, &Observance::start);
    if (naiveUtcMs(earliest->start) - qint64(earliest->offsetFrom) * MsPerSec > utcMs)
        return earliest->offsetFrom;
    const auto latest = std::ranges::max_element(observances, {}, &Observance::start);
    return latest->offsetTo;
}

std::optional<qint64> ZoneDefinition::toUtc(const NaiveDateTime &local) const
{
    if (observances.isEmpty())
        return std::nullopt;
    QList<int> candidates;
    for (const Observance &o : observances) {
        if (!candidates.contains(o.offsetTo))
            candidates.append(o.offsetTo);
        if (!candidates.contains(o.offsetFrom))
            candidates.append(o.offsetFrom);
    }
    const qint64 asUtc = naiveUtcMs(local);
    std::optional<qint64> best;
    for (int o : std::as_const(candidates)) {
        const qint64 u = asUtc - qint64(o) * MsPerSec;
        if (offsetAtUtc(u) == o && (!best || u < *best))
            best = u;
    }
    if (best)
        return best;
    // A gap: the offset before it.
    const int maxOffset = *std::ranges::max_element(candidates);
    const int before = offsetAtUtc(asUtc - qint64(maxOffset) * MsPerSec - 3 * 3600 * MsPerSec).value_or(0);
    return asUtc - qint64(before) * MsPerSec;
}

QTimeZone systemZoneFor(const QString &tzid)
{
#if QT_CONFIG(timezone)
    const QString id = tzid.trimmed();
    if (id.isEmpty())
        return QTimeZone();
    if (id.compare(u"UTC"_s, Qt::CaseInsensitive) == 0 || id.compare(u"GMT"_s, Qt::CaseInsensitive) == 0
        || id == u"Etc/UTC" || id == u"Z")
        return QTimeZone(QTimeZone::UTC);
    QTimeZone zone(id.toUtf8());
    if (zone.isValid())
        return zone;
    const QStringList parts = id.split(u'/', Qt::SkipEmptyParts);
    for (qsizetype k = std::max<qsizetype>(0, parts.size() - 3); k < parts.size(); ++k) {
        const QTimeZone candidate(parts.mid(k).join(u'/').toUtf8());
        if (candidate.isValid())
            return candidate;
    }
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(id.toUtf8());
    if (!iana.isEmpty())
        return QTimeZone(iana);
    return QTimeZone();
#else
    if (tzid.trimmed().compare(u"UTC"_s, Qt::CaseInsensitive) == 0)
        return QTimeZone(QTimeZone::UTC);
    return QTimeZone();
#endif
}

void ZoneTable::addCalendar(const io::ical::Component &vcalendar)
{
    for (const io::ical::Component &c : vcalendar.children) {
        if (c.name != u"VTIMEZONE")
            continue;
        if (auto zone = parseVTimezone(c))
            add(*zone);
    }
}

void ZoneTable::add(const ZoneDefinition &zone)
{
    m_zones.insert(zone.tzid, zone);
}

void ZoneTable::merge(const ZoneTable &other)
{
    for (const ZoneDefinition &zone : other.m_zones)
        m_zones.insert(zone.tzid, zone);
}

const ZoneDefinition *ZoneTable::definition(const QString &tzid) const
{
    const auto it = m_zones.constFind(tzid);
    return it == m_zones.constEnd() ? nullptr : &*it;
}

std::optional<qint64> ZoneTable::instant(const io::ical::DateTimeValue &value, bool *unknownZone) const
{
    if (unknownZone)
        *unknownZone = false;
    if (!value.time)
        return std::nullopt;
    if (value.utc)
        return QDateTime(value.date, *value.time, QTimeZone::UTC).toMSecsSinceEpoch();
    if (value.tzid.isEmpty())
        return std::nullopt;
    const QTimeZone zone = systemZoneFor(value.tzid);
    if (zone.isValid()) {
        const QDateTime t(value.date, *value.time, zone);
        if (t.isValid())
            return t.toMSecsSinceEpoch();
    }
    if (const ZoneDefinition *def = definition(value.tzid))
        return def->toUtc({value.date, *value.time});
    if (unknownZone)
        *unknownZone = true;
    return std::nullopt;
}

NaiveDateTime ZoneTable::toTarget(const io::ical::DateTimeValue &value, const QTimeZone &target, bool *unknownZone) const
{
    if (!value.time)
        return {value.date, QTime(0, 0)};
    const auto ms = instant(value, unknownZone);
    if (!ms)
        return {value.date, QTime(value.time->hour(), value.time->minute())};
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(*ms, target);
    return {local.date(), QTime(local.time().hour(), local.time().minute())};
}

NaiveDateTime ZoneTable::inZoneOf(qint64 utcMs, const io::ical::DateTimeValue &like, const QTimeZone &target) const
{
    if (like.utc)
        return naiveFromUtcMs(utcMs);
    if (!like.tzid.isEmpty()) {
        const QTimeZone zone = systemZoneFor(like.tzid);
        if (zone.isValid()) {
            const QDateTime t = QDateTime::fromMSecsSinceEpoch(utcMs, zone);
            return {t.date(), t.time()};
        }
        if (const ZoneDefinition *def = definition(like.tzid))
            return naiveFromUtcMs(utcMs + qint64(def->offsetAtUtc(utcMs).value_or(0)) * MsPerSec);
    }
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(utcMs, target);
    return {t.date(), t.time()};
}

} // namespace brulion::caldav

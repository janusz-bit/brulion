#include "schedule.h"

#include "dates/format.h"
#include "dates/local.h"
#include "dates/natural.h"
#include "text/text.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion::schedule {

namespace {

using dates::RecurrenceRule;
using Freq = dates::RecurrenceRule::Freq;

/// "H:mm" ("7:30", "18:00").
QString clock(QTime time)
{
    return time.toString(u"H:mm"_s);
}

/// The text with its edges tidied: spaces simplified, a full stop at its end dropped.
QString tidy(QStringView text)
{
    QString out = text.toString().simplified();
    while (out.endsWith(u'.') || out.endsWith(u'!'))
        out.chop(1);
    return out.trimmed();
}

/// An interval in minutes ("co 3 godziny", "every 30 minutes", "hourly"); 0 none, -1 one out of
/// range. `key` is folded (foldKey()).
int intervalMinutes(const QString &key)
{
    static const QRegularExpression polish(
            u"^co\\s+(?:(\\d+)\\s*)?(godzin[aey]?|godz\\.?|h|minut[aey]?|min\\.?|m)$"_s);
    static const QRegularExpression english(
            u"^(?:every|each)\\s+(?:(\\d+)\\s*)?(hours?|hrs?|h|minutes?|mins?|m)$"_s);
    static const QRegularExpression halfHour(u"^(?:co\\s+pol\\s+godziny|every\\s+half\\s+(?:an\\s+)?hour)$"_s);
    static const QRegularExpression quarter(
            u"^(?:co\\s+kwadrans|every\\s+quarter\\s+(?:of\\s+an\\s+)?(?:an\\s+)?hour)$"_s);
    if (key == u"hourly" || key == u"co godzine")
        return 60;
    if (halfHour.match(key).hasMatch())
        return 30;
    if (quarter.match(key).hasMatch())
        return 15;
    QRegularExpressionMatch m = polish.match(key);
    if (!m.hasMatch())
        m = english.match(key);
    if (!m.hasMatch())
        return 0;
    const QString digits = m.captured(1);
    bool ok = true;
    const qint64 count = digits.isEmpty() ? 1 : digits.toLongLong(&ok);
    if (!ok || count <= 0 || count > MaxIntervalMinutes)
        return -1;
    const QString unit = m.captured(2);
    const bool hours = unit.startsWith(u'g') || unit.startsWith(u'h');
    const qint64 minutes = hours ? count * 60 : count;
    if (minutes < MinIntervalMinutes || minutes > MaxIntervalMinutes)
        return -1;
    return int(minutes);
}

/// The phrase found covers all of `text` (but spaces around it).
bool whole(const dates::NaturalDate &found, QStringView text)
{
    return text.left(found.start).trimmed().isEmpty() && text.mid(found.end()).trimmed().isEmpty();
}

/// A rule of days ("codziennie", "w poniedziałki", "every monday", "weekdays"): Brulion's
/// recurrence rules, and English weekdays in the plural after "on" ("on mondays and fridays" —
/// "on monday" is a single day, as "w piątek" is).
std::optional<RecurrenceRule> rule(QStringView text, dates::UiLocale locale)
{
    // A day named once ("w piątek", "on monday") is a date, whatever the rules read in it.
    if (const auto found =
                dates::parseNaturalDate(text, QDateTime(QDate(2001, 1, 1), QTime(0, 0)), Qt::Monday, locale);
        found && whole(*found, text))
        return std::nullopt;
    if (const auto r = dates::parseRecurrence(text))
        return r;
    const QString key = text::foldKey(text).simplified();
    if (key == u"weekdays" || key == u"on weekdays" || key == u"workdays" || key == u"dni robocze") {
        RecurrenceRule r;
        r.weekdaysOnly = true;
        return r;
    }
    if (!key.startsWith(u"on "_s))
        return std::nullopt;
    const QString rest = key.mid(3);
    static const QRegularExpression plural(u"[a-z]s\\b"_s);
    const QList<int> days = dates::parseWeekdayList(rest);
    if (days.isEmpty() || !plural.match(rest).hasMatch())
        return std::nullopt;
    RecurrenceRule r;
    r.freq = Freq::Week;
    r.weekdays = days;
    return dates::normalizeRule(r);
}

/// A time of day alone ("o 7:30", "at 9am", "rano"), no day named; nullopt otherwise.
std::optional<QTime> timeOnly(QStringView text, dates::UiLocale locale)
{
    // Read from two days: a phrase that names a day ("w piątek", "jutro") lands elsewhere than
    // the day itself in one of them. (A time of midnight is the next day's.)
    std::optional<QTime> time;
    for (const QDate day : {QDate(2001, 1, 1), QDate(2001, 6, 13)}) {
        const auto found = dates::parseNaturalDate(text, QDateTime(day, QTime(0, 0)), Qt::Monday, locale);
        if (!found || !whole(*found, text) || !found->spec.time)
            return std::nullopt;
        const qint64 offset = day.daysTo(found->spec.date);
        if (!(offset == 0 || (offset == 1 && *found->spec.time == QTime(0, 0))))
            return std::nullopt;
        if (time && *time != *found->spec.time)
            return std::nullopt;
        time = found->spec.time;
    }
    return time;
}

/// Monthly rules without a day run on the day the schedule is set up (a run's day would let the
/// series drift).
RecurrenceRule anchored(RecurrenceRule r, QDate today)
{
    if (r.freq == Freq::Month && !r.monthDay && !r.lastDayOfMonth)
        r.monthDay = today.day();
    return dates::normalizeRule(r);
}

/// The rule's series has a run on `day` (its interval counted from its first run).
bool matches(const RecurrenceRule &r, QDate day)
{
    const bool workday = day.dayOfWeek() <= 5;
    switch (r.freq) {
    case Freq::Day: return !r.weekdaysOnly || workday;
    case Freq::Week:
        if (!r.weekdays.isEmpty())
            return r.weekdays.contains(day.dayOfWeek());
        return !r.weekdaysOnly || workday;
    case Freq::Month:
        if (r.lastDayOfMonth)
            return day == dates::endOfMonth(day);
        return !r.monthDay || day.day() == std::min(*r.monthDay, day.daysInMonth());
    case Freq::Year: return true;
    }
    return false;
}

QString spec(const DateSpec &s)
{
    return s.isValid() ? s.toString() : QString();
}

DateSpec specFrom(const QJsonValue &value)
{
    const auto s = dates::parseDateSpec(value.toString());
    return s && s->time ? *s : DateSpec{};
}

} // namespace

bool When::isValid() const
{
    switch (kind) {
    case Kind::Once: return date.isValid() && time.isValid();
    case Kind::Repeat: return time.isValid() && !rule.whenDone;
    case Kind::Interval: return minutes >= MinIntervalMinutes && minutes <= MaxIntervalMinutes;
    }
    return false;
}

DateSpec wallClock(const QDateTime &moment)
{
    const QDateTime local = moment.toLocalTime();
    return DateSpec{local.date(), QTime(local.time().hour(), local.time().minute())};
}

std::optional<When> parseWhen(QStringView text, const QDateTime &now, dates::UiLocale locale, WhenError *error)
{
    const auto fail = [error](WhenError why) -> std::optional<When> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (error)
        *error = WhenError::None;
    const QString written = tidy(text);
    if (written.isEmpty())
        return fail(WhenError::Empty);
    const QString key = text::foldKey(written);
    // An interval: "co 3 godziny", "every 30 minutes".
    if (const int minutes = intervalMinutes(key); minutes != 0) {
        if (minutes < 0)
            return fail(WhenError::TooOften);
        When w;
        w.kind = When::Kind::Interval;
        w.minutes = minutes;
        return w;
    }
    const QDate today = now.toLocalTime().date();
    const auto repeat = [&](const RecurrenceRule &r, QTime time) -> std::optional<When> {
        if (r.whenDone)
            return fail(WhenError::WhenDone);
        When w;
        w.kind = When::Kind::Repeat;
        w.rule = anchored(r, today);
        w.time = time;
        return w;
    };
    // A rule of days alone ("codziennie": at DefaultHour), or with a time before or after it.
    if (const auto r = rule(written, locale))
        return repeat(*r, QTime(DefaultHour, 0));
    const QStringList words = written.split(u' ', Qt::SkipEmptyParts);
    for (qsizetype k = 1; k < words.size(); ++k) {
        const QString left = words.first(k).join(u' ');
        const QString right = words.sliced(k).join(u' ');
        if (const auto r = rule(left, locale)) {
            if (const auto time = timeOnly(right, locale))
                return repeat(*r, *time);
        }
        if (const auto time = timeOnly(left, locale)) {
            if (const auto r = rule(right, locale))
                return repeat(*r, *time);
        }
    }
    // Once: a day and a time ("jutro o 8"; a day alone at DefaultHour).
    if (const auto found = dates::parseNaturalDate(written, now.toLocalTime(), Qt::Monday, locale);
        found && whole(*found, written)) {
        When w;
        w.kind = When::Kind::Once;
        w.date = found->spec.date;
        w.time = found->spec.time.value_or(QTime(DefaultHour, 0));
        if (dates::compareDateSpecs(DateSpec{w.date, w.time}, wallClock(now)) <= 0)
            return fail(WhenError::Past);
        return w;
    }
    return fail(WhenError::NotUnderstood);
}

QString describeWhen(const When &when, dates::UiLocale locale, QDate today)
{
    const bool pl = locale == dates::UiLocale::Polish;
    switch (when.kind) {
    case When::Kind::Interval: {
        const int m = when.minutes;
        if (m % 60 == 0) {
            const int h = m / 60;
            if (h == 1)
                return pl ? u"co godzinę"_s : u"every hour"_s;
            return pl ? u"co %1 %2"_s.arg(h).arg(dates::polishPlural(h, u"godzinę", u"godziny", u"godzin"))
                      : u"every %1 hours"_s.arg(h);
        }
        return pl ? u"co %1 %2"_s.arg(m).arg(dates::polishPlural(m, u"minutę", u"minuty", u"minut"))
                  : u"every %1 minutes"_s.arg(m);
    }
    case When::Kind::Repeat:
        return dates::formatRecurrence(when.rule, locale) + (pl ? u" o "_s : u" at "_s) + clock(when.time);
    case When::Kind::Once: {
        const QString day = dates::formatHuman(DateSpec{when.date, std::nullopt}, locale, today);
        return (pl ? u"jednorazowo: %1 o %2"_s : u"once: %1 at %2"_s).arg(day, clock(when.time));
    }
    }
    return {};
}

std::optional<DateSpec> nextRun(const When &when, const DateSpec &after)
{
    if (!when.isValid() || !after.isValid())
        return std::nullopt;
    const DateSpec from{after.date, after.time.value_or(QTime(0, 0))};
    switch (when.kind) {
    case When::Kind::Once: {
        const DateSpec at{when.date, when.time};
        if (dates::compareDateSpecs(at, from) > 0)
            return at;
        return std::nullopt;
    }
    case When::Kind::Interval: {
        // Elapsed time (QDateTime's local time knows the changes of the clock).
        const QDateTime start(from.date, *from.time);
        return wallClock(start.addSecs(qint64(when.minutes) * 60));
    }
    case When::Kind::Repeat: {
        // Its time later today, when the series has a run today; else the series' next day.
        if (matches(when.rule, from.date) && when.time > *from.time)
            return DateSpec{from.date, when.time};
        if (const auto next = dates::nextOccurrence(when.rule, DateSpec{from.date, std::nullopt}))
            return DateSpec{next->date, when.time};
        return std::nullopt;
    }
    }
    return std::nullopt;
}

QJsonObject toJson(const When &when)
{
    switch (when.kind) {
    case When::Kind::Once: return {{u"kind"_s, u"once"_s}, {u"at"_s, DateSpec{when.date, when.time}.toString()}};
    case When::Kind::Repeat:
        return {{u"kind"_s, u"repeat"_s},
                {u"rule"_s, dates::formatRecurrence(when.rule, dates::UiLocale::English)},
                {u"time"_s, when.time.toString(u"HH:mm"_s)}};
    case When::Kind::Interval: return {{u"kind"_s, u"interval"_s}, {u"minutes"_s, when.minutes}};
    }
    return {};
}

std::optional<When> whenFromJson(const QJsonObject &object)
{
    const QString kind = object.value(u"kind"_s).toString();
    When w;
    if (kind == u"once") {
        const DateSpec at = specFrom(object.value(u"at"_s));
        w.kind = When::Kind::Once;
        w.date = at.date;
        w.time = at.time.value_or(QTime());
    } else if (kind == u"repeat") {
        const auto r = dates::parseRecurrence(object.value(u"rule"_s).toString());
        const auto time = dates::parseClockTime(object.value(u"time"_s).toString());
        if (!r || !time)
            return std::nullopt;
        w.kind = When::Kind::Repeat;
        w.rule = *r;
        w.time = *time;
    } else if (kind == u"interval") {
        w.kind = When::Kind::Interval;
        w.minutes = object.value(u"minutes"_s).toInt(0);
    } else {
        return std::nullopt;
    }
    if (!w.isValid())
        return std::nullopt;
    return w;
}

std::optional<DateSpec> Schedule::next() const
{
    return nextRun(when, lastRun.isValid() ? lastRun : created);
}

bool Schedule::isDue(const QDateTime &now) const
{
    if (!enabled)
        return false;
    const std::optional<DateSpec> at = next();
    return at && dates::compareDateSpecs(*at, wallClock(now)) <= 0;
}

QJsonObject toJson(const Schedule &s)
{
    QJsonObject out{{u"id"_s, s.id},
                    {u"title"_s, s.title},
                    {u"prompt"_s, s.prompt},
                    {u"partner"_s, s.partner},
                    {u"when"_s, s.whenText},
                    {u"rule"_s, toJson(s.when)},
                    {u"continues"_s, s.continues},
                    {u"enabled"_s, s.enabled},
                    {u"created"_s, spec(s.created)}};
    const auto put = [&out](const QString &key, const QString &value) {
        if (!value.isEmpty())
            out.insert(key, value);
    };
    put(u"conversation"_s, s.conversation);
    put(u"lastRun"_s, spec(s.lastRun));
    put(u"lastConversation"_s, s.lastConversation);
    put(u"lastError"_s, s.lastError);
    put(u"by"_s, s.by);
    put(u"parent"_s, s.parent);
    return out;
}

std::optional<Schedule> scheduleFromJson(const QJsonObject &o)
{
    Schedule s;
    s.id = o.value(u"id"_s).toString().trimmed();
    s.prompt = o.value(u"prompt"_s).toString().trimmed().left(MaxPromptChars);
    const auto when = whenFromJson(o.value(u"rule"_s).toObject());
    static const QRegularExpression id(u"^[A-Za-z0-9_-]{1,64}$"_s);
    if (!id.match(s.id).hasMatch() || s.prompt.isEmpty() || !when)
        return std::nullopt;
    s.when = *when;
    s.title = o.value(u"title"_s).toString().simplified().left(MaxTitleChars);
    s.partner = o.value(u"partner"_s).toString().trimmed();
    s.whenText = o.value(u"when"_s).toString().simplified();
    s.continues = o.value(u"continues"_s).toBool(false);
    s.conversation = o.value(u"conversation"_s).toString();
    s.enabled = o.value(u"enabled"_s).toBool(true);
    s.created = specFrom(o.value(u"created"_s));
    s.lastRun = specFrom(o.value(u"lastRun"_s));
    s.lastConversation = o.value(u"lastConversation"_s).toString();
    s.lastError = o.value(u"lastError"_s).toString();
    s.by = o.value(u"by"_s).toString().simplified().left(64);
    s.parent = o.value(u"parent"_s).toString();
    if (!s.created.isValid())
        return std::nullopt;
    return s;
}

QList<Schedule> parseSchedules(const QByteArray &json)
{
    QList<Schedule> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return out;
    QSet<QString> ids;
    for (const QJsonValue &v : doc.array()) {
        if (out.size() == MaxSchedules)
            break;
        const auto s = scheduleFromJson(v.toObject());
        if (!s || ids.contains(s->id))
            continue;
        ids.insert(s->id);
        out.append(*s);
    }
    return out;
}

QByteArray serializeSchedules(const QList<Schedule> &schedules)
{
    QJsonArray array;
    for (const Schedule &s : schedules)
        array.append(toJson(s));
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

} // namespace brulion::schedule

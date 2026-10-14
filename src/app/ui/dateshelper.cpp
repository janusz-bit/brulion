#include "dateshelper.h"

#include "dates/dates.h"

#include <QRegularExpression>

#include <algorithm>
#include <chrono>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

QString g_defaultLocale = u"pl"_s;
QString g_defaultFixedNow;

dates::UiLocale toUiLocale(bool english)
{
    return english ? dates::UiLocale::English : dates::UiLocale::Polish;
}

QString isoDate(QDate d)
{
    return d.isValid() ? d.toString(u"yyyy-MM-dd"_s) : QString();
}

QDate parseDate(const QString &text)
{
    return dates::parseIsoDate(QStringView(text).trimmed()).value_or(QDate());
}

QString freqName(dates::RecurrenceRule::Freq f)
{
    switch (f) {
    case dates::RecurrenceRule::Freq::Day: return u"day"_s;
    case dates::RecurrenceRule::Freq::Week: return u"week"_s;
    case dates::RecurrenceRule::Freq::Month: return u"month"_s;
    case dates::RecurrenceRule::Freq::Year: return u"year"_s;
    }
    return {};
}

std::optional<dates::RecurrenceRule> ruleFromMap(const QVariantMap &map)
{
    dates::RecurrenceRule rule;
    const QString freq = map.value(u"freq"_s).toString();
    if (freq == u"day")
        rule.freq = dates::RecurrenceRule::Freq::Day;
    else if (freq == u"week")
        rule.freq = dates::RecurrenceRule::Freq::Week;
    else if (freq == u"month")
        rule.freq = dates::RecurrenceRule::Freq::Month;
    else if (freq == u"year")
        rule.freq = dates::RecurrenceRule::Freq::Year;
    else
        return std::nullopt;
    bool ok = false;
    const int interval = map.value(u"interval"_s, 1).toInt(&ok);
    rule.interval = ok ? interval : 1;
    for (const QVariant &d : map.value(u"weekdays"_s).toList())
        rule.weekdays.append(d.toInt());
    const int monthDay = map.value(u"monthDay"_s).toInt();
    if (monthDay > 0)
        rule.monthDay = monthDay;
    rule.lastDayOfMonth = map.value(u"lastDayOfMonth"_s).toBool();
    rule.weekdaysOnly = map.value(u"weekdaysOnly"_s).toBool();
    rule.whenDone = map.value(u"whenDone"_s).toBool();
    return dates::normalizeRule(rule);
}

QVariantMap ruleToMap(const dates::RecurrenceRule &rule)
{
    QVariantList weekdays;
    for (int d : rule.weekdays)
        weekdays.append(d);
    return {
            {u"freq"_s, freqName(rule.freq)},
            {u"interval"_s, rule.interval},
            {u"weekdays"_s, weekdays},
            {u"monthDay"_s, rule.monthDay.value_or(0)},
            {u"lastDayOfMonth"_s, rule.lastDayOfMonth},
            {u"weekdaysOnly"_s, rule.weekdaysOnly},
            {u"whenDone"_s, rule.whenDone},
    };
}

} // namespace

DatesHelper::DatesHelper(QObject *parent) : QObject(parent), m_locale(g_defaultLocale)
{
    m_midnight.setSingleShot(true);
    connect(&m_midnight, &QTimer::timeout, this, [this] {
        Q_EMIT todayChanged();
        scheduleMidnight();
    });
    setFixedNow(g_defaultFixedNow);
    scheduleMidnight();
}

void DatesHelper::setDefaults(const QString &locale, const QString &fixedNow)
{
    g_defaultLocale = locale == u"en" ? u"en"_s : u"pl"_s;
    g_defaultFixedNow = fixedNow;
}

void DatesHelper::setLocale(const QString &locale)
{
    const QString normalized = locale.startsWith(u"en") ? u"en"_s : u"pl"_s;
    if (normalized == m_locale)
        return;
    m_locale = normalized;
    Q_EMIT localeChanged();
}

void DatesHelper::setWeekStart(int weekStart)
{
    const int day = dates::sanitizeWeekStart(weekStart);
    if (day == m_weekStart)
        return;
    m_weekStart = day;
    Q_EMIT weekStartChanged();
}

QString DatesHelper::today() const
{
    return isoDate(currentDateTime().date());
}

QString DatesHelper::fixedNow() const
{
    return m_fixedNow.isValid() ? m_fixedNow.toString(u"yyyy-MM-dd HH:mm"_s) : QString();
}

void DatesHelper::setFixedNow(const QString &now)
{
    QDateTime parsed;
    if (const auto spec = dates::parseDateSpec(now))
        parsed = spec->toLocalDateTime();
    if (parsed == m_fixedNow)
        return;
    m_fixedNow = parsed;
    Q_EMIT todayChanged();
}

QString DatesHelper::now() const
{
    return currentDateTime().toString(u"yyyy-MM-dd HH:mm"_s);
}

QDateTime DatesHelper::currentDateTime() const
{
    return m_fixedNow.isValid() ? m_fixedNow : QDateTime::currentDateTime();
}

void DatesHelper::scheduleMidnight()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime next(now.date().addDays(1), QTime(0, 0, 1));
    // Re-check at least hourly: the clock or the time zone may jump (suspend, travel).
    const qint64 ms = std::clamp<qint64>(now.msecsTo(next), 1000, 3600 * 1000);
    m_midnight.start(std::chrono::milliseconds(ms));
}

bool DatesHelper::english(const QString &locale) const
{
    return (locale.isEmpty() ? m_locale : locale).startsWith(u"en");
}

QVariantMap DatesHelper::parseNatural(const QString &text) const
{
    const QDateTime now = currentDateTime();
    const bool en = english({});
    const auto found = dates::parseNaturalDate(text, now, static_cast<Qt::DayOfWeek>(m_weekStart), toUiLocale(en));
    if (!found)
        return {{u"ok"_s, false}};
    const DateSpec &spec = found->spec;
    const QString specText = spec.toString();
    const qint64 days = now.date().daysTo(spec.date);
    QString relativeWord;
    if (days >= -1 && days <= 1) {
        static const char16_t *const pl[] = {u"wczoraj", u"dziś", u"jutro"};
        static const char16_t *const enWords[] = {u"yesterday", u"today", u"tomorrow"};
        relativeWord = QString::fromUtf16(en ? enWords[days + 1] : pl[days + 1]);
    }
    return {
            {u"ok"_s, true},
            {u"date"_s, isoDate(spec.date)},
            {u"time"_s, spec.time ? spec.time->toString(u"HH:mm"_s) : QString()},
            {u"hasTime"_s, spec.hasTime()},
            {u"spec"_s, specText},
            {u"display"_s, formatShort(specText)},
            {u"human"_s, dates::formatHuman(spec, toUiLocale(en), now.date())},
            {u"relative"_s, !relativeWord.isEmpty()},
            {u"relativeWord"_s, relativeWord},
            {u"matched"_s, text.mid(found->start, found->length)},
            {u"start"_s, found->start},
            {u"length"_s, found->length},
    };
}

QString DatesHelper::formatHuman(const QString &spec) const
{
    const auto parsed = dates::parseDateSpec(spec);
    if (!parsed)
        return {};
    return dates::formatHuman(*parsed, toUiLocale(english({})), currentDateTime().date());
}

QString DatesHelper::formatShort(const QString &spec) const
{
    const auto parsed = dates::parseDateSpec(spec);
    if (!parsed)
        return {};
    const bool en = english({});
    const auto loc = toUiLocale(en);
    const QDate d = parsed->date;
    const bool sameYear = d.year() == currentDateTime().date().year();
    const QString weekday = dates::weekdayName(d.dayOfWeek(), loc, dates::NameForm::Short);
    const QString month = dates::monthName(d.month(), loc, dates::NameForm::Short);
    QString out = en ? u"%1, %2 %3"_s.arg(weekday, month, QString::number(d.day()))
                     : u"%1, %2 %3"_s.arg(weekday, QString::number(d.day()), month);
    if (!sameYear)
        out += (en ? u", "_s : u" "_s) + QString::number(d.year());
    if (parsed->time)
        out += u' ' + parsed->time->toString(u"HH:mm"_s);
    return out;
}

QString DatesHelper::formatLong(const QString &date) const
{
    const QDate d = parseDate(date);
    if (!d.isValid())
        return {};
    const bool en = english({});
    const auto loc = toUiLocale(en);
    const QString weekday = dates::weekdayName(d.dayOfWeek(), loc, dates::NameForm::Nominative);
    if (en) {
        return u"%1, %2 %3, %4"_s.arg(weekday, dates::monthName(d.month(), loc, dates::NameForm::Nominative),
                                      QString::number(d.day()), QString::number(d.year()));
    }
    return u"%1, %2 %3 %4"_s.arg(weekday, QString::number(d.day()),
                                 dates::monthName(d.month(), loc, dates::NameForm::Genitive),
                                 QString::number(d.year()));
}

QString DatesHelper::monthTitle(int year, int month) const
{
    if (month < 1 || month > 12)
        return {};
    return dates::monthName(month, toUiLocale(english({})), dates::NameForm::Nominative) + u' ' + QString::number(year);
}

QString DatesHelper::formatRule(const QVariantMap &rule, const QString &locale) const
{
    const auto parsed = ruleFromMap(rule);
    if (!parsed)
        return {};
    return dates::formatRecurrence(*parsed, toUiLocale(english(locale)));
}

QString DatesHelper::ruleLabel(const QString &text) const
{
    const auto parsed = dates::parseRecurrence(text);
    return parsed ? dates::formatRecurrence(*parsed, toUiLocale(english({}))) : QString();
}

QString DatesHelper::canonicalRule(const QString &text) const
{
    return dates::canonicalRecurrence(text);
}

QVariantMap DatesHelper::parseRule(const QString &text) const
{
    const auto parsed = dates::parseRecurrence(text);
    if (!parsed)
        return {{u"ok"_s, false}};
    QVariantMap map = ruleToMap(*parsed);
    map.insert(u"ok"_s, true);
    map.insert(u"canonical"_s, dates::formatRecurrence(*parsed, dates::UiLocale::English));
    map.insert(u"label"_s, dates::formatRecurrence(*parsed, toUiLocale(english({}))));
    return map;
}

QStringList DatesHelper::nextOccurrences(const QVariant &rule, const QString &fromSpec, int count) const
{
    std::optional<dates::RecurrenceRule> parsed;
    if (rule.typeId() == QMetaType::QString)
        parsed = dates::parseRecurrence(rule.toString());
    else
        parsed = ruleFromMap(rule.toMap());
    const auto from = dates::parseDateSpec(fromSpec);
    QStringList out;
    if (!parsed || !from)
        return out;
    DateSpec cursor = *from;
    for (int i = 0; i < std::clamp(count, 0, 50); ++i) {
        const auto next = dates::nextOccurrence(*parsed, cursor);
        if (!next)
            break;
        out.append(next->toString());
        cursor = *next;
    }
    return out;
}

QStringList DatesHelper::weekdayNames(const QString &form, const QString &locale) const
{
    const auto loc = toUiLocale(english(locale));
    QStringList out;
    for (int d = 1; d <= 7; ++d) {
        if (form == u"narrow") {
            out.append(dates::weekdayName(d, loc, dates::NameForm::Nominative).left(1).toUpper());
        } else {
            out.append(
                    dates::weekdayName(d, loc, form == u"full" ? dates::NameForm::Nominative : dates::NameForm::Short));
        }
    }
    return out;
}

QStringList DatesHelper::monthNames(const QString &form, const QString &locale) const
{
    const auto loc = toUiLocale(english(locale));
    const dates::NameForm nameForm = form == u"short"      ? dates::NameForm::Short
                                     : form == u"genitive" ? dates::NameForm::Genitive
                                                           : dates::NameForm::Nominative;
    QStringList out;
    for (int m = 1; m <= 12; ++m)
        out.append(dates::monthName(m, loc, nameForm));
    return out;
}

QVariantList DatesHelper::monthGrid(int year, int month, int weekStart) const
{
    const QDate first(year, month, 1);
    QVariantList cells;
    if (!first.isValid())
        return cells;
    const auto start = static_cast<Qt::DayOfWeek>(weekStart > 0 ? dates::sanitizeWeekStart(weekStart) : m_weekStart);
    const QDate today = currentDateTime().date();
    QDate d = dates::startOfWeek(first, start);
    cells.reserve(42);
    for (int i = 0; i < 42; ++i, d = d.addDays(1)) {
        cells.append(QVariantMap{
                {u"date"_s, isoDate(d)},
                {u"day"_s, d.day()},
                {u"inMonth"_s, d.month() == month},
                {u"isToday"_s, d == today},
                {u"isWeekend"_s, d.dayOfWeek() >= 6},
                {u"dayOfWeek"_s, d.dayOfWeek()},
        });
    }
    return cells;
}

QString DatesHelper::addDays(const QString &date, int days) const
{
    const QDate d = parseDate(date);
    return d.isValid() ? isoDate(d.addDays(days)) : QString();
}

QString DatesHelper::addMonths(const QString &date, int months) const
{
    const QDate d = parseDate(date);
    return d.isValid() ? isoDate(d.addMonths(months)) : QString();
}

QString DatesHelper::addMinutes(const QString &spec, int minutes) const
{
    const auto parsed = dates::parseDateSpec(spec);
    if (!parsed)
        return {};
    const QDateTime dt = parsed->toLocalDateTime().addSecs(qint64(minutes) * 60);
    return dt.toString(u"yyyy-MM-dd HH:mm"_s);
}

int DatesHelper::dayOfWeek(const QString &date) const
{
    const QDate d = parseDate(date);
    return d.isValid() ? d.dayOfWeek() : 0;
}

QString DatesHelper::startOfWeek(const QString &date) const
{
    const QDate d = parseDate(date);
    return d.isValid() ? isoDate(dates::startOfWeek(d, static_cast<Qt::DayOfWeek>(m_weekStart))) : QString();
}

bool DatesHelper::isValidDate(const QString &date) const
{
    return parseDate(date).isValid();
}

QString DatesHelper::normalizeTime(const QString &text) const
{
    // "9", "09", "9:30", "18.30", "930", "1830" (backtracking splits "930" into 9 + 30).
    static const QRegularExpression re(u"^(\\d{1,2})(?:[:.]?(\\d{2}))?$"_s);
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch())
        return {};
    const int h = m.captured(1).toInt();
    const int min = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
    const QTime time(h, min);
    return time.isValid() ? time.toString(u"HH:mm"_s) : QString();
}

QVariantMap DatesHelper::splitSpec(const QString &spec) const
{
    const auto parsed = dates::parseDateSpec(spec);
    if (!parsed)
        return {{u"ok"_s, false}, {u"date"_s, QString()}, {u"time"_s, QString()}};
    return {
            {u"ok"_s, true},
            {u"date"_s, isoDate(parsed->date)},
            {u"time"_s, parsed->time ? parsed->time->toString(u"HH:mm"_s) : QString()},
    };
}

int DatesHelper::compare(const QString &a, const QString &b) const
{
    return dates::compareDateSpecs(dates::parseDateSpec(a).value_or(DateSpec{}),
                                   dates::parseDateSpec(b).value_or(DateSpec{}));
}

} // namespace brulion::ui

#include "io/ical.h"

#include "io/importutil_p.h"
#include "io/messages_p.h"

#include <QRegularExpression>

#include <algorithm>
#include <array>

namespace brulion::io::ical {

using namespace Qt::StringLiterals;
using detail::IoMessages;

namespace {

constexpr int MaxLineOctets = 75;
constexpr int MaxNesting = 16;

const std::array<QStringView, 7> &weekdayCodes()
{
    static const std::array<QStringView, 7> codes = {u"MO", u"TU", u"WE", u"TH", u"FR", u"SA", u"SU"};
    return codes;
}

/// Unfold on bytes: a line break followed by a space or tab continues the previous line.
QByteArray unfold(QByteArrayView data)
{
    QByteArray out;
    out.reserve(data.size());
    for (qsizetype i = 0; i < data.size(); ++i) {
        const char c = data[i];
        if (c == '\r' || c == '\n') {
            qsizetype j = i;
            if (c == '\r' && j + 1 < data.size() && data[j + 1] == '\n')
                ++j;
            if (j + 1 < data.size() && (data[j + 1] == ' ' || data[j + 1] == '\t')) {
                i = j + 1;
                continue;
            }
            out += '\n';
            i = j;
            continue;
        }
        out += c;
    }
    return out;
}

/// Split a content line at `;` / `:` outside double quotes.
std::optional<Property> parseLine(QStringView line)
{
    qsizetype colon = -1;
    QList<qsizetype> semicolons;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == u'"') {
            quoted = !quoted;
        } else if (!quoted && c == u';') {
            semicolons.append(i);
        } else if (!quoted && c == u':') {
            colon = i;
            break;
        }
    }
    if (colon <= 0)
        return std::nullopt;
    Property p;
    const qsizetype nameEnd = semicolons.isEmpty() ? colon : semicolons.first();
    p.name = line.first(nameEnd).trimmed().toString().toUpper();
    if (p.name.isEmpty())
        return std::nullopt;
    for (qsizetype k = 0; k < semicolons.size(); ++k) {
        const qsizetype from = semicolons[k] + 1;
        const qsizetype to = k + 1 < semicolons.size() ? semicolons[k + 1] : colon;
        const QStringView param = line.sliced(from, to - from);
        const qsizetype eq = param.indexOf(u'=');
        if (eq <= 0)
            continue;
        QString value = param.sliced(eq + 1).toString();
        value.remove(u'"');
        p.params.insert(param.first(eq).trimmed().toString().toUpper(), value);
    }
    p.value = line.sliced(colon + 1).toString();
    return p;
}

std::optional<int> weekdayFromCode(QStringView code)
{
    const auto &codes = weekdayCodes();
    for (int d = 0; d < 7; ++d) {
        if (code.compare(codes[d], Qt::CaseInsensitive) == 0)
            return d + 1;
    }
    return std::nullopt;
}

/// The zone a TZID names, invalid when unknown (toWallClock() then keeps the time as written).
/// Builds of Qt without the time zone database (`QT_CONFIG(timezone)` off: Qt for WebAssembly)
/// know no zone by name: every TZID time is taken as a local wall-clock time there — exact for
/// the usual calendar of one's own zone (and flagged as an unknown zone).
QTimeZone zoneFromTzid(const QString &tzid)
{
#if QT_CONFIG(timezone)
    QString id = tzid.trimmed();
    QTimeZone zone(id.toUtf8());
    if (zone.isValid())
        return zone;
    // "/mozilla.org/20050126_1/Europe/Warsaw" and similar prefixes: try the trailing parts.
    const QStringList parts = id.split(u'/', Qt::SkipEmptyParts);
    for (qsizetype k = std::max<qsizetype>(0, parts.size() - 3); k < parts.size(); ++k) {
        const QTimeZone candidate(parts.mid(k).join(u'/').toUtf8());
        if (candidate.isValid())
            return candidate;
    }
    // Windows names ("Central European Standard Time").
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(id.toUtf8());
    if (!iana.isEmpty())
        return QTimeZone(iana);
    return QTimeZone();
#else
    Q_UNUSED(tzid);
    return QTimeZone();
#endif
}

} // namespace

const Property *Component::property(QStringView key) const
{
    for (const Property &p : properties) {
        if (p.name == key)
            return &p;
    }
    return nullptr;
}

QString Component::text(QStringView key) const
{
    const Property *p = property(key);
    return p ? unescapeText(p->value) : QString();
}

QList<const Component *> Component::childrenNamed(QStringView componentName) const
{
    QList<const Component *> out;
    for (const Component &c : children) {
        if (c.name == componentName)
            out.append(&c);
    }
    return out;
}

QList<Component> parse(QByteArrayView data, QStringList *warnings)
{
    QList<Component> roots;
    QList<Component> stack;
    bool malformed = false;
    // UTF-8 as RFC 5545 requires; old exports in a Windows code page are read like other text.
    const QString text = detail::decodeText(unfold(data));
    for (const QStringView rawLine : QStringView(text).split(u'\n')) {
        const QStringView line = rawLine.trimmed().isEmpty() ? QStringView() : rawLine;
        if (line.isEmpty())
            continue;
        const auto prop = parseLine(line);
        if (!prop) {
            malformed = true;
            continue;
        }
        if (prop->name == u"BEGIN") {
            if (stack.size() >= MaxNesting) {
                malformed = true;
                continue;
            }
            stack.append(Component{prop->value.trimmed().toUpper(), {}, {}});
        } else if (prop->name == u"END") {
            const QString name = prop->value.trimmed().toUpper();
            // Close up to the matching component (unterminated inner ones are closed too).
            qsizetype match = -1;
            for (qsizetype k = stack.size() - 1; k >= 0; --k) {
                if (stack[k].name == name) {
                    match = k;
                    break;
                }
            }
            if (match < 0) {
                malformed = true;
                continue;
            }
            while (stack.size() > match) {
                Component done = stack.takeLast();
                if (stack.isEmpty())
                    roots.append(std::move(done));
                else
                    stack.last().children.append(std::move(done));
            }
        } else if (!stack.isEmpty()) {
            stack.last().properties.append(*prop);
        } else {
            malformed = true;
        }
    }
    if (!stack.isEmpty())
        malformed = true;
    while (!stack.isEmpty()) {
        Component done = stack.takeLast();
        if (stack.isEmpty())
            roots.append(std::move(done));
        else
            stack.last().children.append(std::move(done));
    }
    if (malformed && warnings)
        warnings->append(IoMessages::tr("The calendar contains malformed lines; they were ignored."));
    return roots;
}

QString unescapeText(QStringView value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value[i];
        if (c != u'\\' || i + 1 == value.size()) {
            out += c;
            continue;
        }
        const QChar n = value[i + 1];
        if (n == u'n' || n == u'N') {
            out += u'\n';
            ++i;
        } else if (n == u'\\' || n == u';' || n == u',' || n == u':' || n == u'"') {
            out += n;
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

QString escapeText(QStringView text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'\\' || c == u';' || c == u',') {
            out += u'\\';
            out += c;
        } else if (c == u'\r') {
            if (i + 1 < text.size() && text[i + 1] == u'\n')
                ++i;
            out += u"\\n";
        } else if (c == u'\n') {
            out += u"\\n";
        } else if ((c.unicode() < 0x20 && c != u'\t') || c.unicode() == 0x7f) {
            continue; // control characters are not allowed in TEXT
        } else {
            out += c;
        }
    }
    return out;
}

QByteArray foldLine(const QString &line)
{
    const QByteArray utf8 = line.toUtf8();
    QByteArray out;
    out.reserve(utf8.size() + utf8.size() / MaxLineOctets * 3);
    qsizetype pos = 0;
    int limit = MaxLineOctets;
    while (utf8.size() - pos > limit) {
        qsizetype cut = pos + limit;
        // Never split a UTF-8 sequence: move back over continuation bytes (10xxxxxx).
        while (cut > pos && (static_cast<unsigned char>(utf8[cut]) & 0xC0) == 0x80)
            --cut;
        if (cut == pos)
            cut = pos + limit; // not UTF-8 at all; cannot happen for QString::toUtf8()
        out += utf8.sliced(pos, cut - pos);
        out += "\r\n ";
        pos = cut;
        limit = MaxLineOctets - 1; // the leading space counts
    }
    out += utf8.sliced(pos);
    return out;
}

std::optional<DateTimeValue> parseDateTime(const Property &property)
{
    QString v = property.value.trimmed();
    v.remove(u'-');
    v.remove(u':');
    static const QRegularExpression re(u"^(\\d{8})(?:T(\\d{2})(\\d{2})(\\d{2})?(?:\\.\\d+)?(Z)?)?$"_s,
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(v);
    if (!m.hasMatch())
        return std::nullopt;
    DateTimeValue out;
    out.date = QDate::fromString(m.captured(1), u"yyyyMMdd"_s);
    if (!out.date.isValid())
        return std::nullopt;
    const bool dateOnly =
            property.param(u"VALUE"_s).compare(u"DATE"_s, Qt::CaseInsensitive) == 0 || m.capturedLength(2) == 0;
    if (dateOnly)
        return out;
    const QTime time(m.captured(2).toInt(), m.captured(3).toInt());
    if (!time.isValid())
        return std::nullopt;
    out.time = time;
    out.utc = m.capturedLength(5) > 0;
    if (!out.utc)
        out.tzid = property.param(u"TZID"_s);
    return out;
}

DateSpec toWallClock(const DateTimeValue &value, const QTimeZone &zone, bool *unknownZone)
{
    if (unknownZone)
        *unknownZone = false;
    if (!value.time)
        return DateSpec{value.date, std::nullopt};
    QTimeZone source;
    if (value.utc) {
        source = QTimeZone(QTimeZone::UTC);
    } else if (!value.tzid.isEmpty()) {
        source = zoneFromTzid(value.tzid);
        if (!source.isValid()) {
            if (unknownZone)
                *unknownZone = true;
            return DateSpec{value.date, value.time};
        }
    } else {
        return DateSpec{value.date, value.time}; // floating
    }
    const QDateTime local = QDateTime(value.date, *value.time, source).toTimeZone(zone);
    if (!local.isValid())
        return DateSpec{value.date, value.time};
    return DateSpec{local.date(), QTime(local.time().hour(), local.time().minute())};
}

std::optional<Duration> parseDurationParts(QStringView value)
{
    static const QRegularExpression re(
            u"^([+-])?P(?:(\\d+)W)?(?:(\\d+)D)?(?:T(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+)S)?)?$"_s,
            QRegularExpression::CaseInsensitiveOption);
    const QStringView trimmed = value.trimmed();
    const auto m = re.matchView(trimmed);
    if (!m.hasMatch() || trimmed.endsWith(u'T', Qt::CaseInsensitive))
        return std::nullopt;
    bool any = false;
    const auto num = [&](int group) -> qint64 {
        if (m.capturedLength(group) == 0)
            return 0;
        any = true;
        return m.captured(group).left(9).toLongLong();
    };
    Duration d;
    d.days = num(2) * 7 + num(3);
    d.seconds = num(4) * 3600 + num(5) * 60 + num(6);
    if (!any)
        return std::nullopt;
    if (m.captured(1) == u"-") {
        d.days = -d.days;
        d.seconds = -d.seconds;
    }
    return d;
}

std::optional<qint64> parseDuration(QStringView value)
{
    const auto d = parseDurationParts(value);
    if (!d)
        return std::nullopt;
    return d->days * 86400 + d->seconds;
}

std::optional<dates::RecurrenceRule> parseRRule(QStringView value, QStringList *dropped)
{
    QHash<QString, QString> parts;
    for (const QStringView part : value.trimmed().split(u';', Qt::SkipEmptyParts)) {
        const qsizetype eq = part.indexOf(u'=');
        if (eq <= 0)
            return std::nullopt;
        parts.insert(part.first(eq).trimmed().toString().toUpper(), part.sliced(eq + 1).trimmed().toString().toUpper());
    }
    dates::RecurrenceRule rule;
    const QString freq = parts.take(u"FREQ"_s);
    if (freq == u"DAILY")
        rule.freq = dates::RecurrenceRule::Freq::Day;
    else if (freq == u"WEEKLY")
        rule.freq = dates::RecurrenceRule::Freq::Week;
    else if (freq == u"MONTHLY")
        rule.freq = dates::RecurrenceRule::Freq::Month;
    else if (freq == u"YEARLY")
        rule.freq = dates::RecurrenceRule::Freq::Year;
    else
        return std::nullopt;

    if (parts.contains(u"INTERVAL"_s)) {
        bool ok = false;
        const int interval = parts.take(u"INTERVAL"_s).toInt(&ok);
        if (!ok || interval < 1 || interval > dates::MaxRecurrenceInterval)
            return std::nullopt;
        rule.interval = interval;
    }
    if (parts.contains(u"BYDAY"_s)) {
        QList<int> days;
        for (const QString &code : parts.take(u"BYDAY"_s).split(u',', Qt::SkipEmptyParts)) {
            const auto day = weekdayFromCode(code.trimmed());
            if (!day)
                return std::nullopt; // "2TU", "-1FR": nth weekday of the month
            days.append(*day);
        }
        std::sort(days.begin(), days.end());
        days.erase(std::unique(days.begin(), days.end()), days.end());
        const QList<int> workdays = {1, 2, 3, 4, 5};
        if (rule.freq == dates::RecurrenceRule::Freq::Week) {
            rule.weekdays = days;
        } else if (rule.freq == dates::RecurrenceRule::Freq::Day && days == workdays) {
            rule.weekdaysOnly = true;
        } else {
            return std::nullopt;
        }
    }
    if (parts.contains(u"BYMONTHDAY"_s)) {
        const QString day = parts.take(u"BYMONTHDAY"_s);
        bool ok = false;
        const int n = day.toInt(&ok);
        if (rule.freq != dates::RecurrenceRule::Freq::Month || !ok || n == 0 || n > 31 || n < -1)
            return std::nullopt;
        if (n == -1)
            rule.lastDayOfMonth = true;
        else
            rule.monthDay = n;
    }
    parts.remove(u"WKST"_s);
    if (dropped) {
        QStringList keys = parts.keys();
        keys.sort();
        dropped->append(keys);
    }
    return dates::normalizeRule(rule);
}

QString formatRRule(const dates::RecurrenceRule &input)
{
    const dates::RecurrenceRule rule = dates::normalizeRule(input);
    QStringList parts;
    const auto byDay = [](const QList<int> &days) -> QString {
        QStringList codes;
        for (int d : days) {
            if (d >= 1 && d <= 7)
                codes.append(weekdayCodes()[d - 1].toString());
        }
        return u"BYDAY="_s + codes.join(u',');
    };
    const QList<int> workdays = {1, 2, 3, 4, 5};
    switch (rule.freq) {
    case dates::RecurrenceRule::Freq::Day: parts.append(u"FREQ=DAILY"_s); break;
    case dates::RecurrenceRule::Freq::Week: parts.append(u"FREQ=WEEKLY"_s); break;
    case dates::RecurrenceRule::Freq::Month: parts.append(u"FREQ=MONTHLY"_s); break;
    case dates::RecurrenceRule::Freq::Year: parts.append(u"FREQ=YEARLY"_s); break;
    }
    if (rule.interval > 1)
        parts.append(u"INTERVAL="_s + QString::number(rule.interval));
    if (rule.freq == dates::RecurrenceRule::Freq::Week && !rule.weekdays.isEmpty())
        parts.append(byDay(rule.weekdays));
    else if (rule.weekdaysOnly)
        parts.append(byDay(workdays));
    if (rule.freq == dates::RecurrenceRule::Freq::Month) {
        if (rule.lastDayOfMonth)
            parts.append(u"BYMONTHDAY=-1"_s);
        else if (rule.monthDay)
            parts.append(u"BYMONTHDAY="_s + QString::number(*rule.monthDay));
    }
    return parts.join(u';');
}

QString formatDate(QDate date)
{
    return date.toString(u"yyyyMMdd"_s);
}

QString formatLocalDateTime(QDate date, QTime time)
{
    return date.toString(u"yyyyMMdd"_s) + u'T' + time.toString(u"HHmmss"_s);
}

QString formatUtc(const QDateTime &dateTime)
{
    return dateTime.toUTC().toString(u"yyyyMMdd'T'HHmmss'Z'"_s);
}

void Writer::begin(QStringView component)
{
    property(u"BEGIN", component);
}

void Writer::end(QStringView component)
{
    property(u"END", component);
}

void Writer::property(QStringView name, QStringView value, const QList<std::pair<QString, QString>> &params)
{
    QString line = name.toString();
    for (const auto &[key, rawValue] : params) {
        QString v = rawValue;
        v.remove(u'"');
        v.remove(u'\r');
        v.remove(u'\n');
        const bool quote = v.contains(u':') || v.contains(u';') || v.contains(u',');
        line += u';' + key + u'=' + (quote ? u'"' + v + u'"' : v);
    }
    line += u':';
    // A raw value must not break the line structure.
    QString clean = value.toString();
    clean.replace(u'\r', u' ').replace(u'\n', u' ');
    line += clean;
    m_data += foldLine(line);
    m_data += "\r\n";
}

void Writer::text(QStringView name, QStringView value, const QList<std::pair<QString, QString>> &params)
{
    property(name, escapeText(value), params);
}

} // namespace brulion::io::ical

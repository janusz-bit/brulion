#include "caldav/taskmap.h"

#include "caldav/ids_p.h"

#include "caldav/eventedit.h"

#include "dates/recurrence.h"
#include "markdown/tokenedit.h"
#include "markdown/tokens.h"
#include "text/text.h"
#include "util/strings.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringTokenizer>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;
namespace tokens = markdown::tokens;

namespace {

constexpr int MaxText = 20'000;

QString cut(QString text, int max = MaxText)
{
    if (text.size() > max)
        text.truncate(max);
    return text;
}

/// A description line as a continuation line of a task: a line that would start a list item, a
/// heading, a quote or a table gets a backslash before it.
QString escapeContinuation(const QString &line)
{
    static const QRegularExpression blockStart(u"^([-*+](\\s|$)|#|>|\\||\\d{1,9}[.)](\\s|$)|```|~~~|\\$\\$)"_s);
    return blockStart.match(line).hasMatch() ? u'\\' + line : line;
}

/// escapeContinuation() undone.
QString unescapeContinuation(const QString &line)
{
    static const QRegularExpression escaped(u"^\\\\([-*+](\\s|$)|#|>|\\||\\d{1,9}[.)](\\s|$)|```|~~~|\\$\\$)"_s);
    return escaped.match(line).hasMatch() ? line.sliced(1) : line;
}

Priority priorityClass(Priority p)
{
    switch (p) {
    case Priority::Highest:
    case Priority::High: return Priority::High;
    case Priority::Lowest:
    case Priority::Low: return Priority::Low;
    default: return p;
    }
}

QString normalizedDescription(const QString &text)
{
    QString t = text;
    t.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    QStringList lines = t.split(u'\n');
    for (QString &l : lines) {
        while (!l.isEmpty() && l.back().isSpace())
            l.chop(1);
    }
    while (!lines.isEmpty() && lines.constFirst().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.constLast().isEmpty())
        lines.removeLast();
    return cut(lines.join(u'\n'));
}

QString statusChar(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u" "_s;
    case TaskStatus::Doing: return u"/"_s;
    case TaskStatus::Done: return u"x"_s;
    case TaskStatus::Cancelled: return u"-"_s;
    case TaskStatus::Deferred: return u">"_s;
    }
    return u" "_s;
}

/// A category as a tag name: blanks become "-", characters a tag cannot hold are dropped.
QString tagFor(const QString &category)
{
    QString out;
    for (const QChar c : category.trimmed()) {
        if (c.isLetterOrNumber() || c == u'_' || c == u'-' || c == u'/')
            out += c;
        else if (c.isSpace() && !out.endsWith(u'-'))
            out += u'-';
    }
    while (out.endsWith(u'-') || out.endsWith(u'/'))
        out.chop(1);
    // A tag of digits only is no tag (Obsidian): "#2026" would stay text.
    bool digits = !out.isEmpty();
    for (const QChar c : std::as_const(out))
        digits = digits && c.isDigit();
    return digits ? QString() : out;
}

/// The text of a task's first line split into its summary and trailing #tags.
std::pair<QString, QStringList> splitTrailingTags(const QString &text)
{
    QString rest = text.trimmed();
    QStringList tags;
    static const QRegularExpression trailing(u"(?:^|\\s)#(\\[\\[([^\\]]+)\\]\\]|[^\\s#\\[\\]]+)$"_s);
    while (true) {
        const auto m = trailing.match(rest);
        if (!m.hasMatch())
            break;
        const QString tag = m.capturedLength(2) ? m.captured(2) : m.captured(1);
        tags.prepend(tag);
        rest = rest.first(m.capturedStart(0)).trimmed();
    }
    return {rest.simplified(), tags};
}

std::optional<DateSpec> wallClock(const io::ical::Component &c, QStringView name, const ZoneTable &zones,
                                  const QTimeZone &zone)
{
    const io::ical::Property *p = c.property(name);
    if (!p)
        return std::nullopt;
    const auto v = io::ical::parseDateTime(*p);
    if (!v)
        return std::nullopt;
    if (!v->time)
        return DateSpec{v->date, std::nullopt};
    const NaiveDateTime t = zones.toTarget(*v, zone);
    return DateSpec{t.date, QTime(t.time.hour(), t.time.minute())};
}

/// `base` moved by a duration on the wall clock of `zone` (days nominal, seconds exact).
DateSpec shifted(const DateSpec &base, const io::ical::Duration &d, const QTimeZone &zone)
{
    const QDate day = base.date.addDays(d.days);
    const QDateTime moved = QDateTime(day, base.time.value_or(QTime(0, 0)), zone).addSecs(d.seconds).toTimeZone(zone);
    if (!moved.isValid())
        return DateSpec{day, base.time.value_or(QTime(0, 0))};
    return DateSpec{moved.date(), QTime(moved.time().hour(), moved.time().minute())};
}

// ---- VTODO properties ---------------------------------------------------------------------------

void removeProperty(io::ical::Component &c, QStringView name)
{
    c.properties.removeIf([name](const io::ical::Property &p) { return p.name == name; });
}

void setProperty(io::ical::Component &c, const QString &name, const QString &value,
                 const QHash<QString, QString> &params = {})
{
    for (io::ical::Property &p : c.properties) {
        if (p.name == name) {
            p.value = value;
            p.params = params;
            removeProperty(c, u"__none__"); // (keeps the first one only below)
            // Drop later duplicates of a single-valued property.
            bool seen = false;
            c.properties.removeIf([&seen, &name](const io::ical::Property &q) {
                if (q.name != name)
                    return false;
                if (!seen) {
                    seen = true;
                    return false;
                }
                return true;
            });
            return;
        }
    }
    c.properties.append(io::ical::Property{name, params, value});
}

/// The IANA id of a zone ("" when unknown: builds without the time zone database).
QString zoneId(const QTimeZone &zone)
{
#if QT_CONFIG(timezone)
    return zone.isValid() ? QString::fromUtf8(zone.id()) : QString();
#else
    Q_UNUSED(zone);
    return {};
#endif
}

bool usableZone(const QTimeZone &zone)
{
    if (!zone.isValid() || zone.timeSpec() != Qt::TimeZone)
        return false;
    const QString id = zoneId(zone);
    return !id.isEmpty() && id != u"UTC" && !id.startsWith(u"UTC"_s);
}

/// A DATE or DATE-TIME property of `spec` (the device's wall clock): TZID of the device's zone.
io::ical::Property dateProperty(const QString &name, const DateSpec &spec, const QTimeZone &zone, bool *usedZone)
{
    if (!spec.time)
        return {name, {{u"VALUE"_s, u"DATE"_s}}, io::ical::formatDate(spec.date)};
    if (usableZone(zone)) {
        if (usedZone)
            *usedZone = true;
        return {name, {{u"TZID"_s, zoneId(zone)}}, io::ical::formatLocalDateTime(spec.date, *spec.time)};
    }
    if (zone.isValid() && zone.timeSpec() == Qt::UTC)
        return {name, {}, io::ical::formatUtc(QDateTime(spec.date, *spec.time, QTimeZone::UTC))};
    return {name, {}, io::ical::formatLocalDateTime(spec.date, *spec.time)}; // floating
}

void setDate(io::ical::Component &c, const QString &name, const DateSpec &spec, const QTimeZone &zone, bool *usedZone)
{
    const io::ical::Property p = dateProperty(name, spec, zone, usedZone);
    setProperty(c, name, p.value, p.params);
}

QString completedValue(QDate date, const TodoContext &ctx)
{
    const QDateTime now = ctx.now.toTimeZone(ctx.zone.isValid() ? ctx.zone : QTimeZone::LocalTime);
    const QDateTime when = now.date() == date ? now : QDateTime(date, QTime(12, 0), ctx.zone.isValid() ? ctx.zone : QTimeZone(QTimeZone::LocalTime));
    return io::ical::formatUtc(when);
}

void setAlarms(io::ical::Component &todo, const TaskFields &f, const TodoContext &ctx)
{
    // Only the alarms Brulion shows: DISPLAY and AUDIO ones go, e-mail and others stay.
    todo.children.removeIf([](const io::ical::Component &a) {
        if (a.name != u"VALARM")
            return false;
        const QString action = a.text(u"ACTION").trimmed().toUpper();
        return action.isEmpty() || action == u"DISPLAY" || action == u"AUDIO";
    });
    const QTimeZone zone = ctx.zone.isValid() ? ctx.zone : QTimeZone(QTimeZone::LocalTime);
    for (const TaskReminder &r : f.reminders) {
        io::ical::Component alarm;
        alarm.name = u"VALARM"_s;
        alarm.properties.append({u"ACTION"_s, {}, r.alarm ? u"AUDIO"_s : u"DISPLAY"_s});
        if (!r.alarm)
            alarm.properties.append({u"DESCRIPTION"_s, {}, io::ical::escapeText(f.summary)});
        // An absolute trigger is UTC (RFC 5545): the reminder's wall-clock time in the device's zone.
        const QDateTime at(r.at.date, r.at.time.value_or(QTime(9, 0)), zone);
        alarm.properties.append({u"TRIGGER"_s, {{u"VALUE"_s, u"DATE-TIME"_s}}, io::ical::formatUtc(at)});
        todo.children.append(alarm);
    }
}

void applyToTodo(io::ical::Component &todo, const TaskFields &f, unsigned mask, const TodoContext &ctx, bool *usedZone)
{
    if (mask & FieldSummary)
        setProperty(todo, u"SUMMARY"_s, io::ical::escapeText(f.summary));
    if (mask & (FieldStatus | FieldCompleted)) {
        switch (f.status) {
        case TaskStatus::Todo:
        case TaskStatus::Deferred: setProperty(todo, u"STATUS"_s, u"NEEDS-ACTION"_s); break;
        case TaskStatus::Doing: setProperty(todo, u"STATUS"_s, u"IN-PROCESS"_s); break;
        case TaskStatus::Done: setProperty(todo, u"STATUS"_s, u"COMPLETED"_s); break;
        case TaskStatus::Cancelled: setProperty(todo, u"STATUS"_s, u"CANCELLED"_s); break;
        }
        if (f.status == TaskStatus::Done) {
            setProperty(todo, u"COMPLETED"_s, completedValue(f.completed.value_or(ctx.now.date()), ctx));
            setProperty(todo, u"PERCENT-COMPLETE"_s, u"100"_s);
        } else {
            removeProperty(todo, u"COMPLETED");
            if (const io::ical::Property *pc = todo.property(u"PERCENT-COMPLETE"); pc && pc->value.trimmed() == u"100")
                removeProperty(todo, u"PERCENT-COMPLETE");
        }
    }
    if (mask & FieldDue) {
        if (f.due)
            setDate(todo, u"DUE"_s, *f.due, ctx.zone, usedZone);
        else
            removeProperty(todo, u"DUE");
    }
    if (mask & FieldStart) {
        if (f.start)
            setDate(todo, u"DTSTART"_s, *f.start, ctx.zone, usedZone);
        else
            removeProperty(todo, u"DTSTART");
    }
    if (mask & FieldPriority) {
        switch (priorityClass(f.priority)) {
        case Priority::High: setProperty(todo, u"PRIORITY"_s, u"1"_s); break;
        case Priority::Medium: setProperty(todo, u"PRIORITY"_s, u"5"_s); break;
        case Priority::Low: setProperty(todo, u"PRIORITY"_s, u"9"_s); break;
        default: removeProperty(todo, u"PRIORITY"); break;
        }
    }
    if (mask & FieldRecurrence) {
        const auto rule = f.recurrence.isEmpty() ? std::nullopt : dates::parseRecurrence(f.recurrence);
        if (rule)
            setProperty(todo, u"RRULE"_s, io::ical::formatRRule(*rule));
        else
            removeProperty(todo, u"RRULE");
    }
    if (mask & FieldCategories) {
        removeProperty(todo, u"CATEGORIES");
        if (!f.categories.isEmpty()) {
            QStringList parts;
            for (const QString &c : f.categories)
                parts.append(io::ical::escapeText(c));
            todo.properties.append({u"CATEGORIES"_s, {}, parts.join(u',')});
        }
    }
    if (mask & FieldDescription) {
        if (f.description.isEmpty())
            removeProperty(todo, u"DESCRIPTION");
        else
            setProperty(todo, u"DESCRIPTION"_s, io::ical::escapeText(f.description));
    }
    if (mask & FieldParent) {
        // Only the PARENT relation: CHILD and SIBLING ones stay as the server has them.
        todo.properties.removeIf([](const io::ical::Property &p) {
            if (p.name != u"RELATED-TO")
                return false;
            const QString type = p.param(u"RELTYPE"_s).toUpper();
            return type.isEmpty() || type == u"PARENT";
        });
        if (!f.parentUid.isEmpty())
            todo.properties.append({u"RELATED-TO"_s, {{u"RELTYPE"_s, u"PARENT"_s}}, io::ical::escapeText(f.parentUid)});
    }
    if (mask & (FieldReminders | FieldStatus)) {
        // A done or cancelled task rings no more (SYNTAX §6): its alarms are not sent.
        TaskFields ringing = f;
        if (f.status == TaskStatus::Done || f.status == TaskStatus::Cancelled)
            ringing.reminders.clear();
        if ((mask & FieldReminders) || ringing.reminders.isEmpty())
            setAlarms(todo, ringing, ctx);
    }
}

void stamp(io::ical::Component &todo, const TodoContext &ctx, bool bumpSequence)
{
    const QString now = io::ical::formatUtc(ctx.now);
    setProperty(todo, u"DTSTAMP"_s, now);
    setProperty(todo, u"LAST-MODIFIED"_s, now);
    if (bumpSequence) {
        const int seq = todo.text(u"SEQUENCE").trimmed().toInt();
        setProperty(todo, u"SEQUENCE"_s, QString::number(seq + 1));
    }
}

void ensureTimezone(io::ical::Component &calendar, const TodoContext &ctx)
{
    if (!usableZone(ctx.zone))
        return;
    const QString id = zoneId(ctx.zone);
    for (const io::ical::Component &c : calendar.children) {
        if (c.name == u"VTIMEZONE" && c.text(u"TZID").trimmed() == id)
            return;
    }
    if (auto tz = vtimezoneFor(ctx.zone, ctx.now.date().year()))
        calendar.children.prepend(*tz);
}

void writeComponent(io::ical::Writer &w, const io::ical::Component &c)
{
    w.begin(c.name);
    for (const io::ical::Property &p : c.properties) {
        QList<std::pair<QString, QString>> params;
        QStringList keys = p.params.keys();
        keys.sort();
        for (const QString &k : std::as_const(keys))
            params.append({k, p.params.value(k)});
        w.property(p.name, p.value, params);
    }
    for (const io::ical::Component &child : c.children)
        writeComponent(w, child);
    w.end(c.name);
}

QString offsetText(int seconds)
{
    const QChar sign = seconds < 0 ? u'-' : u'+';
    const int a = std::abs(seconds);
    return sign + u"%1%2"_s.arg(a / 3600, 2, 10, QChar(u'0')).arg((a % 3600) / 60, 2, 10, QChar(u'0'));
}

} // namespace

TaskFields TaskFields::normalized() const
{
    TaskFields n = *this;
    n.summary = summary.simplified();
    if (n.status == TaskStatus::Deferred)
        n.status = TaskStatus::Todo;
    n.priority = priorityClass(priority);
    if (n.status != TaskStatus::Done)
        n.completed.reset();
    // A done or cancelled task's reminders never ring (SYNTAX §6): they are not compared.
    if (n.status == TaskStatus::Done || n.status == TaskStatus::Cancelled)
        n.reminders.clear();
    if (!n.recurrence.isEmpty()) {
        const QString canonical = dates::canonicalRecurrence(n.recurrence);
        n.recurrence = canonical;
    }
    std::ranges::sort(n.reminders, [](const TaskReminder &a, const TaskReminder &b) {
        if (a.at.date != b.at.date)
            return a.at.date < b.at.date;
        if (a.at.time != b.at.time)
            return a.at.time.value_or(QTime(0, 0)) < b.at.time.value_or(QTime(0, 0));
        return a.alarm < b.alarm;
    });
    n.reminders.erase(std::unique(n.reminders.begin(), n.reminders.end()), n.reminders.end());
    QStringList cats;
    for (const QString &c : categories) {
        const QString key = c.trimmed().toLower();
        if (!key.isEmpty() && !cats.contains(key))
            cats.append(key);
    }
    cats.sort();
    n.categories = cats;
    n.description = normalizedDescription(description);
    n.parentUid = parentUid.trimmed();
    return n;
}

QJsonObject TaskFields::toJson() const
{
    QJsonObject o;
    o.insert(u"summary"_s, summary);
    o.insert(u"status"_s, int(status));
    if (due)
        o.insert(u"due"_s, due->toString());
    if (start)
        o.insert(u"start"_s, start->toString());
    if (priority != Priority::None)
        o.insert(u"priority"_s, int(priority));
    if (completed)
        o.insert(u"completed"_s, completed->toString(Qt::ISODate));
    if (!recurrence.isEmpty())
        o.insert(u"recurrence"_s, recurrence);
    if (!reminders.isEmpty()) {
        QJsonArray list;
        for (const TaskReminder &r : reminders)
            list.append(QJsonObject{{u"at"_s, r.at.toString()}, {u"alarm"_s, r.alarm}});
        o.insert(u"reminders"_s, list);
    }
    if (!categories.isEmpty())
        o.insert(u"categories"_s, QJsonArray::fromStringList(categories));
    if (!description.isEmpty())
        o.insert(u"description"_s, description);
    if (!parentUid.isEmpty())
        o.insert(u"parent"_s, parentUid);
    return o;
}

namespace {
std::optional<DateSpec> specFrom(const QString &text)
{
    if (text.isEmpty())
        return std::nullopt;
    const QDate d = QDate::fromString(text.left(10), Qt::ISODate);
    if (!d.isValid())
        return std::nullopt;
    if (text.size() >= 16) {
        const QTime t = QTime::fromString(text.mid(11, 5), u"HH:mm"_s);
        if (t.isValid())
            return DateSpec{d, t};
    }
    return DateSpec{d, std::nullopt};
}
} // namespace

TaskFields TaskFields::fromJson(const QJsonObject &o)
{
    TaskFields f;
    f.summary = o.value(u"summary"_s).toString();
    f.status = TaskStatus(std::clamp(o.value(u"status"_s).toInt(), 0, 4));
    f.due = specFrom(o.value(u"due"_s).toString());
    f.start = specFrom(o.value(u"start"_s).toString());
    f.priority = Priority(std::clamp(o.value(u"priority"_s).toInt(), 0, 5));
    if (const QDate d = QDate::fromString(o.value(u"completed"_s).toString(), Qt::ISODate); d.isValid())
        f.completed = d;
    f.recurrence = o.value(u"recurrence"_s).toString();
    for (const QJsonValue &v : o.value(u"reminders"_s).toArray()) {
        if (const auto at = specFrom(v.toObject().value(u"at"_s).toString()))
            f.reminders.append({*at, v.toObject().value(u"alarm"_s).toBool()});
    }
    for (const QJsonValue &v : o.value(u"categories"_s).toArray())
        f.categories.append(v.toString());
    f.description = o.value(u"description"_s).toString();
    f.parentUid = o.value(u"parent"_s).toString();
    return f;
}

unsigned changedFields(const TaskFields &x, const TaskFields &y)
{
    const TaskFields a = x.normalized();
    const TaskFields b = y.normalized();
    unsigned m = 0;
    if (a.summary != b.summary)
        m |= FieldSummary;
    if (a.status != b.status)
        m |= FieldStatus;
    if (a.due != b.due)
        m |= FieldDue;
    if (a.start != b.start)
        m |= FieldStart;
    if (a.priority != b.priority)
        m |= FieldPriority;
    if (a.completed != b.completed)
        m |= FieldCompleted;
    if (a.recurrence != b.recurrence)
        m |= FieldRecurrence;
    if (a.reminders != b.reminders)
        m |= FieldReminders;
    if (a.categories != b.categories)
        m |= FieldCategories;
    if (a.description != b.description)
        m |= FieldDescription;
    if (a.parentUid != b.parentUid)
        m |= FieldParent;
    return m;
}

TaskFields withFields(const TaskFields &to, const TaskFields &from, unsigned mask)
{
    TaskFields out = to;
    if (mask & FieldSummary)
        out.summary = from.summary;
    if (mask & FieldStatus)
        out.status = from.status;
    if (mask & FieldDue)
        out.due = from.due;
    if (mask & FieldStart)
        out.start = from.start;
    if (mask & FieldPriority)
        out.priority = from.priority;
    if (mask & (FieldCompleted | FieldStatus))
        out.completed = from.completed;
    if (mask & FieldRecurrence)
        out.recurrence = from.recurrence;
    if (mask & FieldReminders)
        out.reminders = from.reminders;
    if (mask & FieldCategories)
        out.categories = from.categories;
    if (mask & FieldDescription)
        out.description = from.description;
    if (mask & FieldParent)
        out.parentUid = from.parentUid;
    return out;
}

QString blockIdForUid(const QString &uid)
{
    if (uid.startsWith(u"brulion-"_s)) {
        const QString id = uid.sliced(8);
        if (!id.isEmpty() && id.size() <= 64 && markdown::isValidBlockId(id))
            return id;
    }
    quint64 h = text::fnv1a64(uid.toUtf8());
    QString digits;
    for (int i = 0; i < 12; ++i) {
        const int d = int(h % 36);
        h /= 36;
        digits.prepend(QChar(d < 10 ? u'0' + d : u'a' + d - 10));
    }
    return u"c"_s + digits;
}

QString uidForBlockId(const QString &id)
{
    return u"brulion-"_s + id;
}

QString detail::randomId(int length)
{
    return strings::randomText(u"abcdefghijklmnopqrstuvwxyz0123456789", length);
}

QString newTaskBlockId()
{
    return detail::randomId(12);
}

QString lineSafeText(const QString &text)
{
    QString t = text.simplified();
    static const QStringList tokenChars = {u"📅"_s, u"📆"_s, u"🗓"_s, u"⏳"_s, u"⌛"_s, u"🛫"_s, u"➕"_s, u"✅"_s,
                                           u"❌"_s, u"🔁"_s, u"⏰"_s, u"🔺"_s, u"⏫"_s, u"🔼"_s, u"🔽"_s, u"⏬"_s,
                                           u"🆔"_s, u"⛔"_s, u"🏁"_s};
    for (const QString &c : tokenChars)
        t.remove(c);
    t = t.simplified();
    if (const auto id = markdown::blockIdAtEnd(t))
        t = t.first(id->from).trimmed();
    return cut(t, 2000);
}

TaskFields fieldsFromBlock(const Block &block, QTime defaultReminderTime)
{
    TaskFields f;
    if (!block.task)
        return f;
    const Task &t = *block.task;
    const auto [summary, tags] = splitTrailingTags(t.text);
    f.summary = summary;
    f.categories = tags;
    f.status = t.status;
    f.due = t.due;
    f.start = t.scheduled ? t.scheduled : t.start;
    f.priority = t.priority;
    f.completed = t.doneDate;
    f.recurrence = t.recurrence;
    for (const Reminder &r : block.reminders) {
        DateSpec at = r.at;
        if (!at.time)
            at.time = defaultReminderTime;
        f.reminders.append({at, r.alarm});
    }
    const qsizetype nl = block.text.indexOf(u'\n');
    if (nl >= 0) {
        QStringList lines = block.text.sliced(nl + 1).split(u'\n');
        for (QString &l : lines)
            l = unescapeContinuation(l);
        f.description = normalizedDescription(lines.join(u'\n'));
    }
    return f;
}

const io::ical::Component *firstTodo(const QList<io::ical::Component> &roots)
{
    for (const io::ical::Component &root : roots) {
        if (root.name != u"VCALENDAR")
            continue;
        const io::ical::Component *found = nullptr;
        for (const io::ical::Component &c : root.children) {
            if (c.name != u"VTODO")
                continue;
            if (!c.property(u"RECURRENCE-ID"))
                return &c;
            if (!found)
                found = &c;
        }
        if (found)
            return found;
    }
    return nullptr;
}

TaskFields fieldsFromTodo(const io::ical::Component &c, const ZoneTable &zones, const QTimeZone &zone,
                          QTime defaultReminderTime)
{
    TaskFields f;
    f.summary = lineSafeText(c.text(u"SUMMARY"));
    const QString status = c.text(u"STATUS").trimmed().toUpper();
    if (status == u"COMPLETED" || (status.isEmpty() && c.property(u"COMPLETED")))
        f.status = TaskStatus::Done;
    else if (status == u"IN-PROCESS")
        f.status = TaskStatus::Doing;
    else if (status == u"CANCELLED")
        f.status = TaskStatus::Cancelled;
    f.due = wallClock(c, u"DUE", zones, zone);
    f.start = wallClock(c, u"DTSTART", zones, zone);
    const int priority = c.text(u"PRIORITY").trimmed().toInt();
    if (priority >= 1 && priority <= 4)
        f.priority = Priority::High;
    else if (priority == 5)
        f.priority = Priority::Medium;
    else if (priority >= 6 && priority <= 9)
        f.priority = Priority::Low;
    if (f.status == TaskStatus::Done) {
        if (const auto done = wallClock(c, u"COMPLETED", zones, zone))
            f.completed = done->date;
    }
    if (const io::ical::Property *rrule = c.property(u"RRULE")) {
        QStringList dropped;
        if (const auto rule = io::ical::parseRRule(rrule->value, &dropped))
            f.recurrence = dates::formatRecurrence(*rule, dates::UiLocale::English);
    }
    for (const io::ical::Component *a : c.childrenNamed(u"VALARM")) {
        const QString action = a->text(u"ACTION").trimmed().toUpper();
        if (!action.isEmpty() && action != u"DISPLAY" && action != u"AUDIO")
            continue;
        const io::ical::Property *trigger = a->property(u"TRIGGER");
        if (!trigger)
            continue;
        std::optional<DateSpec> at;
        if (trigger->param(u"VALUE"_s).compare(u"DATE-TIME"_s, Qt::CaseInsensitive) == 0
            || !trigger->value.trimmed().contains(u'P')) {
            if (const auto v = io::ical::parseDateTime(*trigger)) {
                const NaiveDateTime t = zones.toTarget(*v, zone);
                at = DateSpec{t.date, QTime(t.time.hour(), t.time.minute())};
            }
        } else if (const auto d = io::ical::parseDurationParts(trigger->value)) {
            const bool fromEnd = trigger->param(u"RELATED"_s).compare(u"END"_s, Qt::CaseInsensitive) == 0;
            const std::optional<DateSpec> base = fromEnd || !f.start ? (f.due ? f.due : f.start) : f.start;
            if (base)
                at = shifted(DateSpec{base->date, base->time.value_or(QTime(0, 0))}, *d, zone);
        }
        if (!at || !at->isValid())
            continue;
        if (!at->time)
            at->time = defaultReminderTime;
        f.reminders.append({*at, action == u"AUDIO"});
    }
    for (const io::ical::Property &p : c.properties) {
        if (p.name == u"CATEGORIES") {
            for (const QStringView cat : qTokenize(QStringView(p.value), u',', Qt::SkipEmptyParts)) {
                const QString name = io::ical::unescapeText(cat).trimmed();
                if (!name.isEmpty() && !f.categories.contains(name, Qt::CaseInsensitive))
                    f.categories.append(name);
            }
        } else if (p.name == u"RELATED-TO" && f.parentUid.isEmpty()) {
            const QString type = p.param(u"RELTYPE"_s).toUpper();
            if (type.isEmpty() || type == u"PARENT")
                f.parentUid = io::ical::unescapeText(p.value).trimmed();
        }
    }
    f.description = normalizedDescription(c.text(u"DESCRIPTION"));
    return f;
}

QString newTaskBody(const TaskFields &f, const QString &blockId)
{
    QString line = u'[' + statusChar(f.status) + u"] "_s + lineSafeText(f.summary);
    for (const QString &cat : f.categories) {
        if (const QString tag = tagFor(cat); !tag.isEmpty())
            line += u" #"_s + tag;
    }
    if (f.priority != Priority::None)
        line += u' ' + tokens::priorityEmoji(f.priority).toString();
    if (!f.recurrence.isEmpty())
        line += u' ' + tokens::Recurrence.toString() + u' ' + f.recurrence;
    if (f.start)
        line += u' ' + tokens::Scheduled.toString() + u' ' + f.start->toString();
    if (f.due)
        line += u' ' + tokens::Due.toString() + u' ' + f.due->toString();
    if (f.status != TaskStatus::Done && f.status != TaskStatus::Cancelled) {
        for (const TaskReminder &r : f.reminders)
            line += u' ' + markdown::formatReminderToken(r.at, r.alarm);
    }
    if (f.status == TaskStatus::Done && f.completed)
        line += u' ' + tokens::Done.toString() + u' ' + f.completed->toString(u"yyyy-MM-dd"_s);
    if (!blockId.isEmpty())
        line += u" ^"_s + blockId;
    QString body = line;
    if (!f.description.isEmpty()) {
        for (const QString &l : f.description.split(u'\n'))
            body += u'\n' + escapeContinuation(l);
    }
    return body;
}

namespace {

/// The status of a body's task marker set to `s`: a checkbox's character, or a Logseq keyword.
QString setStatusMarker(const QString &body, TaskStatus s)
{
    static const QRegularExpression box(u"^\\[(.)\\]"_s);
    if (const auto m = box.match(body); m.hasMatch())
        return body.first(1) + statusChar(s) + body.sliced(2);
    static const QRegularExpression keyword(u"^(TODO|DOING|DONE|LATER|NOW|CANCELED|CANCELLED|WAITING)\\b"_s);
    if (const auto m = keyword.match(body); m.hasMatch()) {
        const QString old = m.captured(1);
        const bool laterWorkflow = old == u"LATER" || old == u"NOW";
        QString word;
        switch (s) {
        case TaskStatus::Todo: word = laterWorkflow ? u"LATER"_s : u"TODO"_s; break;
        case TaskStatus::Doing: word = laterWorkflow ? u"NOW"_s : u"DOING"_s; break;
        case TaskStatus::Done: word = u"DONE"_s; break;
        case TaskStatus::Cancelled: word = u"CANCELED"_s; break;
        case TaskStatus::Deferred: word = u"WAITING"_s; break;
        }
        return word + body.sliced(old.size());
    }
    return body;
}

/// [from, to) of the text of the body's first line: after the task marker, up to its first
/// metadata token or block id.
std::pair<qsizetype, qsizetype> textRange(const QString &first)
{
    qsizetype from = 0;
    static const QRegularExpression box(u"^\\[.\\][ \\t]?"_s);
    static const QRegularExpression keyword(u"^(?:TODO|DOING|DONE|LATER|NOW|CANCELED|CANCELLED|WAITING)\\b[ \\t]?"_s);
    if (const auto m = box.match(first); m.hasMatch())
        from = m.capturedLength(0);
    else if (const auto k = keyword.match(first); k.hasMatch())
        from = k.capturedLength(0);
    qsizetype to = first.size();
    if (const auto id = markdown::blockIdAtEnd(first))
        to = id->from;
    for (const markdown::Token &t : markdown::findTokens(first)) {
        if (t.from >= from && t.from < to)
            to = t.from;
    }
    while (to > from && first.at(to - 1).isSpace())
        --to;
    return {from, std::max(from, to)};
}

bool isKeptContinuation(const QString &line, bool &inDrawer)
{
    const QString t = line.trimmed();
    if (inDrawer) {
        if (t.compare(u":END:"_s, Qt::CaseInsensitive) == 0)
            inDrawer = false;
        return true;
    }
    static const QRegularExpression drawer(u"^:[A-Za-z_-]+:$"_s);
    if (drawer.match(t).hasMatch()) {
        inDrawer = true;
        return true;
    }
    static const QRegularExpression property(u"^[^\\s:][^:]*::(\\s|$)"_s);
    static const QRegularExpression planning(u"^(SCHEDULED|DEADLINE):"_s);
    static const QRegularExpression idLine(u"^\\^[A-Za-z0-9-]+$"_s);
    return property.match(t).hasMatch() || planning.match(t).hasMatch() || idLine.match(t).hasMatch();
}

QString removeAllFirstLineTokens(QString body, markdown::TokenKind kind)
{
    for (int guard = 0; guard < 64; ++guard) {
        const auto range = markdown::findFirstLineToken(body, kind);
        if (!range)
            break;
        body = markdown::replaceTokenRange(body, range->first, range->second, {});
    }
    return body;
}

} // namespace

QString applyFieldsToBody(const QString &input, const TaskFields &f, unsigned mask, QDate today)
{
    QString body = input;
    if (mask & FieldStatus) {
        body = setStatusMarker(body, f.status);
        const QString done = f.status == TaskStatus::Done
                                     ? tokens::Done.toString() + u' ' + f.completed.value_or(today).toString(u"yyyy-MM-dd"_s)
                                     : QString();
        body = markdown::setFirstLineToken(body, markdown::TokenKind::Done, done);
        if (f.status == TaskStatus::Cancelled) {
            if (!markdown::findFirstLineToken(body, markdown::TokenKind::Cancelled))
                body = markdown::setFirstLineToken(body, markdown::TokenKind::Cancelled,
                                                   tokens::Cancelled.toString() + u' ' + today.toString(u"yyyy-MM-dd"_s));
        } else {
            body = markdown::setFirstLineToken(body, markdown::TokenKind::Cancelled, {});
        }
    } else if ((mask & FieldCompleted) && f.status == TaskStatus::Done && f.completed) {
        body = markdown::setFirstLineToken(body, markdown::TokenKind::Done,
                                           tokens::Done.toString() + u' ' + f.completed->toString(u"yyyy-MM-dd"_s));
    }
    if (mask & (FieldSummary | FieldCategories)) {
        const qsizetype nl = body.indexOf(u'\n');
        const QString first = nl < 0 ? body : body.first(nl);
        const auto [from, to] = textRange(first);
        const auto [oldSummary, oldTags] = splitTrailingTags(first.sliced(from, to - from));
        QString text = (mask & FieldSummary) ? lineSafeText(f.summary) : oldSummary;
        QStringList tags = oldTags;
        if (mask & FieldCategories) {
            tags.clear();
            for (const QString &c : f.categories) {
                if (const QString tag = tagFor(c); !tag.isEmpty())
                    tags.append(tag);
            }
        }
        for (const QString &tag : std::as_const(tags))
            text += tag.contains(u' ') ? QString(u" #[["_s + tag + u"]]"_s) : QString(u" #"_s + tag);
        QString rest = first.sliced(to);
        if (!rest.isEmpty() && !rest.front().isSpace())
            rest.prepend(u' ');
        body = first.first(from) + text.trimmed() + rest + (nl < 0 ? QString() : body.sliced(nl));
    }
    if (mask & FieldDue)
        body = markdown::setFirstLineToken(body, markdown::TokenKind::Due,
                                           f.due ? tokens::Due.toString() + u' ' + f.due->toString() : QString());
    if (mask & FieldStart) {
        const bool usesStart = markdown::findFirstLineToken(body, markdown::TokenKind::Start)
                               && !markdown::findFirstLineToken(body, markdown::TokenKind::Scheduled);
        const auto kind = usesStart ? markdown::TokenKind::Start : markdown::TokenKind::Scheduled;
        const QStringView emoji = usesStart ? tokens::Start : tokens::Scheduled;
        body = markdown::setFirstLineToken(body, kind, f.start ? emoji.toString() + u' ' + f.start->toString() : QString());
    }
    if (mask & FieldPriority) {
        if (f.priority == Priority::None) {
            if (const auto range = markdown::findFirstLineToken(body, markdown::TokenKind::Priority))
                body = markdown::replaceTokenRange(body, range->first, range->second, {});
            if (const auto range = markdown::findFirstLineToken(body, markdown::TokenKind::LogseqPriority))
                body = markdown::replaceTokenRange(body, range->first, range->second, {});
        } else if (priorityClass(markdown::blockPriority(body).first) != priorityClass(f.priority)) {
            body = markdown::setBlockPriority(body, f.priority);
        }
    }
    if (mask & FieldRecurrence)
        body = markdown::setBlockRecurrence(body, f.recurrence);
    if (mask & FieldReminders) {
        body = removeAllFirstLineTokens(body, markdown::TokenKind::Reminder);
        if (f.status != TaskStatus::Done && f.status != TaskStatus::Cancelled) {
            for (const TaskReminder &r : f.reminders)
                body = markdown::setFirstLineToken(body, markdown::TokenKind::Reminder,
                                                   markdown::formatReminderToken(r.at, r.alarm), true);
        }
    }
    if (mask & FieldDescription) {
        QStringList lines = body.split(u'\n');
        const QString first = lines.takeFirst();
        QStringList kept;
        bool inDrawer = false;
        for (const QString &l : std::as_const(lines)) {
            if (isKeptContinuation(l, inDrawer))
                kept.append(l);
        }
        if (!f.description.isEmpty()) {
            for (const QString &l : f.description.split(u'\n'))
                kept.append(escapeContinuation(l));
        }
        body = first;
        for (const QString &l : std::as_const(kept))
            body += u'\n' + l;
    }
    return body;
}

QByteArray serialize(const QList<io::ical::Component> &roots)
{
    io::ical::Writer w;
    for (const io::ical::Component &c : roots)
        writeComponent(w, c);
    return w.data();
}

QByteArray newTodo(const QString &uid, const TaskFields &fields, const TodoContext &context)
{
    io::ical::Component cal;
    cal.name = u"VCALENDAR"_s;
    cal.properties.append({u"VERSION"_s, {}, u"2.0"_s});
    cal.properties.append({u"PRODID"_s, {}, context.prodId});
    io::ical::Component todo;
    todo.name = u"VTODO"_s;
    todo.properties.append({u"UID"_s, {}, io::ical::escapeText(uid)});
    const QString now = io::ical::formatUtc(context.now);
    todo.properties.append({u"CREATED"_s, {}, now});
    bool usedZone = false;
    applyToTodo(todo, fields, AllFields, context, &usedZone);
    stamp(todo, context, false);
    cal.children.append(todo);
    if (usedZone)
        ensureTimezone(cal, context);
    return serialize({cal});
}

QByteArray patchTodo(const QByteArray &ics, const TaskFields &fields, unsigned mask, const TodoContext &context)
{
    QList<io::ical::Component> roots = io::ical::parse(ics);
    for (io::ical::Component &root : roots) {
        if (root.name != u"VCALENDAR")
            continue;
        for (io::ical::Component &c : root.children) {
            if (c.name != u"VTODO" || c.property(u"RECURRENCE-ID"))
                continue;
            bool usedZone = false;
            applyToTodo(c, fields, mask, context, &usedZone);
            stamp(c, context, true);
            if (usedZone)
                ensureTimezone(root, context);
            return serialize(roots);
        }
    }
    return {};
}

std::optional<io::ical::Component> vtimezoneFor(const QTimeZone &zone, int year)
{
    if (!usableZone(zone))
        return std::nullopt;
#if QT_CONFIG(timezone)
    io::ical::Component tz;
    tz.name = u"VTIMEZONE"_s;
    tz.properties.append({u"TZID"_s, {}, zoneId(zone)});
    const QDateTime from(QDate(year, 1, 1), QTime(0, 0), QTimeZone::UTC);
    const QDateTime to(QDate(year + 1, 1, 1), QTime(0, 0), QTimeZone::UTC);
    const QTimeZone::OffsetDataList list = zone.transitions(from, to);
    if (list.isEmpty()) {
        const int offset = zone.offsetFromUtc(from);
        io::ical::Component std_;
        std_.name = u"STANDARD"_s;
        std_.properties.append({u"DTSTART"_s, {}, u"19700101T000000"_s});
        std_.properties.append({u"TZOFFSETFROM"_s, {}, offsetText(offset)});
        std_.properties.append({u"TZOFFSETTO"_s, {}, offsetText(offset)});
        tz.children.append(std_);
        return tz;
    }
    static constexpr std::array<QStringView, 7> codes = {u"MO", u"TU", u"WE", u"TH", u"FR", u"SA", u"SU"};
    for (const QTimeZone::OffsetData &t : list) {
        const int before = zone.offsetFromUtc(t.atUtc.addSecs(-1));
        const int after = t.offsetFromUtc;
        const QDateTime local = t.atUtc.toUTC().addSecs(before);
        const QDate day = local.date();
        io::ical::Component o;
        o.name = t.daylightTimeOffset > 0 ? u"DAYLIGHT"_s : u"STANDARD"_s;
        o.properties.append({u"DTSTART"_s, {}, io::ical::formatLocalDateTime(day, local.time())});
        o.properties.append({u"TZOFFSETFROM"_s, {}, offsetText(before)});
        o.properties.append({u"TZOFFSETTO"_s, {}, offsetText(after)});
        if (!t.abbreviation.isEmpty())
            o.properties.append({u"TZNAME"_s, {}, io::ical::escapeText(t.abbreviation)});
        const int nth = (day.day() - 1) / 7 + 1;
        const bool last = day.day() + 7 > day.daysInMonth();
        o.properties.append({u"RRULE"_s, {},
                             u"FREQ=YEARLY;BYMONTH=%1;BYDAY=%2%3"_s.arg(day.month())
                                     .arg(last ? u"-1"_s : QString::number(nth))
                                     .arg(codes[day.dayOfWeek() - 1])});
        tz.children.append(o);
    }
    return tz;
#else
    Q_UNUSED(year);
    return std::nullopt;
#endif
}

// ---- events ---------------------------------------------------------------------------------------

namespace {

void writeEventFields(io::ical::Component &e, const EventEdit &edit, const TodoContext &ctx, bool *usedZone)
{
    setProperty(e, u"SUMMARY"_s, io::ical::escapeText(edit.title.simplified()));
    if (edit.location.trimmed().isEmpty())
        removeProperty(e, u"LOCATION");
    else
        setProperty(e, u"LOCATION"_s, io::ical::escapeText(edit.location.simplified()));
    if (edit.description.trimmed().isEmpty())
        removeProperty(e, u"DESCRIPTION");
    else
        setProperty(e, u"DESCRIPTION"_s, io::ical::escapeText(normalizedDescription(edit.description)));
    removeProperty(e, u"DURATION");
    if (edit.allDay()) {
        setDate(e, u"DTSTART"_s, DateSpec{edit.startDate, std::nullopt}, ctx.zone, usedZone);
        const QDate last = edit.endDate.isValid() && edit.endDate >= edit.startDate ? edit.endDate : edit.startDate;
        setDate(e, u"DTEND"_s, DateSpec{last.addDays(1), std::nullopt}, ctx.zone, usedZone); // exclusive
    } else {
        setDate(e, u"DTSTART"_s, DateSpec{edit.startDate, edit.startTime}, ctx.zone, usedZone);
        QDate endDay = edit.endDate.isValid() ? edit.endDate : edit.startDate;
        QTime endTime = edit.endTime.value_or(edit.startTime->addSecs(3600));
        if (QDateTime(endDay, endTime) < QDateTime(edit.startDate, *edit.startTime)) {
            endDay = edit.startDate;
            endTime = *edit.startTime;
        }
        setDate(e, u"DTEND"_s, DateSpec{endDay, endTime}, ctx.zone, usedZone);
    }
    if (edit.reminderChanged) {
        e.children.removeIf([](const io::ical::Component &a) {
            if (a.name != u"VALARM")
                return false;
            const QString action = a.text(u"ACTION").trimmed().toUpper();
            return action.isEmpty() || action == u"DISPLAY" || action == u"AUDIO";
        });
        if (edit.reminderMinutes >= 0) {
            io::ical::Component alarm;
            alarm.name = u"VALARM"_s;
            alarm.properties.append({u"ACTION"_s, {}, u"DISPLAY"_s});
            alarm.properties.append({u"DESCRIPTION"_s, {}, io::ical::escapeText(edit.title.simplified())});
            alarm.properties.append({u"TRIGGER"_s, {},
                                     edit.reminderMinutes == 0 ? u"PT0S"_s : u"-PT%1M"_s.arg(edit.reminderMinutes)});
            e.children.append(alarm);
        }
    }
}

void writeRule(io::ical::Component &e, const EventEdit &edit)
{
    if (!edit.ruleChanged)
        return;
    const auto rule = edit.rule.isEmpty() ? std::nullopt : dates::parseRecurrence(edit.rule);
    if (rule)
        setProperty(e, u"RRULE"_s, io::ical::formatRRule(*rule));
    else
        removeProperty(e, u"RRULE");
}

/// The series' component and its calendar in `roots`.
std::pair<io::ical::Component *, io::ical::Component *> masterOf(QList<io::ical::Component> &roots, const QString &uid)
{
    for (io::ical::Component &root : roots) {
        if (root.name != u"VCALENDAR")
            continue;
        for (io::ical::Component &c : root.children) {
            if (c.name == u"VEVENT" && c.text(u"UID").trimmed() == uid && !c.property(u"RECURRENCE-ID"))
                return {&root, &c};
        }
    }
    return {nullptr, nullptr};
}

bool sameInstance(const io::ical::Property &rid, const io::ical::DateTimeValue &instance)
{
    const auto v = io::ical::parseDateTime(rid);
    return v && v->date == instance.date && v->time == instance.time;
}

} // namespace

QString formatDateTimeValue(const io::ical::DateTimeValue &v)
{
    if (!v.time)
        return io::ical::formatDate(v.date);
    QString text = io::ical::formatLocalDateTime(v.date, *v.time);
    if (v.utc)
        text += u'Z';
    return text;
}

namespace {

/// Whether an EXDATE of `master` already names `instance`.
bool excluded(const io::ical::Component &master, const io::ical::DateTimeValue &instance)
{
    for (const io::ical::Property &p : master.properties) {
        if (p.name != u"EXDATE")
            continue;
        for (const QString &value : p.value.split(u',')) {
            io::ical::Property one = p;
            one.value = value.trimmed();
            if (sameInstance(one, instance))
                return true;
        }
    }
    return false;
}

QHash<QString, QString> paramsOf(const io::ical::DateTimeValue &v)
{
    QHash<QString, QString> params;
    if (!v.time)
        params.insert(u"VALUE"_s, u"DATE"_s);
    else if (!v.utc && !v.tzid.isEmpty())
        params.insert(u"TZID"_s, v.tzid);
    return params;
}

} // namespace

QByteArray newEvent(const QString &uid, const EventEdit &edit, const TodoContext &context)
{
    io::ical::Component cal;
    cal.name = u"VCALENDAR"_s;
    cal.properties.append({u"VERSION"_s, {}, u"2.0"_s});
    cal.properties.append({u"PRODID"_s, {}, context.prodId});
    io::ical::Component e;
    e.name = u"VEVENT"_s;
    e.properties.append({u"UID"_s, {}, io::ical::escapeText(uid)});
    e.properties.append({u"CREATED"_s, {}, io::ical::formatUtc(context.now)});
    bool usedZone = false;
    writeEventFields(e, edit, context, &usedZone);
    writeRule(e, edit);
    stamp(e, context, false);
    cal.children.append(e);
    if (usedZone)
        ensureTimezone(cal, context);
    return serialize({cal});
}

QByteArray patchEvent(const QByteArray &ics, const QString &uid, const EventEdit &edit, const TodoContext &context)
{
    QList<io::ical::Component> roots = io::ical::parse(ics);
    auto [cal, e] = masterOf(roots, uid);
    if (!e) {
        // A single override without its series (an invitation to one meeting): that one.
        for (io::ical::Component &root : roots) {
            for (io::ical::Component &c : root.children) {
                if (c.name == u"VEVENT" && c.text(u"UID").trimmed() == uid) {
                    cal = &root;
                    e = &c;
                }
            }
        }
        if (!e)
            return {};
    }
    bool usedZone = false;
    writeEventFields(*e, edit, context, &usedZone);
    writeRule(*e, edit);
    stamp(*e, context, true);
    if (usedZone)
        ensureTimezone(*cal, context);
    return serialize(roots);
}

QByteArray patchOccurrence(const QByteArray &ics, const QString &uid, const io::ical::DateTimeValue &instance,
                           const EventEdit &edit, const TodoContext &context)
{
    QList<io::ical::Component> roots = io::ical::parse(ics);
    auto [cal, master] = masterOf(roots, uid);
    if (!master)
        return {};
    io::ical::Component *target = nullptr;
    for (io::ical::Component &c : cal->children) {
        if (c.name != u"VEVENT" || c.text(u"UID").trimmed() != uid)
            continue;
        if (const io::ical::Property *rid = c.property(u"RECURRENCE-ID"); rid && sameInstance(*rid, instance))
            target = &c;
    }
    if (!target) {
        io::ical::Component copy = *master;
        removeProperty(copy, u"RRULE");
        removeProperty(copy, u"RDATE");
        removeProperty(copy, u"EXDATE");
        copy.properties.append({u"RECURRENCE-ID"_s, paramsOf(instance), formatDateTimeValue(instance)});
        cal->children.append(copy);
        target = &cal->children.last();
    }
    EventEdit one = edit;
    one.ruleChanged = false;
    bool usedZone = false;
    writeEventFields(*target, one, context, &usedZone);
    stamp(*target, context, true);
    if (usedZone)
        ensureTimezone(*cal, context);
    return serialize(roots);
}

QByteArray excludeOccurrence(const QByteArray &ics, const QString &uid, const io::ical::DateTimeValue &instance,
                             const TodoContext &context)
{
    QList<io::ical::Component> roots = io::ical::parse(ics);
    auto [cal, master] = masterOf(roots, uid);
    if (!master)
        return {};
    if (!excluded(*master, instance)) // (left out once: the same text again)
        master->properties.append({u"EXDATE"_s, paramsOf(instance), formatDateTimeValue(instance)});
    stamp(*master, context, true);
    cal->children.removeIf([&uid, &instance](const io::ical::Component &c) {
        if (c.name != u"VEVENT" || c.text(u"UID").trimmed() != uid)
            return false;
        const io::ical::Property *rid = c.property(u"RECURRENCE-ID");
        return rid && sameInstance(*rid, instance);
    });
    return serialize(roots);
}

namespace {

/// `master`'s EXDATEs, one value each, renamed.
QList<io::ical::Property> renamedExdates(const io::ical::Component &master,
                                         const std::function<DateSpec(const io::ical::DateTimeValue &)> &rename,
                                         const TodoContext &context, bool *usedZone)
{
    QList<io::ical::Property> out;
    for (const io::ical::Property &p : master.properties) {
        if (p.name != u"EXDATE") {
            out.append(p);
            continue;
        }
        for (const QString &value : p.value.split(u',')) {
            io::ical::Property one = p;
            one.value = value.trimmed();
            if (const auto v = io::ical::parseDateTime(one))
                out.append(dateProperty(u"EXDATE"_s, rename(*v), context.zone, usedZone));
        }
    }
    return out;
}

} // namespace

QByteArray renameInstances(const QByteArray &ics, const QString &uid,
                           const std::function<DateSpec(const io::ical::DateTimeValue &)> &rename,
                           const TodoContext &context)
{
    QList<io::ical::Component> roots = io::ical::parse(ics);
    auto [cal, master] = masterOf(roots, uid);
    if (!master)
        return ics;
    bool usedZone = false;
    master->properties = renamedExdates(*master, rename, context, &usedZone);
    for (io::ical::Component &c : cal->children) {
        const bool ofSeries = c.name == u"VEVENT" && c.text(u"UID").trimmed() == uid;
        const io::ical::Property *rid = ofSeries ? c.property(u"RECURRENCE-ID") : nullptr;
        if (const auto v = rid ? io::ical::parseDateTime(*rid) : std::nullopt)
            setDate(c, u"RECURRENCE-ID"_s, rename(*v), context.zone, &usedZone);
    }
    if (usedZone)
        ensureTimezone(*cal, context);
    return serialize(roots);
}

} // namespace brulion::caldav

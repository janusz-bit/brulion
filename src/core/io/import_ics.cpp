// iCalendar importer: events (VEVENT) become bullets on the journal page of their date, to-dos
// (VTODO) become tasks on a page named after the calendar. Times are converted to the vault's
// wall-clock zone; simple RRULEs become 🔁 rules, VALARMs ⏰ reminders (⏰! for audio alarms).
#include "io/htmlmarkdown.h"
#include "io/ical.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "markdown/tokens.h"
#include "util/paths.h"

#include <QMap>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

bool isCalendarFile(const InputFile &f)
{
    const QString ext = paths::extension(f.path);
    if (ext == u".ics" || ext == u".ical" || ext == u".icalendar" || ext == u".ifb")
        return true;
    QByteArrayView data = f.data;
    if (data.startsWith("\xEF\xBB\xBF"))
        data = data.sliced(3);
    return data.trimmed().startsWith("BEGIN:VCALENDAR");
}

/// `base` moved by `d` in the wall-clock `zone`: days are nominal (the same wall-clock time on
/// another day), seconds exact — RFC 5545 3.3.6. Never computed in the system zone.
DateSpec shifted(const DateSpec &base, const ical::Duration &d, const QTimeZone &zone)
{
    const QDate day = base.date.addDays(d.days);
    if (!base.time && d.seconds == 0)
        return DateSpec{day, std::nullopt};
    const QDateTime moved = QDateTime(day, base.time.value_or(QTime(0, 0)), zone).addSecs(d.seconds).toTimeZone(zone);
    if (!moved.isValid())
        return DateSpec{day, base.time};
    return DateSpec{moved.date(), QTime(moved.time().hour(), moved.time().minute())};
}

struct Times {
    std::optional<DateSpec> start;
    std::optional<DateSpec> end;
};

class IcsImporter final : public Importer {
public:
    QString id() const override { return u"ics"_s; }
    QString name() const override { return u"iCalendar"_s; }
    QStringList fileTypes() const override { return {u"*.ics"_s, u"*.ical"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        // Only calendars: certain. A calendar among notes (an attachment of a vault or an
        // export) loses against the importers of the notes.
        if (files.isEmpty() || !std::any_of(files.begin(), files.end(), isCalendarFile))
            return 0;
        return std::all_of(files.begin(), files.end(), isCalendarFile) ? 100 : 15;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        for (const InputFile &f : files) {
            if (!isCalendarFile(f))
                continue;
            QStringList warnings;
            const QList<ical::Component> roots = ical::parse(f.data, &warnings);
            for (const QString &w : std::as_const(warnings))
                ctx.warn(w);
            for (const ical::Component &cal : roots) {
                QString calendarName = cal.text(u"X-WR-CALNAME").trimmed();
                if (calendarName.isEmpty())
                    calendarName = paths::basename(f.path).section(u'.', 0, -2);
                importCalendar(cal, calendarName.isEmpty() ? name() : calendarName, ctx);
            }
        }
        return ctx.take();
    }

private:
    struct State {
        explicit State(ImportContext &context) : ctx(context) { }
        ImportContext &ctx;
        bool unknownZone = false;
        bool droppedRecurrence = false;
        bool unsupportedRecurrence = false;
    };

    static void importCalendar(const ical::Component &cal, const QString &calendarName, ImportContext &ctx)
    {
        State st(ctx);
        QStringList tasks;
        // Events of the same day in time order.
        QMap<QDate, QList<std::pair<QString, QString>>> days; // date -> (sort key, text)
        for (const ical::Component &c : cal.children) {
            if (c.name == u"VEVENT") {
                if (c.property(u"RECURRENCE-ID")
                    || c.text(u"STATUS").trimmed().compare(u"CANCELLED"_s, Qt::CaseInsensitive) == 0)
                    continue;
                const Times t = times(c, st);
                if (!t.start) {
                    ctx.warn(IoMessages::tr("Events without a start date were skipped."));
                    continue;
                }
                const QString key = t.start->time ? t.start->time->toString(u"HH:mm"_s) : QString();
                days[t.start->date].append({key, eventText(c, t, st)});
            } else if (c.name == u"VTODO") {
                tasks.append(taskText(c, st));
            }
        }
        for (auto it = days.begin(); it != days.end(); ++it) {
            auto &events = it.value();
            std::stable_sort(events.begin(), events.end(),
                             [](const auto &a, const auto &b) { return a.first < b.first; });
            QStringList texts;
            for (const auto &e : std::as_const(events))
                texts.append(e.second);
            ctx.addJournal(it.key(), texts.join(u'\n') + u'\n');
        }
        if (!tasks.isEmpty())
            ctx.addPage(ctx.claimPage(pageSegment(calendarName)), tasks.join(u'\n') + u'\n');
        if (st.unknownZone)
            ctx.warn(IoMessages::tr("Some time zones of the calendar are unknown; their times were kept as written."));
        if (st.droppedRecurrence)
            ctx.warn(
                    IoMessages::tr("Some recurrence rules have an end (COUNT/UNTIL) or details that are not supported; "
                                   "they were simplified."));
        if (st.unsupportedRecurrence)
            ctx.warn(
                    IoMessages::tr("Some recurrence rules are not supported; only the first occurrence was imported."));
    }

    static std::optional<DateSpec> wallClock(const ical::Component &c, QStringView property, State &st)
    {
        const ical::Property *p = c.property(property);
        if (!p)
            return std::nullopt;
        const auto value = ical::parseDateTime(*p);
        if (!value)
            return std::nullopt;
        bool unknown = false;
        const DateSpec spec = ical::toWallClock(*value, st.ctx.options().timeZone, &unknown);
        st.unknownZone = st.unknownZone || unknown;
        return spec;
    }

    static Times times(const ical::Component &c, State &st)
    {
        Times t;
        t.start = wallClock(c, u"DTSTART", st);
        t.end = wallClock(c, u"DTEND", st);
        if (!t.end && t.start) {
            if (const ical::Property *d = c.property(u"DURATION")) {
                if (const auto duration = ical::parseDurationParts(d->value)) {
                    t.end = shifted(*t.start, *duration, st.ctx.options().timeZone);
                    if (!t.start->time)
                        t.end->time.reset();
                }
            }
        }
        return t;
    }

    static QString recurrence(const ical::Component &c, State &st)
    {
        const ical::Property *p = c.property(u"RRULE");
        if (!p)
            return {};
        QStringList dropped;
        const auto rule = ical::parseRRule(p->value, &dropped);
        if (!rule) {
            st.unsupportedRecurrence = true;
            return {};
        }
        if (!dropped.isEmpty())
            st.droppedRecurrence = true;
        return markdown::tokens::Recurrence.toString() + u' '
               + dates::formatRecurrence(*rule, dates::UiLocale::English);
    }

    /// ⏰ tokens of the VALARMs (relative triggers count from the start, or the end).
    static QStringList alarms(const ical::Component &c, const Times &t, State &st)
    {
        QStringList out;
        for (const ical::Component *a : c.childrenNamed(u"VALARM")) {
            const ical::Property *trigger = a->property(u"TRIGGER");
            if (!trigger)
                continue;
            std::optional<DateSpec> at;
            if (trigger->param(u"VALUE"_s).compare(u"DATE-TIME"_s, Qt::CaseInsensitive) == 0
                || !trigger->value.trimmed().contains(u'P')) {
                if (const auto v = ical::parseDateTime(*trigger)) {
                    bool unknown = false;
                    at = ical::toWallClock(*v, st.ctx.options().timeZone, &unknown);
                }
            } else if (const auto duration = ical::parseDurationParts(trigger->value)) {
                const bool fromEnd = trigger->param(u"RELATED"_s).compare(u"END"_s, Qt::CaseInsensitive) == 0;
                const std::optional<DateSpec> &base = fromEnd && t.end ? t.end : t.start;
                if (base) {
                    // Relative to the start of the day for all-day events.
                    const DateSpec from{base->date, base->time.value_or(QTime(0, 0))};
                    at = shifted(from, *duration, st.ctx.options().timeZone);
                }
            }
            if (!at || !at->isValid())
                continue;
            if (!at->time)
                at->time = st.ctx.config().defaultReminderTime;
            const bool loud = a->text(u"ACTION").trimmed().compare(u"AUDIO"_s, Qt::CaseInsensitive) == 0;
            const QString token = markdown::formatReminderToken(*at, loud);
            if (!out.contains(token))
                out.append(token);
        }
        return out;
    }

    /// `lineStart`: the text begins the bullet's content, where block syntax ("- ", "# ", "1. ")
    /// would be read as a nested list / heading.
    static QString singleLine(const QString &text, bool lineStart = false)
    {
        return escapeMarkdownText(QString(text).replace(u'\n', u' ').simplified(), lineStart);
    }

    /// Continuation lines: location / url properties and the description.
    static QStringList details(const ical::Component &c)
    {
        QStringList out;
        if (const QString location = c.text(u"LOCATION").simplified(); !location.isEmpty())
            out.append(u"location:: "_s + location);
        if (const QString url = c.text(u"URL").trimmed(); !url.isEmpty())
            out.append(u"url:: "_s + url);
        const QString description = normalizeNewlines(c.text(u"DESCRIPTION"));
        if (!description.isEmpty()) {
            for (const QString &line : description.split(u'\n'))
                out.append(line.trimmed().isEmpty() ? QString() : escapeMarkdownText(line.trimmed(), true));
        }
        return out;
    }

    static QString withDetails(QString first, const QStringList &detailLines)
    {
        for (const QString &line : detailLines)
            first += line.isEmpty() ? u"\n"_s : u"\n  "_s + line;
        return first;
    }

    static QString eventText(const ical::Component &c, const Times &t, State &st)
    {
        QString summary = singleLine(c.text(u"SUMMARY"), !t.start->time);
        if (summary.isEmpty())
            summary = IoMessages::tr("(no title)");
        QString line = u"- "_s;
        if (t.start->time) {
            line += t.start->time->toString(u"HH:mm"_s);
            if (t.end && t.end->time && t.end->date == t.start->date && *t.end->time != *t.start->time)
                line += u"–"_s + t.end->time->toString(u"HH:mm"_s);
            line += u' ';
        }
        line += summary;
        // Multi-day events: the last day (DTEND of all-day events is exclusive).
        if (t.end) {
            const QDate last = t.end->time ? t.end->date : t.end->date.addDays(-1);
            if (last > t.start->date)
                line += u" (→ "_s + last.toString(u"yyyy-MM-dd"_s) + u')';
        }
        if (const QString rule = recurrence(c, st); !rule.isEmpty())
            line += u' ' + rule;
        for (const QString &a : alarms(c, t, st))
            line += u' ' + a;
        return withDetails(line, details(c));
    }

    static QString taskText(const ical::Component &c, State &st)
    {
        const QString status = c.text(u"STATUS").trimmed().toUpper();
        const QChar box = status == u"COMPLETED"    ? u'x'
                          : status == u"CANCELLED"  ? u'-'
                          : status == u"IN-PROCESS" ? u'/'
                                                    : u' ';
        QString summary = singleLine(c.text(u"SUMMARY"));
        if (summary.isEmpty())
            summary = IoMessages::tr("(no title)");
        QString line = u"- ["_s + box + u"] "_s + summary;
        for (const QString &category : c.text(u"CATEGORIES").split(u',', Qt::SkipEmptyParts)) {
            const QString tag = tagName(category);
            if (!tag.isEmpty())
                line += u" #"_s + tag;
        }
        const int priority = c.text(u"PRIORITY").trimmed().toInt();
        if (priority >= 1 && priority <= 4)
            line += u' ' + markdown::tokens::PriorityHigh.toString();
        else if (priority == 5)
            line += u' ' + markdown::tokens::PriorityMedium.toString();
        else if (priority >= 6 && priority <= 9)
            line += u' ' + markdown::tokens::PriorityLow.toString();
        Times t;
        t.start = wallClock(c, u"DTSTART", st);
        t.end = wallClock(c, u"DUE", st);
        if (t.start)
            line += u' ' + markdown::tokens::Scheduled.toString() + u' ' + t.start->toString();
        if (t.end)
            line += u' ' + markdown::tokens::Due.toString() + u' ' + t.end->toString();
        if (const QString rule = recurrence(c, st); !rule.isEmpty())
            line += u' ' + rule;
        if (box != u'x' && box != u'-') {
            Times alarmBase = t;
            if (!alarmBase.start)
                alarmBase.start = t.end;
            for (const QString &a : alarms(c, alarmBase, st))
                line += u' ' + a;
        }
        if (box == u'x') {
            if (const auto completed = wallClock(c, u"COMPLETED", st))
                line += u' ' + markdown::tokens::Done.toString() + u' ' + completed->date.toString(u"yyyy-MM-dd"_s);
        }
        return withDetails(line, details(c));
    }
};

} // namespace

std::unique_ptr<Importer> makeIcsImporter()
{
    return std::make_unique<IcsImporter>();
}

} // namespace brulion::io::detail

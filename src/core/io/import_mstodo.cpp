// Microsoft To Do importer: the JSON of Microsoft Graph's To Do API (todoTaskList / todoTask).
// Accepted shapes (several files at once or a ZIP of them, too):
//   * the bundle Brulion's Graph download writes (mstodo module): {"brulion": "mstodo-export",
//     "version": 1, "lists": [{list…, "tasks": [{task…, "checklistItems", "linkedResources",
//     "attachments"}]}]};
//   * Graph Explorer's `GET /me/todo/lists?$expand=tasks`: {"value": [{list…, "tasks": […]}]};
//   * one list's tasks, `GET /me/todo/lists/{id}/tasks`: {"value": [task…]} — the page is named
//     after the file.
// Every list becomes a page of tasks (Obsidian Tasks syntax, docs/SYNTAX.md §5); Graph's times
// (dateTimeTimeZone: UTC unless the request asked otherwise, IANA or Windows zone names) are
// converted to the vault's wall-clock zone, never through the system zone.
#include "io/htmlmarkdown.h"
#include "io/htmlutil_p.h"
#include "io/ical.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "markdown/tokens.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr int MaxDetectFiles = 40;
/// Larger attachments are left out (Microsoft To Do itself takes at most 25 MB per file).
constexpr qint64 MaxAttachmentBytes = 25 * 1024 * 1024;

bool isTaskObject(const QJsonObject &o)
{
    return o.contains(u"title"_s) && (o.contains(u"status"_s) || o.contains(u"importance"_s));
}

bool isListObject(const QJsonObject &o)
{
    return o.contains(u"displayName"_s) && (o.contains(u"tasks"_s) || o.contains(u"wellknownListName"_s));
}

/// The file is (most likely) To Do's JSON.
bool isMsTodoJson(const QJsonObject &o)
{
    if (o.value(u"brulion"_s).toString() == u"mstodo-export")
        return true;
    if (o.value(u"@odata.context"_s).toString().contains(u"/todo/lists"_s, Qt::CaseInsensitive))
        return true;
    const QJsonArray value = o.value(u"value"_s).toArray();
    if (value.isEmpty())
        return false;
    const QJsonObject first = value.first().toObject();
    return isTaskObject(first) || isListObject(first);
}

/// A dateTimeTimeZone (or a plain ISO date-time) as written: date, time and zone.
struct GraphTime {
    std::optional<ical::DateTimeValue> value;
    bool malformed = false; ///< there was a value, but it is no date
};

bool isUtcName(const QString &zone)
{
    const QString z = zone.trimmed().toLower();
    return z.isEmpty() || z == u"utc" || z == u"z" || z == u"gmt" || z == u"etc/utc" || z == u"etc/gmt"
           || z == u"coordinated universal time" || z.endsWith(u"/utc");
}

/// "2026-10-27T22:00:00.0000000" in `zone` ("UTC", "Europe/Warsaw", "Central European Standard
/// Time"), "2026-10-20T08:12:33.123Z", "2026-10-27". An empty zone is Graph's default, UTC.
/// Graph's "no date" (year 1) counts as missing.
GraphTime parseGraphTime(const QString &text, const QString &zone)
{
    GraphTime out;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return out;
    static const QRegularExpression re(
            uR"(^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:[.,]\d+)?)?)?\s*(Z|[+-]\d{2}:?\d{2})?$)"_s);
    const QRegularExpressionMatch m = re.match(t);
    if (!m.hasMatch()) {
        out.malformed = true;
        return out;
    }
    const QDate date(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    if (!date.isValid()) {
        out.malformed = true;
        return out;
    }
    if (date.year() < 1900)
        return out;
    ical::DateTimeValue v;
    v.date = date;
    if (m.capturedLength(4) > 0) {
        const QTime time(m.captured(4).toInt(), m.captured(5).toInt());
        if (!time.isValid()) {
            out.malformed = true;
            return out;
        }
        v.time = time;
    }
    const QString offset = m.captured(7);
    if (offset == u"Z") {
        v.utc = true;
    } else if (!offset.isEmpty()) {
        // An explicit offset: the instant in UTC.
        const QString digits = QString(offset).remove(u':');
        const int seconds = (digits.mid(1, 2).toInt() * 60 + digits.mid(3, 2).toInt()) * 60;
        const QDateTime at(v.date, v.time.value_or(QTime(0, 0)),
                           QTimeZone::fromSecondsAheadOfUtc(offset.startsWith(u'-') ? -seconds : seconds));
        const QDateTime utc = at.toUTC();
        v.date = utc.date();
        v.time = QTime(utc.time().hour(), utc.time().minute());
        v.utc = true;
    } else if (isUtcName(zone)) {
        v.utc = true;
    } else {
        v.tzid = zone.trimmed();
    }
    out.value = v;
    return out;
}

/// A web address as a Markdown link destination: what would end it (spaces, parentheses, angle
/// brackets, control characters) percent-encoded; escapes already in the address stay as they are.
QString linkDestination(const QString &url)
{
    QString out;
    for (const QChar c : url) {
        if (c.unicode() <= 0x20 || c == u'(' || c == u')' || c == u'<' || c == u'>' || c.unicode() == 0x7f)
            out += u'%' + QString::number(c.unicode(), 16).toUpper().rightJustified(2, u'0');
        else
            out += c;
    }
    return out;
}

/// Convert recurrence patterns (recurrencePattern + recurrenceRange) to a rule.
struct RuleResult {
    std::optional<dates::RecurrenceRule> rule;
    bool unsupported = false;
    bool simplified = false;
};

std::optional<int> weekdayNumber(const QString &name)
{
    static const QStringList names{u"monday"_s, u"tuesday"_s,  u"wednesday"_s, u"thursday"_s,
                                   u"friday"_s, u"saturday"_s, u"sunday"_s};
    const qsizetype i = names.indexOf(name.trimmed().toLower());
    return i < 0 ? std::nullopt : std::optional<int>(int(i) + 1);
}

RuleResult recurrenceRule(const QJsonObject &recurrence)
{
    RuleResult out;
    const QJsonObject pattern = recurrence.value(u"pattern"_s).toObject();
    const QString type = pattern.value(u"type"_s).toString().trimmed().toLower();
    if (type.isEmpty())
        return out;
    dates::RecurrenceRule rule;
    rule.interval = std::clamp(pattern.value(u"interval"_s).toInt(1), 1, dates::MaxRecurrenceInterval);
    if (type == u"daily") {
        rule.freq = dates::RecurrenceRule::Freq::Day;
    } else if (type == u"weekly") {
        rule.freq = dates::RecurrenceRule::Freq::Week;
        for (const QJsonValue &d : pattern.value(u"daysOfWeek"_s).toArray()) {
            if (const auto n = weekdayNumber(d.toString()); n && !rule.weekdays.contains(*n))
                rule.weekdays.append(*n);
        }
        std::sort(rule.weekdays.begin(), rule.weekdays.end());
        if (rule.interval == 1 && rule.weekdays == QList<int>{1, 2, 3, 4, 5}) {
            // To Do's "Weekdays".
            rule.freq = dates::RecurrenceRule::Freq::Day;
            rule.weekdays.clear();
            rule.weekdaysOnly = true;
        }
    } else if (type == u"absolutemonthly") {
        rule.freq = dates::RecurrenceRule::Freq::Month;
        const int day = pattern.value(u"dayOfMonth"_s).toInt();
        if (day >= 1 && day <= 31)
            rule.monthDay = day;
    } else if (type == u"absoluteyearly") {
        rule.freq = dates::RecurrenceRule::Freq::Year; // on the due date's month and day
    } else {
        // relativeMonthly / relativeYearly ("the second Tuesday") and anything newer.
        out.unsupported = true;
        return out;
    }
    const QString range = recurrence.value(u"range"_s).toObject().value(u"type"_s).toString().trimmed().toLower();
    if (range == u"enddate" || range == u"numbered")
        out.simplified = true;
    out.rule = dates::normalizeRule(rule);
    return out;
}

/// One input file's lists: {name, list object (tasks inside)}.
struct SourceList {
    QString name;
    QJsonObject list;
    QJsonArray tasks;
    bool incomplete = false;
};

class MsTodoImporter final : public Importer {
public:
    QString id() const override { return u"mstodo"_s; }
    QString name() const override { return u"Microsoft To Do"_s; }
    QStringList fileTypes() const override { return {u"*.json"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        int checked = 0;
        for (const InputFile &f : files) {
            if (paths::extension(f.path) != u".json")
                continue;
            if (isMsTodoJson(QJsonDocument::fromJson(f.data).object())) {
                // Among notes (a vault with a stray export) the notes' importer wins.
                const bool notes = anyPath(files, [](const QString &p) {
                    const QString ext = paths::extension(p);
                    return paths::isMarkdown(p) || ext == u".txt" || ext == u".html" || ext == u".htm"
                           || ext == u".enex";
                });
                return notes ? 15 : 95;
            }
            if (++checked >= MaxDetectFiles)
                break;
        }
        return 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        State st(ctx);
        int emptyLists = 0;
        for (const InputFile &f : files) {
            if (paths::extension(f.path) != u".json")
                continue;
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(f.data, &error);
            if (error.error != QJsonParseError::NoError) {
                ctx.warn(IoMessages::tr("“%1” is not valid JSON and was skipped.").arg(f.path));
                continue;
            }
            const QJsonObject o = doc.object();
            if (!isMsTodoJson(o) && !o.value(u"lists"_s).isArray()) {
                ctx.warn(IoMessages::tr("“%1” is not a Microsoft To Do export and was skipped.").arg(f.path));
                continue;
            }
            for (const SourceList &list : listsOf(o, f.path)) {
                st.incomplete = st.incomplete || list.incomplete;
                if (!importList(list, st))
                    ++emptyLists;
            }
        }
        if (emptyLists > 0)
            ctx.warn(IoMessages::tr("%n empty list(s) were skipped.", nullptr, emptyLists));
        if (st.incomplete)
            ctx.warn(IoMessages::tr("The export is incomplete: Microsoft To Do sent more tasks than the file holds. "
                                    "Use “Sign in to Microsoft”, or export each list separately."));
        if (st.attachmentsMissing)
            ctx.warn(IoMessages::tr("Some tasks have attachments that are not in this file; sign in to Microsoft "
                                    "to import them."));
        if (st.attachmentsTooLarge)
            ctx.warn(IoMessages::tr("Attachments larger than 25 MB were left out."));
        if (st.attachmentsUnreadable)
            ctx.warn(IoMessages::tr("Some attachments could not be read and were left out."));
        if (st.malformedDate)
            ctx.warn(IoMessages::tr("Some dates could not be read and were left out."));
        if (st.unknownZone)
            ctx.warn(IoMessages::tr("Some time zones of the calendar are unknown; their times were kept as written."));
        if (st.droppedRecurrence)
            ctx.warn(
                    IoMessages::tr("Some recurrence rules have an end (COUNT/UNTIL) or details that are not supported; "
                                   "they were simplified."));
        if (st.unsupportedRecurrence)
            ctx.warn(
                    IoMessages::tr("Some recurrence rules are not supported; only the first occurrence was imported."));
        return ctx.take();
    }

private:
    struct State {
        explicit State(ImportContext &context) : ctx(context) { }
        ImportContext &ctx;
        bool incomplete = false;
        bool attachmentsMissing = false;
        bool attachmentsTooLarge = false;
        bool attachmentsUnreadable = false;
        bool malformedDate = false;
        bool unknownZone = false;
        bool droppedRecurrence = false;
        bool unsupportedRecurrence = false;
    };

    static QList<SourceList> listsOf(const QJsonObject &o, const QString &path)
    {
        QList<SourceList> out;
        const bool pagedTop = o.contains(u"@odata.nextLink"_s);
        const auto addList = [&](const QJsonObject &list) {
            SourceList s;
            s.name = list.value(u"displayName"_s).toString().simplified();
            s.list = list;
            s.tasks = list.value(u"tasks"_s).toArray();
            s.incomplete = list.contains(u"tasks@odata.nextLink"_s);
            out.append(s);
        };
        if (o.value(u"lists"_s).isArray()) {
            for (const QJsonValue &v : o.value(u"lists"_s).toArray()) {
                if (v.isObject())
                    addList(v.toObject());
            }
            return out;
        }
        const QJsonArray value = o.value(u"value"_s).toArray();
        const bool tasks = o.value(u"@odata.context"_s).toString().contains(u"/tasks"_s, Qt::CaseInsensitive)
                           || (!value.isEmpty() && isTaskObject(value.first().toObject()));
        if (tasks) {
            // One list's tasks: the page is named after the file.
            SourceList s;
            s.name = paths::basename(path).section(u'.', 0, -2).simplified();
            s.tasks = value;
            s.incomplete = pagedTop;
            out.append(s);
            return out;
        }
        for (const QJsonValue &v : value) {
            if (v.isObject())
                addList(v.toObject());
        }
        if (pagedTop && !out.isEmpty())
            out.last().incomplete = true;
        return out;
    }

    /// Adds the list's page; false when the list has no tasks.
    bool importList(const SourceList &list, State &st) const
    {
        struct Item {
            QString text;
            bool done = false;
            QDateTime completed;
        };
        QList<Item> items;
        for (const QJsonValue &v : list.tasks) {
            if (!v.isObject())
                continue;
            const QJsonObject task = v.toObject();
            Item item;
            item.done = task.value(u"status"_s).toString() == u"completed";
            if (item.done) {
                const QJsonObject c = task.value(u"completedDateTime"_s).toObject();
                if (const GraphTime t = parseGraphTime(c.value(u"dateTime"_s).toString(),
                                                       c.value(u"timeZone"_s).toString());
                    t.value) {
                    item.completed = QDateTime(t.value->date, t.value->time.value_or(QTime(0, 0)), QTimeZone::UTC);
                }
            }
            item.text = taskText(task, st);
            items.append(item);
        }
        if (items.isEmpty())
            return false;
        // Open tasks in the list's order, then the completed ones, the most recent first.
        std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            if (a.done != b.done)
                return !a.done;
            if (!a.done)
                return false;
            if (a.completed.isValid() != b.completed.isValid())
                return a.completed.isValid();
            return a.completed > b.completed;
        });
        QStringList texts;
        for (const Item &item : std::as_const(items))
            texts.append(item.text);
        const QString name = list.name.isEmpty() ? this->name() : list.name;
        QVariantMap fm;
        fm.insert(u"source"_s, this->name());
        addTitleAlias(fm, name);
        st.ctx.addPage(st.ctx.claimPage(pageSegment(name)),
                       noteWithFrontmatter(fm, {u"aliases"_s, u"source"_s}, texts.join(u'\n') + u'\n'));
        return true;
    }

    /// Wall-clock date-time of a dateTimeTimeZone (or an ISO string) in the vault's zone.
    /// `dateOnly`: a field To Do keeps as a day (due, start).
    static std::optional<DateSpec> wallClock(const QJsonValue &v, State &st, bool dateOnly = false)
    {
        GraphTime t;
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            t = parseGraphTime(o.value(u"dateTime"_s).toString(), o.value(u"timeZone"_s).toString());
        } else if (v.isString()) {
            t = parseGraphTime(v.toString(), {});
        } else if (!v.isUndefined() && !v.isNull()) {
            t.malformed = true;
        }
        st.malformedDate = st.malformedDate || t.malformed;
        if (!t.value)
            return std::nullopt;
        bool unknown = false;
        DateSpec spec = ical::toWallClock(*t.value, st.ctx.options().timeZone, &unknown);
        st.unknownZone = st.unknownZone || unknown;
        // A date-only field (due, start) saved as midnight UTC — To Do does so for dates without
        // a zone — names that day, whatever the vault's zone; midnight in the user's zone sent as
        // UTC ("2026-10-26T22:00:00" for 27 October in Warsaw) is converted to that day.
        if (dateOnly && spec.time && *spec.time != QTime(0, 0) && t.value->time == QTime(0, 0))
            spec.date = t.value->date;
        return spec;
    }

    /// `lineStart`: the text begins the item's content, where block syntax would be read.
    static QString singleLine(const QString &text, bool lineStart = false)
    {
        return escapeMarkdownText(QString(text).replace(u'\n', u' ').simplified(), lineStart);
    }

    static QString isoDate(const DateSpec &spec) { return spec.date.toString(u"yyyy-MM-dd"_s); }

    static QString taskText(const QJsonObject &task, State &st)
    {
        const QString status = task.value(u"status"_s).toString();
        const bool done = status == u"completed";
        QChar box = u' ';
        if (done)
            box = u'x';
        else if (status == u"inProgress")
            box = u'/';
        else if (status == u"waitingOnOthers" || status == u"deferred")
            box = u'>';
        QString title = singleLine(task.value(u"title"_s).toString());
        if (title.isEmpty())
            title = IoMessages::tr("(no title)");
        QString line = u"- ["_s + box + u"] "_s + title;
        for (const QJsonValue &c : task.value(u"categories"_s).toArray()) {
            const QString tag = tagName(c.toString());
            if (!tag.isEmpty())
                line += u" #"_s + tag;
        }
        const QString importance = task.value(u"importance"_s).toString();
        if (importance == u"high")
            line += u' ' + markdown::tokens::PriorityHigh.toString();
        else if (importance == u"low")
            line += u' ' + markdown::tokens::PriorityLow.toString();

        const auto field = [&](QStringView key) { return task.value(key.toString()); };
        if (const auto created = wallClock(field(u"createdDateTime"), st))
            line += u' ' + markdown::tokens::Created.toString() + u' ' + isoDate(*created);
        if (const auto start = wallClock(field(u"startDateTime"), st, true))
            line += u' ' + markdown::tokens::Start.toString() + u' ' + isoDate(*start);
        std::optional<DateSpec> due = wallClock(field(u"dueDateTime"), st, true);
        const QJsonObject recurrence = task.value(u"recurrence"_s).toObject();
        if (!due && !recurrence.isEmpty()) {
            // A series without a due date: its first day.
            const QDate first = QDate::fromString(
                    recurrence.value(u"range"_s).toObject().value(u"startDate"_s).toString(), u"yyyy-MM-dd"_s);
            if (first.isValid() && first.year() >= 1900)
                due = DateSpec{first, std::nullopt};
        }
        if (due)
            line += u' ' + markdown::tokens::Due.toString() + u' ' + isoDate(*due);
        if (!recurrence.isEmpty()) {
            const RuleResult r = recurrenceRule(recurrence);
            st.unsupportedRecurrence = st.unsupportedRecurrence || r.unsupported;
            st.droppedRecurrence = st.droppedRecurrence || r.simplified;
            if (r.rule)
                line += u' ' + markdown::tokens::Recurrence.toString() + u' '
                        + dates::formatRecurrence(*r.rule, dates::UiLocale::English);
        }
        if (!done && task.value(u"isReminderOn"_s).toBool()) {
            if (const auto at = wallClock(field(u"reminderDateTime"), st); at && at->time)
                line += u' ' + markdown::formatReminderToken(*at);
        }
        if (done) {
            if (const auto completed = wallClock(field(u"completedDateTime"), st)) {
                // Completion is an instant: its day in the vault's zone.
                line += u' ' + markdown::tokens::Done.toString() + u' ' + isoDate(*completed);
            }
        }
        return withDetails(line, details(task, st));
    }

    /// Lines under the task: its note, then steps, links and attachments as sub-items.
    static QStringList details(const QJsonObject &task, State &st)
    {
        QStringList out;
        const QJsonObject body = task.value(u"body"_s).toObject();
        const QString content = body.value(u"content"_s).toString();
        if (!content.trimmed().isEmpty()) {
            if (body.value(u"contentType"_s).toString().compare(u"html"_s, Qt::CaseInsensitive) == 0) {
                for (const QString &l : normalizeNewlines(htmlToMarkdown(content).markdown).split(u'\n'))
                    out.append(l.trimmed().isEmpty() ? QString() : l);
            } else {
                for (const QString &l : normalizeNewlines(content).split(u'\n'))
                    out.append(l.trimmed().isEmpty() ? QString() : escapeMarkdownText(l.trimmed(), true));
            }
            while (!out.isEmpty() && out.first().isEmpty())
                out.removeFirst();
        }
        for (const QJsonValue &v : task.value(u"checklistItems"_s).toArray()) {
            const QJsonObject step = v.toObject();
            const QString text = singleLine(step.value(u"displayName"_s).toString());
            if (text.isEmpty())
                continue;
            out.append((step.value(u"isChecked"_s).toBool() ? u"- [x] "_s : u"- [ ] "_s) + text);
        }
        for (const QJsonValue &v : task.value(u"linkedResources"_s).toArray()) {
            const QJsonObject link = v.toObject();
            const QString url = link.value(u"webUrl"_s).toString().trimmed();
            const QString label = link.value(u"displayName"_s).toString().simplified();
            const QString scheme = QUrl(url).scheme().toLower();
            if (url.isEmpty() || (scheme != u"http" && scheme != u"https" && scheme != u"mailto")) {
                // Without a usable address only its name is kept (never a link to another scheme).
                if (!label.isEmpty())
                    out.append(u"- "_s + singleLine(label));
                continue;
            }
            if (label.isEmpty() || label == url)
                out.append(u"- <"_s + linkDestination(url) + u'>');
            else
                out.append(u"- ["_s + escapeMarkdownText(label, false, true) + u"]("_s + linkDestination(url) + u')');
        }
        const QJsonValue attachments = task.value(u"attachments"_s);
        if (attachments.isArray()) {
            for (const QJsonValue &v : attachments.toArray()) {
                const QJsonObject a = v.toObject();
                if (!a.contains(u"contentBytes"_s)) {
                    st.attachmentsMissing = true;
                    continue;
                }
                const QByteArray base64 = a.value(u"contentBytes"_s).toString().toLatin1();
                if (base64.size() > MaxAttachmentBytes / 3 * 4 + 4 || a.value(u"size"_s).toDouble() > MaxAttachmentBytes) {
                    st.attachmentsTooLarge = true;
                    continue;
                }
                const auto decoded = QByteArray::fromBase64Encoding(base64, QByteArray::AbortOnBase64DecodingErrors);
                if (!decoded || base64.isEmpty()) {
                    st.attachmentsUnreadable = true;
                    continue;
                }
                QString fileName = paths::basename(a.value(u"name"_s).toString().trimmed());
                if (fileName.isEmpty())
                    fileName = u"attachment"_s + extensionForMime(a.value(u"contentType"_s).toString());
                out.append(u"- "_s + attachmentLink(st.ctx.addAttachment(fileName, *decoded)));
            }
        } else if (task.value(u"hasAttachments"_s).toBool()) {
            st.attachmentsMissing = true;
        }
        return out;
    }

    /// The item: its first line and the indented lines below it; dollars that would pair into a
    /// formula across them are escaped.
    static QString withDetails(QString first, const QStringList &detailLines)
    {
        for (const QString &line : detailLines)
            first += line.isEmpty() ? u"\n"_s : u"\n  "_s + line;
        return escapeTextFormulas(first);
    }
};

} // namespace

std::unique_ptr<Importer> makeMsTodoImporter()
{
    return std::make_unique<MsTodoImporter>();
}

} // namespace brulion::io::detail

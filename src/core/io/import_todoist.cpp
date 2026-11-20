// Todoist importer (docs/IMPORT.md "Todoist"): every project becomes a page of tasks in the
// Obsidian Tasks syntax (docs/SYNTAX.md §5), sections its headings, subtasks indented items,
// descriptions and comments lines under a task. Accepted:
//   * the CSV template of a project (Todoist: the project's ••• → "Export as a template", also
//     each file of a backup ZIP): TYPE (task | section | note | meta), CONTENT, DESCRIPTION,
//     PRIORITY (1 = p1, the highest), INDENT (1 = a top task), AUTHOR, RESPONSIBLE, DATE (Todoist's
//     own words: "every monday", "jutro o 9", "Nov 25"), DATE_LANG, TIMEZONE, DURATION,
//     DURATION_UNIT, DEADLINE, DEADLINE_LANG; labels are "@label" in CONTENT;
//   * the JSON of the Sync API (`POST https://api.todoist.com/api/v1/sync` with sync_token=* —
//     what Brulion's download writes, marked {"brulion": "todoist-export"}): projects, sections,
//     items (priority 4 = p1), labels, notes (task comments, with file_attachment), project_notes,
//     reminders; downloaded comment files are in the input (csvAttachmentInputPath()).
// Due dates: a date or a date-time (floating, or UTC with a zone: converted to the wall-clock
// zone); recurring strings ("every 2 weeks on monday", "co tydzień", "every!" = from completion)
// become 🔁 rules where parseRecurrence() understands them; a deadline with a due date makes the
// due date ⏳ (scheduled) and the deadline 📅.
#include "dates/natural.h"
#include "dates/recurrence.h"
#include "io/csvimport_p.h"
#include "io/htmlutil_p.h"
#include "io/importers_p.h"
#include "io/importutil_p.h"
#include "io/messages_p.h"
#include "markdown/tokens.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

/// Deepest subtask level kept (deeper ones are put at this level).
constexpr int MaxDepth = 12;

bool isTodoistTemplateHeader(const QStringList &columns)
{
    return columns.size() >= 2 && columns[0].compare(u"TYPE"_s, Qt::CaseInsensitive) == 0
           && columns[1].compare(u"CONTENT"_s, Qt::CaseInsensitive) == 0;
}

bool isSyncJson(const QJsonObject &o)
{
    if (o.value(u"brulion"_s).toString() == u"todoist-export")
        return true;
    return o.value(u"items"_s).isArray() && o.value(u"projects"_s).isArray()
           && (o.contains(u"sync_token"_s) || o.contains(u"full_sync"_s));
}

/// A task as the page writes it.
struct Task {
    QString id;
    QString parentId;
    QString sectionId;
    QString content;
    QStringList description;
    QStringList labels;
    int priority = 0; ///< 1 = p1 (highest) … 4 = p4 (none)
    std::optional<DateSpec> due;
    std::optional<DateSpec> deadline;
    QString rule; ///< canonical 🔁 text
    std::optional<DateSpec> reminder;
    bool done = false;
    QDate completed;
    QStringList comments; ///< lines (Markdown)
    int order = 0;
    int depth = 0;    ///< CSV: INDENT - 1
    int section = -1; ///< CSV: the index of its section
};

struct Section {
    QString id;
    QString name;
    int order = 0;
};

struct Project {
    QString id;
    QString name;
    QString parentId;
    int order = 0;
    QStringList notes; ///< project comments, the template's notes before the first task
    QList<Section> sections;
    QList<Task> tasks;
};

/// Shared state of one run.
struct State {
    explicit State(ImportContext &c) : ctx(c) { }
    ImportContext &ctx;
    int unreadDates = 0;
    int unsupportedRules = 0;
    bool durations = false;
    bool assignments = false;
    bool relativeWithoutNow = false;
    int missingFiles = 0;
};

/// Todoist's text on one line: line breaks as spaces, characters that mean something else in
/// Brulion escaped (a "#1" would be a tag, "[[" a link); Todoist's own Markdown (links, bold) stays.
QString taskText(const QString &content)
{
    QString s = content.simplified();
    static const QRegularExpression hash(u"(^|[\\s(])#(?=[\\p{L}\\p{N}_])"_s);
    s.replace(hash, u"\\1\\#"_s);
    s.replace(u"[["_s, u"\\[\\["_s);
    s.replace(u"(("_s, u"\\(\\("_s);
    s.replace(u"%%"_s, u"\\%\\%"_s);
    s.replace(u"=="_s, u"\\=\\="_s);
    // A leading checkbox or list marker of the text itself.
    if (s.startsWith(u'[') && s.size() > 2 && s.at(2) == u']')
        s.prepend(u'\\');
    return s;
}

QString descriptionLine(const QString &line)
{
    QString s = line;
    s.replace(u"[["_s, u"\\[\\["_s);
    return s;
}

/// "every monday at 9am" -> {"every monday", 09:00}; "co tydzień o 18" -> {"co tydzień", 18:00}.
std::pair<QString, std::optional<QTime>> splitRuleTime(const QString &text)
{
    static const QRegularExpression at(
            u"\\s+(?:at|@|o|o godz\\.?|godz\\.?)\\s+(\\d{1,2})(?:[:.](\\d{2}))?\\s*([ap]\\.?m\\.?)?\\s*$"_s,
            QRegularExpression::CaseInsensitiveOption);
    const auto m = at.match(text);
    if (!m.hasMatch())
        return {text.trimmed(), std::nullopt};
    int hour = m.captured(1).toInt();
    const int minute = m.captured(2).toInt();
    if (const QString ampm = m.captured(3).toLower(); !ampm.isEmpty())
        hour = (hour % 12) + (ampm.startsWith(u'p') ? 12 : 0);
    const QTime t(hour, minute);
    return {text.first(m.capturedStart()).trimmed(), t.isValid() ? std::optional<QTime>(t) : std::nullopt};
}

/// A recurring due string as a rule (canonical English), or "" when not understood.
QString ruleFrom(const QString &dueString, std::optional<QTime> *time)
{
    QString s = dueString.trimmed();
    bool whenDone = false;
    // Todoist: "every!" / "every! 3 days" counts from the completion; "after 3 days" too.
    static const QRegularExpression bang(u"^(every|ev|co|każd[ya])!\\s*"_s, QRegularExpression::CaseInsensitiveOption);
    if (const auto m = bang.match(s); m.hasMatch()) {
        whenDone = true;
        s = m.captured(1) + u' ' + s.sliced(m.capturedEnd());
    }
    static const QRegularExpression after(u"^after\\s+"_s, QRegularExpression::CaseInsensitiveOption);
    if (after.match(s).hasMatch()) {
        whenDone = true;
        s.replace(after, u"every "_s);
    }
    static const QRegularExpression ev(u"^ev\\s+"_s, QRegularExpression::CaseInsensitiveOption);
    s.replace(ev, u"every "_s);
    // "every day starting jan 3", "every monday until dec 31" — the range is not kept.
    static const QRegularExpression range(u"\\s+(?:starting|from|until|ending|for)\\s+.*$"_s,
                                          QRegularExpression::CaseInsensitiveOption);
    s.remove(range);
    auto [rule, t] = splitRuleTime(s);
    *time = t;
    auto parsed = dates::parseRecurrence(rule);
    if (!parsed)
        return {};
    parsed->whenDone = parsed->whenDone || whenDone;
    return dates::formatRecurrence(*parsed, dates::UiLocale::English);
}

/// The first day of a rule from `today` on (a weekday rule's first such day, a month day's next).
QDate firstOccurrence(const QString &canonical, QDate today)
{
    const auto rule = dates::parseRecurrence(canonical);
    if (!rule || !today.isValid())
        return today;
    if (rule->weekdays.isEmpty() && !rule->monthDay && !rule->lastDayOfMonth && !rule->weekdaysOnly)
        return today;
    const auto next = dates::nextOccurrence(*rule, DateSpec{today.addDays(-1), std::nullopt});
    return next ? next->date : today;
}

/// Todoist's due object of the Sync API: {date: "2026-11-25" | "2026-11-25T09:00:00" (floating)
/// | "2026-11-25T08:00:00Z" (fixed, with timezone), string, lang, is_recurring}.
void applyDue(const QJsonObject &due, Task &task, State &st, bool deadline = false)
{
    const QString date = due.value(u"date"_s).toString().trimmed();
    if (date.isEmpty())
        return;
    static const QRegularExpression re(
            u"^(\\d{4})-(\\d{2})-(\\d{2})(?:T(\\d{2}):(\\d{2})(?::(\\d{2}))?(?:\\.\\d+)?(Z)?)?$"_s);
    const auto m = re.match(date);
    if (!m.hasMatch()) {
        ++st.unreadDates;
        return;
    }
    DateSpec spec{QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()), std::nullopt};
    if (!spec.date.isValid()) {
        ++st.unreadDates;
        return;
    }
    if (m.capturedLength(4) > 0) {
        QTime t(m.captured(4).toInt(), m.captured(5).toInt());
        if (!t.isValid()) {
            ++st.unreadDates;
            return;
        }
        if (!m.captured(7).isEmpty()) {
            const QDateTime local = QDateTime(spec.date, t, QTimeZone::UTC).toTimeZone(st.ctx.options().timeZone);
            spec.date = local.date();
            t = QTime(local.time().hour(), local.time().minute());
        }
        spec.time = t;
    }
    if (deadline) {
        task.deadline = spec;
        return;
    }
    task.due = spec;
    if (due.value(u"is_recurring"_s).toBool()) {
        std::optional<QTime> ignored;
        task.rule = ruleFrom(due.value(u"string"_s).toString(), &ignored);
        if (task.rule.isEmpty())
            ++st.unsupportedRules;
    }
}

/// A template's DATE: Todoist's words in DATE_LANG ("every monday", "tomorrow at 9", "jutro",
/// "Nov 25", "25 lis 2026", "2026-11-25").
void applyTemplateDate(const QString &text, const QString &lang, Task &task, State &st, bool deadline = false)
{
    const QString s = text.trimmed();
    if (s.isEmpty())
        return;
    const QDateTime now = st.ctx.options().now;
    const dates::UiLocale locale =
            lang.startsWith(u"pl"_s, Qt::CaseInsensitive) ? dates::UiLocale::Polish : dates::UiLocale::English;
    std::optional<QTime> ruleTime;
    const QString rule = deadline ? QString() : ruleFrom(s, &ruleTime);
    if (!rule.isEmpty()) {
        task.rule = rule;
        if (now.isValid())
            task.due = DateSpec{firstOccurrence(rule, now.date()), ruleTime};
        else
            st.relativeWithoutNow = true;
        return;
    }
    // An exact date first (no "now" needed), then Todoist's words relative to now.
    if (const auto d =
                parseCsvDate(s, locale == dates::UiLocale::English ? DateOrder::MonthFirst : DateOrder::DayFirst)) {
        DateSpec spec{d->date,
                      d->time ? std::optional<QTime>(QTime(d->time->hour(), d->time->minute())) : std::nullopt};
        (deadline ? task.deadline : task.due) = spec;
        return;
    }
    if (!now.isValid()) {
        st.relativeWithoutNow = true;
        return;
    }
    const auto found = dates::parseNaturalDate(s, now, Qt::Monday, locale);
    if (!found || found->length < s.size() / 2) {
        // "every …" rules Brulion does not write ("every 2nd tuesday"): only counted.
        static const QRegularExpression recurring(u"^(every|ev|co|każd|after)\\b"_s,
                                                  QRegularExpression::CaseInsensitiveOption);
        if (recurring.match(s).hasMatch())
            ++st.unsupportedRules;
        else
            ++st.unreadDates;
        return;
    }
    (deadline ? task.deadline : task.due) = found->spec;
}

QString dateToken(QStringView token, const DateSpec &spec)
{
    QString out = u' ' + token + u' ' + spec.date.toString(u"yyyy-MM-dd"_s);
    if (spec.time)
        out += u' ' + spec.time->toString(u"HH:mm"_s);
    return out;
}

/// The item's first line.
QString taskLine(const Task &t)
{
    QString line = u"- ["_s + (t.done ? u'x' : u' ') + u"] "_s;
    QString text = taskText(t.content);
    if (text.isEmpty())
        text = IoMessages::tr("(no title)");
    line += text;
    for (const QString &label : t.labels) {
        const QString tag = tagName(label);
        if (!tag.isEmpty())
            line += u" #"_s + tag;
    }
    switch (t.priority) {
    case 1: line += u' ' + markdown::tokens::PriorityHighest.toString(); break;
    case 2: line += u' ' + markdown::tokens::PriorityHigh.toString(); break;
    case 3: line += u' ' + markdown::tokens::PriorityMedium.toString(); break;
    default: break;
    }
    if (t.due && t.deadline) {
        line += dateToken(markdown::tokens::Scheduled, *t.due);
        line += dateToken(markdown::tokens::Due, *t.deadline);
    } else if (t.due) {
        line += dateToken(markdown::tokens::Due, *t.due);
    } else if (t.deadline) {
        line += dateToken(markdown::tokens::Due, *t.deadline);
    }
    if (!t.rule.isEmpty())
        line += u' ' + markdown::tokens::Recurrence.toString() + u' ' + t.rule;
    if (t.reminder && t.reminder->time && !t.done)
        line += u' ' + markdown::formatReminderToken(*t.reminder);
    if (t.done && t.completed.isValid())
        line += u' ' + markdown::tokens::Done.toString() + u' ' + t.completed.toString(u"yyyy-MM-dd"_s);
    return line;
}

/// The project's page: its notes, the tasks without a section, then each section's.
QString projectBody(const Project &p)
{
    QStringList out;
    for (const QString &n : p.notes)
        out.append(n);
    if (!p.notes.isEmpty())
        out.append(QString());
    // Children by parent; roots by section.
    QHash<QString, QList<const Task *>> children;
    QHash<QString, bool> known;
    for (const Task &t : p.tasks)
        known.insert(t.id, true);
    QHash<QString, QList<const Task *>> roots; // section id -> tasks
    for (const Task &t : p.tasks) {
        if (!t.parentId.isEmpty() && known.contains(t.parentId) && t.parentId != t.id)
            children[t.parentId].append(&t);
        else
            roots[t.sectionId].append(&t);
    }
    const auto byOrder = [](const Task *a, const Task *b) { return a->order < b->order; };
    for (auto &list : children)
        std::ranges::stable_sort(list, byOrder);
    for (auto &list : roots)
        std::ranges::stable_sort(list, byOrder);
    QSet<QString> written;
    const std::function<void(const Task *, int)> write = [&](const Task *t, int depth) {
        if (written.contains(t->id))
            return; // a cycle of parents
        written.insert(t->id);
        const QString indent(std::min(depth, MaxDepth) * 2, u' ');
        out.append(indent + taskLine(*t));
        for (const QString &d : t->description)
            out.append(d.trimmed().isEmpty() ? QString() : indent + u"  "_s + descriptionLine(d));
        for (const QString &c : t->comments)
            out.append(indent + u"  - "_s + c);
        for (const Task *child : children.value(t->id))
            write(child, depth + 1);
    };
    for (const Task *t : roots.value(QString()))
        write(t, 0);
    QList<Section> sections = p.sections;
    std::ranges::stable_sort(sections, [](const Section &a, const Section &b) { return a.order < b.order; });
    for (const Section &s : std::as_const(sections)) {
        if (!out.isEmpty() && !out.last().isEmpty())
            out.append(QString());
        out.append(u"## "_s + escapeMarkdownText(s.name.simplified(), false));
        out.append(QString());
        for (const Task *t : roots.value(s.id))
            write(t, 0);
    }
    // Tasks whose section is unknown (deleted, another project's).
    for (auto it = roots.constBegin(); it != roots.constEnd(); ++it) {
        if (it.key().isEmpty() || std::ranges::any_of(sections, [&](const Section &s) { return s.id == it.key(); }))
            continue;
        for (const Task *t : it.value())
            write(t, 0);
    }
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    // Descriptions keep blank lines inside an item; runs of blank lines are one.
    QStringList clean;
    for (const QString &l : std::as_const(out)) {
        if (l.isEmpty() && !clean.isEmpty() && clean.last().isEmpty())
            continue;
        clean.append(l);
    }
    return escapeTextFormulas(clean.join(u'\n'));
}

/// "Inbox [2203306141]" / "Inbox_2203306141" -> "Inbox" (backups add the project's id).
QString projectNameFromFile(const QString &path)
{
    QString stem = paths::basename(path);
    if (const qsizetype dot = stem.lastIndexOf(u'.'); dot > 0)
        stem.truncate(dot);
    static const QRegularExpression id(u"\\s*(?:\\[\\d{6,}\\]|_\\d{6,})$"_s);
    stem.remove(id);
    return stem.trimmed().isEmpty() ? u"Todoist"_s : stem.trimmed();
}

class TodoistImporter final : public Importer {
public:
    QString id() const override { return u"todoist"_s; }
    QString name() const override { return u"Todoist"_s; }
    QStringList fileTypes() const override { return {u"*.csv"_s, u"*.json"_s, u"*.zip"_s}; }

    int detect(const QList<InputFile> &files) const override
    {
        int checked = 0;
        for (const InputFile &f : files) {
            if (f.path.startsWith(u"_attachments/"_s))
                continue;
            const QString ext = paths::extension(f.path);
            if (ext == u".csv") {
                const QByteArray head = f.data.left(300);
                QString text = decodeText(head);
                if (text.startsWith(QChar(0xFEFF)))
                    text.remove(0, 1);
                if (text.startsWith(u"TYPE,CONTENT"_s, Qt::CaseInsensitive)
                    || text.startsWith(u"\"TYPE\",\"CONTENT\""_s, Qt::CaseInsensitive)
                    || text.startsWith(u"TYPE;CONTENT"_s, Qt::CaseInsensitive))
                    return 95;
            } else if (ext == u".json" && (f.data.contains("todoist-export") || f.data.contains("\"sync_token\""))) {
                if (isSyncJson(QJsonDocument::fromJson(f.data).object()))
                    return 95;
            }
            if (++checked > 40)
                break;
        }
        return 0;
    }

    ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const override
    {
        ImportContext ctx(options, name());
        State st(ctx);
        QList<Project> projects;
        QHash<QString, const InputFile *> inputs;
        for (const InputFile &f : files) {
            if (f.path.startsWith(u"_attachments/"_s))
                inputs.insert(f.path, &f);
        }
        for (const InputFile &f : files) {
            if (f.path.startsWith(u"_attachments/"_s))
                continue;
            const QString ext = paths::extension(f.path);
            if (ext == u".csv") {
                CsvImportOptions csv;
                csv.header = true;
                CsvData data = readCsv(f, csv);
                if (!isTodoistTemplateHeader(data.columns)) {
                    ctx.warn(IoMessages::tr("“%1” is not a Todoist template and was skipped.").arg(f.path));
                    continue;
                }
                projects.append(fromTemplate(data, st));
            } else if (ext == u".json") {
                QJsonParseError error{};
                const QJsonObject o = QJsonDocument::fromJson(f.data, &error).object();
                if (error.error != QJsonParseError::NoError || !isSyncJson(o)) {
                    ctx.warn(IoMessages::tr("“%1” is not a Todoist export and was skipped.").arg(f.path));
                    continue;
                }
                projects += fromSync(o, st, inputs);
            }
        }
        writePages(projects, st);
        if (st.unreadDates > 0)
            ctx.warn(IoMessages::tr("%n due date(s) could not be read and were left out.", nullptr, st.unreadDates));
        if (st.unsupportedRules > 0)
            ctx.warn(IoMessages::tr("%n recurring date(s) cannot be written as a 🔁 rule; only their next date was "
                                    "kept.",
                                    nullptr, st.unsupportedRules));
        if (st.relativeWithoutNow)
            ctx.warn(IoMessages::tr("Relative dates (“tomorrow”, “every monday”) could not be resolved."));
        if (st.durations)
            ctx.warn(IoMessages::tr("Task durations are not imported."));
        if (st.assignments)
            ctx.warn(IoMessages::tr("Assignments to people are not imported."));
        if (st.missingFiles > 0)
            ctx.warn(IoMessages::tr("%n comment file(s) are not in the import and were linked to Todoist.", nullptr,
                                    st.missingFiles));
        if (projects.isEmpty())
            ctx.warn(IoMessages::tr("No Todoist project was found."));
        return ctx.take();
    }

private:
    static Project fromTemplate(const CsvData &data, State &st)
    {
        Project p;
        p.name = projectNameFromFile(data.path);
        const auto col = [&data](QStringView name) {
            for (int c = 0; c < data.columns.size(); ++c) {
                if (data.columns[c].compare(name, Qt::CaseInsensitive) == 0)
                    return c;
            }
            return -1;
        };
        const int type = col(u"TYPE"), content = col(u"CONTENT"), description = col(u"DESCRIPTION"),
                  priority = col(u"PRIORITY"), indent = col(u"INDENT"), date = col(u"DATE"),
                  dateLang = col(u"DATE_LANG"), deadline = col(u"DEADLINE"), deadlineLang = col(u"DEADLINE_LANG"),
                  duration = col(u"DURATION"), responsible = col(u"RESPONSIBLE");
        const auto cell = [](const QStringList &row, int c) { return c >= 0 ? row.value(c) : QString(); };
        // Parents by depth: the last task at each level.
        QList<QString> stack;
        QString currentSection;
        int order = 0;
        for (const QStringList &row : data.rows) {
            const QString kind = cell(row, type).trimmed().toLower();
            if (kind == u"section") {
                Section s;
                s.id = u"s%1"_s.arg(p.sections.size());
                s.name = cell(row, content).simplified();
                s.order = int(p.sections.size());
                p.sections.append(s);
                currentSection = s.id;
                stack.clear();
                continue;
            }
            if (kind == u"note") {
                const QString text = normalizeNewlines(cell(row, content)).simplified();
                if (text.isEmpty())
                    continue;
                if (!p.tasks.isEmpty() && !stack.isEmpty())
                    p.tasks.last().comments.append(taskText(text));
                else
                    p.notes.append(escapeMarkdownText(text, true));
                continue;
            }
            if (kind != u"task")
                continue; // "meta" (view_style) and anything newer
            Task t;
            t.id = u"t%1"_s.arg(p.tasks.size());
            t.order = order++;
            t.sectionId = currentSection;
            QString text = cell(row, content);
            // Labels are written into the content: "Kupić mleko @zakupy @dom".
            static const QRegularExpression label(u"(?:^|\\s)@([\\p{L}\\p{N}_\\-/]+)"_s);
            for (auto it = label.globalMatch(text); it.hasNext();)
                t.labels.append(it.next().captured(1));
            text.remove(label);
            t.content = text.simplified();
            for (const QString &l : normalizeNewlines(cell(row, description)).split(u'\n')) {
                if (!l.trimmed().isEmpty() || !t.description.isEmpty())
                    t.description.append(l);
            }
            const int prio = cell(row, priority).trimmed().toInt();
            t.priority = prio >= 1 && prio <= 4 ? prio : 4;
            int level = std::clamp(cell(row, indent).trimmed().toInt(), 1, MaxDepth + 1) - 1;
            level = std::min<int>(level, int(stack.size()));
            stack.resize(level);
            t.parentId = level > 0 ? stack.value(level - 1) : QString();
            stack.append(t.id);
            applyTemplateDate(cell(row, date), cell(row, dateLang), t, st);
            applyTemplateDate(cell(row, deadline), cell(row, deadlineLang), t, st, true);
            st.durations = st.durations || !cell(row, duration).trimmed().isEmpty();
            st.assignments = st.assignments || !cell(row, responsible).trimmed().isEmpty();
            p.tasks.append(t);
        }
        return p;
    }

    static QList<Project> fromSync(const QJsonObject &o, State &st, const QHash<QString, const InputFile *> &inputs)
    {
        const auto idOf = [](const QJsonValue &v) {
            return v.isDouble() ? QString::number(qint64(v.toDouble())) : v.toString();
        };
        const auto alive = [](const QJsonObject &x) {
            return !x.value(u"is_deleted"_s).toBool() && x.value(u"is_deleted"_s).toInt() == 0;
        };
        QList<Project> projects;
        QHash<QString, qsizetype> byId;
        for (const auto &v : o.value(u"projects"_s).toArray()) {
            const QJsonObject x = v.toObject();
            if (!alive(x))
                continue;
            Project p;
            p.id = idOf(x.value(u"id"_s));
            p.name = x.value(u"name"_s).toString().simplified();
            if (p.name.isEmpty())
                p.name = u"Todoist"_s;
            p.parentId = idOf(x.value(u"parent_id"_s));
            p.order = x.value(u"child_order"_s).toInt();
            const QString description = normalizeNewlines(x.value(u"description"_s).toString());
            for (const QString &l : description.split(u'\n')) {
                if (!l.trimmed().isEmpty())
                    p.notes.append(descriptionLine(l));
            }
            byId.insert(p.id, projects.size());
            projects.append(p);
        }
        for (const auto &v : o.value(u"sections"_s).toArray()) {
            const QJsonObject x = v.toObject();
            const auto it = byId.constFind(idOf(x.value(u"project_id"_s)));
            if (!alive(x) || it == byId.constEnd())
                continue;
            projects[*it].sections.append(Section{idOf(x.value(u"id"_s)), x.value(u"name"_s).toString(),
                                                  x.value(u"section_order"_s).toInt()});
        }
        // Comments (with their files) by task / project.
        const auto commentLines = [&](const QJsonObject &n) {
            QStringList lines;
            QString text = normalizeNewlines(n.value(u"content"_s).toString()).simplified();
            if (!text.isEmpty())
                lines.append(taskText(text));
            const QJsonObject file = n.value(u"file_attachment"_s).toObject();
            const QString url = file.value(u"file_url"_s).toString().trimmed();
            if (!url.isEmpty()) {
                const QString name = file.value(u"file_name"_s).toString().trimmed();
                const InputFile *f = inputs.value(csvAttachmentInputPath(url, name));
                if (f) {
                    lines.append(attachmentLink(st.ctx.addAttachment(paths::basename(f->path), f->data)));
                } else if (url.startsWith(u"https://"_s)) {
                    ++st.missingFiles;
                    lines.append(u"["_s + escapeMarkdownText(name.isEmpty() ? url : name, false, true) + u"]("_s
                                 + QString(url).replace(u' ', u"%20"_s).replace(u'(', u"%28"_s).replace(u')', u"%29"_s)
                                 + u')');
                }
            }
            return lines.join(u' ');
        };
        QHash<QString, QStringList> taskComments;
        for (const auto &v : o.value(u"notes"_s).toArray()) {
            const QJsonObject n = v.toObject();
            if (!alive(n))
                continue;
            const QString line = commentLines(n);
            if (!line.isEmpty())
                taskComments[idOf(n.value(u"item_id"_s))].append(line);
        }
        for (const auto &v : o.value(u"project_notes"_s).toArray()) {
            const QJsonObject n = v.toObject();
            const auto it = byId.constFind(idOf(n.value(u"project_id"_s)));
            if (!alive(n) || it == byId.constEnd())
                continue;
            if (const QString line = commentLines(n); !line.isEmpty())
                projects[*it].notes.append(u"- "_s + line);
        }
        QHash<QString, QJsonObject> reminders; // the first absolute / relative reminder of a task
        for (const auto &v : o.value(u"reminders"_s).toArray()) {
            const QJsonObject r = v.toObject();
            const QString item = idOf(r.value(u"item_id"_s));
            if (alive(r) && !reminders.contains(item) && r.value(u"type"_s).toString() != u"location")
                reminders.insert(item, r);
        }
        for (const auto &v : o.value(u"items"_s).toArray()) {
            const QJsonObject x = v.toObject();
            const auto it = byId.constFind(idOf(x.value(u"project_id"_s)));
            if (!alive(x) || it == byId.constEnd())
                continue;
            Task t;
            t.id = idOf(x.value(u"id"_s));
            t.parentId = idOf(x.value(u"parent_id"_s));
            t.sectionId = idOf(x.value(u"section_id"_s));
            t.content = x.value(u"content"_s).toString();
            for (const QString &l : normalizeNewlines(x.value(u"description"_s).toString()).split(u'\n')) {
                if (!l.trimmed().isEmpty() || !t.description.isEmpty())
                    t.description.append(l);
            }
            for (const auto &l : x.value(u"labels"_s).toArray())
                t.labels.append(l.toString());
            // The API counts the other way: 4 = p1.
            const int prio = x.value(u"priority"_s).toInt(1);
            t.priority = prio >= 1 && prio <= 4 ? 5 - prio : 4;
            t.order = x.value(u"child_order"_s).toInt();
            applyDue(x.value(u"due"_s).toObject(), t, st);
            applyDue(x.value(u"deadline"_s).toObject(), t, st, true);
            t.done = x.value(u"checked"_s).toBool() || x.value(u"checked"_s).toInt() == 1;
            if (t.done) {
                const QDateTime at = QDateTime::fromString(x.value(u"completed_at"_s).toString(), Qt::ISODateWithMs);
                if (at.isValid())
                    t.completed = at.toTimeZone(st.ctx.options().timeZone).date();
            }
            if (const auto r = reminders.constFind(t.id); r != reminders.constEnd()) {
                if (r->value(u"type"_s).toString() == u"relative" && t.due && t.due->time) {
                    const QDateTime at =
                            QDateTime(t.due->date, *t.due->time).addSecs(-60LL * r->value(u"minute_offset"_s).toInt());
                    t.reminder = DateSpec{at.date(), at.time()};
                } else {
                    Task probe;
                    applyDue(r->value(u"due"_s).toObject(), probe, st);
                    if (probe.due && probe.due->time)
                        t.reminder = probe.due;
                }
            }
            st.durations = st.durations || x.value(u"duration"_s).isObject();
            st.assignments = st.assignments || !x.value(u"responsible_uid"_s).isNull();
            t.comments = taskComments.value(t.id);
            projects[*it].tasks.append(t);
        }
        // Children projects become sub-pages: "Parent/Child".
        for (Project &p : projects) {
            QStringList path{p.name};
            QSet<QString> seen{p.id};
            QString parent = p.parentId;
            while (!parent.isEmpty() && byId.contains(parent) && !seen.contains(parent) && path.size() < 8) {
                seen.insert(parent);
                const Project &up = projects[byId.value(parent)];
                path.prepend(up.name);
                parent = up.parentId;
            }
            p.name = path.join(u'/');
        }
        return projects;
    }

    void writePages(const QList<Project> &projects, State &st) const
    {
        for (const Project &p : projects) {
            if (p.tasks.isEmpty() && p.notes.isEmpty() && p.sections.isEmpty())
                continue;
            QStringList segments;
            for (const QString &s : p.name.split(u'/'))
                segments.append(pageSegment(s.simplified()));
            QVariantMap fm;
            fm.insert(u"source"_s, name());
            addTitleAlias(fm, p.name.section(u'/', -1));
            st.ctx.addPage(st.ctx.claimPage(segments.join(u'/')),
                           noteWithFrontmatter(fm, {u"aliases"_s, u"source"_s}, projectBody(p) + u'\n'));
        }
    }
};

} // namespace

std::unique_ptr<Importer> makeTodoistImporter()
{
    return std::make_unique<TodoistImporter>();
}

} // namespace brulion::io::detail

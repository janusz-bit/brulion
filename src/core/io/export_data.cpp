// Data exporters: JSON (brulion-export v1), iCalendar (tasks, reminders, scheduled days), OPML
// (the block outline) and a CSV of tasks.
#include "dates/recurrence.h"
#include "io/csv.h"
#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/ical.h"
#include "io/messages_p.h"
#include "markdown/inline.h"
#include "markdown/notemode.h"
#include "markdown/properties.h"
#include "markdown/tasks.h"
#include "markdown/tokens.h"
#include "text/text.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QXmlStreamWriter>

#include <algorithm>
#include <tuple>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

QString statusName(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return u"todo"_s;
}

QString priorityName(Priority p)
{
    switch (p) {
    case Priority::Highest: return u"highest"_s;
    case Priority::High: return u"high"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Lowest: return u"lowest"_s;
    case Priority::None: break;
    }
    return {};
}

QString linkKindName(LinkKind k)
{
    switch (k) {
    case LinkKind::Wiki: return u"wiki"_s;
    case LinkKind::Embed: return u"embed"_s;
    case LinkKind::BlockRef: return u"blockRef"_s;
    case LinkKind::BlockEmbed: return u"blockEmbed"_s;
    case LinkKind::Markdown: return u"markdown"_s;
    case LinkKind::Url: return u"url"_s;
    }
    return {};
}

QString blockKindName(BlockKind k)
{
    switch (k) {
    case BlockKind::ListItem: return u"listItem"_s;
    case BlockKind::Paragraph: return u"paragraph"_s;
    case BlockKind::Heading: return u"heading"_s;
    case BlockKind::Code: return u"code"_s;
    case BlockKind::Table: return u"table"_s;
    case BlockKind::Quote: return u"quote"_s;
    case BlockKind::Callout: return u"callout"_s;
    case BlockKind::Rule: return u"rule"_s;
    case BlockKind::Html: return u"html"_s;
    case BlockKind::Frontmatter: return u"frontmatter"_s;
    }
    return {};
}

QString spec(const std::optional<DateSpec> &s)
{
    return s ? s->toString() : QString();
}

QString date(const std::optional<QDate> &d)
{
    return d ? d->toString(u"yyyy-MM-dd"_s) : QString();
}

void insertIf(QJsonObject &o, const QString &key, const QString &value)
{
    if (!value.isEmpty())
        o.insert(key, value);
}

// --- JSON ----------------------------------------------------------------------------------------

class JsonExporter final : public Exporter {
public:
    QString id() const override { return u"json"_s; }
    QString name() const override { return u"JSON"_s; }
    QString fileExtension() const override { return u".json"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        QJsonObject root;
        root.insert(u"format"_s, u"brulion-export"_s);
        root.insert(u"version"_s, 1);
        root.insert(u"vault"_s, vault.vaultName());
        if (input.now.isValid())
            root.insert(u"exportedAt"_s, input.now.toString(Qt::ISODate));
        root.insert(u"config"_s, configJson(input.config));
        QJsonArray notes;
        for (const ExportVault::Note &n : vault.notes())
            notes.append(noteJson(vault, n));
        root.insert(u"notes"_s, notes);
        QJsonArray attachments;
        for (const FileEntry &a : vault.attachments()) {
            QJsonObject o;
            o.insert(u"path"_s, a.path);
            o.insert(u"size"_s, qint64(a.data.size()));
            o.insert(u"mime"_s, mimeForPath(a.path));
            o.insert(u"data"_s, QString::fromLatin1(a.data.toBase64()));
            attachments.append(o);
        }
        root.insert(u"attachments"_s, attachments);
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName({}, u".json"_s), u"application/json"_s,
                                    QJsonDocument(root).toJson(QJsonDocument::Indented)});
        return r;
    }

private:
    static QJsonObject configJson(const VaultConfig &c)
    {
        QJsonObject o;
        o.insert(u"journalsFolder"_s, c.journalsFolder);
        o.insert(u"journalFileFormat"_s, c.journalFileFormat);
        o.insert(u"newPagesFolder"_s, c.newPagesFolder);
        o.insert(u"attachmentsFolder"_s, c.attachmentsFolder);
        o.insert(u"templatesFolder"_s, c.templatesFolder);
        o.insert(u"journalTemplate"_s, c.journalTemplate);
        o.insert(u"defaultReminderTime"_s, c.defaultReminderTime.toString(u"HH:mm"_s));
        o.insert(u"weekStartsOn"_s, int(c.weekStartsOn));
        o.insert(u"journalMode"_s, markdown::noteModeName(c.journalMode));
        o.insert(u"pageMode"_s, markdown::noteModeName(c.pageMode));
        return o;
    }

    static QJsonObject noteJson(const ExportVault &vault, const ExportVault::Note &n)
    {
        const ParsedNote &p = *n.parsed;
        QJsonObject o;
        o.insert(u"path"_s, n.path);
        o.insert(u"title"_s, p.title);
        if (n.journal)
            o.insert(u"journalDate"_s, n.journal->toString(u"yyyy-MM-dd"_s));
        o.insert(u"content"_s, n.content);
        o.insert(u"frontmatter"_s, QJsonObject::fromVariantMap(p.frontmatter));
        o.insert(u"tags"_s, QJsonArray::fromStringList(p.tags));
        o.insert(u"aliases"_s, QJsonArray::fromStringList(p.aliases));
        QJsonArray links;
        for (const Link &l : p.links) {
            QJsonObject lo;
            lo.insert(u"kind"_s, linkKindName(l.kind));
            lo.insert(u"target"_s, l.target);
            insertIf(lo, u"heading"_s, l.heading);
            insertIf(lo, u"blockId"_s, l.blockId);
            insertIf(lo, u"display"_s, l.display);
            lo.insert(u"line"_s, l.line);
            if (l.kind == LinkKind::BlockRef || l.kind == LinkKind::BlockEmbed) {
                if (const auto b = vault.block(l.target))
                    lo.insert(u"resolved"_s, b->first);
            } else if (l.kind != LinkKind::Url) {
                if (const auto r = vault.resolve(l.target, n.path))
                    lo.insert(u"resolved"_s, *r);
            }
            links.append(lo);
        }
        o.insert(u"links"_s, links);
        QJsonArray tasks;
        for (const Task &t : p.tasks) {
            QJsonObject to;
            to.insert(u"line"_s, t.line);
            to.insert(u"status"_s, statusName(t.status));
            to.insert(u"text"_s, t.text);
            insertIf(to, u"due"_s, spec(t.due));
            insertIf(to, u"scheduled"_s, spec(t.scheduled));
            insertIf(to, u"start"_s, spec(t.start));
            insertIf(to, u"created"_s, date(t.created));
            insertIf(to, u"done"_s, date(t.doneDate));
            insertIf(to, u"cancelled"_s, date(t.cancelledDate));
            insertIf(to, u"recurrence"_s, t.recurrence);
            insertIf(to, u"priority"_s, priorityName(t.priority));
            tasks.append(to);
        }
        o.insert(u"tasks"_s, tasks);
        QJsonArray reminders;
        for (const Reminder &rem : p.reminders) {
            QJsonObject ro;
            ro.insert(u"line"_s, rem.line);
            ro.insert(u"at"_s, rem.at.toString());
            ro.insert(u"alarm"_s, rem.alarm);
            insertIf(ro, u"recurrence"_s, rem.recurrence);
            ro.insert(u"text"_s, rem.text);
            reminders.append(ro);
        }
        o.insert(u"reminders"_s, reminders);
        QJsonArray blocks;
        for (const Block &b : p.blocks) {
            if (b.id.isEmpty())
                continue;
            QJsonObject bo;
            bo.insert(u"id"_s, b.id);
            bo.insert(u"kind"_s, blockKindName(b.kind));
            bo.insert(u"line"_s, b.line);
            bo.insert(u"text"_s, b.text);
            blocks.append(bo);
        }
        o.insert(u"blocks"_s, blocks);
        return o;
    }
};

// --- iCalendar -----------------------------------------------------------------------------------

/// Stable UIDs: the block id when there is one, else a hash of path and text (numbered when the
/// same text occurs again in the note).
class UidMaker {
public:
    QString uid(const VaultPath &path, const Block &b, const QString &kind)
    {
        QString base = b.id.isEmpty() ? text::fnv1a64Hex((path + u'\n' + b.text).toUtf8()) : b.id;
        const QString key = base + u'/' + kind;
        const int n = ++m_seen[key];
        if (n > 1)
            base += u'-' + QString::number(n);
        return base + u'-' + kind + u"@brulion"_s;
    }

private:
    QHash<QString, int> m_seen;
};

void writeDate(ical::Writer &w, QStringView name, const DateSpec &s)
{
    if (s.time)
        w.property(name, ical::formatLocalDateTime(s.date, *s.time));
    else
        w.property(name, ical::formatDate(s.date), {{u"VALUE"_s, u"DATE"_s}});
}

QString rrule(const QString &rule)
{
    const auto parsed = dates::parseRecurrence(rule);
    return parsed ? ical::formatRRule(*parsed) : QString();
}

class IcsExporter final : public Exporter {
public:
    QString id() const override { return u"ics"_s; }
    QString name() const override { return u"iCalendar"_s; }
    QString fileExtension() const override { return u".ics"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        ical::Writer w;
        w.begin(u"VCALENDAR");
        w.property(u"VERSION", u"2.0");
        w.property(u"PRODID", u"-//Brulion//Brulion//PL");
        w.property(u"CALSCALE", u"GREGORIAN");
        w.text(u"X-WR-CALNAME", vault.vaultName());
        const QString stamp = ical::formatUtc(
                input.now.isValid() ? input.now : QDateTime(QDate(1980, 1, 1), QTime(0, 0), QTimeZone::UTC));
        UidMaker uids;
        for (const ExportVault::Note &n : vault.notes()) {
            for (const Block &b : n.parsed->blocks) {
                if (b.task)
                    writeTask(w, uids, stamp, n, b);
                writeReminders(w, uids, stamp, n, b);
            }
        }
        w.end(u"VCALENDAR");
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName({}, u".ics"_s), u"text/calendar"_s, w.data()});
        return r;
    }

private:
    static QString summary(const QString &text, const ParsedNote &note)
    {
        const QString first = text.section(u'\n', 0, 0).trimmed();
        return first.isEmpty() ? note.title : first;
    }

    static void common(ical::Writer &w, const QString &uid, const QString &stamp, const QString &title,
                       const ExportVault::Note &n, const Block &b)
    {
        w.text(u"UID", uid);
        w.property(u"DTSTAMP", stamp);
        w.text(u"SUMMARY", title);
        w.text(u"DESCRIPTION", QString(n.parsed->title + u" (" + n.path + u')'));
        if (!b.tags.isEmpty()) {
            // A list value: each tag escaped on its own, the separating commas not.
            QStringList categories;
            for (const QString &tag : b.tags)
                categories.append(ical::escapeText(tag));
            w.property(u"CATEGORIES", categories.join(u','));
        }
    }

    static void writeTask(ical::Writer &w, UidMaker &uids, const QString &stamp, const ExportVault::Note &n,
                          const Block &b)
    {
        const Task &t = *b.task;
        const QString title = summary(t.text.isEmpty() ? b.text : t.text, *n.parsed);
        const bool open =
                t.status == TaskStatus::Todo || t.status == TaskStatus::Doing || t.status == TaskStatus::Deferred;
        const QString rule = t.recurrence.isEmpty() ? QString() : rrule(t.recurrence);
        if (t.due) {
            w.begin(u"VTODO");
            common(w, uids.uid(n.path, b, u"task"_s), stamp, title, n, b);
            const std::optional<DateSpec> start = t.scheduled ? t.scheduled : t.start;
            if (start && start->date <= t.due->date && start->hasTime() == t.due->hasTime())
                writeDate(w, u"DTSTART", *start);
            writeDate(w, u"DUE", *t.due);
            switch (t.status) {
            case TaskStatus::Todo:
            case TaskStatus::Deferred: w.property(u"STATUS", u"NEEDS-ACTION"); break;
            case TaskStatus::Doing: w.property(u"STATUS", u"IN-PROCESS"); break;
            case TaskStatus::Done: w.property(u"STATUS", u"COMPLETED"); break;
            case TaskStatus::Cancelled: w.property(u"STATUS", u"CANCELLED"); break;
            }
            if (t.status == TaskStatus::Done && t.doneDate)
                // Only the date is known: noon UTC keeps it on the same day everywhere.
                w.property(u"COMPLETED", ical::formatUtc(QDateTime(*t.doneDate, QTime(12, 0), QTimeZone::UTC)));
            switch (t.priority) {
            case Priority::Highest:
            case Priority::High: w.property(u"PRIORITY", u"1"); break;
            case Priority::Medium: w.property(u"PRIORITY", u"5"); break;
            case Priority::Low:
            case Priority::Lowest: w.property(u"PRIORITY", u"9"); break;
            case Priority::None: break;
            }
            if (!rule.isEmpty())
                w.property(u"RRULE", rule);
            w.end(u"VTODO");
        }
        if (t.scheduled && open) {
            // A scheduled day as an event (all-day, or timed when the task has a time).
            w.begin(u"VEVENT");
            common(w, uids.uid(n.path, b, u"scheduled"_s), stamp, title, n, b);
            writeDate(w, u"DTSTART", *t.scheduled);
            if (t.scheduled->time)
                w.property(u"DURATION", u"PT30M");
            else
                w.property(u"DTEND", ical::formatDate(t.scheduled->date.addDays(1)), {{u"VALUE"_s, u"DATE"_s}});
            w.property(u"TRANSP", u"TRANSPARENT");
            if (!rule.isEmpty())
                w.property(u"RRULE", rule);
            w.end(u"VEVENT");
        }
    }

    static void writeReminders(ical::Writer &w, UidMaker &uids, const QString &stamp, const ExportVault::Note &n,
                               const Block &b)
    {
        if (b.task && (b.task->status == TaskStatus::Done || b.task->status == TaskStatus::Cancelled))
            return; // reminders of finished tasks never fire
        for (const Reminder &rem : b.reminders) {
            const QString title = summary(rem.text, *n.parsed);
            w.begin(u"VEVENT");
            common(w, uids.uid(n.path, b, u"reminder"_s), stamp, title, n, b);
            writeDate(w, u"DTSTART", rem.at);
            w.property(u"DURATION", u"PT15M");
            if (!rem.recurrence.isEmpty()) {
                if (const QString rule = rrule(rem.recurrence); !rule.isEmpty())
                    w.property(u"RRULE", rule);
            }
            w.begin(u"VALARM");
            w.property(u"ACTION", u"DISPLAY");
            w.text(u"DESCRIPTION", title);
            w.property(u"TRIGGER", u"PT0S");
            w.end(u"VALARM");
            if (rem.alarm) {
                w.begin(u"VALARM");
                w.property(u"ACTION", u"AUDIO");
                w.property(u"TRIGGER", u"PT0S");
                w.end(u"VALARM");
            }
            w.end(u"VEVENT");
        }
    }
};

// --- OPML ----------------------------------------------------------------------------------------

/// The first line of a list item without marker / checkbox / block id, and its other lines.
std::pair<QString, QString> itemText(const QList<text::Line> &lines, const Block &b)
{
    if (b.line >= lines.size())
        return {b.text, {}};
    QString first = lines[b.line].text.trimmed();
    static const QRegularExpression marker(u"^([-*+]|\\d{1,9}[.)])\\s*"_s);
    first.remove(marker);
    if (const auto caret = markdown::blockIdAtEnd(first))
        first = first.first(caret->from);
    if (const auto task = markdown::matchTaskMarker(first, 0))
        first = first.sliced(task->contentStart);
    QStringList rest;
    for (int l = b.line + 1; l <= b.ownEndLine && l < lines.size(); ++l) {
        const QString t = lines[l].text.trimmed();
        if (markdown::matchPropertyLine(t))
            continue; // properties (id::, collapsed::, …) are metadata
        rest.append(t);
    }
    while (!rest.isEmpty() && rest.last().isEmpty())
        rest.removeLast();
    return {first.trimmed(), rest.join(u'\n')};
}

class OpmlExporter final : public Exporter {
public:
    QString id() const override { return u"opml"_s; }
    QString name() const override { return u"OPML"_s; }
    QString fileExtension() const override { return u".opml"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        QByteArray data;
        QXmlStreamWriter x(&data);
        x.setAutoFormatting(true);
        x.setAutoFormattingIndent(2);
        x.writeStartDocument();
        x.writeStartElement(u"opml"_s);
        x.writeAttribute(u"version"_s, u"2.0"_s);
        x.writeStartElement(u"head"_s);
        x.writeTextElement(u"title"_s, vault.vaultName());
        if (input.now.isValid())
            x.writeTextElement(u"dateCreated"_s,
                               QLocale::c().toString(input.now.toUTC(), u"ddd, dd MMM yyyy HH:mm:ss 'GMT'"_s));
        x.writeEndElement();
        x.writeStartElement(u"body"_s);
        QList<const ExportVault::Note *> pages;
        QList<const ExportVault::Note *> journals;
        for (const ExportVault::Note &n : vault.notes())
            (n.journal ? journals : pages).append(&n);
        std::stable_sort(journals.begin(), journals.end(), [](const ExportVault::Note *a, const ExportVault::Note *b) {
            return *a->journal > *b->journal;
        });
        for (const ExportVault::Note *n : pages + journals)
            writeNote(x, *n);
        x.writeEndElement();
        x.writeEndElement();
        x.writeEndDocument();
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName({}, u".opml"_s), u"text/x-opml"_s, data});
        return r;
    }

private:
    static void writeNote(QXmlStreamWriter &x, const ExportVault::Note &n)
    {
        const ParsedNote &p = *n.parsed;
        const QList<text::Line> lines = text::splitLines(n.content);
        x.writeStartElement(u"outline"_s);
        x.writeAttribute(u"text"_s, p.title);
        x.writeAttribute(u"_note"_s, n.path);
        // Root blocks; blocks after a heading nest under it (until a heading of the same level).
        QList<int> headingLevels;
        for (int i = 0; i < p.blocks.size(); ++i) {
            const Block &b = p.blocks[i];
            if (b.parent >= 0 || b.kind == BlockKind::Frontmatter || b.kind == BlockKind::Rule)
                continue;
            if (b.kind == BlockKind::Heading) {
                while (!headingLevels.isEmpty() && headingLevels.last() >= b.headingLevel) {
                    x.writeEndElement();
                    headingLevels.removeLast();
                }
                x.writeStartElement(u"outline"_s);
                x.writeAttribute(u"text"_s, b.text);
                headingLevels.append(b.headingLevel);
                continue;
            }
            writeBlock(x, p, i, lines);
        }
        for (qsizetype k = 0; k < headingLevels.size(); ++k)
            x.writeEndElement();
        x.writeEndElement();
    }

    static void writeBlock(QXmlStreamWriter &x, const ParsedNote &p, int index, const QList<text::Line> &lines)
    {
        const Block &b = p.blocks[index];
        QString text;
        QString note;
        switch (b.kind) {
        case BlockKind::ListItem: std::tie(text, note) = itemText(lines, b); break;
        case BlockKind::Code:
        case BlockKind::Table:
        case BlockKind::Html:
            text = b.kind == BlockKind::Code ? u"```"_s + b.lang : b.text.section(u'\n', 0, 0);
            note = b.text;
            break;
        default:
            text = b.text.section(u'\n', 0, 0);
            note = b.text.section(u'\n', 1).trimmed();
            break;
        }
        x.writeStartElement(u"outline"_s);
        x.writeAttribute(u"text"_s, text);
        if (!note.isEmpty())
            x.writeAttribute(u"_note"_s, note);
        if (b.task && (b.task->status == TaskStatus::Done || b.task->status == TaskStatus::Cancelled))
            x.writeAttribute(u"_complete"_s, u"true"_s);
        for (int c : b.children)
            writeBlock(x, p, c, lines);
        x.writeEndElement();
    }
};

// --- tasks CSV -----------------------------------------------------------------------------------

class TasksCsvExporter final : public Exporter {
public:
    QString id() const override { return u"tasks-csv"_s; }
    QString name() const override { return IoMessages::tr("Tasks (CSV)"); }
    QString fileExtension() const override { return u".csv"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        QString out =
                csvRow({u"Page"_s, u"Path"_s, u"Line"_s, u"Status"_s, u"Task"_s, u"Due"_s, u"Scheduled"_s, u"Start"_s,
                        u"Created"_s, u"Done"_s, u"Cancelled"_s, u"Priority"_s, u"Recurrence"_s, u"Tags"_s})
                + u"\r\n"_s;
        for (const ExportVault::Note &n : vault.notes()) {
            for (const Block &b : n.parsed->blocks) {
                if (!b.task)
                    continue;
                const Task &t = *b.task;
                QStringList tags;
                for (const QString &tag : b.tags)
                    tags.append(u'#' + tag);
                out += csvRow({csvSafeText(n.parsed->title), csvSafeText(n.path), QString::number(t.line + 1),
                               statusName(t.status), csvSafeText(t.text), spec(t.due), spec(t.scheduled), spec(t.start),
                               date(t.created), date(t.doneDate), date(t.cancelledDate), priorityName(t.priority),
                               t.recurrence, tags.join(u' ')})
                       + u"\r\n"_s;
            }
        }
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName(u"tasks"_s, u".csv"_s), u"text/csv"_s,
                                    QByteArray("\xEF\xBB\xBF") + out.toUtf8()});
        return r;
    }
};

} // namespace

std::unique_ptr<Exporter> makeJsonExporter()
{
    return std::make_unique<JsonExporter>();
}

std::unique_ptr<Exporter> makeIcsExporter()
{
    return std::make_unique<IcsExporter>();
}

std::unique_ptr<Exporter> makeOpmlExporter()
{
    return std::make_unique<OpmlExporter>();
}

std::unique_ptr<Exporter> makeTasksCsvExporter()
{
    return std::make_unique<TasksCsvExporter>();
}

} // namespace brulion::io::detail

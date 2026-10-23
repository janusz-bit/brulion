#include "planningutil.h"

#include "dates/format.h"
#include "dates/natural.h"
#include "index/vaultindex.h"
#include "markdown/tokens.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "text/text.h"

#include <QCollator>
#include <QColor>
#include <QRegularExpression>

namespace brulion::planning {

using namespace Qt::StringLiterals;

QDate parseIsoDate(const QString &text)
{
    if (text.size() != 10)
        return {};
    return QDate::fromString(text, u"yyyy-MM-dd"_s);
}

QString isoDate(QDate date)
{
    return date.isValid() ? date.toString(u"yyyy-MM-dd"_s) : QString();
}

QString clockTime(const std::optional<QTime> &time)
{
    return time && time->isValid() ? time->toString(u"HH:mm"_s) : QString();
}

dates::UiLocale uiLocale(const QString &locale)
{
    return locale.startsWith(u"en"_s, Qt::CaseInsensitive) ? dates::UiLocale::English : dates::UiLocale::Polish;
}

namespace {

QString colorString(const QVariant &v, const QString &fallback)
{
    if (!v.isValid())
        return fallback;
    QColor c;
    if (v.metaType() == QMetaType::fromType<QColor>())
        c = v.value<QColor>();
    else
        c = QColor::fromString(v.toString());
    if (!c.isValid())
        return fallback;
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

} // namespace

markdown::Palette paletteFromMap(const QVariantMap &map)
{
    markdown::Palette p;
    const auto get = [&map](const QString &key, const QString &fallback) { return colorString(map.value(key), fallback); };
    p.link = get(u"link"_s, p.link);
    p.unresolved = get(u"unresolved"_s, p.unresolved);
    p.muted = get(u"muted"_s, p.muted);
    p.codeBackground = get(u"codeBackground"_s, p.codeBackground);
    p.highlightBackground = get(u"highlightBackground"_s, p.highlightBackground);
    const auto chip = [&](const QString &name, markdown::ChipColors &c) {
        c.foreground = get(name + u"Fg"_s, c.foreground);
        c.background = get(name + u"Bg"_s, c.background);
    };
    chip(u"tag"_s, p.tag);
    chip(u"due"_s, p.due);
    chip(u"overdue"_s, p.overdue);
    chip(u"scheduled"_s, p.scheduled);
    chip(u"reminder"_s, p.reminder);
    chip(u"alarm"_s, p.alarm);
    chip(u"recurrence"_s, p.recurrence);
    chip(u"done"_s, p.done);
    chip(u"neutral"_s, p.neutral);
    return p;
}

QString renderInline(const VaultIndex *index, const QString &markdown, const VaultPath &fromPath, QDate today,
                     dates::UiLocale locale, const markdown::Palette &palette)
{
    markdown::RenderContext ctx;
    if (index) {
        ctx.resolvePage = [index, fromPath](const QString &target) -> std::optional<QString> {
            return index->resolveLink(target, fromPath);
        };
        ctx.blockText = [index](const QString &id) -> std::optional<QString> {
            const auto ref = index->blockById(id);
            if (!ref)
                return std::nullopt;
            const ParsedNote *note = index->note(ref->path);
            if (!note || ref->blockIndex < 0 || ref->blockIndex >= note->blocks.size())
                return std::nullopt;
            return note->blocks.at(ref->blockIndex).text;
        };
        // [[Page#^id]]: the block of that page, else (moved) the notebook's block with the id.
        ctx.linkedBlockText = [index, fromPath, byId = ctx.blockText](const QString &target,
                                                                      const QString &id) -> std::optional<QString> {
            const std::optional<QString> path =
                    target.isEmpty() ? std::optional<QString>(fromPath) : index->resolveLink(target, fromPath);
            const ParsedNote *note = path ? index->note(*path) : nullptr;
            if (!note)
                return std::nullopt;
            if (const int i = note->blockIds.value(id, -1); i >= 0 && i < note->blocks.size())
                return note->blocks.at(i).text;
            return byId(id);
        };
    }
    ctx.today = today;
    ctx.locale = locale;
    ctx.palette = palette;
    ctx.taskCheckbox = false;
    // [[2026-10-13]] shows as "wt 13 paź" (the note keeps the date as written).
    ctx.linkTitle = markdown::journalLinkTitle(index ? index->config() : VaultConfig(), locale, today);
    return markdown::InlineRenderer(ctx).render(markdown);
}

QString plainText(const QString &markdown)
{
    static const QRegularExpression wiki(u"!?\\[\\[([^\\]|]*)(?:\\|([^\\]]*))?\\]\\]"_s);
    static const QRegularExpression mdLink(u"!?\\[([^\\]]*)\\]\\([^)]*\\)"_s);
    static const QRegularExpression blockRef(u"!?\\(\\(([A-Za-z0-9-]+)\\)\\)"_s);
    static const QRegularExpression emphasis(u"(\\*\\*|__|~~|==)"_s);
    static const QRegularExpression code(u"`([^`]*)`"_s);
    static const QRegularExpression spaces(u"\\s+"_s);
    const QString &s = markdown;
    // Wiki links: display text, else the target without its fragment.
    QString out;
    qsizetype last = 0;
    auto it = wiki.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += QStringView(s).sliced(last, m.capturedStart() - last);
        QString shown = m.captured(2).trimmed();
        if (shown.isEmpty()) {
            shown = m.captured(1);
            const qsizetype hash = shown.indexOf(u'#');
            if (hash > 0)
                shown.truncate(hash);
        }
        out += shown;
        last = m.capturedEnd();
    }
    out += QStringView(s).sliced(last);
    out.replace(mdLink, u"\\1"_s);
    out.replace(blockRef, u"((\\1))"_s);
    out.replace(code, u"\\1"_s);
    out.replace(emphasis, QString());
    out.replace(spaces, u" "_s);
    return out.trimmed();
}

QString statusName(TaskStatus status)
{
    return notebook::taskStatusName(status); // the names of the JSON (CLI, query results)
}

std::optional<TaskStatus> statusFromName(const QString &name)
{
    if (name == u"todo")
        return TaskStatus::Todo;
    if (name == u"doing")
        return TaskStatus::Doing;
    if (name == u"done")
        return TaskStatus::Done;
    if (name == u"cancelled")
        return TaskStatus::Cancelled;
    if (name == u"deferred")
        return TaskStatus::Deferred;
    return std::nullopt;
}

bool isClosed(TaskStatus status)
{
    return status == TaskStatus::Done || status == TaskStatus::Cancelled;
}

QString priorityName(Priority priority)
{
    return notebook::priorityName(priority);
}

Priority priorityFromName(const QString &name)
{
    if (name == u"lowest")
        return Priority::Lowest;
    if (name == u"low")
        return Priority::Low;
    if (name == u"medium")
        return Priority::Medium;
    if (name == u"high")
        return Priority::High;
    if (name == u"highest")
        return Priority::Highest;
    return Priority::None;
}

QString humanDate(const DateSpec &spec, dates::UiLocale locale, QDate today)
{
    return dates::formatHuman(spec, locale, today);
}

namespace {

/// The user's text as one line, without a list marker / checkbox of its own.
QString cleanItemText(const QString &text)
{
    static const QRegularExpression marker(u"^\\s*(?:[-*+]|\\d+[.)])\\s+"_s);
    static const QRegularExpression checkbox(u"^\\[[ xX/>\\-]\\]\\s*"_s);
    static const QRegularExpression breaks(u"\\s*[\\r\\n]+\\s*"_s);
    QString t = text;
    t.replace(breaks, u" "_s);
    t = t.trimmed();
    t.remove(marker);
    t.remove(checkbox);
    return t.trimmed();
}

} // namespace

QString taskLine(const QString &text, const std::optional<DateSpec> &due, const std::optional<DateSpec> &reminder)
{
    QString line = u"- [ ] "_s + cleanItemText(text);
    if (due && due->isValid())
        line += u' ' + markdown::tokens::Due.toString() + u' ' + due->toString();
    if (reminder && reminder->isValid() && reminder->hasTime())
        line += u' ' + markdown::formatReminderToken(*reminder, false);
    return line;
}

QString reminderLine(const QString &text, const DateSpec &at, bool alarm, const QString &recurrence)
{
    QString line = u"- "_s + cleanItemText(text);
    if (at.isValid())
        line += u' ' + markdown::formatReminderToken(at, alarm);
    const QString rule = recurrence.trimmed();
    if (!rule.isEmpty())
        line += u' ' + markdown::tokens::Recurrence.toString() + u' ' + rule;
    return line;
}

QString appendBlock(const QString &content, const QString &block)
{
    // The block as one item: blank lines around it dropped, each line trimmed, continuation lines
    // of a list item indented under it (a paragraph stays a paragraph, unlike
    // notebook::blockFromText(), which makes everything a list item); appended as the command
    // line and quick capture append (notebook::appendBlock: line endings, a template's empty
    // bullet, a BOM).
    static const QRegularExpression listMarker(u"^([-*+]|\\d+[.)])\\s"_s);
    QStringList lines = QString(block).remove(u'\r').split(u'\n');
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return content;
    const QString first = lines.first().trimmed();
    const bool listItem = listMarker.match(first).hasMatch();
    QString item = first;
    for (qsizetype i = 1; i < lines.size(); ++i)
        item += u'\n' + (listItem ? u"  "_s : QString()) + lines.at(i).trimmed();
    return notebook::appendBlock(content, item);
}

QuickAdd parseQuickAdd(const QString &input, const QDateTime &now, Qt::DayOfWeek weekStart, dates::UiLocale locale)
{
    QuickAdd r;
    const QString cleaned = cleanItemText(input);
    QString body = cleaned;
    if (const auto found = dates::parseNaturalDate(cleaned, now, weekStart, locale)) {
        const QString stripped = dates::stripNaturalDate(cleaned, *found).trimmed();
        // A date before today is a note about the past; a phrase that is the whole text is the
        // text ("Jutro" as a task name).
        if (found->spec.date >= now.date() && !stripped.isEmpty()) {
            r.when = found->spec;
            r.phrase = cleaned.mid(found->start, found->length);
            body = stripped;
        }
    }
    r.text = body.trimmed();
    r.ok = !r.text.isEmpty();
    if (!r.ok)
        return r;
    std::optional<DateSpec> due;
    std::optional<DateSpec> reminder;
    if (r.when) {
        if (r.when->hasTime())
            reminder = r.when;
        else
            due = DateSpec{r.when->date, std::nullopt};
    }
    r.line = taskLine(r.text, due, reminder);
    return r;
}

QString queryValue(const QString &value)
{
    const QString v = value.trimmed();
    bool needsQuotes = v.isEmpty();
    for (const QChar c : v) {
        if (c.isSpace() || c == u',' || c == u'"' || c == u'„' || c == u'“' || c == u'«') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes)
        return v;
    QString inner = v;
    inner.replace(u'"', u'\'');
    return u'"' + inner + u'"';
}

namespace {

const QCollator &nameCollator()
{
    static thread_local QCollator collator = [] {
        QCollator c{QLocale(QLocale::Polish, QLocale::Poland)};
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    return collator;
}

} // namespace

int compareNames(const QString &a, const QString &b)
{
    const int r = nameCollator().compare(a, b);
    return r != 0 ? r : QString::compare(a, b);
}

QCollatorSortKey nameSortKey(const QString &name)
{
    return nameCollator().sortKey(name);
}

} // namespace brulion::planning

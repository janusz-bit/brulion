// Reading the cells of view rows (cells_p.h).
#include "views/cells_p.h"
#include "views/messages_p.h"

#include "dataview/dataview.h"
#include "markdown/blockprops.h"
#include "markdown/pagelook.h"
#include "markdown/properties.h"
#include "util/paths.h"

#include <QLocale>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::views::detail {

namespace {

using T = dataview::Value::Type;

QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + u" B"_s;
    if (bytes < 1024 * 1024)
        return QString::number(double(bytes) / 1024.0, 'f', 1) + u" KB"_s;
    return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 1) + u" MB"_s;
}

QString priorityKeyName(Priority p)
{
    switch (p) {
    case Priority::Highest: return u"highest"_s;
    case Priority::High: return u"high"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Lowest: return u"lowest"_s;
    case Priority::None: return u"none"_s;
    }
    return {};
}

Cell textCell(const QString &markdown, const QString &text, const QVariant &value = {})
{
    Cell c;
    c.markdown = markdown;
    c.text = text;
    c.value = value.isValid() ? value : (text.isEmpty() ? QVariant() : QVariant(text));
    c.empty = text.isEmpty() && markdown.isEmpty();
    return c;
}

/// A value written on a `key:: value` line as a cell (typed as parseNote() would type it).
Cell blockValueCell(const QString &key, const QString &raw)
{
    Cell c;
    c.markdown = raw;
    c.text = raw;
    c.empty = raw.trimmed().isEmpty();
    c.target = u"block"_s;
    if (c.empty)
        return c;
    switch (markdown::inferPropertyType(key, raw, markdown::PropertySource::Logseq)) {
    case markdown::PropertyType::Number:
        c.value = markdown::parseNumberInput(raw).value_or(QVariant(raw)).toDouble();
        break;
    case markdown::PropertyType::Checkbox: c.value = raw.trimmed().compare(u"true", Qt::CaseInsensitive) == 0; break;
    case markdown::PropertyType::List:
    case markdown::PropertyType::Tags:
    case markdown::PropertyType::Aliases: {
        QStringList items;
        for (const QString &part : markdown::detail::splitTopLevelCommas(raw)) {
            if (!part.trimmed().isEmpty())
                items.append(part.trimmed());
        }
        c.value = items;
        break;
    }
    case markdown::PropertyType::Date: {
        QString d = raw.trimmed();
        if (d.startsWith(u"[[") && d.endsWith(u"]]"))
            d = d.sliced(2, d.size() - 4);
        c.value = d;
        break;
    }
    default: c.value = raw; break;
    }
    return c;
}

} // namespace

std::optional<TaskStatus> statusNamed(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == u"todo" || n == u"open")
        return TaskStatus::Todo;
    if (n == u"doing")
        return TaskStatus::Doing;
    if (n == u"done")
        return TaskStatus::Done;
    if (n == u"cancelled" || n == u"canceled")
        return TaskStatus::Cancelled;
    if (n == u"deferred")
        return TaskStatus::Deferred;
    return std::nullopt;
}

QString statusName(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

QString editType(markdown::PropertyType type)
{
    if (type == markdown::PropertyType::Unsupported)
        return {};
    return markdown::propertyTypeName(type);
}

Cell valueCell(const dataview::Value &value, const VaultIndex &index)
{
    Cell c;
    if (value.isNull())
        return c;
    c.markdown = dataview::toMarkdown(value, index);
    c.text = dataview::toPlainText(value);
    c.empty = c.text.isEmpty() && c.markdown.isEmpty();
    switch (value.type()) {
    case T::Boolean: c.value = value.asBool(); break;
    case T::Number: c.value = value.asNumber(); break;
    case T::String: c.value = value.asString(); break;
    case T::Date: {
        const dataview::DateValue &d = value.asDate();
        c.value = d.hasTime ? d.at.toString(u"yyyy-MM-dd HH:mm"_s) : d.at.date().toString(Qt::ISODate);
        break;
    }
    case T::List: {
        QStringList items;
        for (const dataview::Value &item : value.asList())
            items.append(dataview::toPlainText(item));
        c.value = items;
        c.empty = items.isEmpty();
        break;
    }
    default: c.value = c.text; break;
    }
    return c;
}

Cell pagePropertyCell(const markdown::PageProperty &p)
{
    Cell c;
    c.empty = p.empty;
    if (p.empty)
        return c;
    switch (p.type) {
    case markdown::PropertyType::List: c.markdown = p.items.join(u", "_s); break;
    case markdown::PropertyType::Tags: {
        QStringList tags;
        for (const QString &t : p.items)
            tags.append(t.contains(u' ') ? QString(u"#[["_s + t + u"]]"_s) : QString(u"#"_s + t));
        c.markdown = tags.join(u' ');
        break;
    }
    case markdown::PropertyType::Aliases: c.markdown = p.items.join(u", "_s); break;
    default: c.markdown = p.text; break;
    }
    c.text = p.text;
    c.value = p.type == markdown::PropertyType::List || p.type == markdown::PropertyType::Tags
                              || p.type == markdown::PropertyType::Aliases
                      ? QVariant(p.items)
                      : p.value;
    if (c.value.typeId() == QMetaType::LongLong || c.value.typeId() == QMetaType::Int)
        c.value = c.value.toDouble();
    return c;
}

CellReader::CellReader(const VaultIndex &index, const ViewOptions &options) : m_index(index), m_options(options) { }

CellReader::~CellReader() = default;

const markdown::PageProperties &CellReader::pageProperties(const VaultPath &path)
{
    if (const auto it = m_pages.constFind(path); it != m_pages.cend())
        return *it;
    return *m_pages.insert(path, markdown::readPageProperties(m_index.content(path)));
}

const markdown::PageProperty *CellReader::pageProperty(const VaultPath &path, const QString &folded)
{
    const markdown::PageProperties &props = pageProperties(path);
    for (const markdown::PageProperty &p : props.items) {
        if (queryPropertyKey(p.key) == folded)
            return &p;
    }
    return nullptr;
}

Cell CellReader::propertyCell(const Row &row, const QString &folded)
{
    if (row.blockIndex >= 0) {
        if (const ParsedNote *note = m_index.note(row.path)) {
            const QList<Block> &blocks = note->blocks;
            for (int b = row.blockIndex; b >= 0 && b < blocks.size(); b = blocks[b].parent) {
                for (auto [key, value] : blocks[b].properties.asKeyValueRange()) {
                    Q_UNUSED(value);
                    if (queryPropertyKey(key) != folded)
                        continue;
                    const std::optional<QString> raw =
                            markdown::blockPropertyText(m_index.content(row.path), blocks[b].line, blocks[b].ownEndLine, key);
                    if (raw)
                        return blockValueCell(key, *raw);
                }
            }
        }
    }
    Cell c;
    if (const markdown::PageProperty *p = pageProperty(row.path, folded))
        c = pagePropertyCell(*p);
    if (row.blockIndex >= 0)
        c.target = u"block"_s;
    return c;
}

std::optional<FileStat> CellReader::stat(const VaultPath &path)
{
    if (const auto it = m_stats.constFind(path); it != m_stats.cend())
        return *it;
    std::optional<FileStat> s = m_options.stat ? m_options.stat(path) : std::nullopt;
    return *m_stats.insert(path, s);
}

Cell CellReader::builtinCell(const Row &row, ColumnKind kind)
{
    const ParsedNote *note = m_index.note(row.path);
    const Block *block = note && row.blockIndex >= 0 && row.blockIndex < note->blocks.size()
                                 ? &note->blocks.at(row.blockIndex)
                                 : nullptr;
    const auto pageLink = [&] {
        const QString target = m_index.linkTarget(row.path).value_or(paths::stripMarkdownExtension(row.path));
        const QString title = row.title.isEmpty() ? paths::pageTitle(row.path) : row.title;
        const QString last = target.section(u'/', -1);
        const QString md = last == title ? QString(u"[["_s + target + u"]]"_s)
                                         : QString(u"[["_s + target + u'|' + title + u"]]"_s);
        return textCell(md, title, title);
    };
    switch (kind) {
    case ColumnKind::Title: return pageLink();
    case ColumnKind::Text: {
        if (!block)
            return pageLink();
        const QString text = (row.task ? row.task->text : block->text).section(u'\n', 0, 0).trimmed();
        return textCell(text, text);
    }
    case ColumnKind::Path: return textCell(row.path, row.path);
    case ColumnKind::Folder: {
        const QString f = paths::dirname(row.path);
        return textCell(f, f);
    }
    case ColumnKind::Tags: {
        QStringList tags;
        if (block) {
            for (int b = row.blockIndex; b >= 0; b = note->blocks[b].parent) {
                for (const QString &t : note->blocks[b].tags) {
                    if (!tags.contains(t))
                        tags.append(t);
                }
            }
        } else if (note) {
            tags = note->tags;
        }
        QStringList shown;
        for (const QString &t : std::as_const(tags))
            shown.append(t.contains(u' ') ? QString(u"#[["_s + t + u"]]"_s) : QString(u"#"_s + t));
        Cell c = textCell(shown.join(u' '), shown.join(u' '), tags.isEmpty() ? QVariant() : QVariant(tags));
        return c;
    }
    case ColumnKind::Modified: {
        const std::optional<FileStat> s = stat(row.path);
        if (!s || s->mtime <= 0)
            return {};
        const QString at = QDateTime::fromMSecsSinceEpoch(s->mtime).toString(u"yyyy-MM-dd HH:mm"_s);
        return textCell(at, at);
    }
    case ColumnKind::Size: {
        const std::optional<FileStat> s = stat(row.path);
        const qint64 bytes = s ? s->size : m_index.content(row.path).toUtf8().size();
        return textCell(humanSize(bytes), humanSize(bytes), double(bytes));
    }
    case ColumnKind::Date: {
        if (!note || !note->journalDate)
            return {};
        const QString d = note->journalDate->toString(Qt::ISODate);
        return textCell(d, d);
    }
    case ColumnKind::Status: {
        if (!row.task)
            return {};
        Cell c = textCell(queryStatusLabel(row.task->status), queryStatusLabel(row.task->status),
                          statusName(row.task->status));
        c.target = u"block"_s;
        return c;
    }
    case ColumnKind::Priority: {
        if (!row.task)
            return {};
        Cell c;
        if (row.task->priority != Priority::None) {
            c = textCell(queryPriorityLabel(row.task->priority), queryPriorityLabel(row.task->priority),
                         priorityKeyName(row.task->priority));
        }
        c.target = u"block"_s;
        return c;
    }
    case ColumnKind::Due:
    case ColumnKind::Scheduled:
    case ColumnKind::Start: {
        if (!row.task)
            return {};
        const std::optional<DateSpec> &d = kind == ColumnKind::Due ? row.task->due
                                           : kind == ColumnKind::Scheduled ? row.task->scheduled
                                                                           : row.task->start;
        Cell c;
        if (d && d->isValid())
            c = textCell(d->toString(), d->toString());
        c.target = u"block"_s;
        return c;
    }
    case ColumnKind::Done:
    case ColumnKind::Created: {
        if (!row.task)
            return {};
        const std::optional<QDate> &d = kind == ColumnKind::Done ? row.task->doneDate : row.task->created;
        if (!d || !d->isValid())
            return {};
        return textCell(d->toString(Qt::ISODate), d->toString(Qt::ISODate));
    }
    case ColumnKind::Property:
    case ColumnKind::Formula: break;
    }
    return {};
}

void CellReader::cover(Row &row)
{
    const ParsedNote *note = m_index.note(row.path);
    if (!note)
        return;
    const auto picture = [&](const QString &target) -> bool {
        const std::optional<VaultPath> p = m_index.resolveLink(target, row.path);
        if (!p || !paths::isPicturePath(*p))
            return false;
        row.cover = *p;
        row.coverKind = u"file"_s;
        return true;
    };
    if (row.blockIndex < 0) {
        const markdown::PageCover c = markdown::pageLook(note->properties).cover;
        if (c.kind == markdown::PageCover::Kind::Web) {
            row.cover = c.target;
            row.coverKind = u"web"_s;
            return;
        }
        if (c.kind == markdown::PageCover::Kind::File && picture(c.target))
            return;
    }
    const int from = row.blockIndex >= 0 ? row.line : 0;
    const int to = row.blockIndex >= 0 ? row.ownEndLine : std::numeric_limits<int>::max();
    for (const Link &l : note->links) {
        if (l.kind != LinkKind::Embed || l.line < from || l.line > to)
            continue;
        if (picture(l.target))
            return;
    }
}

void CellReader::coverFrom(Row &row, const QString &written)
{
    QString w = written.trimmed();
    if (w.isEmpty())
        return;
    static const auto isHex = [](QStringView s) {
        return std::ranges::all_of(s, [](QChar c) { return c.isDigit() || (c.toLower() >= u'a' && c.toLower() <= u'f'); });
    };
    if (w.startsWith(u'#') && (w.size() == 7 || w.size() == 4) && isHex(QStringView(w).sliced(1))) {
        row.cover = w;
        row.coverKind = u"color"_s;
        return;
    }
    const markdown::PageCover c = markdown::parsePageCover(w);
    if (c.kind == markdown::PageCover::Kind::Web) {
        row.cover = c.target;
        row.coverKind = u"web"_s;
    } else if (c.kind == markdown::PageCover::Kind::File) {
        if (const std::optional<VaultPath> p = m_index.resolveLink(c.target, row.path); p && paths::isPicturePath(*p)) {
            row.cover = *p;
            row.coverKind = u"file"_s;
        }
    }
}

dataview::Evaluator &CellReader::evaluator()
{
    if (!m_evaluator) {
        dataview::Options o;
        o.currentPath = m_options.currentPath;
        o.now = m_options.now.isValid() ? m_options.now : QDateTime(m_options.today, QTime(0, 0));
        o.weekStart = m_options.weekStart;
        o.stat = m_options.stat;
        o.maxSteps = m_options.maxSteps;
        m_evaluator = std::make_unique<dataview::Evaluator>(m_index, o);
    }
    return *m_evaluator;
}

} // namespace brulion::views::detail

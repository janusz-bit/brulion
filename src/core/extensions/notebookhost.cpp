#include "extensions/notebookhost.h"

#include "extensions/blockedit.h"
#include "index/vaultindex.h"
#include "markdown/tasks.h"
#include "notebook/itemref.h"
#include "notebook/journals.h"
#include "notebook/pages.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

QString propertyText(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QStringList || value.metaType().id() == QMetaType::QVariantList)
        return value.toStringList().join(u", "_s);
    return value.toString();
}

QVariantMap propertiesMap(const Properties &properties)
{
    QVariantMap out;
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        out.insert(it.key(), propertyText(it.value()));
    return out;
}

QStringList splitTags(const QVariant &value)
{
    QStringList raw;
    if (value.metaType().id() == QMetaType::QStringList || value.metaType().id() == QMetaType::QVariantList)
        raw = value.toStringList();
    else
        raw = value.toString().split(QRegularExpression(u"[,\\s]+"_s), Qt::SkipEmptyParts);
    QStringList out;
    for (QString tag : raw) {
        tag = tag.trimmed();
        while (tag.startsWith(u'#'))
            tag.remove(0, 1);
        if (tag.startsWith(u"[["_s) && tag.endsWith(u"]]"_s))
            tag = tag.mid(2, tag.size() - 4);
        tag = tag.normalized(QString::NormalizationForm_C).toLower();
        if (!tag.isEmpty() && !out.contains(tag))
            out << tag;
    }
    return out;
}

QString rawLine(const QString &content, int line)
{
    const QList<text::Line> lines = text::splitLines(content);
    return line >= 0 && line < lines.size() ? lines[line].text : QString();
}

QVariantMap blockMap(const VaultPath &path, const ParsedNote &note, const QList<text::Line> &lines, int index, int depth)
{
    const Block &block = note.blocks[index];
    const QString line = block.line >= 0 && block.line < lines.size() ? lines[block.line].text : QString();
    QVariantMap map{
        {u"ref"_s, notebook::formatLineRef(path, block.line, line)},
        {u"page"_s, note.title},
        {u"text"_s, blockLineText(line)},
        {u"tags"_s, block.tags},
        {u"props"_s, propertiesMap(block.properties)},
    };
    if (note.journalDate)
        map.insert(u"date"_s, note.journalDate->toString(Qt::ISODate));
    if (block.task) {
        const std::optional<QString> c = taskCharacter(line);
        map.insert(u"task"_s, c ? *c : QString(markdown::statusChar(block.task->status)));
    }
    QVariantList children;
    if (depth < 64) {
        for (int child : block.children)
            children << blockMap(path, note, lines, child, depth + 1);
    }
    map.insert(u"children"_s, children);
    return map;
}

} // namespace

QStringList pageTags(const ParsedNote &note)
{
    QStringList tags = splitTags(note.properties.value(u"tags"_s));
    for (const QString &tag : splitTags(note.frontmatter.value(u"tags"_s)))
        if (!tags.contains(tag))
            tags << tag;
    return tags;
}

NotebookHost::NotebookHost(Vault &vault, VaultIndex &index, Options options)
    : m_vault(vault), m_index(index), m_options(std::move(options))
{
}

void NotebookHost::setLanguages(const QString &language, const QString &uiLanguage)
{
    m_options.language = language;
    m_options.uiLanguage = uiLanguage;
}

QDateTime NotebookHost::now() const { return m_options.now ? m_options.now() : QDateTime::currentDateTime(); }

QString NotebookHost::today() { return now().date().toString(Qt::ISODate); }

VaultPath NotebookHost::pathOf(const QString &page) const
{
    const QString name = page.trimmed();
    if (name.isEmpty())
        return {};
    const auto path = m_index.resolveLink(name);
    if (!path || !paths::isMarkdown(*path) || !m_index.note(*path))
        return {};
    return *path;
}

QStringList NotebookHost::pages(const QString &tag)
{
    const QString wanted = tag.normalized(QString::NormalizationForm_C).toLower();
    QStringList out;
    m_index.forEachNote([&](const VaultPath &, const ParsedNote &note) {
        if (wanted.isEmpty() || pageTags(note).contains(wanted))
            out << note.title;
    });
    std::stable_sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return text::compareNames(a, b) < 0; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::optional<QString> NotebookHost::read(const QString &page)
{
    const VaultPath path = pathOf(page);
    if (path.isEmpty())
        return std::nullopt;
    return m_index.content(path);
}

QVariantMap NotebookHost::page(const QString &page)
{
    const VaultPath path = pathOf(page);
    const ParsedNote *note = path.isEmpty() ? nullptr : m_index.note(path);
    if (!note)
        return {};
    QVariantMap map{{u"name"_s, note->title}, {u"tags"_s, pageTags(*note)}, {u"props"_s, propertiesMap(note->properties)}};
    if (note->journalDate)
        map.insert(u"date"_s, note->journalDate->toString(Qt::ISODate));
    return map;
}

QVariantMap NotebookHost::blockAt(const VaultPath &path, int line) const
{
    const ParsedNote *note = m_index.note(path);
    if (!note)
        return {};
    int found = -1;
    for (int i = 0; i < note->blocks.size(); ++i)
        if (note->blocks[i].line == line)
            found = i;
    if (found < 0)
        return {};
    return blockMap(path, *note, text::splitLines(m_index.content(path)), found, 0);
}

QVariantList NotebookHost::blocks(const BlockQuery &query)
{
    QVariantList out;
    if (!query.page.isEmpty()) {
        const VaultPath path = pathOf(query.page);
        const ParsedNote *note = path.isEmpty() ? nullptr : m_index.note(path);
        if (!note)
            return out;
        const QList<text::Line> lines = text::splitLines(m_index.content(path));
        for (int i = 0; i < note->blocks.size() && out.size() < m_options.maxBlocks; ++i)
            if (note->blocks[i].parent < 0)
                out << blockMap(path, *note, lines, i, 0);
        return out;
    }
    const QString tag = query.tag.normalized(QString::NormalizationForm_C).toLower();
    const QDate from = QDate::fromString(query.from, Qt::ISODate), to = QDate::fromString(query.to, Qt::ISODate);
    struct Hit
    {
        QDate date;
        VaultPath path;
        int block;
    };
    QList<Hit> hits;
    m_index.forEachNote([&](const VaultPath &path, const ParsedNote &note) {
        if (!note.tags.contains(tag))
            return;
        const bool journal = note.journalDate.has_value();
        if ((query.journalOnly || from.isValid() || to.isValid()) && !journal)
            return;
        if (journal && ((from.isValid() && *note.journalDate < from) || (to.isValid() && *note.journalDate > to)))
            return;
        for (int i = 0; i < note.blocks.size(); ++i)
            if (note.blocks[i].tags.contains(tag))
                hits.append({note.journalDate.value_or(QDate()), path, i});
    });
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.date != b.date)
            return a.date < b.date;
        return a.path != b.path ? a.path < b.path : a.block < b.block;
    });
    VaultPath linesOf;
    QList<text::Line> lines;
    for (const Hit &hit : std::as_const(hits)) {
        if (out.size() >= m_options.maxBlocks)
            break;
        const ParsedNote *note = m_index.note(hit.path);
        if (!note)
            continue;
        if (linesOf != hit.path) {
            linesOf = hit.path;
            lines = text::splitLines(m_index.content(hit.path));
        }
        out << blockMap(hit.path, *note, lines, hit.block, 0);
    }
    return out;
}

QString NotebookHost::writeFile(const QString &extension, const VaultPath &path, const QString &content)
{
    if (!m_vault.writeText(path, content, ChangeSource::User))
        return m_vault.errorString();
    if (m_options.updateIndex)
        m_index.update(path, content);
    if (onWritten)
        onWritten(extension, path);
    return {};
}

QString NotebookHost::write(const QString &extension, const QString &page, const QString &content)
{
    VaultPath path = pathOf(page);
    if (path.isEmpty()) {
        const notebook::CreatedPage created = notebook::createPage(m_vault, page, QString(), std::nullopt, content);
        if (created.path.isEmpty())
            return u"cannot create the page \"%1\""_s.arg(page);
        if (!created.existing) {
            if (m_options.updateIndex)
                m_index.update(created.path, m_vault.readText(created.path));
            if (onWritten)
                onWritten(extension, created.path);
            return {};
        }
        path = created.path;
    }
    return writeFile(extension, path, content);
}

QString NotebookHost::appendToJournal(const QString &extension, const QString &date, const QString &markdown)
{
    const QDate day = QDate::fromString(date, Qt::ISODate);
    const notebook::EnsuredJournal journal = notebook::ensureJournal(
            m_vault, day, now(), m_options.language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish);
    if (journal.path.isEmpty())
        return journal.error.isEmpty() ? u"cannot open the daily note of %1"_s.arg(date) : journal.error;
    bool ok = false;
    const QString content = m_vault.readText(journal.path, &ok);
    if (!ok)
        return m_vault.errorString();
    return writeFile(extension, journal.path, notebook::appendBlock(content, markdown));
}

QString NotebookHost::replaceBlock(const QString &extension, const QString &ref, const QString &text,
                                   const std::optional<QString> &task, QString *newRef)
{
    const std::optional<notebook::ItemRef> item = notebook::parseItemRef(ref);
    if (!item || !item->hasLine())
        return u"not a block reference: "_s + ref;
    bool ok = false;
    const QString content = m_vault.readText(item->page, &ok);
    if (!ok)
        return u"stale: the page is gone ("_s + ref + u")"_s;
    const notebook::ResolvedLine resolved = notebook::resolve(content, *item, m_vault.config());
    if (resolved.status != notebook::ResolvedLine::Ok && resolved.status != notebook::ResolvedLine::Moved)
        return u"stale: the block changed since it was read ("_s + ref + u")"_s;
    const std::optional<QString> edited = replaceBlockText(content, resolved.line, text, task, now().date());
    if (!edited)
        return u"cannot replace the line of "_s + ref;
    if (*edited != content) {
        const QString error = writeFile(extension, item->page, *edited);
        if (!error.isEmpty())
            return error;
    }
    if (newRef)
        *newRef = notebook::formatLineRef(item->page, resolved.line, rawLine(*edited, resolved.line));
    return {};
}

void NotebookHost::timer(const QString &extension, int seconds, const QString &label)
{
    if (onTimer)
        onTimer(extension, seconds, label);
}

void NotebookHost::log(const QString &extension, const QString &message)
{
    if (onLog)
        onLog(extension, message);
}

} // namespace brulion::extensions

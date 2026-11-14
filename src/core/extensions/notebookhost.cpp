#include "extensions/notebookhost.h"

#include "extensions/blockedit.h"
#include "extensions/sandbox.h"
#include "index/query.h"
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
    static const QRegularExpression separators(u"[,\\s]+"_s); // (made once: pages() asks for every note)
    QStringList raw;
    if (value.metaType().id() == QMetaType::QStringList || value.metaType().id() == QMetaType::QVariantList)
        raw = value.toStringList();
    else if (value.isValid())
        raw = value.toString().split(separators, Qt::SkipEmptyParts);
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
        return m_dryPages.value(text::foldKey(name));
    return *path;
}

NotebookHost::DryWrite *NotebookHost::dryWrite(const VaultPath &path)
{
    for (DryWrite &w : m_dryWrites)
        if (w.path == path)
            return &w;
    return nullptr;
}

QString NotebookHost::currentText(const VaultPath &path, bool *ok)
{
    if (const DryWrite *w = dryWrite(path)) {
        *ok = true;
        return w->after;
    }
    return m_vault.readText(path, ok);
}

QStringList NotebookHost::pages(const QString &tag)
{
    const QString wanted = tag.normalized(QString::NormalizationForm_C).toLower();
    QStringList out;
    m_index.forEachNote([&](const VaultPath &, const ParsedNote &note) {
        // (note.tags has every tag of the page, its blocks' too: the cheap test first.)
        if (wanted.isEmpty() || (note.tags.contains(wanted) && pageTags(note).contains(wanted)))
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
    if (const DryWrite *w = dryWrite(path))
        return w->after;
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

QVariantList NotebookHost::search(const QString &query, int limit)
{
    QVariantList out;
    const QList<SearchHit> hits = m_index.search(query, std::clamp(limit, 1, 200));
    for (const SearchHit &hit : hits) {
        const ParsedNote *note = m_index.note(hit.path);
        if (!note)
            continue;
        QVariantMap item{{u"page"_s, note->title}, {u"excerpt"_s, hit.excerpt}};
        if (note->journalDate)
            item.insert(u"date"_s, note->journalDate->toString(Qt::ISODate));
        if (hit.line >= 0) {
            item.insert(u"line"_s, hit.line + 1);
            // The block holding that line: the last one starting at it or above.
            int holding = -1;
            for (int i = 0; i < note->blocks.size(); ++i)
                if (note->blocks[i].line >= 0 && note->blocks[i].line <= hit.line
                    && (holding < 0 || note->blocks[i].line >= note->blocks[holding].line))
                    holding = i;
            if (holding >= 0) {
                const QVariantMap block =
                        blockMap(hit.path, *note, text::splitLines(m_index.content(hit.path)), holding, 64);
                for (const QString &key : {u"ref"_s, u"text"_s, u"task"_s})
                    if (block.contains(key))
                        item.insert(key, block.value(key));
            }
        }
        out << item;
    }
    return out;
}

QVariantMap NotebookHost::query(const QString &source, int limit, QString *error)
{
    const std::variant<Query, QString> parsed = parseQuery(source);
    if (const QString *message = std::get_if<QString>(&parsed)) {
        if (error)
            *error = *message;
        return {};
    }
    Query query = std::get<Query>(parsed);
    limit = std::clamp(limit, 1, 1000);
    if (!query.limit || *query.limit > limit)
        query.limit = limit;
    QueryOptions options;
    options.now = now().time();
    options.defaultLimit = limit;
    const QueryResult result = runQuery(query, m_index, now().date(), Qt::Monday, options);
    QVariantList items;
    VaultPath linesOf;
    QList<text::Line> lines;
    for (const QueryItem &item : result.items) {
        const ParsedNote *note = m_index.note(item.path);
        if (!note)
            continue;
        if (item.blockIndex < 0 || item.blockIndex >= note->blocks.size()) {
            QVariantMap page{{u"page"_s, note->title}};
            if (note->journalDate)
                page.insert(u"date"_s, note->journalDate->toString(Qt::ISODate));
            items << page;
            continue;
        }
        if (linesOf != item.path) {
            linesOf = item.path;
            lines = text::splitLines(m_index.content(item.path));
        }
        // (Depth 64: the block without its subtree.)
        QVariantMap block = blockMap(item.path, *note, lines, item.blockIndex, 64);
        block.remove(u"children"_s);
        if (item.task) {
            if (item.task->due)
                block.insert(u"due"_s, item.task->due->toString());
            if (item.task->scheduled)
                block.insert(u"scheduled"_s, item.task->scheduled->toString());
            if (item.task->start)
                block.insert(u"start"_s, item.task->start->toString());
        }
        if (item.reminder)
            block.insert(u"reminder"_s, item.reminder->at.toString());
        items << block;
    }
    return {{u"total"_s, result.total}, {u"items"_s, items}};
}

QVariantMap NotebookHost::checkExtension(const QString &page, const QString &content)
{
    VaultPath path = pathOf(page);
    if (path.isEmpty())
        path = notebook::plannedPage(m_vault, page).path;
    if (path.isEmpty())
        return {{u"ok"_s, false}, {u"error"_s, u"no page name"_s}};
    Options options = m_options;
    options.updateIndex = false;
    const QDateTime when = now();
    options.now = [when] { return when; };
    return checkExtensionPage(m_vault, m_index, path, content, options, Limits{1000, 32ll << 20});
}

QString NotebookHost::writeFile(const QString &extension, const VaultPath &path, const QString &content)
{
    if (m_options.dryRun) {
        if (DryWrite *w = dryWrite(path)) {
            w->after = content;
        } else {
            bool ok = false;
            const QString before = m_vault.readText(path, &ok);
            m_dryWrites.append({path, ok ? before : QString(), content, ok && m_vault.exists(path)});
        }
        if (onWritten)
            onWritten(extension, path);
        return {};
    }
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
    if (path.isEmpty() && m_options.dryRun) {
        const notebook::CreatedPage planned = notebook::plannedPage(m_vault, page);
        if (planned.path.isEmpty())
            return u"cannot create the page \"%1\""_s.arg(page);
        if (!planned.existing) {
            m_dryWrites.append({planned.path, QString(), notebook::newPageText(m_vault, planned.path, std::nullopt, content), false});
            m_dryPages.insert(text::foldKey(page.trimmed()), planned.path);
            if (onWritten)
                onWritten(extension, planned.path);
            return {};
        }
        path = planned.path;
    }
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
    const dates::UiLocale locale = m_options.language == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    if (m_options.dryRun) {
        const VaultPath path = notebook::journalFileFor(m_vault, day);
        if (path.isEmpty())
            return u"cannot open the daily note of %1"_s.arg(date);
        bool ok = false;
        QString content = currentText(path, &ok);
        if (!ok || (!dryWrite(path) && !m_vault.exists(path)))
            content = notebook::newJournalText(m_vault, day, now(), locale);
        return writeFile(extension, path, notebook::appendBlock(content, markdown));
    }
    const notebook::EnsuredJournal journal = notebook::ensureJournal(
            m_vault, day, now(), locale);
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
    // A block's ref as blocks(), search() and query() give it: a note's path, a line and the hash
    // of that line's text. A script can write any text as a ref, so nothing else is taken: a ref
    // without the hash would overwrite a line nobody read, and a path that is no note of the
    // notebook would reach the other files of its folder (.git/config, .brulion/config.json, the
    // attachments) — an extension allowed to change notes changes notes.
    const std::optional<notebook::ItemRef> item = notebook::parseItemRef(ref);
    if (!item || !item->hasLine() || item->lineHash.isEmpty())
        return u"not a block reference: "_s + ref;
    const VaultPath path = paths::normalize(item->page);
    if (!paths::isMarkdown(path) || paths::isIgnored(path))
        return u"not a block reference: "_s + ref;
    if (!m_index.note(path) && !dryWrite(path))
        return u"stale: the page is gone ("_s + ref + u")"_s;
    bool ok = false;
    const QString content = currentText(path, &ok);
    if (!ok)
        return u"stale: the page is gone ("_s + ref + u")"_s;
    const notebook::ResolvedLine resolved = notebook::resolve(content, *item, m_vault.config());
    if (resolved.status != notebook::ResolvedLine::Ok && resolved.status != notebook::ResolvedLine::Moved)
        return u"stale: the block changed since it was read ("_s + ref + u")"_s;
    const std::optional<QString> edited = replaceBlockText(content, resolved.line, text, task, now().date());
    if (!edited)
        return u"cannot replace the line of "_s + ref;
    if (*edited != content) {
        const QString error = writeFile(extension, path, *edited);
        if (!error.isEmpty())
            return error;
    }
    if (newRef)
        *newRef = notebook::formatLineRef(path, resolved.line, rawLine(*edited, resolved.line));
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

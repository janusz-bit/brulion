#include "extensions/notebookhost.h"

#include "extensions/appapi.h"
#include "extensions/blockedit.h"
#include "extensions/pageextension.h"
#include "extensions/sandbox.h"
#include "index/query.h"
#include "index/vaultindex.h"
#include "markdown/edit.h"
#include "markdown/parse.h"
#include "markdown/tasks.h"
#include "notebook/itemref.h"
#include "notebook/journals.h"
#include "notebook/json.h"
#include "notebook/pages.h"
#include "text/text.h"
#include "util/paths.h"
#include "util/unicode.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QJsonDocument>
#include <QJsonObject>

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
        tag = unicode::nfc(tag).toLower();
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
        {u"path"_s, path},
        {u"line"_s, block.line + 1},
        {u"text"_s, blockLineText(line)},
        {u"tags"_s, block.tags},
        {u"props"_s, propertiesMap(block.properties)},
        // What the block says: its text without task fields (dates, priority, ids), tags and with
        // links as their labels.
        {u"plain"_s, plainWords(block.task ? block.task->text : block.text)},
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

QDateTime NotebookHost::clock() const { return m_options.now ? m_options.now() : QDateTime::currentDateTime(); }

QString NotebookHost::today() { return clock().date().toString(Qt::ISODate); }

QString NotebookHost::now() { return clock().toString(u"yyyy-MM-dd HH:mm"_s); }

int NotebookHost::weekStart() { return int(m_index.config().weekStartsOn); }

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
    const QString wanted = unicode::nfc(tag).toLower();
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
    QVariantMap map{{u"name"_s, note->title},
                    {u"path"_s, path},
                    {u"tags"_s, pageTags(*note)},
                    {u"props"_s, propertiesMap(note->properties)}};
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
    const QString tag = unicode::nfc(query.tag).toLower();
    QStringList props;
    for (const QString &key : query.props)
        if (const QString k = key.trimmed().toLower(); !k.isEmpty() && !props.contains(k))
            props << k;
    // A block that carries the tag itself and has one of the properties (each when asked for).
    const auto matches = [&](const Block &block) {
        if (!tag.isEmpty() && !block.tags.contains(tag))
            return false;
        if (props.isEmpty())
            return true;
        if (block.kind == BlockKind::Frontmatter || block.properties.isEmpty())
            return false;
        return std::ranges::any_of(props, [&](const QString &k) { return block.properties.contains(k); });
    };
    const bool filtered = !tag.isEmpty() || !props.isEmpty();
    if (!query.page.isEmpty()) {
        const VaultPath path = pathOf(query.page);
        const ParsedNote *note = path.isEmpty() ? nullptr : m_index.note(path);
        if (!note)
            return out;
        const QList<text::Line> lines = text::splitLines(m_index.content(path));
        for (int i = 0; i < note->blocks.size() && out.size() < m_options.maxBlocks; ++i)
            if (filtered ? matches(note->blocks[i]) : note->blocks[i].parent < 0)
                out << blockMap(path, *note, lines, i, 0);
        return out;
    }
    const QDate from = QDate::fromString(query.from, Qt::ISODate), to = QDate::fromString(query.to, Qt::ISODate);
    struct Hit
    {
        QDate date;
        VaultPath path;
        int block;
    };
    QList<Hit> hits;
    m_index.forEachNote([&](const VaultPath &path, const ParsedNote &note) {
        if (!tag.isEmpty() && !note.tags.contains(tag))
            return;
        const bool journal = note.journalDate.has_value();
        if ((query.journalOnly || from.isValid() || to.isValid()) && !journal)
            return;
        if (journal && ((from.isValid() && *note.journalDate < from) || (to.isValid() && *note.journalDate > to)))
            return;
        for (int i = 0; i < note.blocks.size(); ++i)
            if (matches(note.blocks[i]))
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

QStringList NotebookHost::findPages(const PageQuery &query)
{
    const QString wanted = unicode::nfc(query.tag).toLower();
    QStringList props;
    for (const QString &key : query.props)
        if (const QString k = key.trimmed().toLower(); !k.isEmpty())
            props << k;
    QStringList out;
    m_index.forEachNote([&](const VaultPath &path, const ParsedNote &note) {
        if (!wanted.isEmpty() && !(note.tags.contains(wanted) && pageTags(note).contains(wanted)))
            return;
        if (!props.isEmpty() && std::ranges::none_of(props, [&](const QString &k) { return note.properties.contains(k); }))
            return;
        // (A path is one page: two pages of one name in different folders are two paths.)
        out << (query.paths ? QString(path) : note.title);
    });
    std::stable_sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return text::compareNames(a, b) < 0; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

QStringList NotebookHost::attachments()
{
    QStringList out;
    for (const VaultPath &path : m_index.attachments())
        if (!paths::isIgnored(path))
            out << path;
    std::sort(out.begin(), out.end());
    return out;
}

qint64 NotebookHost::revision() { return qint64(m_index.version()) + (m_writes << 32); }

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
    return queryPage(source, limit, 0, error);
}

QVariantMap NotebookHost::queryPage(const QString &source, int limit, int offset, QString *error)
{
    const std::variant<Query, QString> parsed = parseQuery(source);
    if (const QString *message = std::get_if<QString>(&parsed)) {
        if (error)
            *error = *message;
        return {};
    }
    Query query = std::get<Query>(parsed);
    limit = std::clamp(limit, 1, 1000);
    offset = std::max(0, offset);
    // A smaller `limit:` of the query itself ends the items there, the pages after it too.
    const int last = query.limit ? std::min(*query.limit, offset + limit) : offset + limit;
    query.limit = last;
    QueryOptions options;
    options.now = clock().time();
    options.defaultLimit = last;
    const QueryResult result = runQuery(query, m_index, clock().date(), m_index.config().weekStartsOn, options);
    QVariantList items;
    VaultPath linesOf;
    QList<text::Line> lines;
    for (qsizetype i = offset; i < result.items.size(); ++i) {
        const QueryItem &item = result.items[i];
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
        // As docs/EXTENSIONS.md "Blocks" says: children, path and line are b.blocks' and ctx.block's.
        block.remove(u"children"_s);
        block.remove(u"path"_s);
        block.remove(u"line"_s);
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
    const QDateTime when = clock();
    options.now = [when] { return when; };
    return checkExtensionPage(m_vault, m_index, path, content, options, Limits{1000, 32ll << 20});
}

QString NotebookHost::writeFile(const QString &extension, const VaultPath &path, const QString &content)
{
    ++m_writes;
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
            ++m_writes;
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
            ++m_writes;
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
            content = notebook::newJournalText(m_vault, day, clock(), locale);
        return writeFile(extension, path, notebook::appendBlock(content, markdown));
    }
    const notebook::EnsuredJournal journal = notebook::ensureJournal(
            m_vault, day, clock(), locale);
    if (journal.path.isEmpty())
        return journal.error.isEmpty() ? u"cannot open the daily note of %1"_s.arg(date) : journal.error;
    bool ok = false;
    const QString content = m_vault.readText(journal.path, &ok);
    if (!ok)
        return m_vault.errorString();
    return writeFile(extension, journal.path, notebook::appendBlock(content, markdown));
}

QString NotebookHost::resolveRef(const QString &ref, VaultPath *path, QString *content, int *line)
{
    // A block's ref as blocks(), search() and query() give it: a note's path, a line and the hash
    // of that line's text. A script can write any text as a ref, so nothing else is taken: a ref
    // without the hash would overwrite a line nobody read, and a path that is no note of the
    // notebook would reach the other files of its folder (.git/config, .brulion/config.json, the
    // attachments) — an extension allowed to change notes changes notes.
    const std::optional<notebook::ItemRef> item = notebook::parseItemRef(ref);
    if (!item || !item->hasLine() || item->lineHash.isEmpty())
        return u"not a block reference: "_s + ref;
    *path = paths::normalize(item->page);
    if (!paths::isMarkdown(*path) || paths::isIgnored(*path))
        return u"not a block reference: "_s + ref;
    if (!m_index.note(*path) && !dryWrite(*path))
        return u"stale: the page is gone ("_s + ref + u")"_s;
    bool ok = false;
    *content = currentText(*path, &ok);
    if (!ok)
        return u"stale: the page is gone ("_s + ref + u")"_s;
    const notebook::ResolvedLine resolved = notebook::resolve(*content, *item, m_vault.config());
    if (resolved.status != notebook::ResolvedLine::Ok && resolved.status != notebook::ResolvedLine::Moved)
        return u"stale: the block changed since it was read ("_s + ref + u")"_s;
    *line = resolved.line;
    return {};
}

QString NotebookHost::replaceBlock(const QString &extension, const QString &ref, const QString &text,
                                   const std::optional<QString> &task, QString *newRef)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    const std::optional<QString> edited = replaceBlockText(content, line, text, task, clock().date());
    if (!edited)
        return u"cannot replace the line of "_s + ref;
    if (*edited != content) {
        const QString error = writeFile(extension, path, *edited);
        if (!error.isEmpty())
            return error;
    }
    if (newRef)
        *newRef = notebook::formatLineRef(path, line, rawLine(*edited, line));
    return {};
}

namespace {

// The block whose first line is `line` in `content` parsed as the note at `path`, or nullopt.
std::optional<Block> blockOfLine(const VaultPath &path, const QString &content, const VaultConfig &config, int line)
{
    const ParsedNote note = markdown::parseNote(path, content, config);
    for (const Block &block : note.blocks)
        if (block.line == line && block.kind != BlockKind::Frontmatter)
            return block;
    return std::nullopt;
}

} // namespace

QString NotebookHost::appendToPage(const QString &extension, const QString &page, const QString &markdown)
{
    if (markdown.trimmed().isEmpty())
        return u"nothing to append"_s;
    const VaultPath path = pathOf(page);
    if (path.isEmpty())
        return write(extension, page, notebook::appendBlock(QString(), markdown));
    bool ok = false;
    const QString content = currentText(path, &ok);
    if (!ok)
        return m_vault.errorString();
    return writeFile(extension, path, notebook::appendBlock(content, markdown));
}

QString NotebookHost::insertBlock(const QString &extension, const QString &ref, const QString &markdown, InsertAt where,
                                  QString *newRef)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    const std::optional<Block> block = blockOfLine(path, content, m_vault.config(), line);
    if (!block)
        return u"stale: the block changed since it was read ("_s + ref + u")"_s;
    // As a child: indented as the block's first child is, beyond the block's own indentation.
    QString childIndent;
    const QList<text::Line> lines = text::splitLines(content);
    if (where == InsertAt::Child && block->endLine > block->ownEndLine) {
        const ParsedNote note = markdown::parseNote(path, content, m_vault.config());
        for (const Block &b : note.blocks) {
            if (b.line > block->ownEndLine && b.line <= block->endLine && b.parent >= 0
                && note.blocks[b.parent].line == line && b.line < lines.size()) {
                const QString own = lines[line].text, child = lines[b.line].text;
                qsizetype a = 0, c = 0;
                while (a < own.size() && (own[a] == u' ' || own[a] == u'\t'))
                    ++a;
                while (c < child.size() && (child[c] == u' ' || child[c] == u'\t'))
                    ++c;
                if (c > a)
                    childIndent = child.mid(a, c - a);
                break;
            }
        }
    }
    const Placement placement = where == InsertAt::Before ? Placement::Before
                                : where == InsertAt::Child ? Placement::Child
                                                           : Placement::After;
    int first = -1;
    const std::optional<QString> edited =
            insertBlockLines(content, line, block->endLine, markdown, placement, childIndent, &first);
    if (!edited)
        return u"nothing to insert"_s;
    if (const QString error = writeFile(extension, path, *edited); !error.isEmpty())
        return error;
    if (newRef)
        *newRef = notebook::formatLineRef(path, first, rawLine(*edited, first));
    return {};
}

QString NotebookHost::removeBlock(const QString &extension, const QString &ref)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    const std::optional<Block> block = blockOfLine(path, content, m_vault.config(), line);
    if (!block)
        return u"stale: the block changed since it was read ("_s + ref + u")"_s;
    const std::optional<QString> edited = removeBlockLines(content, line, block->endLine);
    if (!edited)
        return u"cannot remove the block of "_s + ref;
    return writeFile(extension, path, *edited);
}

QString NotebookHost::setTask(const QString &extension, const QString &ref, const QString &status, QString *newRef)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    const TaskStatus wanted = markdown::statusFromCheckbox(status.at(0));
    const std::optional<QString> current = taskCharacter(rawLine(content, line));
    QString edited = content;
    if (!current || markdown::statusFromCheckbox(current->at(0)) != wanted)
        edited = markdown::setTaskStatus(content, line, wanted, clock().date());
    if (edited != content) {
        if (const QString error = writeFile(extension, path, edited); !error.isEmpty())
            return error;
    }
    // Completing a recurring task puts its next occurrence above it: the task set is lower now.
    const int added = int(text::splitLines(edited).size() - text::splitLines(content).size());
    for (int i = line; added > 0 && i <= line + added; ++i) {
        const std::optional<QString> c = taskCharacter(rawLine(edited, i));
        if (c && markdown::statusFromCheckbox(c->at(0)) == wanted) {
            line = i;
            break;
        }
    }
    if (newRef)
        *newRef = notebook::formatLineRef(path, line, rawLine(edited, line));
    return {};
}

QString NotebookHost::setProperty(const QString &extension, const QString &ref, const QString &key,
                                  const QString &value, QString *newRef)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    const std::optional<Block> block = blockOfLine(path, content, m_vault.config(), line);
    if (!block)
        return u"stale: the block changed since it was read ("_s + ref + u")"_s;
    const std::optional<QString> edited = setBlockProperty(content, line, block->ownEndLine, key, value);
    if (!edited)
        return u"cannot set the property \"%1\""_s.arg(key);
    if (*edited != content) {
        if (const QString error = writeFile(extension, path, *edited); !error.isEmpty())
            return error;
    }
    if (newRef)
        *newRef = notebook::formatLineRef(path, line, rawLine(*edited, line));
    return {};
}

QString NotebookHost::setProperties(const QString &extension, const QString &ref,
                                    const QList<std::pair<QString, QString>> &props, QString *newRef)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    QString edited = content;
    for (const auto &[key, value] : props) {
        // The block again after each property: a line added moves the end of its own lines.
        const std::optional<Block> block = blockOfLine(path, edited, m_vault.config(), line);
        if (!block)
            return u"stale: the block changed since it was read ("_s + ref + u")"_s;
        const std::optional<QString> next = setBlockProperty(edited, line, block->ownEndLine, key, value);
        if (!next)
            return u"cannot set the property \"%1\""_s.arg(key);
        edited = *next;
    }
    if (edited != content) {
        if (const QString error = writeFile(extension, path, edited); !error.isEmpty())
            return error;
    }
    if (newRef)
        *newRef = notebook::formatLineRef(path, line, rawLine(edited, line));
    return {};
}

std::optional<QVariantList> NotebookHost::lines(const QString &page)
{
    const VaultPath path = pathOf(page);
    if (path.isEmpty())
        return std::nullopt;
    const std::optional<QString> content = read(page);
    if (!content)
        return std::nullopt;
    QList<text::Line> split = text::splitLines(*content);
    // A note ending with a line break has no line after it.
    if (split.size() > 1 && split.last().text.isEmpty() && split.last().eol.isEmpty())
        split.removeLast();
    QVariantList out;
    out.reserve(split.size());
    for (qsizetype i = 0; i < split.size(); ++i) {
        QString text = split[i].text;
        if (i == 0 && text.startsWith(QChar(0xFEFF)))
            text.remove(0, 1); // the file's BOM, no character of the line
        out << QVariantMap{{u"text"_s, text}, {u"ref"_s, notebook::formatLineRef(path, int(i), split[i].text)}};
    }
    return out;
}

QString NotebookHost::replaceLine(const QString &extension, const QString &ref, const QString &text, QString *newRef)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    const std::optional<QString> edited = replaceLineText(content, line, text);
    if (!edited)
        return u"cannot replace the line of "_s + ref;
    if (*edited != content) {
        if (const QString error = writeFile(extension, path, *edited); !error.isEmpty())
            return error;
    }
    if (newRef)
        *newRef = notebook::formatLineRef(path, line, rawLine(*edited, line));
    return {};
}

QString NotebookHost::insertLines(const QString &extension, const QString &ref, const QString &text, bool before,
                                  QString *newRef)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    int first = -1;
    const std::optional<QString> edited = insertLinesAt(content, line, text, before, &first);
    if (!edited)
        return u"cannot insert at the line of "_s + ref;
    if (const QString error = writeFile(extension, path, *edited); !error.isEmpty())
        return error;
    if (newRef)
        *newRef = notebook::formatLineRef(path, first, rawLine(*edited, first));
    return {};
}

QString NotebookHost::removeLine(const QString &extension, const QString &ref)
{
    VaultPath path;
    QString content;
    int line = -1;
    if (const QString error = resolveRef(ref, &path, &content, &line); !error.isEmpty())
        return error;
    const std::optional<QString> edited = removeLineAt(content, line);
    if (!edited)
        return u"cannot remove the line of "_s + ref;
    return writeFile(extension, path, *edited);
}

void NotebookHost::timer(const QString &extension, int seconds, const QString &label)
{
    if (onTimer)
        onTimer(extension, seconds, label);
}

void NotebookHost::startTimer(const QString &extension, const TimerSpec &spec)
{
    if (onExtensionTimer)
        onExtensionTimer(extension, spec);
    else
        timer(extension, spec.seconds, spec.label);
}

void NotebookHost::stopTimer(const QString &extension)
{
    if (onExtensionTimer)
        onExtensionTimer(extension, TimerSpec{});
}

QString NotebookHost::notify(const QString &extension, const NotifySpec &spec)
{
    if (!onNotify)
        return u"not available here"_s;
    return onNotify(extension, spec);
}

ModuleSource NotebookHost::module(const QString &requester, const QString &name)
{
    ModuleSource out;
    const VaultPath path = pathOf(name);
    if (path.isEmpty()) {
        out.error = u"no page named “%1”"_s.arg(name);
        return out;
    }
    // A page the dry run wrote is read as it wrote it (an extension writing its library first).
    bool ok = false;
    const QString content = currentText(path, &ok);
    const ParsedNote *indexed = dryWrite(path) ? nullptr : m_index.note(path);
    const ParsedNote parsed = indexed ? ParsedNote() : markdown::parseNote(path, content, m_vault.config());
    const ParsedNote &note = indexed ? *indexed : parsed;
    if (!ok || !isExtensionPage(note)) {
        out.error = u"“%1” is no extension page (tags: [extension])"_s.arg(note.title.isEmpty() ? name : note.title);
        return out;
    }
    const PageExtension page = readPageExtension(path, note, content);
    if (page.blocks == 0) {
        out.error = u"“%1” has no ```luau block"_s.arg(page.title);
        return out;
    }
    if (page.id == requester) {
        out.error = u"“%1” is the extension's own page"_s.arg(page.title);
        return out;
    }
    if (m_options.moduleAllowed) {
        if (const QString why = m_options.moduleAllowed(path, page.hash); !why.isEmpty()) {
            out.error = u"“%1” %2"_s.arg(page.title, why);
            return out;
        }
    }
    out.id = page.id;
    out.title = page.title;
    out.path = path;
    out.source = page.source;
    out.hash = page.hash;
    return out;
}

QVariantMap NotebookHost::settings(const QString &extension)
{
    if (const auto dry = m_drySettings.constFind(extension); dry != m_drySettings.constEnd())
        return *dry;
    LocalStore *store = m_vault.store();
    if (!store)
        return {};
    return store->get(settingsStoreKey(extension)).toObject().toVariantMap();
}

QString NotebookHost::setSetting(const QString &extension, const QString &key, const QVariant &value)
{
    QVariantMap values = settings(extension);
    if (!value.isValid() || value.isNull())
        values.remove(key);
    else
        values.insert(key, value);
    const QJsonObject object = QJsonObject::fromVariantMap(values);
    if (QJsonDocument(object).toJson(QJsonDocument::Compact).size() > kMaxSettingsBytes)
        return u"the extension's settings are full (%1 KB)"_s.arg(kMaxSettingsBytes / 1024);
    if (m_options.dryRun) {
        m_drySettings.insert(extension, values);
        return {};
    }
    LocalStore *store = m_vault.store();
    if (!store)
        return u"this notebook keeps no settings"_s;
    store->set(settingsStoreKey(extension), values.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(object));
    if (onSettingChanged)
        onSettingChanged(extension);
    return {};
}

void NotebookHost::log(const QString &extension, const QString &message)
{
    if (onLog)
        onLog(extension, message);
}

void NotebookHost::status(const QString &extension, const QString &text)
{
    if (onStatus)
        onStatus(extension, text);
}

QVariant NotebookHost::app(const QString &extension, const QString &operation, const QVariantMap &args, QString *error)
{
    if (onApp)
        return onApp(extension, operation, args, error);
    if (!m_options.dryRun)
        return Host::app(extension, operation, args, error);
    // A dry run: no app, but an extension tried out on the command line sees one that shows
    // nothing, and what it would change is noted.
    if (operation == u"state")
        return QVariantMap{{u"view"_s, QString()},    {u"route"_s, QVariantMap()},
                           {u"platform"_s, u"cli"_s}, {u"touch"_s, false},
                           {u"narrow"_s, false},      {u"dark"_s, false},
                           {u"width"_s, 0},           {u"height"_s, 0},
                           {u"sidePanel"_s, false},   {u"language"_s, m_options.uiLanguage},
                           {u"version"_s, QString()}};
    // The notebook's own settings are there without the app (.brulion/config.json); a device's are not.
    if (operation == u"commands")
        return QVariantList();
    if (operation == u"settings")
        return notebookSettingRows(notebook::vaultConfigJson(m_vault.config()).toVariantMap());
    if (operation == u"get") {
        const QString key = args.value(u"key"_s).toString();
        if (const NotebookSetting *setting = notebookSetting(key))
            return notebookSettingValue(*setting, notebook::vaultConfigJson(m_vault.config()).toVariantMap());
        *error = u"no setting “%1” here: brulion-cli runs extensions without the app, so only the notebook's own "
                 u"settings are there (b.app.settings())"_s.arg(key);
        return {};
    }
    QVariantMap asked = args;
    asked.insert(u"extension"_s, extension);
    asked.insert(u"operation"_s, operation);
    if (m_dryApp.size() < 1000)
        m_dryApp << asked;
    return {};
}

void NotebookHost::appChanged(const QString &extension)
{
    if (onAppChanged)
        onAppChanged(extension);
}

} // namespace brulion::extensions

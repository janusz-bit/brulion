// The objects rows are made of: pages (their fields and the implicit `file`), list items and
// tasks — each field computed when a query first reads it, once per run (Context).
#include "dataview/eval_p.h"

#include "markdown/properties.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

namespace {

using Fields = QList<std::pair<QString, Value>>;

/// The value of `key` in `fields`, or nullopt.
std::optional<Value> fieldIn(const Fields &fields, const QString &key)
{
    const auto it = std::ranges::find(fields, key, &Fields::value_type::first);
    if (it == fields.cend())
        return std::nullopt;
    return it->second;
}

/// Add `value` under `key`; a key written again holds a list of every value.
void addField(Fields &fields, const QString &key, const Value &value)
{
    const auto it = std::ranges::find(fields, key, &Fields::value_type::first);
    if (it == fields.end()) {
        fields.append({key, value});
        return;
    }
    ValueList items = it->second.is(Value::Type::List) ? it->second.asList() : ValueList{it->second};
    if (value.is(Value::Type::List))
        items.append(value.asList());
    else
        items.append(value);
    it->second = Value::list(std::move(items));
}

/// Keys whose text values are names: tags (written with '#') and aliases.
bool isNamesKey(const QString &key)
{
    const QString k = canonicalKey(key);
    return k == u"tags" || k == u"tag" || k == u"aliases" || k == u"alias";
}

/// `[[Page]]` written without quotes, which YAML reads as a list holding a list holding "Page".
std::optional<Value> bareLink(const QVariant &v, const LinkResolver &resolve)
{
    if (v.typeId() != QMetaType::QVariantList)
        return std::nullopt;
    const QVariantList inner = v.toList();
    if (inner.size() != 1 || inner.first().typeId() != QMetaType::QString)
        return std::nullopt;
    return Value::link(resolve(inner.first().toString(), QString(), QString(), false));
}

/// A frontmatter value as Dataview reads it: strings may be links or dates, `[[Page]]` written
/// without quotes (a nested YAML list) is a link, maps are objects.
Value fromYaml(const QString &key, const QVariant &v, const LinkResolver &resolve, int depth = 0)
{
    if (depth > 16 || !v.isValid() || v.isNull())
        return Value();
    switch (v.typeId()) {
    case QMetaType::Bool: return Value::boolean(v.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double: return Value::number(v.toDouble());
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        const QVariantList list = v.toList();
        // `up: [[Dom]]` is a list holding a list holding "Dom".
        if (list.size() == 1) {
            if (auto link = bareLink(list.first(), resolve))
                return *std::move(link);
        }
        // `tags: [a, b]` is one list of names (each string item reads as a list of its names).
        const bool names = isNamesKey(key);
        ValueList items;
        for (const QVariant &item : list) {
            if (auto link = bareLink(item, resolve)) {
                items.append(*std::move(link));
                continue;
            }
            const Value value = fromYaml(key, item, resolve, depth + 1);
            if (names && value.is(Value::Type::List))
                items.append(value.asList());
            else
                items.append(value);
        }
        return Value::list(items);
    }
    case QMetaType::QVariantMap: {
        Fields fields;
        const QVariantMap map = v.toMap();
        for (auto [name, value] : map.asKeyValueRange())
            fields.append({canonicalKey(name), fromYaml(name, value, resolve, depth + 1)});
        return Value::map(fields);
    }
    default: break;
    }
    const QString s = v.toString();
    if (isNamesKey(key))
        return parseFieldValue(key, s, resolve);
    if (const auto link = parseLinkText(s, resolve))
        return *link;
    if (const auto date = parseDate(s))
        return Value::date(*date);
    return Value::string(s);
}

/// The text of a block line without its indentation, list marker, checkbox and quote marks.
QStringView lineBody(QStringView line)
{
    static const QRegularExpression lead(u"^\\s*(?:>\\s*)*(?:(?:[-*+]|\\d+[.)])\\s+)?(?:\\[[^\\]]?\\]\\s+)?"_s);
    const QRegularExpressionMatch m = lead.matchView(line);
    return m.hasMatch() ? line.sliced(m.capturedLength()) : line;
}

/// The `key:: value` fields of a block's own lines: whole lines and `[key:: value]` /
/// `(key:: value)` inside a line, in the order written.
Fields inlineFields(const Block &block, const LinkResolver &resolve)
{
    Fields out;
    const QStringView raw = block.raw;
    // (Leading `key:: value` lines are a frontmatter block too; YAML is read from the note's data.)
    if (block.kind == BlockKind::Code || block.kind == BlockKind::Html
        || (block.kind == BlockKind::Frontmatter && raw.trimmed().startsWith(u"---")))
        return out;
    if (!raw.contains(u"::"))
        return out;
    // (The value keeps its trailing blanks, parseFieldValue() trims it: a lazy value before `\s*$`
    // would make a long run of blanks inside a line quadratic.)
    static const QRegularExpression whole(u"^([^\\s:\\[\\]()][^:\\[\\]()]{0,80}?)::(?:\\s+(.*))?$"_s);
    // (Values bounded: a long line of "[a:: " synced from anywhere stays linear.)
    static const QRegularExpression bracket(
            u"\\[([^\\[\\]:]{1,80}?)::\\s*([^\\]\\n]{0,500}?)\\s*\\]|\\(([^()\\[\\]:]{1,80}?)::\\s*([^)\\n]{0,500}?)\\s*\\)"_s);
    for (QStringView line : raw.tokenize(u'\n')) {
        if (!line.contains(u"::"))
            continue;
        if (line.endsWith(u'\r'))
            line.chop(1);
        const QStringView body = lineBody(line);
        const QRegularExpressionMatch m = whole.matchView(body);
        // A whole-line field: not inside a bracket of its own, not a URL ("http://" has one ':').
        if (m.hasMatch() && !body.startsWith(u'[') && !body.startsWith(u'(')) {
            const QString key = canonicalKey(m.capturedView(1));
            if (!key.isEmpty() && key != u"collapsed" && !(key == u"id" && !block.id.isEmpty()))
                addField(out, key, parseFieldValue(key, m.capturedView(2), resolve));
            continue;
        }
        auto it = bracket.globalMatchView(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch b = it.next();
            const bool square = b.capturedStart(1) >= 0;
            const QString key = canonicalKey(b.capturedView(square ? 1 : 3));
            if (!key.isEmpty())
                addField(out, key, parseFieldValue(key, b.capturedView(square ? 2 : 4), resolve));
        }
    }
    return out;
}

LinkResolver resolverFor(Context *ctx, const VaultPath &from)
{
    return [ctx, from](const QString &target, const QString &display, const QString &subpath, bool embed) {
        return ctx->resolveLink(target, display, subpath, embed, from);
    };
}

QString tagValue(Context &ctx, const QString &tag)
{
    return u'#' + ctx.tagDisplay(tag);
}

/// A note's links on lines [from, to] (all of them for from < 0), unique, in document order.
ValueList linksIn(Context &ctx, const VaultPath &path, int from, int to)
{
    ValueList out;
    QSet<QString> seen;
    for (const QList<ResolvedLink> links = ctx.index().outgoingLinks(path); const ResolvedLink &l : links) {
        if (from >= 0 && (l.link.line < from || l.link.line > to))
            continue;
        LinkValue v;
        v.resolved = !l.target.isEmpty();
        v.path = v.resolved ? l.target : l.link.target;
        v.display = l.link.kind == LinkKind::Markdown ? QString() : l.link.display;
        const bool toBlock = l.link.kind == LinkKind::BlockRef || !l.link.blockId.isEmpty();
        v.subpath = toBlock ? u'^' + l.link.blockId : l.link.heading;
        v.embed = l.link.kind == LinkKind::Embed || l.link.kind == LinkKind::BlockEmbed;
        const QString key = v.path + u'#' + v.subpath;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(Value::link(v));
    }
    return out;
}

/// A date in a file name: yyyy-MM-dd or yyyyMMdd.
std::optional<QDate> dateInName(const QString &name)
{
    static const QRegularExpression dashed(u"(\\d{4})-(\\d{2})-(\\d{2})"_s);
    static const QRegularExpression compact(u"(?<!\\d)(\\d{4})(\\d{2})(\\d{2})(?!\\d)"_s);
    for (const QRegularExpression *re : {&dashed, &compact}) {
        if (const QRegularExpressionMatch m = re->match(name); m.hasMatch()) {
            const QDate d(m.capturedView(1).toInt(), m.capturedView(2).toInt(), m.capturedView(3).toInt());
            if (d.isValid())
                return d;
        }
    }
    return std::nullopt;
}

/// The item objects of the blocks of `path` that `keep` accepts, in document order.
template<typename Keep>
ValueList itemsOf(Context &ctx, const VaultPath &path, Keep keep)
{
    ValueList out;
    if (const ParsedNote *note = ctx.index().note(path)) {
        for (qsizetype i = 0; i < note->blocks.size(); ++i) {
            if (keep(note->blocks.at(i)))
                out.append(ctx.item(path, int(i)));
        }
    }
    return out;
}

// ---- file ------------------------------------------------------------------------------------------

class FileObject : public Object {
public:
    FileObject(Context *ctx, VaultPath path, const ParsedNote *note, std::weak_ptr<const Object> page)
        : m_ctx(ctx), m_path(std::move(path)), m_note(note), m_page(std::move(page))
    { }

    QStringList keys() const override
    {
        return {u"name"_s,    u"folder"_s, u"path"_s,  u"ext"_s,     u"link"_s,     u"size"_s,    u"ctime"_s,
                u"cday"_s,    u"mtime"_s,  u"mday"_s,  u"day"_s,     u"tags"_s,     u"etags"_s,   u"inlinks"_s,
                u"outlinks"_s, u"aliases"_s, u"tasks"_s, u"lists"_s, u"frontmatter"_s, u"starred"_s};
    }

    std::optional<Value> field(const QString &key) const override
    {
        if (const auto it = m_cache.constFind(key); it != m_cache.constEnd())
            return *it;
        std::optional<Value> v = compute(key);
        if (v)
            m_cache.insert(key, *v);
        return v;
    }

private:
    std::optional<FileStat> stat() const
    {
        if (!m_statDone) {
            m_statDone = true;
            if (m_ctx->options().stat)
                m_stat = m_ctx->options().stat(m_path);
        }
        return m_stat;
    }

    std::optional<Value> compute(const QString &key) const
    {
        Context &ctx = *m_ctx;
        if (key == u"name")
            return Value::string(m_note->title.isEmpty() ? paths::pageTitle(m_path) : m_note->title);
        if (key == u"path")
            return Value::string(m_path);
        if (key == u"folder")
            return Value::string(paths::dirname(m_path));
        if (key == u"ext") {
            const QString ext = paths::extension(m_path);
            return Value::string(ext.startsWith(u'.') ? ext.sliced(1) : ext);
        }
        if (key == u"link")
            return Value::link(LinkValue{m_path, QString(), QString(), false, true});
        if (key == u"size") {
            if (const auto s = stat())
                return Value::number(double(s->size));
            return Value::number(double(ctx.index().content(m_path).toUtf8().size()));
        }
        if (key == u"mtime" || key == u"ctime" || key == u"mday" || key == u"cday") {
            const auto s = stat();
            if (!s || s->mtime <= 0)
                return Value();
            const QDateTime at = QDateTime::fromMSecsSinceEpoch(s->mtime);
            if (key.endsWith(u"day"))
                return Value::date(at.date());
            return Value::date(DateValue{at, true});
        }
        if (key == u"day") {
            if (m_note->journalDate)
                return Value::date(*m_note->journalDate);
            if (const auto d = dateInName(m_note->title))
                return Value::date(*d);
            const Value date = fieldOf(Value::object(m_page.lock()), u"date"_s, ctx);
            return date.is(Value::Type::Date) ? Value::date(date.asDate().at.date()) : Value();
        }
        if (key == u"tags" || key == u"etags") {
            ValueList out;
            QSet<QString> seen;
            const auto add = [&](const QString &form) {
                if (seen.contains(form))
                    return;
                seen.insert(form);
                out.append(Value::string(tagValue(ctx, form)));
            };
            for (const QString &tag : m_note->tags) {
                // `tags`: a nested tag with its parents (#a/b/c: #a, #a/b, #a/b/c).
                if (key == u"tags") {
                    for (qsizetype i = tag.indexOf(u'/'); i >= 0; i = tag.indexOf(u'/', i + 1))
                        add(tag.first(i));
                }
                add(tag);
            }
            return Value::list(out);
        }
        if (key == u"inlinks") {
            ValueList out;
            QSet<VaultPath> seen;
            for (const QList<Backlink> backlinks = ctx.index().backlinks(m_path); const Backlink &b : backlinks) {
                if (seen.contains(b.path))
                    continue;
                seen.insert(b.path);
                out.append(Value::link(LinkValue{b.path, QString(), QString(), false, true}));
            }
            return Value::list(out);
        }
        if (key == u"outlinks")
            return Value::list(linksIn(ctx, m_path, -1, -1));
        if (key == u"aliases") {
            ValueList out;
            for (const QString &a : m_note->aliases)
                out.append(Value::string(a));
            return Value::list(out);
        }
        if (key == u"tasks")
            return Value::list(ctx.tasksOf(m_path));
        if (key == u"lists")
            return Value::list(ctx.listsOf(m_path));
        if (key == u"frontmatter")
            return fromYaml(u"frontmatter"_s, QVariant(m_note->frontmatter), resolverFor(m_ctx, m_path));
        if (key == u"starred")
            return Value::boolean(false);
        return std::nullopt;
    }

    Context *m_ctx;
    VaultPath m_path;
    const ParsedNote *m_note;
    std::weak_ptr<const Object> m_page; ///< (weak: the page holds its file object)
    mutable QHash<QString, Value> m_cache;
    mutable bool m_statDone = false;
    mutable std::optional<FileStat> m_stat;
};

// ---- page ------------------------------------------------------------------------------------------

class PageObject : public Object {
public:
    PageObject(Context *ctx, VaultPath path, const ParsedNote *note)
        : m_ctx(ctx), m_path(std::move(path)), m_note(note)
    { }

    /// Set once the Value holding this object exists (the file object reads page fields).
    void setSelf(std::weak_ptr<const Object> self) { m_self = std::move(self); }

    QStringList keys() const override
    {
        QStringList out{u"file"_s};
        for (const auto &f : fields())
            out.append(f.first);
        return out;
    }

    std::optional<Value> field(const QString &key) const override
    {
        if (key == u"file") {
            if (!m_file)
                m_file = Value::object(std::make_shared<FileObject>(m_ctx, m_path, m_note, m_self));
            return m_file;
        }
        return fieldIn(fields(), key);
    }

private:
    const Fields &fields() const
    {
        if (m_fields)
            return *m_fields;
        Fields out;
        const LinkResolver resolve = resolverFor(m_ctx, m_path);
        for (auto [name, value] : m_note->frontmatter.asKeyValueRange()) {
            if (const QString key = canonicalKey(name); !key.isEmpty())
                addField(out, key, fromYaml(name, value, resolve));
        }
        for (const Block &b : m_note->blocks) {
            m_ctx->step();
            for (const Fields own = inlineFields(b, resolve); const auto &[k, v] : own)
                addField(out, k, v);
        }
        return m_fields.emplace(std::move(out));
    }

    Context *m_ctx;
    VaultPath m_path;
    const ParsedNote *m_note;
    std::weak_ptr<const Object> m_self;
    mutable std::optional<Fields> m_fields;
    mutable std::optional<Value> m_file;
};

// ---- list items and tasks --------------------------------------------------------------------------

class ItemObject : public Object {
public:
    ItemObject(Context *ctx, VaultPath path, const ParsedNote *note, int blockIndex)
        : m_ctx(ctx), m_path(std::move(path)), m_note(note), m_index(blockIndex)
    { }

    QStringList keys() const override
    {
        QStringList out{u"status"_s,  u"checked"_s, u"completed"_s, u"fullycompleted"_s, u"text"_s,
                        u"visual"_s,  u"line"_s,    u"linecount"_s, u"path"_s,           u"section"_s,
                        u"link"_s,    u"tags"_s,    u"outlinks"_s,  u"children"_s,       u"task"_s,
                        u"annotated"_s, u"parent"_s, u"blockid"_s,  u"created"_s,        u"due"_s,
                        u"start"_s,   u"scheduled"_s, u"completion"_s};
        for (const auto &f : own())
            out.append(f.first);
        return out;
    }

    std::optional<Value> field(const QString &key) const override
    {
        if (auto v = fieldIn(own(), key))
            return v;
        const Block &b = block();
        const Task *t = b.task ? &*b.task : nullptr;
        const auto spec = [](const std::optional<DateSpec> &d) {
            return d ? Value::date(DateValue{d->toLocalDateTime(), d->hasTime()}) : Value();
        };
        const auto day = [](std::optional<QDate> d) { return d ? Value::date(*d) : Value(); };
        if (key == u"status")
            return t ? Value::string(t->marker) : Value();
        if (key == u"checked")
            return Value::boolean(t && t->status != TaskStatus::Todo);
        if (key == u"completed")
            return Value::boolean(t && t->status == TaskStatus::Done);
        if (key == u"fullycompleted")
            return Value::boolean(fullyCompleted(m_index));
        if (key == u"text" || key == u"visual")
            return Value::string(t ? t->text : b.text);
        if (key == u"line")
            return Value::number(b.line);
        if (key == u"linecount")
            return Value::number(b.ownEndLine - b.line + 1);
        if (key == u"path")
            return Value::string(m_path);
        if (key == u"section")
            return Value::link(section());
        if (key == u"link") {
            if (!b.id.isEmpty())
                return Value::link(LinkValue{m_path, QString(), u'^' + b.id, false, true});
            return Value::link(section());
        }
        if (key == u"tags") {
            ValueList out;
            for (const QString &tag : b.tags)
                out.append(Value::string(tagValue(*m_ctx, tag)));
            return Value::list(out);
        }
        if (key == u"outlinks")
            return Value::list(linksIn(*m_ctx, m_path, b.line, b.ownEndLine));
        if (key == u"children" || key == u"subtasks") {
            ValueList out;
            for (const int c : b.children) {
                if (c >= 0 && c < m_note->blocks.size() && m_note->blocks.at(c).kind == BlockKind::ListItem)
                    out.append(m_ctx->item(m_path, c));
            }
            return Value::list(out);
        }
        if (key == u"task")
            return Value::boolean(t != nullptr);
        if (key == u"annotated")
            return Value::boolean(!own().isEmpty());
        if (key == u"parent") {
            if (b.parent >= 0 && b.parent < m_note->blocks.size()
                && m_note->blocks.at(b.parent).kind == BlockKind::ListItem)
                return Value::number(m_note->blocks.at(b.parent).line);
            return Value();
        }
        if (key == u"blockid")
            return b.id.isEmpty() ? Value() : Value::string(b.id);
        if (key == u"created")
            return t ? day(t->created) : Value();
        if (key == u"due")
            return t ? spec(t->due) : Value();
        if (key == u"start")
            return t ? spec(t->start) : Value();
        if (key == u"scheduled")
            return t ? spec(t->scheduled) : Value();
        if (key == u"completion")
            return t ? day(t->doneDate) : Value();
        // Anything else: the page's fields (`file.name`, a frontmatter key).
        const Value page = m_ctx->page(m_path);
        if (page.is(Value::Type::Object))
            return page.asObject().field(key);
        return std::nullopt;
    }

private:
    const Block &block() const { return m_note->blocks.at(m_index); }

    const Fields &own() const
    {
        if (!m_own)
            m_own = inlineFields(block(), resolverFor(m_ctx, m_path));
        return *m_own;
    }

    bool fullyCompleted(int index) const
    {
        const Block &b = m_note->blocks.at(index);
        if (b.task && b.task->status != TaskStatus::Done)
            return false;
        if (!b.task && index == m_index)
            return false;
        const auto done = [this](int c) { return c < 0 || c >= m_note->blocks.size() || fullyCompleted(c); };
        return std::ranges::all_of(b.children, done);
    }

    LinkValue section() const
    {
        const int line = block().line;
        QString heading;
        for (const Heading &h : m_note->headings) {
            if (h.line > line)
                break;
            heading = h.text;
        }
        return LinkValue{m_path, QString(), heading, false, true};
    }

    Context *m_ctx;
    VaultPath m_path;
    const ParsedNote *m_note;
    int m_index;
    mutable std::optional<Fields> m_own;
};

} // namespace

// ---- Context ---------------------------------------------------------------------------------------

Context::Context(const VaultIndex &index, const Options &options) : m_index(index), m_options(options) { }

void Context::throwTooCostly()
{
    throw EvalError{QString()};
}

Value Context::page(const VaultPath &path)
{
    if (const auto it = m_pages.constFind(path); it != m_pages.constEnd())
        return *it;
    Value v;
    if (const ParsedNote *note = m_index.note(path)) {
        auto obj = std::make_shared<PageObject>(this, path, note);
        obj->setSelf(obj);
        v = Value::object(obj);
    }
    m_pages.insert(path, v);
    return v;
}

Value Context::thisPage()
{
    return m_options.currentPath.isEmpty() ? Value() : page(m_options.currentPath);
}

LinkValue Context::resolveLink(const QString &target, const QString &display, const QString &subpath, bool embed,
                               const VaultPath &from) const
{
    LinkValue v;
    v.display = display;
    v.subpath = subpath;
    v.embed = embed;
    const QString t = target.trimmed();
    if (t.isEmpty()) {
        v.path = from;
        v.resolved = !from.isEmpty() && m_index.contains(from);
        return v;
    }
    if (const auto resolved = m_index.resolveLink(t, from)) {
        v.path = *resolved;
        v.resolved = true;
    } else {
        v.path = t;
    }
    return v;
}

QString Context::linkTarget(const VaultPath &path) const
{
    return m_index.linkTarget(path).value_or(paths::stripMarkdownExtension(path));
}

QString Context::tagDisplay(const QString &tag)
{
    const auto spelling = [](const TagCount &t) { return t.display.isEmpty() ? t.tag : t.display; };
    if (!m_tagDisplay) {
        m_tagDisplay.emplace();
        for (const QList<TagCount> tags = m_index.tags(); const TagCount &t : tags)
            m_tagDisplay->insert(t.tag, spelling(t));
    }
    if (const auto it = m_tagDisplay->constFind(tag); it != m_tagDisplay->constEnd())
        return *it;
    // A parent of a nested tag: the parent part of the spelling of the most used tag under it (in
    // the index's order, not the hash's: the same on every run; kept for the next ask).
    const QString prefix = tag + u'/';
    QString display = tag;
    for (const QList<TagCount> tags = m_index.tags(); const TagCount &t : tags) {
        if (t.tag.startsWith(prefix)) {
            display = spelling(t).section(u'/', 0, int(tag.count(u'/')));
            break;
        }
    }
    m_tagDisplay->insert(tag, display);
    return display;
}

Value Context::item(const VaultPath &path, int blockIndex)
{
    const QString key = path + u'\x1f' + QString::number(blockIndex);
    if (const auto it = m_items.constFind(key); it != m_items.constEnd())
        return *it;
    const ParsedNote *note = m_index.note(path);
    Value v;
    if (note && blockIndex >= 0 && blockIndex < note->blocks.size())
        v = Value::object(std::make_shared<ItemObject>(this, path, note, blockIndex));
    m_items.insert(key, v);
    return v;
}

ValueList Context::tasksOf(const VaultPath &path)
{
    return itemsOf(*this, path, [](const Block &b) { return b.task.has_value(); });
}

ValueList Context::listsOf(const VaultPath &path)
{
    return itemsOf(*this, path, [](const Block &b) { return b.kind == BlockKind::ListItem; });
}

} // namespace brulion::dataview

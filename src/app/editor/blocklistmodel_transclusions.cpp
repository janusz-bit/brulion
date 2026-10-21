// BlockListModel: the `transclusions` role — pages, sections and blocks embedded in a block
// (`![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]`, `![[#Heading]]`, `!((id))`), rendered
// read-only like the editor renders blocks. Sources are read through the EditorHost (this page:
// the live document, so the embed follows unsaved edits); nested embeds are followed up to
// MaxTransclusionDepth levels, an embed of something already being shown (a cycle) becomes a
// note instead, and at most MaxTransclusionBlocks blocks are rendered per transclusion.
#include "blocklistmodel.h"

#include "blockrender_p.h"
#include "editorhost.h"

#include "text/text.h"
#include "util/paths.h"

#include <QSet>
#include <QUrl>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

QString encoded(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

/// "Budżet#Październik" (Obsidian heading paths): the last part names the heading.
QString headingKey(const QString &heading)
{
    const qsizetype hash = heading.lastIndexOf(u'#');
    return text::foldKey((hash >= 0 ? heading.mid(hash + 1) : heading).trimmed());
}

QString withoutMd(const QString &path)
{
    return path.endsWith(u".md"_s, Qt::CaseInsensitive) ? path.chopped(3) : path;
}

/// Rows [first, last] of a document shown by a transclusion; depths relative to baseDepth.
struct Range {
    int first = 0;
    int last = -1;
    int baseDepth = 0;
    bool isEmpty() const { return last < first; }
};

/// The section under the heading `heading`: the blocks after it up to the next heading of the
/// same or a higher level outside lists; for a Logseq "- # Heading" item its children.
std::optional<Range> sectionRange(const markdown::BlockDocument &doc, const QString &heading)
{
    const QString key = headingKey(heading);
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &e = doc.at(i);
        const BlockShape s = shapeOf(e);
        if (s.view != BlockView::Heading || s.headingLevel <= 0 || text::foldKey(plainTextOf(e, 400)) != key)
            continue;
        if (e.kind == BlockKind::ListItem) {
            const markdown::RowRange sub = doc.subtreeRange(i);
            return Range{i + 1, sub.last(), e.depth + 1};
        }
        int last = i;
        for (int k = i + 1; k < doc.count(); ++k) {
            const markdown::BlockEntry &next = doc.at(k);
            if (next.depth == 0) {
                const BlockShape ns = shapeOf(next);
                if (ns.view == BlockView::Heading && ns.headingLevel > 0 && ns.headingLevel <= s.headingLevel)
                    break;
            }
            last = k;
        }
        return Range{i + 1, last, 0};
    }
    return std::nullopt;
}

/// The block with id `id` and its children.
std::optional<Range> blockRange(const markdown::BlockDocument &doc, const QString &id)
{
    for (int i = 0; i < doc.count(); ++i) {
        if (doc.at(i).blockId == id) {
            const markdown::RowRange sub = doc.subtreeRange(i);
            return Range{i, sub.last(), doc.at(i).depth};
        }
    }
    return std::nullopt;
}

/// The whole page without its properties and without a first "# Title" repeating the title
/// (the embed's header names the page).
Range pageRange(const markdown::BlockDocument &doc, const QString &path)
{
    int first = 0;
    while (first < doc.count() && doc.at(first).kind == BlockKind::Frontmatter)
        ++first;
    if (first < doc.count()) {
        const markdown::BlockEntry &e = doc.at(first);
        const BlockShape s = shapeOf(e);
        if (s.view == BlockView::Heading && s.headingLevel == 1 && e.kind == BlockKind::Heading
            && text::foldKey(plainTextOf(e, 400)) == text::foldKey(paths::pageTitle(path)))
            ++first;
    }
    return Range{first, doc.count() - 1, 0};
}

QString statusName(const BlockShape &s)
{
    return taskStatusName(s.task);
}

/// Builds the transclusions of one block.
class Builder {
public:
    Builder(const BlockListModel &model, EditorHost *host, const markdown::BlockDocument &own, const QString &ownPath)
        : m_model(model), m_host(host), m_own(own), m_ownPath(ownPath)
    {
    }

    bool usedOwnPage() const { return m_local; }

    QVariantMap build(const Transclusion &t)
    {
        const Resolved r = resolve(t, m_ownPath);
        QVariantMap out{{u"kind"_s, kindName(t)},
                        {u"target"_s, t.blockRef ? t.blockId : t.target},
                        {u"title"_s, r.title},
                        {u"href"_s, r.href},
                        {u"path"_s, r.found ? r.path : QString()},
                        {u"found"_s, r.found},
                        {u"message"_s, r.message},
                        {u"more"_s, 0}};
        QVariantList blocks;
        if (r.found) {
            int budget = BlockListModel::MaxTransclusionBlocks;
            int more = 0;
            QSet<QString> visited{r.key};
            if (!r.fallbackText.isEmpty()) {
                blocks.append(blockEntry(markdown::InlineRenderer::escape(r.fallbackText), 0, u"text"_s, 0, QString(),
                                         QString(), -1));
            } else {
                addBlocks(r, 0, 1, visited, blocks, budget, more);
            }
            if (blocks.isEmpty())
                out.insert(u"message"_s, u"empty"_s);
            out.insert(u"more"_s, more);
        }
        out.insert(u"blocks"_s, blocks);
        return out;
    }

private:
    struct Source {
        QString path;
        QString content;
        const markdown::BlockDocument *doc = nullptr;
    };

    struct Resolved {
        bool found = false;
        QString message;
        Source src;
        Range range;
        QString title;
        QString href;
        QString path;
        QString key;
        QString fallbackText; ///< `!((id))` whose page is unknown: the block's text
    };

    static QString kindName(const Transclusion &t)
    {
        switch (t.kind) {
        case Transclusion::Kind::Page: return u"page"_s;
        case Transclusion::Kind::Heading: return u"heading"_s;
        case Transclusion::Kind::Block: return u"block"_s;
        }
        return u"page"_s;
    }

    /// The document of `path` (this page: the live document), nullopt when it cannot be read.
    std::optional<Source> load(const QString &path)
    {
        if (path.isEmpty())
            return std::nullopt;
        if (path == m_ownPath) {
            m_local = true;
            return Source{path, m_model.text(), &m_own};
        }
        if (const auto it = m_docs.constFind(path); it != m_docs.constEnd())
            return Source{path, it->first, it->second.get()};
        if (!m_host)
            return std::nullopt;
        bool ok = false;
        const QString content = m_host->read(path, &ok);
        if (!ok || !paths::isMarkdown(path))
            return std::nullopt;
        auto doc = std::make_shared<markdown::BlockDocument>(content);
        m_docs.insert(path, {content, doc});
        return Source{path, content, doc.get()};
    }

    /// The page holding block `id` (this page first, then the vault through the host).
    QString pathOfBlock(const QString &id, QString *text)
    {
        for (int i = 0; i < m_own.count(); ++i) {
            if (m_own.at(i).blockId == id)
                return m_ownPath;
        }
        if (!m_host)
            return {};
        *text = m_host->blockText(id);
        if (text->isEmpty())
            return {};
        // EditorHost has no "where is this block": its block suggestions carry the ids.
        QString query = text->section(u'\n', 0, 0).simplified();
        if (query.size() > 80)
            query = query.left(80);
        for (const QVariant &v : m_host->suggestBlocks(query, 50)) {
            const QVariantMap m = v.toMap();
            if (m.value(u"blockId"_s).toString() == id)
                return m.value(u"path"_s).toString();
        }
        return {};
    }

    Resolved resolve(const Transclusion &t, const QString &fromPath)
    {
        Resolved r;
        if (t.blockRef) {
            r.href = u"block:"_s + t.blockId;
            r.title = u"(("_s + t.blockId + u"))"_s;
            QString text;
            const QString path = pathOfBlock(t.blockId, &text);
            if (path.isEmpty()) {
                if (text.isEmpty()) {
                    r.message = u"missingBlock"_s;
                } else {
                    r.found = true;
                    r.fallbackText = text.simplified();
                    r.key = u"^"_s + t.blockId;
                }
                return r;
            }
            const auto src = load(path);
            const auto range = src ? blockRange(*src->doc, t.blockId) : std::nullopt;
            r.title = paths::pageTitle(path);
            if (!range) {
                if (!text.isEmpty()) {
                    r.found = true;
                    r.fallbackText = text.simplified();
                    r.key = u"^"_s + t.blockId;
                } else {
                    r.message = u"missingBlock"_s;
                }
                return r;
            }
            r.found = true;
            r.src = *src;
            r.range = *range;
            r.path = path;
            r.key = path + u"\n^"_s + t.blockId;
            return r;
        }

        // ![[Page]], ![[Page#Heading]], ![[Page#^id]]; an empty target is the page it is in.
        const QString path = t.target.isEmpty() ? fromPath : m_host ? m_host->resolveLink(t.target, fromPath) : QString();
        const QString linkTarget = t.target.isEmpty() && fromPath != m_ownPath ? withoutMd(fromPath) : t.target;
        QString fragment;
        if (t.kind == Transclusion::Kind::Heading)
            fragment = u'#' + encoded(t.heading);
        else if (t.kind == Transclusion::Kind::Block)
            fragment = u"#^"_s + encoded(t.blockId);
        r.href = u"page:"_s + encoded(linkTarget) + fragment;
        // A daily note by its date ("wt 13 paź"), like links to it.
        const auto dayTitle = markdown::journalLinkTitle(
                m_model.vaultConfig(), m_model.english() ? dates::UiLocale::English : dates::UiLocale::Polish,
                m_model.today());
        const QString shownName = path.isEmpty() ? t.target : paths::pageTitle(path);
        const QString pageName = !t.display.isEmpty()
                                         ? t.display
                                         : dayTitle(path.isEmpty() ? t.target : withoutMd(path)).value_or(shownName);
        r.title = pageName;
        if (t.kind == Transclusion::Kind::Heading)
            r.title = pageName + u" › "_s + t.heading;
        const auto src = load(path);
        if (!src) {
            r.message = u"missingPage"_s;
            return r;
        }
        std::optional<Range> range;
        switch (t.kind) {
        case Transclusion::Kind::Page: range = pageRange(*src->doc, path); break;
        case Transclusion::Kind::Heading: range = sectionRange(*src->doc, t.heading); break;
        case Transclusion::Kind::Block: range = blockRange(*src->doc, t.blockId); break;
        }
        if (!range) {
            r.message = t.kind == Transclusion::Kind::Heading ? u"missingSection"_s : u"missingBlock"_s;
            return r;
        }
        r.found = true;
        r.src = *src;
        r.range = *range;
        r.path = path;
        r.key = path + u'\n' + kindName(t) + u'\n'
                + (t.kind == Transclusion::Kind::Heading ? headingKey(t.heading) : t.blockId);
        return r;
    }

    static QVariantMap blockEntry(const QString &html, int depth, const QString &view, int headingLevel,
                                  const QString &task, const QString &path, int line)
    {
        return {{u"kind"_s, u"block"_s}, {u"html"_s, html},   {u"depth"_s, depth}, {u"view"_s, view},
                {u"headingLevel"_s, headingLevel}, {u"task"_s, task}, {u"path"_s, path}, {u"line"_s, line}};
    }

    void addBlocks(const Resolved &r, int depthOffset, int level, QSet<QString> &visited, QVariantList &out, int &budget,
                   int &more)
    {
        const markdown::BlockDocument &doc = *r.src.doc;
        for (int i = r.range.first; i <= r.range.last && i < doc.count(); ++i) {
            if (budget <= 0) {
                more += r.range.last - i + 1;
                return;
            }
            const markdown::BlockEntry &e = doc.at(i);
            const BlockShape s = shapeOf(e);
            if (s.view == BlockView::Frontmatter)
                continue;
            const int depth = std::max(0, e.depth - r.range.baseDepth) + depthOffset;
            const bool textLike = s.view == BlockView::Text || s.view == BlockView::Heading || s.view == BlockView::Quote
                                  || s.view == BlockView::Callout;
            const QList<Transclusion> nested = textLike ? transclusionsOf(e.body) : QList<Transclusion>();
            const bool onlyNested = !nested.isEmpty() && onlyEmbeds(e.body, true);
            QString html;
            QString view = viewName(s.view);
            if (textLike || s.view == BlockView::Html || s.view == BlockView::Math) {
                const markdown::InlineRenderer renderer(m_model.renderContextFor(r.src.path, r.src.content, e.line));
                html = detail::renderBodyHtml(e, s, renderer);
            } else if (s.view == BlockView::Rule) {
                html.clear();
            } else {
                // Code, calculations, queries and tables: their source as preformatted text.
                // Code-like blocks hide their fence lines in the delegate; an embed shows the
                // same text (`editText`), never the fences.
                html = u"<pre>"_s + s.editText.toHtmlEscaped() + u"</pre>"_s;
                view = u"code"_s;
            }
            if (!onlyNested && (!html.isEmpty() || s.view == BlockView::Rule)) {
                out.append(blockEntry(html, depth, view, s.headingLevel, statusName(s), r.src.path, e.line));
                --budget;
            }
            for (const Transclusion &t : nested) {
                const Resolved inner = resolve(t, r.src.path);
                const int headerDepth = onlyNested ? depth : depth + 1;
                QVariantMap header{{u"kind"_s, u"embed"_s}, {u"title"_s, inner.title},   {u"href"_s, inner.href},
                                   {u"depth"_s, headerDepth},  {u"found"_s, inner.found}, {u"message"_s, inner.message},
                                   {u"path"_s, inner.path}};
                if (inner.found && visited.contains(inner.key)) {
                    header.insert(u"message"_s, u"cycle"_s);
                    out.append(header);
                    --budget;
                    continue;
                }
                if (inner.found && level >= BlockListModel::MaxTransclusionDepth) {
                    header.insert(u"message"_s, u"depth"_s);
                    out.append(header);
                    --budget;
                    continue;
                }
                out.append(header);
                --budget;
                if (!inner.found)
                    continue;
                if (!inner.fallbackText.isEmpty()) {
                    out.append(blockEntry(markdown::InlineRenderer::escape(inner.fallbackText), headerDepth + 1, u"text"_s, 0,
                                          QString(), QString(), -1));
                    --budget;
                    continue;
                }
                visited.insert(inner.key);
                addBlocks(inner, headerDepth + 1, level + 1, visited, out, budget, more);
                visited.remove(inner.key);
            }
        }
    }

    const BlockListModel &m_model;
    EditorHost *m_host = nullptr;
    const markdown::BlockDocument &m_own;
    QString m_ownPath;
    bool m_local = false;
    QHash<QString, std::pair<QString, std::shared_ptr<markdown::BlockDocument>>> m_docs;
};

} // namespace

void BlockListModel::ensureTransclusions(int row) const
{
    const int i = docIndexOfRow(row);
    if (i < 0)
        return;
    RowState &st = stateFor(m_rows[row]);
    if (st.transclusionsValid)
        return;
    st.transclusionsValid = true;
    st.transclusionsLocal = false;
    st.transclusions.clear();
    const markdown::BlockEntry &e = m_doc.at(i);
    const BlockView view = cachedShape(row).view;
    if (view != BlockView::Text && view != BlockView::Heading && view != BlockView::Quote && view != BlockView::Callout)
        return;
    const QList<Transclusion> list = transclusionsOf(e.body);
    if (list.isEmpty())
        return;
    Builder builder(*this, m_host, m_doc, m_path);
    for (const Transclusion &t : list)
        st.transclusions.append(builder.build(t));
    st.transclusionsLocal = builder.usedOwnPage();
}

} // namespace brulion::editor

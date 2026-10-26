// HTML exporters: a static site in a ZIP (a page per note with relative links, tag pages, an
// index of journals and pages, backlinks, CSS) and a single self-contained HTML document (table of
// contents, links as anchors, images as data: URIs).
#include "index/vaultindex.h"
#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/htmlrender_p.h"
#include "io/importutil_p.h"
#include "io/links_p.h"
#include "io/messages_p.h"
#include "util/paths.h"

#include <QSet>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

QString esc(const QString &s)
{
    return HtmlRenderer::escape(s);
}

const char *styleSheet()
{
    return R"css(:root{--fg:#1f2430;--muted:#535a6a;--bg:#fbf9f4;--card:#fff;--line:#e4dfd3;--link:#2c6aa3;--missing:#7a5ea3;
--code:#f3eee2;--mark:#f2b33d55;--chip:#0f1f2410;--due:#2b5f8f;--sched:#6a4f93;--rem:#7a4f00;--rec:#2a6e44}
@media (prefers-color-scheme:dark){:root{--fg:#e6e3dc;--muted:#a8a395;--bg:#1b1d22;--card:#23262d;--line:#3a3d45;
--link:#8ab8e6;--missing:#b9a2e0;--code:#2c2f36;--mark:#f2b33d44;--chip:#ffffff14;--due:#8ab8e6;--sched:#c2a8ea;
--rem:#f2c26b;--rec:#8fd3a8}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.6 system-ui,-apple-system,"Segoe UI",Roboto,Ubuntu,sans-serif}
header,main,footer{max-width:48rem;margin:0 auto;padding:0 1rem}
header{padding-top:1rem;color:var(--muted);font-size:.9rem}
header a{color:var(--muted)}
footer{padding:2rem 1rem;color:var(--muted);font-size:.8rem}
a{color:var(--link)}
.missing{color:var(--missing);border-bottom:1px dashed var(--missing)}
h1.title{margin-bottom:.25rem}
code,pre{font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;font-size:.9em;background:var(--code);border-radius:4px}
code{padding:.1em .3em}
pre{padding:.75rem 1rem;overflow:auto}
pre code{padding:0;background:none}
div.math,.math.display{display:block;margin:1rem 0;text-align:center}
mark{background:var(--mark);color:inherit}
blockquote{margin:1rem 0;padding:.25rem 1rem;border-left:3px solid var(--line);color:var(--muted)}
.callout{margin:1rem 0;padding:.5rem 1rem;border:1px solid var(--line);border-left:4px solid var(--link);border-radius:6px;background:var(--card)}
.callout-title{font-weight:600}
.callout-warning,.callout-danger{border-left-color:#b23c31}
.callout-tip{border-left-color:var(--rec)}
table{border-collapse:collapse;margin:1rem 0}
th,td{border:1px solid var(--line);padding:.3rem .6rem}
ul,ol{padding-left:1.5rem}
li.task{list-style:none;margin-left:-1.3rem}
li.done>input+*,li.cancelled{color:var(--muted)}
li.cancelled{text-decoration:line-through}
.chip{display:inline-block;padding:0 .4em;border-radius:1em;background:var(--chip);font-size:.85em;white-space:nowrap}
.chip.due{color:var(--due)}.chip.scheduled{color:var(--sched)}.chip.reminder,.chip.alarm{color:var(--rem)}.chip.recurrence{color:var(--rec)}
.chip.alarm{font-weight:600}
.tag{color:var(--link);background:var(--chip);padding:0 .3em;border-radius:4px;text-decoration:none}
.tags{margin:.25rem 0 1rem}
.meta{margin-left:.25rem}
.prop .key{color:var(--muted)}
.prop .key::after{content:":"}
.embed{margin:1rem 0;padding:.5rem 1rem;border:1px solid var(--line);border-radius:6px;background:var(--card)}
.backlinks{margin-top:3rem;padding-top:1rem;border-top:1px solid var(--line)}
.backlinks li{margin-bottom:.5rem}
.excerpt{display:block;color:var(--muted);font-size:.9rem}
img,video{max-width:100%;height:auto}
nav.toc ul{columns:2}
section.note{margin-top:3rem;padding-top:1rem;border-top:1px solid var(--line)}
)css";
}

QString pageHtml(const QString &title, const QString &vaultName, const QString &cssHref, const QString &header,
                 const QString &body, const QString &footer)
{
    QString out = u"<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                  u"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                  u"<meta name=\"generator\" content=\"Brulion\">\n<title>"_s
                  + esc(title == vaultName ? title : title + u" – "_s + vaultName) + u"</title>\n"_s;
    if (cssHref.isEmpty())
        out += u"<style>\n"_s + QString::fromUtf8(styleSheet()) + u"</style>\n"_s;
    else
        out += u"<link rel=\"stylesheet\" href=\""_s + esc(cssHref) + u"\">\n"_s;
    out += u"</head>\n<body>\n<header>"_s + header + u"</header>\n<main>\n"_s + body + u"\n</main>\n<footer>"_s + footer
           + u"</footer>\n</body>\n</html>\n"_s;
    return out;
}

QString footerText(const ExportInput &input)
{
    return esc(input.now.isValid()
                       ? IoMessages::tr("Exported from Brulion on %1").arg(input.now.toString(u"yyyy-MM-dd HH:mm"_s))
                       : IoMessages::tr("Exported from Brulion"));
}

/// Renders the notes of an export, building link callbacks for one output location.
class Site {
public:
    /// `href(fromNote, toFile, fragment)`: URL of a vault file (note or attachment) as seen from
    /// the output of note `fromNote`.
    using HrefFn = std::function<QString(const VaultPath &toFile, const QString &fragment)>;

    Site(const ExportVault &vault, HrefFn noteHref,
         std::function<std::optional<QString>(const VaultPath &)> attachmentUrl,
         std::function<QString(const QString &tag)> tagHref, std::function<QString(const VaultPath &)> idPrefix)
        : m_vault(vault)
        , m_noteHref(std::move(noteHref))
        , m_attachmentUrl(std::move(attachmentUrl))
        , m_tagHref(std::move(tagHref))
        , m_idPrefix(std::move(idPrefix))
    { }

    /// Links for rendering note `notePath` (links resolved from `notePath`); embeds allowed once.
    HtmlLinks links(const VaultPath &notePath, const VaultPath &outputNote, bool allowEmbeds) const
    {
        HtmlLinks l;
        l.idPrefix = m_idPrefix ? m_idPrefix(notePath) : QString();
        const auto fragment = [this](const VaultPath &target, const QString &heading,
                                     const QString &blockId) -> QString {
            const QString prefix = m_idPrefix ? m_idPrefix(target) : QString();
            if (!blockId.isEmpty())
                return prefix + HtmlRenderer::blockAnchor(blockId);
            if (!heading.isEmpty()) {
                // The anchor of the heading the fragment names (a heading path, a copied fragment
                // without markup) as the heading itself gets it: its block id's, else its text's.
                const ExportVault::Note *n = m_vault.note(target);
                const int found = n && n->parsed ? linkedHeading(*n->parsed, heading) : -1;
                if (found < 0)
                    return prefix + HtmlRenderer::headingAnchor(heading);
                const Block &b = n->parsed->blocks.at(found);
                return prefix
                       + (b.id.isEmpty() ? HtmlRenderer::headingAnchor(b.text) : HtmlRenderer::blockAnchor(b.id));
            }
            return QString();
        };
        l.page = [=, this](const QString &target, const QString &heading,
                           const QString &blockId) -> std::optional<QString> {
            if (target.isEmpty())
                return u'#' + fragment(notePath, heading, blockId);
            const auto resolved = m_vault.resolve(target, notePath);
            if (!resolved)
                return std::nullopt;
            if (m_vault.note(*resolved))
                return m_noteHref(*resolved, fragment(*resolved, heading, blockId));
            return m_attachmentUrl(*resolved);
        };
        l.attachment = [=, this](const QString &target) -> std::optional<QString> {
            const auto resolved = m_vault.resolve(target, notePath);
            if (!resolved || !m_vault.isAttachment(*resolved))
                return std::nullopt;
            return m_attachmentUrl(*resolved);
        };
        l.tag = [this](const QString &tag) { return tagLink(tag); };
        l.block = [=, this](const QString &id) -> std::optional<std::pair<QString, QString>> {
            const auto block = m_vault.block(id);
            if (!block)
                return std::nullopt;
            const ExportVault::Note *n = m_vault.note(block->first);
            if (!n || !n->parsed)
                return std::nullopt;
            return std::make_pair(m_noteHref(block->first, fragment(block->first, {}, id)),
                                  blockText(*n->parsed, block->second));
        };
        if (allowEmbeds) {
            l.embed = [=, this](const QString &target, const QString &heading, const QString &blockId) -> QString {
                const auto resolved = m_vault.resolve(target, notePath);
                const ExportVault::Note *n = resolved ? m_vault.note(*resolved) : nullptr;
                if (!n || !n->parsed || *resolved == notePath)
                    return {};
                const HtmlRenderer inner(links(*resolved, outputNote, false));
                QList<int> blocks;
                if (!blockId.isEmpty()) {
                    if (const auto it = n->parsed->blockIds.constFind(blockId); it != n->parsed->blockIds.constEnd())
                        blocks.append(*it);
                } else if (!heading.isEmpty()) {
                    blocks = sectionBlocks(*n->parsed, heading);
                }
                const QString title = u"<div class=\"embed-title\"><a href=\""_s
                                      + esc(m_noteHref(*resolved, fragment(*resolved, heading, blockId))) + u"\">"_s
                                      + esc(n->parsed->title) + u"</a></div>"_s;
                return title
                       + (blocks.isEmpty() && heading.isEmpty() && blockId.isEmpty()
                                  ? inner.renderNote(*n->parsed)
                                  : inner.renderBlocks(*n->parsed, blocks));
            };
        }
        return l;
    }

    QString renderNote(const ExportVault::Note &n) const
    {
        const HtmlRenderer renderer(links(n.path, n.path, true));
        return renderer.renderNote(*n.parsed);
    }

    /// Where a tag leads: the page it names (SYNTAX §3) when that note is exported, else the
    /// tag's own page (when the export has them).
    QString tagLink(const QString &tag) const
    {
        if (const auto page = m_vault.index().resolveTag(tag); page && m_vault.note(*page))
            return m_noteHref(*page, {});
        return m_tagHref ? m_tagHref(tag) : QString();
    }

    QString tagsLine(const ExportVault::Note &n) const
    {
        if (n.parsed->tags.isEmpty())
            return {};
        QStringList items;
        for (const QString &t : n.parsed->tags) {
            const QString href = tagLink(t);
            if (href.isEmpty())
                items.append(u"<span class=\"tag\">#"_s + esc(t) + u"</span>"_s);
            else
                items.append(u"<a class=\"tag\" href=\""_s + esc(href) + u"\">#"_s + esc(t) + u"</a>"_s);
        }
        return u"<p class=\"tags\">"_s + items.join(u' ') + u"</p>"_s;
    }

    QString backlinks(const ExportVault::Note &n) const
    {
        QStringList items;
        QSet<QString> seen;
        for (const Backlink &b : m_vault.index().backlinks(n.path)) {
            const QString key = b.path + u'#' + QString::number(b.blockIndex);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            const ExportVault::Note *source = m_vault.note(b.path);
            if (!source || !source->parsed)
                continue;
            const HtmlRenderer renderer(links(b.path, n.path, false));
            items.append(u"<li><a href=\""_s + esc(m_noteHref(b.path, {})) + u"\">"_s + esc(source->parsed->title)
                         + u"</a><span class=\"excerpt\">"_s
                         + renderer.renderInline(blockText(*source->parsed, b.blockIndex)) + u"</span></li>"_s);
        }
        // Tags name pages too: the tagged blocks, and pages tagged as a whole (title only).
        for (const TagReference &r : m_vault.index().tagReferences(n.path)) {
            const QString key = r.path + u'#' + QString::number(r.blockIndex);
            const ExportVault::Note *source = m_vault.note(r.path);
            if (seen.contains(key) || !source || !source->parsed)
                continue;
            seen.insert(key);
            const HtmlRenderer renderer(links(r.path, n.path, false));
            const QString excerpt = r.blockIndex >= 0
                                            ? u"<span class=\"excerpt\">"_s
                                                      + renderer.renderInline(blockText(*source->parsed, r.blockIndex))
                                                      + u"</span>"_s
                                            : QString();
            items.append(u"<li><a href=\""_s + esc(m_noteHref(r.path, {})) + u"\">"_s + esc(source->parsed->title)
                         + u"</a>"_s + excerpt + u"</li>"_s);
        }
        if (items.isEmpty())
            return {};
        return u"<section class=\"backlinks\"><h2>"_s + esc(IoMessages::tr("Linked references")) + u"</h2><ul>"_s
               + items.join(QString()) + u"</ul></section>"_s;
    }

private:
    const ExportVault &m_vault;
    HrefFn m_noteHref;
    std::function<std::optional<QString>(const VaultPath &)> m_attachmentUrl;
    std::function<QString(const QString &)> m_tagHref;
    std::function<QString(const VaultPath &)> m_idPrefix;
};

/// URL of vault file `to` from folder `fromDir` of the site. A first segment containing ':' is
/// prefixed with "./": file names may look like schemes ("javascript:alert(1).md" is a valid
/// file name), and a browser would run or open such an href instead of the file.
QString relativeHref(const QString &fromDir, const QString &to)
{
    QString href = encodeLinkPath(relativePath(fromDir, to));
    const qsizetype colon = href.indexOf(u':');
    const qsizetype slash = href.indexOf(u'/');
    if (colon >= 0 && (slash < 0 || colon < slash))
        href.prepend(u"./"_s);
    return href;
}

QString tagRelativePath(const QString &tag)
{
    return u"tags/"_s + safeRelativePath(tag, false) + u".html"_s;
}

class HtmlSiteExporter final : public Exporter {
public:
    QString id() const override { return u"html-site"_s; }
    QString name() const override { return IoMessages::tr("HTML website (ZIP)"); }
    QString fileExtension() const override { return u".zip"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        PathClaims claims;
        claims.claim(u"index.html"_s);
        claims.claim(u"style.css"_s);
        QHash<VaultPath, QString> htmlFiles;
        for (const FileEntry &a : vault.attachments())
            claims.claim(a.path);
        for (const ExportVault::Note &n : vault.notes())
            htmlFiles.insert(n.path, claims.claim(paths::stripMarkdownExtension(n.path) + u".html"_s));
        const QList<TagCount> tags = vault.index().tags();
        QHash<QString, QString> tagFiles;
        for (const TagCount &t : tags)
            tagFiles.insert(t.tag, claims.claim(tagRelativePath(t.tag)));

        QList<FileEntry> entries;
        const QString root = paths::sanitizeFileName(vault.vaultName()) + u'/';
        const QString footer = footerText(input);
        for (const ExportVault::Note *n : orderedNotes(vault)) {
            const QString file = htmlFiles.value(n->path);
            const QString dir = paths::dirname(file);
            const auto href = [&, dir](const QString &to) -> QString { return relativeHref(dir, to); };
            const Site site(
                    vault,
                    [&, href](const VaultPath &to, const QString &fragment) -> QString {
                        return href(htmlFiles.value(to)) + (fragment.isEmpty() ? QString() : u'#' + fragment);
                    },
                    [href](const VaultPath &to) -> std::optional<QString> { return href(to); },
                    [&, href](const QString &tag) -> QString {
                        const QString f = tagFiles.value(tag.toLower());
                        return f.isEmpty() ? QString() : href(f);
                    },
                    {});
            QString header =
                    u"<a href=\""_s + esc(href(u"index.html"_s)) + u"\">"_s + esc(vault.vaultName()) + u"</a>"_s;
            for (const QString &segment : paths::dirname(n->path).split(u'/', Qt::SkipEmptyParts))
                header += u" / "_s + esc(segment);
            const QString body = u"<article>\n<h1 class=\"title\">"_s + esc(n->parsed->title) + u"</h1>\n"_s
                                 + site.tagsLine(*n) + site.renderNote(*n) + u"\n</article>\n"_s + site.backlinks(*n);
            entries.append(FileEntry{root + file, pageHtml(n->parsed->title, vault.vaultName(), href(u"style.css"_s),
                                                           header, body, footer)
                                                          .toUtf8()});
        }
        for (const TagCount &t : tags) {
            const QString file = tagFiles.value(t.tag);
            const QString dir = paths::dirname(file);
            QStringList items;
            for (const VaultPath &p : t.paths) {
                const ExportVault::Note *n = vault.note(p);
                if (!n)
                    continue;
                items.append(u"<li><a href=\""_s + esc(relativeHref(dir, htmlFiles.value(p))) + u"\">"_s
                             + esc(n->parsed->title) + u"</a></li>"_s);
            }
            const QString title = u'#' + t.display;
            const QString header = u"<a href=\""_s + esc(relativeHref(dir, u"index.html"_s)) + u"\">"_s
                                   + esc(vault.vaultName()) + u"</a>"_s;
            const QString body = u"<article><h1 class=\"title\">"_s + esc(title) + u"</h1><ul>"_s
                                 + items.join(QString()) + u"</ul></article>"_s;
            entries.append(FileEntry{root + file, pageHtml(title, vault.vaultName(), relativeHref(dir, u"style.css"_s),
                                                           header, body, footer)
                                                          .toUtf8()});
        }
        entries.append(FileEntry{root + u"index.html"_s, indexPage(vault, htmlFiles, tags, tagFiles, footer).toUtf8()});
        entries.append(FileEntry{root + u"style.css"_s, QByteArray(styleSheet())});
        for (const FileEntry &a : vault.attachments())
            entries.append(FileEntry{root + a.path, a.data});
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName(u"html"_s, u".zip"_s), u"application/zip"_s, vault.zip(entries)});
        return r;
    }

private:
    static QString indexPage(const ExportVault &vault, const QHash<VaultPath, QString> &htmlFiles,
                             const QList<TagCount> &tags, const QHash<QString, QString> &tagFiles,
                             const QString &footer)
    {
        QStringList journals;
        QStringList pages;
        for (const ExportVault::Note *n : orderedNotes(vault)) {
            const QString link = u"<li><a href=\""_s + esc(relativeHref({}, htmlFiles.value(n->path))) + u"\">"_s
                                 + esc(n->journal ? n->parsed->title : paths::stripMarkdownExtension(n->path))
                                 + u"</a></li>"_s;
            (n->journal ? journals : pages).append(link);
        }
        QString body = u"<article>\n<h1 class=\"title\">"_s + esc(vault.vaultName()) + u"</h1>\n"_s;
        if (!journals.isEmpty())
            body += u"<h2>"_s + esc(IoMessages::tr("Journals")) + u"</h2>\n<ul>"_s + journals.join(QString())
                    + u"</ul>\n"_s;
        if (!pages.isEmpty())
            body += u"<h2>"_s + esc(IoMessages::tr("Pages")) + u"</h2>\n<ul>"_s + pages.join(QString()) + u"</ul>\n"_s;
        if (!tags.isEmpty()) {
            QStringList items;
            for (const TagCount &t : tags)
                items.append(u"<a class=\"tag\" href=\""_s + esc(relativeHref({}, tagFiles.value(t.tag))) + u"\">#"_s
                             + esc(t.display) + u"</a> <small>"_s + QString::number(t.count) + u"</small>"_s);
            body += u"<h2>"_s + esc(IoMessages::tr("Tags")) + u"</h2>\n<p>"_s + items.join(u' ') + u"</p>\n"_s;
        }
        body += u"</article>"_s;
        return pageHtml(vault.vaultName(), vault.vaultName(), u"style.css"_s, esc(vault.vaultName()), body, footer);
    }
};

class HtmlSingleExporter final : public Exporter {
public:
    QString id() const override { return u"html-single"_s; }
    QString name() const override { return IoMessages::tr("HTML document"); }
    QString fileExtension() const override { return u".html"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        const QList<const ExportVault::Note *> notes = orderedNotes(vault);
        QHash<VaultPath, QString> anchors;
        for (qsizetype i = 0; i < notes.size(); ++i)
            anchors.insert(notes[i]->path, u"n"_s + QString::number(i + 1));
        const Site site(
                vault,
                [&](const VaultPath &to, const QString &fragment) -> QString {
                    return u'#' + (fragment.isEmpty() ? anchors.value(to) : fragment);
                },
                [&](const VaultPath &to) -> std::optional<QString> {
                    const FileEntry *a = vault.attachment(to);
                    if (!a)
                        return std::nullopt;
                    // An HTML attachment as a data: document would run its scripts: a download.
                    QString mime = mimeForPath(to);
                    if (mime == u"text/html")
                        mime = u"application/octet-stream"_s;
                    return u"data:"_s + mime + u";base64,"_s + QString::fromLatin1(a->data.toBase64());
                },
                {}, [&](const VaultPath &path) -> QString { return anchors.value(path) + u'-'; });
        QStringList toc;
        QString sections;
        for (const ExportVault::Note *n : notes) {
            const QString anchor = anchors.value(n->path);
            const QString label = n->journal ? n->parsed->title : paths::stripMarkdownExtension(n->path);
            toc.append(u"<li><a href=\"#"_s + anchor + u"\">"_s + esc(label) + u"</a></li>"_s);
            sections += u"<section class=\"note\" id=\""_s + anchor + u"\">\n<h1 class=\"title\">"_s
                        + esc(n->parsed->title) + u"</h1>\n"_s + site.tagsLine(*n) + site.renderNote(*n)
                        + u"\n</section>\n"_s;
        }
        const QString body = u"<h1>"_s + esc(vault.vaultName()) + u"</h1>\n<nav class=\"toc\"><h2>"_s
                             + esc(IoMessages::tr("Contents")) + u"</h2><ul>"_s + toc.join(QString())
                             + u"</ul></nav>\n"_s + sections;
        ExportResult r;
        r.files.append(ExportedFile{
                vault.fileName({}, u".html"_s), u"text/html"_s,
                pageHtml(vault.vaultName(), vault.vaultName(), {}, esc(vault.vaultName()), body, footerText(input))
                        .toUtf8()});
        return r;
    }
};

} // namespace

std::unique_ptr<Exporter> makeHtmlSiteExporter()
{
    return std::make_unique<HtmlSiteExporter>();
}

std::unique_ptr<Exporter> makeHtmlSingleExporter()
{
    return std::make_unique<HtmlSingleExporter>();
}

} // namespace brulion::io::detail

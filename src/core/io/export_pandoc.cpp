// Pandoc exporter: the notes as one Pandoc Markdown document (YAML title block, a section per
// note with identifiers for every link, block references and embeds resolved) in a ZIP with the
// attachments it links to — `pandoc <name>.md -o <name>.docx` (or .pdf, .odt, .epub…) in the
// unpacked folder. One page is exported as the document itself, without a section heading.
#include "index/vaultindex.h"
#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/links_p.h"
#include "io/messages_p.h"
#include "io/pandocrender_p.h"
#include "util/paths.h"

#include <QSet>

#include <algorithm>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

/// A YAML single-quoted scalar (pandoc reads metadata values as Markdown: `text` is escaped).
QString yamlString(const QString &text)
{
    QString escaped = PandocRenderer::escape(text);
    escaped.replace(u'\n', u' ').replace(u'\'', u"''"_s);
    return u'\'' + escaped + u'\'';
}

class Document {
public:
    explicit Document(const ExportVault &vault) : m_vault(vault), m_notes(orderedNotes(vault))
    {
        m_single = m_notes.size() == 1;
        for (qsizetype i = 0; i < m_notes.size(); ++i) {
            const ExportVault::Note *n = m_notes[i];
            m_prefixes.insert(n->path, u'n' + QString::number(i + 1) + u'-');
            if (!m_single)
                m_anchors.insert(n->path, u'n' + QString::number(i + 1));
            // The first heading that repeats the title becomes the title (no heading twice).
            for (int b = 0; b < n->parsed->blocks.size(); ++b) {
                const Block &block = n->parsed->blocks[b];
                if (block.parent >= 0 || block.kind == BlockKind::Frontmatter)
                    continue;
                if (block.kind == BlockKind::Heading && block.headingLevel == 1
                    && block.text.trimmed().compare(n->parsed->title.trimmed(), Qt::CaseInsensitive) == 0)
                    m_titleHeadings.insert(n->path, b);
                break;
            }
        }
    }

    QString render(const ExportInput &input) const
    {
        const QString title = m_single ? m_notes.first()->parsed->title : m_vault.vaultName();
        QString out = u"---\ntitle: "_s + yamlString(title) + u'\n';
        if (input.now.isValid())
            out += u"date: '"_s + input.now.date().toString(u"yyyy-MM-dd"_s) + u"'\n"_s;
        out += u"---\n"_s;
        for (const ExportVault::Note *n : m_notes) {
            QStringList parts;
            if (!m_single)
                parts.append(u"# "_s + PandocRenderer::escape(n->parsed->title) + u" {#"_s + m_anchors.value(n->path)
                             + u'}');
            if (!n->parsed->tags.isEmpty()) {
                QStringList tags;
                for (const QString &t : n->parsed->tags) {
                    const auto page = tagLink(t);
                    tags.append(page && !page->isEmpty()
                                        ? u"[#"_s + PandocRenderer::escape(t) + u"]("_s + *page + u"){.tag}"_s
                                        : PandocRenderer::tagSpan(t));
                }
                parts.append(tags.join(u' '));
            }
            const QString body = PandocRenderer(links(*n, true, true)).renderNote(*n->parsed);
            if (!body.isEmpty())
                parts.append(body);
            if (!parts.isEmpty())
                out += u'\n' + parts.join(u"\n\n"_s) + u'\n';
        }
        return out;
    }

    /// The attachments the document links to, sorted.
    QList<VaultPath> usedAttachments() const
    {
        QList<VaultPath> out(m_used.begin(), m_used.end());
        std::sort(out.begin(), out.end());
        return out;
    }

private:
    /// Identifier a link to `target` (heading / block) leads to; "" for the single note itself.
    QString fragment(const VaultPath &target, const QString &heading, const QString &blockId) const
    {
        const QString prefix = m_prefixes.value(target);
        if (!blockId.isEmpty())
            return prefix + PandocRenderer::blockAnchor(blockId);
        if (!heading.isEmpty()) {
            // The identifier the heading itself gets: its block id's, else its text's.
            const ExportVault::Note *n = m_vault.note(target);
            const int found = n && n->parsed ? linkedHeading(*n->parsed, heading) : -1;
            if (found >= 0 && found != m_titleHeadings.value(target, -1)) {
                const Block &b = n->parsed->blocks.at(found);
                return prefix
                       + (b.id.isEmpty() ? PandocRenderer::headingAnchor(b.text) : PandocRenderer::blockAnchor(b.id));
            }
        }
        return m_anchors.value(target);
    }

    static QString destination(const QString &fragment) { return fragment.isEmpty() ? QString() : u'#' + fragment; }

    /// Path of an attachment from the document (it sits at the root of the archive folder). A
    /// first segment with ':' gets "./" (it would read as a URL scheme).
    QString attachmentPath(const VaultPath &path) const
    {
        m_used.insert(path);
        QString href = encodeLinkPath(path);
        const qsizetype colon = href.indexOf(u':');
        const qsizetype slash = href.indexOf(u'/');
        if (colon >= 0 && (slash < 0 || colon < slash))
            href.prepend(u"./"_s);
        return href;
    }

    std::optional<QString> tagLink(const QString &tag) const
    {
        if (const auto page = m_vault.index().resolveTag(tag); page && m_vault.note(*page))
            return destination(m_anchors.value(*page));
        return std::nullopt;
    }

    PandocLinks links(const ExportVault::Note &note, bool ids, bool allowEmbeds) const
    {
        PandocLinks l;
        const VaultPath notePath = note.path;
        l.idPrefix = m_prefixes.value(notePath);
        l.footnotePrefix = l.idPrefix;
        l.ids = ids;
        l.headingShift = m_single ? 0 : 1;
        l.skipBlock = m_titleHeadings.value(notePath, -1);
        l.page = [=, this](const QString &target, const QString &heading,
                           const QString &blockId) -> std::optional<QString> {
            if (target.isEmpty())
                return destination(fragment(notePath, heading, blockId));
            const auto resolved = m_vault.resolve(target, notePath);
            if (!resolved)
                return std::nullopt;
            if (m_vault.note(*resolved))
                return destination(fragment(*resolved, heading, blockId));
            return attachmentPath(*resolved);
        };
        l.attachment = [=, this](const QString &target) -> std::optional<QString> {
            const auto resolved = m_vault.resolve(target, notePath);
            if (!resolved || !m_vault.isAttachment(*resolved))
                return std::nullopt;
            return attachmentPath(*resolved);
        };
        l.tag = [this](const QString &tag) { return tagLink(tag); };
        l.block = [this](const QString &id) -> std::optional<std::pair<QString, QString>> {
            const auto block = m_vault.block(id);
            if (!block)
                return std::nullopt;
            const ExportVault::Note *n = m_vault.note(block->first);
            if (!n || !n->parsed)
                return std::nullopt;
            return std::make_pair(destination(fragment(block->first, {}, id)), blockText(*n->parsed, block->second));
        };
        if (allowEmbeds) {
            l.embed = [=, this](const QString &target, const QString &heading, const QString &blockId) -> QString {
                const auto resolved = m_vault.resolve(target, notePath);
                const ExportVault::Note *n = resolved ? m_vault.note(*resolved) : nullptr;
                if (!n || !n->parsed || *resolved == notePath)
                    return {};
                // An embedded copy: no identifiers (the original has them), no nested embeds.
                const PandocRenderer inner(links(*n, false, false));
                QList<int> blocks;
                if (!blockId.isEmpty()) {
                    if (const auto it = n->parsed->blockIds.constFind(blockId); it != n->parsed->blockIds.constEnd())
                        blocks.append(*it);
                } else if (!heading.isEmpty()) {
                    blocks = sectionBlocks(*n->parsed, heading);
                }
                const QString titleLink = [&] {
                    const QString label = PandocRenderer::escape(n->parsed->title);
                    const QString href = destination(fragment(*resolved, heading, blockId));
                    return href.isEmpty() ? label : u'[' + label + u"]("_s + href + u')';
                }();
                const QString body = blocks.isEmpty() && heading.isEmpty() && blockId.isEmpty()
                                             ? inner.renderNote(*n->parsed)
                                             : inner.renderBlocks(*n->parsed, blocks);
                return u"**"_s + titleLink + u"**"_s + (body.isEmpty() ? QString() : u"\n\n"_s + body);
            };
        }
        return l;
    }

    const ExportVault &m_vault;
    QList<const ExportVault::Note *> m_notes;
    bool m_single = false;
    QHash<VaultPath, QString> m_prefixes;
    QHash<VaultPath, QString> m_anchors;
    QHash<VaultPath, int> m_titleHeadings;
    mutable QSet<VaultPath> m_used;
};

class PandocExporter final : public Exporter {
public:
    QString id() const override { return u"pandoc"_s; }
    QString name() const override { return IoMessages::tr("Pandoc Markdown (ZIP)"); }
    QString fileExtension() const override { return u".zip"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        const Document document(vault);
        const QString text = document.render(input);
        const QString root = paths::sanitizeFileName(vault.vaultName());
        QList<FileEntry> entries = {FileEntry{root + u'/' + root + u".md"_s, text.toUtf8()}};
        for (const VaultPath &path : document.usedAttachments()) {
            if (const FileEntry *a = vault.attachment(path))
                entries.append(FileEntry{root + u'/' + a->path, a->data});
        }
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName(u"pandoc"_s, u".zip"_s), u"application/zip"_s, vault.zip(entries)});
        return r;
    }
};

} // namespace

std::unique_ptr<Exporter> makePandocExporter()
{
    return std::make_unique<PandocExporter>();
}

} // namespace brulion::io::detail

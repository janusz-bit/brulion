// Markdown exporters: a plain ZIP of the vault and an Obsidian-ready ZIP (Logseq syntax converted
// to Obsidian Tasks syntax, block references as [[Page#^id]], .obsidian settings).
#include "io/exporters_p.h"
#include "io/exportutil_p.h"
#include "io/links_p.h"
#include "io/logseq_p.h"
#include "io/messages_p.h"
#include "util/paths.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

QString rootFolder(const ExportVault &vault)
{
    return paths::sanitizeFileName(vault.vaultName());
}

class MarkdownZipExporter final : public Exporter {
public:
    QString id() const override { return u"markdown-zip"_s; }
    QString name() const override { return IoMessages::tr("Markdown (ZIP)"); }
    QString fileExtension() const override { return u".zip"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        const QString root = rootFolder(vault);
        QList<FileEntry> entries;
        for (const FileEntry &f : input.files)
            entries.append(FileEntry{root + u'/' + f.path, f.data});
        ExportResult r;
        r.files.append(ExportedFile{vault.fileName({}, u".zip"_s), u"application/zip"_s, vault.zip(entries)});
        return r;
    }
};

class ObsidianZipExporter final : public Exporter {
public:
    QString id() const override { return u"obsidian-zip"_s; }
    QString name() const override { return IoMessages::tr("Obsidian (ZIP)"); }
    QString fileExtension() const override { return u".zip"_s; }

    ExportResult run(const ExportInput &input) const override
    {
        const ExportVault vault(input);
        const QString root = rootFolder(vault) + u'/';
        QList<FileEntry> entries;
        for (const ExportVault::Note &n : vault.notes())
            entries.append(FileEntry{root + n.path, toObsidian(vault, n).toUtf8()});
        for (const FileEntry &a : vault.attachments())
            entries.append(FileEntry{root + a.path, a.data});
        entries.append(FileEntry{root + u".obsidian/app.json"_s, appJson(vault.config())});
        entries.append(FileEntry{root + u".obsidian/daily-notes.json"_s, dailyNotesJson(vault.config())});
        ExportResult r;
        r.files.append(
                ExportedFile{vault.fileName(u"obsidian"_s, u".zip"_s), u"application/zip"_s, vault.zip(entries)});
        return r;
    }

private:
    static QString toObsidian(const ExportVault &vault, const ExportVault::Note &note)
    {
        const QString content = logseqBlocksToBrulion(note.content);
        return replaceLinks(content, [&](const FoundLink &l) -> std::optional<QString> {
            switch (l.kind) {
            case LinkKind::BlockRef:
            case LinkKind::BlockEmbed: {
                const auto block = vault.block(l.target);
                if (!block)
                    return std::nullopt;
                const QString page =
                        block->first == note.path ? QString() : paths::stripMarkdownExtension(block->first);
                return wikiLink(page, {}, l.target, {}, l.kind == LinkKind::BlockEmbed);
            }
            case LinkKind::Wiki:
            case LinkKind::Embed: {
                // Brulion resolves path links relative to the note; Obsidian from the vault root.
                if (!l.target.contains(u'/'))
                    return std::nullopt;
                const auto resolved = vault.resolve(l.target, note.path);
                if (!resolved)
                    return std::nullopt;
                const QString target =
                        paths::isMarkdown(*resolved) ? paths::stripMarkdownExtension(*resolved) : *resolved;
                if (target.compare(l.target, Qt::CaseInsensitive) == 0)
                    return std::nullopt;
                return wikiLink(target, l.heading, l.blockId, l.display, l.kind == LinkKind::Embed);
            }
            default: return std::nullopt;
            }
        });
    }

    static QByteArray appJson(const VaultConfig &config)
    {
        QJsonObject o;
        o.insert(u"attachmentFolderPath"_s, config.attachmentsFolder.isEmpty() ? u"/"_s : config.attachmentsFolder);
        o.insert(u"newLinkFormat"_s, u"shortest"_s);
        o.insert(u"useMarkdownLinks"_s, false);
        if (!config.newPagesFolder.isEmpty()) {
            o.insert(u"newFileLocation"_s, u"folder"_s);
            o.insert(u"newFileFolderPath"_s, config.newPagesFolder);
        }
        return QJsonDocument(o).toJson(QJsonDocument::Indented);
    }

    static QByteArray dailyNotesJson(const VaultConfig &config)
    {
        QJsonObject o;
        o.insert(u"folder"_s, config.journalsFolder);
        o.insert(u"format"_s, momentFormat(config.journalFileFormat));
        if (!config.journalTemplate.isEmpty())
            o.insert(u"template"_s, paths::stripMarkdownExtension(config.journalTemplate));
        return QJsonDocument(o).toJson(QJsonDocument::Indented);
    }
};

} // namespace

std::unique_ptr<Exporter> makeMarkdownZipExporter()
{
    return std::make_unique<MarkdownZipExporter>();
}

std::unique_ptr<Exporter> makeObsidianZipExporter()
{
    return std::make_unique<ObsidianZipExporter>();
}

} // namespace brulion::io::detail

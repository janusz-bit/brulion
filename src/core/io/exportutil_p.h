// Shared machinery of the exporters: the vault being exported (notes parsed and indexed with
// VaultIndex for SYNTAX §2 link resolution), file names, MIME types, deterministic block UUIDs.
// Internal to the io module.
#pragma once

#include "io/exporter.h"
#include "types.h"

#include <QHash>

#include <memory>
#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::io::detail {

class ExportVault {
public:
    struct Note {
        VaultPath path;
        QString content;
        const ParsedNote *parsed = nullptr; ///< owned by the index
        std::optional<QDate> journal;
    };

    explicit ExportVault(const ExportInput &input);
    ~ExportVault();
    ExportVault(const ExportVault &) = delete;
    ExportVault &operator=(const ExportVault &) = delete;

    const ExportInput &input() const { return m_input; }
    const VaultConfig &config() const { return m_input.config; }
    /// Notes (Markdown files outside ignored folders), sorted by path.
    const QList<Note> &notes() const { return m_notes; }
    /// Other files (attachments), sorted by path; `.brulion/` is not included.
    const QList<FileEntry> &attachments() const { return m_attachments; }
    const Note *note(const VaultPath &path) const;
    bool isAttachment(const VaultPath &path) const;
    const FileEntry *attachment(const VaultPath &path) const;
    /// SYNTAX §2 resolution of a link written in note `from` (notes and attachments). `target`
    /// is a parsed link target (FoundLink::target): '#' and '|' in it belong to the file name.
    std::optional<VaultPath> resolve(const QString &target, const VaultPath &from) const;
    /// The note and block index of a block id (`^id` / `id::`).
    std::optional<std::pair<VaultPath, int>> block(const QString &id) const;
    const VaultIndex &index() const { return *m_index; }

    /// "Vault name" or "Brulion".
    QString vaultName() const;
    /// "<name>-<yyyy-MM-dd>[-suffix]<extension>" (date from ExportInput::now when valid).
    QString fileName(const QString &suffix, const QString &extension) const;
    /// ZIP with the export time as modification time.
    QByteArray zip(const QList<FileEntry> &entries) const;

private:
    ExportInput m_input;
    std::unique_ptr<VaultIndex> m_index;
    QList<Note> m_notes;
    QList<FileEntry> m_attachments;
    QHash<QString, qsizetype> m_noteIndex;
    QHash<QString, qsizetype> m_attachmentIndex;
};

/// MIME type of a file by extension ("application/octet-stream" when unknown).
QString mimeForPath(const QString &path);

/// Is `id` a UUID (8-4-4-4-12 hex)?
bool isUuid(const QString &id);
/// Logseq id of a block id: the id itself when it is a UUID, else a deterministic UUID.
QString logseqUuid(const QString &id);

/// A Qt date format ("yyyy-MM-dd", "dd.MM.yyyy", "dddd") as a moment.js format ("YYYY-MM-DD",
/// "DD.MM.YYYY", "dddd") — Obsidian's daily notes settings.
QString momentFormat(const QString &qtFormat);

/// Text of a block for backlink excerpts and block references (its first line; the note's title
/// when empty or out of range).
QString blockText(const ParsedNote &note, int index);

/// The block of the heading of `note` that the link fragment `fragment` names, or -1 — by
/// markdown::findHeading, the one matcher of the app's links and embeds (markup and tag hashes
/// ignored, Obsidian heading paths "Budżet#Zadania").
int linkedHeading(const ParsedNote &note, const QString &fragment);

/// Root blocks of a heading section ("[[Page#Heading]]" embeds): the heading and following roots
/// up to the next heading of the same or a higher level.
QList<int> sectionBlocks(const ParsedNote &note, const QString &heading);

/// Notes of a one-document export: pages by path, then journals newest first.
QList<const ExportVault::Note *> orderedNotes(const ExportVault &vault);

} // namespace brulion::io::detail

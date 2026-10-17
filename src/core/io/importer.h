// Importers: turn files exported by other apps into vault files (docs/SYNTAX.md dialect). Part of
// the io module — see io/io.h for the overview and the list of importers.
//
// Path rules (enforced by runImport()): pages go under ImportOptions::targetFolder, daily notes
// to the vault's journal path (VaultConfig::journalsFolder + journalFileFormat, appended to
// existing ones), attachments under `<attachmentsFolder>/<subfolder>/` with links rewritten;
// output paths are relative, never contain "..", never point into ignored folders (.brulion/,
// .trash/, …) and never collide with each other or with ImportOptions::exists. Malformed input
// gives warnings, never a failure.
#pragma once

#include "io/files.h"
#include "io/zip.h"
#include "types.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTimeZone>

#include <functional>

namespace brulion::io {

/// A file to write into the vault.
struct ImportedFile {
    VaultPath path;
    QByteArray data;
    /// A daily note: when the file already exists in the vault, append `data` to it (see
    /// appendImported()) instead of creating a copy. Files without the flag never exist yet
    /// (as far as ImportOptions::exists knows).
    bool append = false;

    friend bool operator==(const ImportedFile &, const ImportedFile &) = default;
};

struct ImportResult {
    QList<ImportedFile> files;
    QStringList warnings; ///< user-visible (translated) messages
};

struct ImportOptions {
    /// Vault folder receiving the pages: "" = vault root, e.g. "Import/Notion".
    QString targetFolder;
    /// Vault settings: journalsFolder / journalFileFormat (daily notes), attachmentsFolder.
    VaultConfig config;
    /// Folder inside VaultConfig::attachmentsFolder for the attachments. Empty: the last segment
    /// of targetFolder, else the importer's name ("assets/Notion/…").
    QString attachmentSubfolder;
    /// Time zone of the wall-clock times written into notes; UTC timestamps of the sources
    /// (created/updated, reminders, calendar events) are converted to it.
    QTimeZone timeZone = QTimeZone(QTimeZone::LocalTime);
    /// Does a vault path exist already (compare case-insensitively)? New pages and attachments
    /// then get a unique name ("Page 2.md"). Unset: the vault is treated as empty.
    std::function<bool(const VaultPath &)> exists;
};

/// One supported source app or format. Implementations are stateless and thread-safe.
class Importer {
public:
    virtual ~Importer() = default;

    /// Stable identifier: "obsidian", "logseq", "notion", "evernote", "keep", "joplin",
    /// "simplenote", "roam", "trilium", "bear", "markdown", "html", "ics".
    virtual QString id() const = 0;
    /// Display name of the app or format ("Obsidian", "Google Keep", "iCalendar").
    virtual QString name() const = 0;
    /// File name patterns for file pickers ("*.enex", "*.zip").
    virtual QStringList fileTypes() const = 0;
    /// How sure the importer is that `files` (prepared, see prepareImportFiles()) come from its
    /// app: 0 = not at all, 100 = certain.
    virtual int detect(const QList<InputFile> &files) const = 0;
    /// Convert prepared `files` (see prepareImportFiles()). Use runImport(), which prepares the
    /// input and enforces the path rules on the result.
    virtual ImportResult run(const QList<InputFile> &files, const ImportOptions &options) const = 0;
};

/// All importers, in presentation order. The objects live as long as the program.
const QList<const Importer *> &importers();

/// Importer with the given id, or nullptr.
const Importer *importerById(QStringView id);

/// Unpack archives in place (.zip, .jex / .tar, .bear2bk, .textpack: their files appear in the
/// archive's folder; archives inside an archive only when it holds no notes of its own — Notion's
/// multi-part export is a ZIP of ZIPs, while a .zip in a vault is an attachment and stays one),
/// make paths safe (sanitizeArchivePath()), drop junk
/// (`__MACOSX/`, `.DS_Store`, `Thumbs.db`, `desktop.ini`, `._*` resource forks) and exact
/// duplicates. Damaged archives are kept as plain files and reported in `warnings`.
/// `limits` bound the unpacking (see ZipReadOptions); maxTotalSize and maxEntries hold for all
/// archives of the input together — archives inside archives included —, so a few small archive
/// bombs cannot add up. An archive over the remaining budget is kept as a plain file.
QList<InputFile> prepareImportFiles(const QList<InputFile> &files, QStringList *warnings = nullptr,
                                    const ZipReadOptions &limits = {});

/// The importer most likely meant for `files` (raw or prepared): highest detect() score, or
/// nullptr when no importer recognises them.
const Importer *detectImporter(const QList<InputFile> &files);

/// Prepare `files`, run `importer` and enforce the path rules on its output: paths normalised
/// (no absolute paths, no ".."), files in ignored folders dropped, daily notes for the same
/// date merged, other duplicate or existing paths renamed (" 2") with links to renamed pages
/// rewritten, and a warning for every such fix.
ImportResult runImport(const Importer &importer, const QList<InputFile> &files, const ImportOptions &options);

/// Content of an existing note with imported content appended (for ImportedFile::append): a
/// blank line between them, the imported frontmatter dropped when `existing` has content, and
/// the line endings of `existing` kept.
QByteArray appendImported(const QByteArray &existing, const QByteArray &imported);

} // namespace brulion::io

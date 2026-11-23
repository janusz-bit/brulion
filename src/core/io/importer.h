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

#include "io/csvimport.h"
#include "io/files.h"
#include "io/zip.h"
#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTimeZone>

#include <functional>

namespace brulion::io {

/// A file to write into the vault.
struct ImportedFile {
    ImportedFile() = default;
    ImportedFile(VaultPath filePath, QByteArray bytes, bool appendToExisting = false, QString onDiskFile = {},
                 qint64 onDiskSize = 0)
        : path(std::move(filePath))
        , data(std::move(bytes))
        , append(appendToExisting)
        , file(std::move(onDiskFile))
        , fileSize(onDiskSize)
    { }

    VaultPath path;
    QByteArray data;
    /// A daily note: when the file already exists in the vault, append `data` to it (see
    /// appendImported()) instead of creating a copy. Files without the flag never exist yet
    /// (as far as ImportOptions::exists knows).
    bool append = false;
    /// The bytes are in this file (an InputFile on disk passed on), not in `data`: written into
    /// the notebook in pieces (Vault::createBinaryFrom()). Never with `append`.
    QString file;
    qint64 fileSize = 0;

    qint64 size() const { return file.isEmpty() ? data.size() : fileSize; }

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
    /// The mapping of spreadsheet columns ("csv" and "airtable" importers, csvimport.h).
    CsvImportOptions csv;
    /// "Now" for relative dates in the source ("tomorrow" in a Todoist template), wall-clock time;
    /// invalid: such dates are left out (the io module never reads the clock).
    QDateTime now;
    /// The files given to runImport() are prepareImportFiles()'s output already (the app and the
    /// command line prepare an input once, unpacking its archives to their staging folder, and
    /// import it as often as the options change): not prepared again.
    bool inputPrepared = false;
};

/// One supported source app or format. Implementations are stateless and thread-safe.
class Importer {
public:
    Importer() = default;
    virtual ~Importer() = default;
    Q_DISABLE_COPY_MOVE(Importer)

    /// Stable identifier: "obsidian", "logseq", "notion", "evernote", "keep", "onenote",
    /// "applenotes", "mstodo", "todoist", "joplin", "simplenote", "roam", "trilium", "bear",
    /// "airtable", "markdown", "html", "csv", "ics".
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
/// bombs cannot add up. An archive over the remaining budget is kept as a plain file. An input
/// file on disk (InputFile::file: a picked archive not read in) is read from there piece by piece;
/// with limits.stagingFolder set, large attachments are unpacked into files there (FileEntry on
/// disk) instead of into memory, and an archive staged there is removed once unpacked.
QList<InputFile> prepareImportFiles(const QList<InputFile> &files, QStringList *warnings = nullptr,
                                    const ZipReadOptions &limits = {});

/// The importer most likely meant for `files` (raw, or `prepared` by prepareImportFiles()):
/// highest detect() score, or nullptr when no importer recognises them.
const Importer *detectImporter(const QList<InputFile> &files, bool prepared = false);

/// Prepare `files` (unless ImportOptions::inputPrepared), run `importer` and enforce the path rules
/// on its output (files on disk passed on as they are): paths normalised
/// (no absolute paths, no ".."), files in ignored folders dropped, daily notes for the same
/// date merged, other duplicate or existing paths renamed (" 2") with links to renamed pages
/// rewritten, and a warning for every such fix.
ImportResult runImport(const Importer &importer, const QList<InputFile> &files, const ImportOptions &options);

/// Content of an existing note with imported content appended (for ImportedFile::append): a
/// blank line between them, the imported frontmatter dropped when `existing` has content, and
/// the line endings of `existing` kept.
QByteArray appendImported(const QByteArray &existing, const QByteArray &imported);

} // namespace brulion::io

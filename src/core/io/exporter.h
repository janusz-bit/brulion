// Exporters: turn the vault (or a part of it) into files for other apps and formats. Part of the
// io module — see io/io.h for the overview and the list of exporters.
#pragma once

#include "io/files.h"
#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::io {

/// What to export.
struct ExportInput {
    /// Vault files with vault-relative paths: Markdown notes and attachments (other files, e.g.
    /// `.brulion/config.json`, are used only where it makes sense — see runExport()).
    QList<FileEntry> files;
    VaultConfig config;
    /// Vault name, used for file and folder names and titles ("" -> "Brulion").
    QString vaultName;
    /// Local time of the export: file names, ZIP timestamps, iCalendar DTSTAMP, JSON exportedAt.
    /// Exporters never read the clock.
    QDateTime now;
};

/// A produced file: a suggested file name, its MIME type and content.
struct ExportedFile {
    QString name;
    QString mime;
    QByteArray data;

    friend bool operator==(const ExportedFile &, const ExportedFile &) = default;
};

struct ExportResult {
    QList<ExportedFile> files;
    QStringList warnings; ///< user-visible (translated) messages
};

/// One supported target format. Implementations are stateless and thread-safe.
class Exporter {
public:
    virtual ~Exporter() = default;

    /// Stable identifier: "markdown-zip", "obsidian-zip", "logseq-zip", "html-site", "html-single",
    /// "pandoc", "json", "ics", "opml", "tasks-csv".
    virtual QString id() const = 0;
    /// Display name ("Obsidian (ZIP)", "iCalendar").
    virtual QString name() const = 0;
    /// Extension of the produced file (".zip", ".html", ".json", ".ics", ".opml", ".csv").
    virtual QString fileExtension() const = 0;
    /// Export. Use runExport(), which cleans the input first.
    virtual ExportResult run(const ExportInput &input) const = 0;
};

/// All exporters, in presentation order. The objects live as long as the program.
const QList<const Exporter *> &exporters();

/// Exporter with the given id, or nullptr.
const Exporter *exporterById(QStringView id);

/// Clean `input` (paths normalised, empty / duplicate paths, ignored folders such as .trash/ and
/// sync-conflict copies dropped; `.brulion/config.json` kept) and run `exporter`.
ExportResult runExport(const Exporter &exporter, const ExportInput &input);

} // namespace brulion::io

// Exporters: turn the vault (or a part of it) into files for other apps and formats. Part of the
// io module — see io/io.h for the overview and the list of exporters.
#pragma once

#include "io/files.h"
#include "types.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::io {

/// A copy of a picture from the internet that an export includes instead of the picture's URL
/// (the app takes it from the device's cache: exporters never touch the network).
struct WebPictureCopy {
    QByteArray data;
    /// The picture's format by its bytes ("png", "jpg", "svg"… — webpicture::sniffFormat).
    QString format;
    /// File name of the copy in the export ("kot.jpg"; made unique and file-name-safe).
    QString fileName;
};

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
    /// Pictures from the internet that the HTML and Pandoc exports include as files ("html-site",
    /// "pandoc": `assets/web/<fileName>`; "html-single": data: URIs), by the URL as the link
    /// target gives it (webPictureTargets()). A picture not in here keeps its URL; other
    /// exporters keep every URL (the notes' text is not changed).
    QHash<QString, WebPictureCopy> webPictures;
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

/// The pictures from the internet the HTML and Pandoc exports of `input` show (`![](https://…)`,
/// `![[https://…]]` outside code, as the exporters see them), by their URL as written, in the
/// order of first use — what ExportInput::webPictures may hold copies of. `input` is cleaned like
/// runExport() does.
QStringList webPictureTargets(const ExportInput &input);

/// Clean `input` (paths normalised, empty / duplicate paths, ignored folders such as .trash/ and
/// sync-conflict copies dropped; `.brulion/config.json` kept) and run `exporter`.
ExportResult runExport(const Exporter &exporter, const ExportInput &input);

} // namespace brulion::io

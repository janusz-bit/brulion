// Exporters: turn the vault (or a part of it) into files for other apps and formats. Part of the
// io module — see io/io.h for the overview and the list of exporters.
#pragma once

#include "calc/calclocale.h"
#include "io/files.h"
#include "markdown/tagstyle.h"
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
    /// Paper of the PDF and DOCX exports: "a4" | "letter" (io/paper.h: paperSizeFor() of the
    /// device's locale); anything else is A4.
    QString paperSize;
    /// What the macros of extensions show (docs/SYNTAX.md §13 "Macros"), by a note's path and the
    /// macro as written (`{{ext:silownia/rekord Przysiad}}`): Markdown text the documents read
    /// ("html-site", "html-single", "pdf", "docx", "pandoc" put it in place of the macro: the app
    /// gives what it computed). A macro not in here — and in the Markdown, Obsidian, Logseq,
    /// JSON and other exports every one — stays as written.
    QHash<QString, QHash<QString, QString>> macroTexts;
    /// The language inline calculations' results are written in ("html-site", "html-single", "pdf",
    /// "docx", "pandoc" show the values of inline calculations and queries: docs/SYNTAX.md §9
    /// "Inline calculations", §10 "Inline queries"): the UI's.
    calc::CalcLocale calcLocale = calc::CalcLocale::Polish;
    /// The footer of the HTML exports' pages ("html-site", "html-single"; "" = "Exported from
    /// Brulion on <now>"). Publishing gives one without the time, so that a page that did not
    /// change stays the same bytes.
    QString footer;
    /// Extensions' styles of blocks by tag (b.app.tagStyle; docs/EXTENSIONS.md "Styles of blocks
    /// by tag"): "html-site", "html-single", "pdf" and "docx" tint the blocks as the editor does
    /// (a light background, a bar at the left edge in HTML; the paper documents the background).
    /// Empty: no tints.
    markdown::TagStyles tagStyles;
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
    Exporter() = default;
    virtual ~Exporter() = default;
    Q_DISABLE_COPY_MOVE(Exporter)

    /// Stable identifier: "markdown-zip", "obsidian-zip", "logseq-zip", "html-site", "html-single",
    /// "pdf", "docx", "pandoc", "json", "ics", "opml", "tasks-csv".
    virtual QString id() const = 0;
    /// Display name ("Obsidian (ZIP)", "iCalendar").
    virtual QString name() const = 0;
    /// Extension of the produced file (".zip", ".html", ".pdf", ".docx", ".json", ".ics", ".opml",
    /// ".csv").
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

/// Options of htmlSiteFiles() (the "html-site" export uses the defaults).
struct HtmlSiteOptions {
    /// A search box in every page's header: `search.js` and the pages' text in `search-index.js`,
    /// loaded when the box is first used — no server needed (file:// pages search too). The site's
    /// own script, the only one a page then has (note content never becomes markup that runs);
    /// off for the ZIP export, on for publishing.
    bool search = false;
};

/// The static website of the "html-site" export as files with site-relative paths ("index.html",
/// "style.css", "Projekty/Dom.html", "tags/dom.html", "search.js", the attachments at their
/// paths) — what its ZIP holds in the notebook's folder. `input` is cleaned (and the macros of
/// extensions replaced) as runExport() does. Publishing uploads them (publish/publish.h).
QList<FileEntry> htmlSiteFiles(const ExportInput &input, const HtmlSiteOptions &options = {});

} // namespace brulion::io

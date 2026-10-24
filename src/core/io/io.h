// brulion::io — import from other apps, export to other formats, and the file formats behind
// them. Include this header to get the whole module (namespace brulion::io; QtGui is used for
// QTextDocument; nothing here reads the clock — ExportInput::now / ImportOptions carry time).
//
// API overview:
//
//   files.h        struct FileEntry { QString path; QByteArray data; }  (using InputFile = FileEntry)
//
//   importer.h     struct ImportOptions { targetFolder, config (VaultConfig), attachmentSubfolder,
//                    timeZone (wall-clock zone of imported times), exists (vault path check) }
//                  struct ImportedFile { VaultPath path; QByteArray data; bool append; }
//                    (append: a daily note — append to an existing file with appendImported())
//                  struct ImportResult { QList<ImportedFile> files; QStringList warnings; }
//                  class Importer { id(), name(), fileTypes(), detect(files) -> 0..100,
//                    run(files, options) }
//                  importers()  — obsidian, logseq, notion, evernote, keep (Google Keep Takeout),
//                    joplin (JEX / RAW / MD+Front Matter), simplenote, roam (JSON), trilium (ZIP
//                    export, HTML or Markdown), bear (TextBundle / .bear2bk), markdown
//                    (md/markdown/txt + attachments), html, ics
//                  importerById(id), detectImporter(files) -> best importer or nullptr
//                  prepareImportFiles(files, &warnings, limits) (archives unpacked in place:
//                    .zip, .jex/.tar, .bear2bk, .textpack — archives in archives only for a ZIP of
//                    ZIPs (Notion); one unpack budget for all; junk dropped; paths made safe)
//                  runImport(importer, files, options) -> ImportResult (path rules enforced:
//                    pages under targetFolder, journals at VaultConfig journal paths, attachments
//                    under <attachmentsFolder>/<subfolder>/, never absolute / ".." / ignored
//                    folders, unique names " 2" with links to renamed pages rewritten)
//                  appendImported(existing, imported) -> merged daily note
//
//   exporter.h     struct ExportInput { QList<FileEntry> files; VaultConfig config; QString
//                    vaultName; QDateTime now; }
//                  struct ExportedFile { QString name, mime; QByteArray data; }
//                  struct ExportResult { QList<ExportedFile> files; QStringList warnings; }
//                  class Exporter { id(), name(), fileExtension(), run(input) }
//                  exporters()  — markdown-zip, obsidian-zip, logseq-zip, html-site,
//                    html-single, pandoc (one Pandoc Markdown document + linked attachments),
//                    json (brulion-export v1), ics, opml, tasks-csv
//                  exporterById(id), runExport(exporter, input) (input cleaned first)
//
//   zip.h          readZip(zip, &warnings, ZipReadOptions{expandNested, maxNestingDepth,
//                    maxEntrySize, maxTotalSize, maxEntries}) -> QList<FileEntry>,
//                  writeZip(entries, ZipWriteOptions{compressionLevel, storeCompressedFormats,
//                    modified}) -> QByteArray, isZip(data), sanitizeArchivePath(name)
//   tar.h          readTar(tar, &warnings), isTar(data) — ustar / GNU long names / PAX paths
//   csv.h          parseCsv(text, sep), csvField, csvRow, csvSafeText (formula injection)
//   ical.h         namespace ical: parse(data) -> Components (unfolding, unescaping),
//                  Writer (CRLF, 75-octet UTF-8-safe folding, escaping), parseDateTime,
//                  toWallClock(value, zone), parseDuration / parseDurationParts (nominal days +
//                  exact seconds, RFC 5545 3.3.6), parseRRule / formatRRule
//                  (<-> dates::RecurrenceRule), escapeText / unescapeText / foldLine
//   htmlmarkdown.h htmlToMarkdown(html, {image, link callbacks}) -> {markdown, title}
//                  (QTextDocument parses — no resources ever loaded —, own Markdown writer:
//                  no wrapping, GFM tables, checkboxes, code languages), htmlToPlainText,
//                  escapeMarkdownText(text, lineStart, linkLabel)
//   clipboardhtml.h clipboardHtml(markdown, {config, attachmentUrl, blockText}) — copied blocks
//                  as escaped HTML for the clipboard (pictures as links, never data: URLs)
//
// Malformed input never crashes or throws: importers and readers report warnings and import
// what they can.
#pragma once

#include "io/clipboardhtml.h"
#include "io/csv.h"
#include "io/exporter.h"
#include "io/files.h"
#include "io/htmlmarkdown.h"
#include "io/ical.h"
#include "io/importer.h"
#include "io/tar.h"
#include "io/zip.h"

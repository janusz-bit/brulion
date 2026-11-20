// Factories of the built-in exporters. Internal to the io module.
#pragma once

#include "io/exporter.h"

#include <memory>

namespace brulion::io::detail {

std::unique_ptr<Exporter> makeMarkdownZipExporter();
std::unique_ptr<Exporter> makeObsidianZipExporter();
std::unique_ptr<Exporter> makeLogseqZipExporter();
std::unique_ptr<Exporter> makeHtmlSiteExporter();
std::unique_ptr<Exporter> makeHtmlSingleExporter();
std::unique_ptr<Exporter> makePdfExporter();
std::unique_ptr<Exporter> makeDocxExporter();
std::unique_ptr<Exporter> makePandocExporter();
std::unique_ptr<Exporter> makeJsonExporter();
std::unique_ptr<Exporter> makeIcsExporter();
std::unique_ptr<Exporter> makeOpmlExporter();
std::unique_ptr<Exporter> makeTasksCsvExporter();

/// runExport()'s cleaning of the input (paths normalised, ignored and duplicate files dropped).
ExportInput cleanExportInput(const ExportInput &input);

/// cleanExportInput() and, for the documents (`exporterId` "html-site", "html-single", "pdf",
/// "docx", "pandoc"), the macros of extensions replaced by what they show (ExportInput::macroTexts).
ExportInput prepareExportInput(const QString &exporterId, const ExportInput &input);

/// The files of the "html-site" export (htmlSiteFiles()) of a prepared input.
QList<FileEntry> htmlSiteEntries(const ExportInput &input, const HtmlSiteOptions &options);

} // namespace brulion::io::detail

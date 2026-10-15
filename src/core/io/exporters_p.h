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
std::unique_ptr<Exporter> makeJsonExporter();
std::unique_ptr<Exporter> makeIcsExporter();
std::unique_ptr<Exporter> makeOpmlExporter();
std::unique_ptr<Exporter> makeTasksCsvExporter();

} // namespace brulion::io::detail

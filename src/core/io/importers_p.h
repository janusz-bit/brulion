// Factories of the built-in importers (one source file each). Internal to the io module.
#pragma once

#include "io/importer.h"

#include <memory>

namespace brulion::io::detail {

std::unique_ptr<Importer> makeObsidianImporter();
std::unique_ptr<Importer> makeLogseqImporter();
std::unique_ptr<Importer> makeNotionImporter();
std::unique_ptr<Importer> makeEvernoteImporter();
std::unique_ptr<Importer> makeKeepImporter();
std::unique_ptr<Importer> makeJoplinImporter();
std::unique_ptr<Importer> makeSimplenoteImporter();
std::unique_ptr<Importer> makeRoamImporter();
std::unique_ptr<Importer> makeBearImporter();
std::unique_ptr<Importer> makeMarkdownImporter();
std::unique_ptr<Importer> makeHtmlImporter();
std::unique_ptr<Importer> makeIcsImporter();

} // namespace brulion::io::detail

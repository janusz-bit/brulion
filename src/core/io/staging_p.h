// What archives unpack into the staging folder instead of memory (ZipReadOptions::stagingFolder):
// shared by the ZIP and tar readers. Internal to the io module.
#pragma once

#include <QString>

namespace brulion::io::detail {

/// Entries unpacked to a file of the staging folder: pictures, audio, video, PDFs and archives —
/// what importers pass on or unpack (prepareImportFiles()) without reading it.
bool isStagedKind(const QString &path);

} // namespace brulion::io::detail

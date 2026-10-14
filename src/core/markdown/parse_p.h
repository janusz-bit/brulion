// Internal parse entry points of the markdown module.
#pragma once

#include "markdown/structure_p.h"
#include "types.h"

namespace brulion::markdown::detail {

/// parseNote() with structure options (used to parse one block of a document on its own).
ParsedNote parseNoteWith(const VaultPath &path, QStringView content, const VaultConfig &config,
                         StructureOptions options);

} // namespace brulion::markdown::detail

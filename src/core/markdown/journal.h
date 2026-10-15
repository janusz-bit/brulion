// Journal (daily note) paths (docs/SYNTAX.md §1): `<journalsFolder>/<journalFileFormat>.md`
// with Qt QDate format strings ("yyyy-MM-dd", "dd.MM.yyyy", "yyyy/MM/yyyy-MM-dd" for nested
// folders). Part of the markdown module — see markdown/markdown.h for the module overview.
#pragma once

#include "types.h"

#include <QDate>
#include <QLocale>

#include <optional>

namespace brulion::markdown {

/// Vault path of the daily note for `date`, e.g. "journals/2026-10-14.md". Day and month names
/// in the format are written in `locale` (English by default). A format that does not
/// round-trip (it cannot be parsed back to the same date) falls back to "yyyy-MM-dd".
VaultPath journalPathForDate(QDate date, const VaultConfig &config, const QLocale &locale = QLocale::c());

/// Date of a daily note path, or nullopt when `path` is not a journal page: a Markdown file
/// inside `config.journalsFolder` (case-insensitive; "" = vault root) whose name parses
/// strictly (exact round trip) with `journalFileFormat` or the common "yyyy-MM-dd" /
/// "yyyy_MM_dd". Day and month names may be English or Polish.
std::optional<QDate> journalDateFromPath(const VaultPath &path, const VaultConfig &config);

/// The day a wiki-link target names, or nullopt: a daily note's name as journalDateFromPath()
/// reads it ("2026-10-13", the vault's journalFileFormat such as "13.10.2026"), also with the
/// journals folder or ".md" ("journals/2026-10-13.md"). Display only: the link stays as written.
std::optional<QDate> journalDateFromLinkTarget(const QString &target, const VaultConfig &config);

} // namespace brulion::markdown

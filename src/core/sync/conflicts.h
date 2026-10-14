// Conflict copies: Brulion's own ("Note.conflict-<device>-<yyyyMMdd-HHmmss>.md", made by the
// WebDAV engine when both sides changed and could not be merged) and Syncthing's
// ("Note.sync-conflict-<yyyyMMdd>-<HHmmss>-<DEVICE>.md"). The Sync Center lists them so the
// user can merge them back.
#pragma once

#include "types.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::sync {

/// A conflict copy found in the vault.
struct ConflictCopy {
    enum class Source { Syncthing, Brulion };
    VaultPath original; ///< the file the copy belongs to
    VaultPath copy;     ///< the conflict copy itself
    QString time;       ///< local time of the conflict, "YYYY-MM-DDTHH:mm:ss"
    QString device;     ///< Syncthing short device id or Brulion device name
    Source source = Source::Brulion;

    friend bool operator==(const ConflictCopy &, const ConflictCopy &) = default;
};

/// Local wall-clock stamp "yyyyMMdd-HHmmss".
QString conflictStamp(const QDateTime &time);

/// Device name usable inside a file name: letters (any script), digits, '_' and '-' only; other
/// runs become one '-', at most 40 UTF-16 code units (a surrogate pair is never split).
/// "Mój laptop" -> "Mój-laptop"; nothing usable -> "urzadzenie".
QString sanitizeDeviceName(const QString &device);

/// "dir/Name.ext" -> "dir/Name.conflict-<device>-<yyyyMMdd-HHmmss>.ext" (no extension: appended).
/// The caller makes the path unique (Vault::create appends " 2").
VaultPath conflictCopyPath(const VaultPath &path, const QString &device, const QDateTime &time);

/// Parsed Brulion conflict copy name.
struct BrulionConflict {
    VaultPath original;
    QString time;   ///< "YYYY-MM-DDTHH:mm:ss"
    QString device;

    friend bool operator==(const BrulionConflict &, const BrulionConflict &) = default;
};

/// Parse "dir/Name.conflict-<device>-<yyyyMMdd>-<HHmmss>[ N].ext"; std::nullopt for other names.
std::optional<BrulionConflict> parseBrulionConflict(const VaultPath &path);

/// Conflict copies among `files` (Syncthing and Brulion; folders skipped), sorted by original
/// path, then time.
QList<ConflictCopy> findConflicts(const QList<FileStat> &files);

} // namespace brulion::sync

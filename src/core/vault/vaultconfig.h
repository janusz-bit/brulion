// `.brulion/config.json` — vault settings shared between devices (docs/SYNTAX.md §1).
//
//   parseVaultConfig(json)            -> ParsedVaultConfig{config, raw, warnings}
//   serializeVaultConfig(cfg, raw)    -> indented JSON; unknown keys of `raw` are preserved
//   vaultConfigToJson(cfg)            -> the known keys only
//   sameVaultConfig(a, b)             -> field-wise equality (VaultConfig has no operator==)
//
// Missing keys and keys with invalid values fall back to the VaultConfig defaults (one warning
// per invalid value); invalid JSON or a non-object yields the defaults plus a warning. Unknown
// keys are kept so a newer Brulion on another device does not lose its settings when this
// device saves the file. `weekStartsOn` uses the JavaScript convention of the v1 app
// (0 = Sunday, 1 = Monday, … 6 = Saturday); 7 is accepted as Sunday too. `journalMode` /
// `pageMode` are "outline" or "document" (VaultConfig::journalMode / pageMode, the note modes of
// markdown/notemode.h; case-insensitive when read).
#pragma once

#include "types.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QStringList>

namespace brulion {

/// ".brulion/config.json" — the vault path of the settings file.
QString vaultConfigPath();

struct ParsedVaultConfig {
    VaultConfig config;   ///< defaults merged with the valid values of the file
    QJsonObject raw;      ///< the whole JSON object incl. unknown keys ({} when invalid)
    QStringList warnings; ///< human-readable problems (English; logged by the Vault)
};

/// Parse config.json text (a leading UTF-8 BOM is ignored).
ParsedVaultConfig parseVaultConfig(QByteArrayView json);

/// The known keys of `config` (normalised; invalid values replaced by the defaults).
QJsonObject vaultConfigToJson(const VaultConfig &config);

/// `raw` with the known keys replaced by `config`'s values, as indented JSON ending in "\n".
QByteArray serializeVaultConfig(const VaultConfig &config, const QJsonObject &raw = {});

/// True when every field of `a` equals the one of `b`.
bool sameVaultConfig(const VaultConfig &a, const VaultConfig &b);

/// True when `format` (Qt QDate format) gives distinct names for distinct days, months and
/// years — a format without day/month/year fields would put every journal into one file.
bool isUsableJournalFormat(const QString &format);

} // namespace brulion

// Obsidian vault settings as Brulion's defaults (docs/IMPORT.md "Folder Obsidiana bez importu").
//
//   qtDateFormatFromMoment(moment)                 -> Qt QDate format, or nullopt
//   adoptObsidianSettings(files, tracked, base)    -> ObsidianAdoption{config, adoptedKeys, warnings}
//
// A notebook opened in an Obsidian vault that has no .brulion/config.json takes the journal
// folder, file name format and template from .obsidian/daily-notes.json, the templates folder
// from .obsidian/templates.json and the attachments folder from .obsidian/app.json — in memory
// (the Vault never writes them; saving the settings once makes them config.json values).
// Pure functions: the Vault reads the files and passes their bytes.
//
// Rules
//  * daily-notes.json {"folder", "format", "template"}: folder normalised ("" and "/" = the top
//    of the notebook); format converted from moment.js (empty = "YYYY-MM-DD", Obsidian's
//    default; a format with a token Qt lacks is not used, with a warning); template normalised
//    with ".md" appended (Obsidian stores it without the extension; empty = no template, unless
//    Brulion's own template file is there).
//  * No daily-notes.json: Obsidian's defaults (top of the notebook, YYYY-MM-DD) are taken only
//    when the daily notes core plugin is not switched off (.obsidian/core-plugins.json, object
//    or array form) and the top of the notebook holds a "yyyy-MM-dd.md" file.
//  * app.json "attachmentFolderPath": missing, "" or "/" = the top of the notebook; "./…"
//    (next to the note) has no Brulion equivalent: the default stays, with a warning.
//  * templates.json "folder": the templates folder.
//  * Guard for notebooks Brulion already used with its defaults (they have no config.json
//    either): nothing journal-related is taken while the default journals folder holds a
//    Markdown file, the attachments folder while the default one holds a file, the templates
//    folder while the default one holds a file. Files in Obsidian's own folder for that setting
//    do not count (Obsidian's days in "journals/" or "Journals/" are no sign of Brulion's use;
//    Brulion's folders match in any letter case).
//  * Invalid JSON, a non-object or a value of the wrong type: a warning, that value is skipped.
//    Warnings are English (shown on the settings page like config.json's).
#pragma once

#include "types.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace brulion {

/// A moment.js date format (Obsidian's daily notes) as a Qt QDate format, or nullopt when it
/// uses a token Qt cannot write (Do, d, dd, DDD, w, W, gggg, Q, X, HH, mm…), an unknown letter,
/// an unclosed [literal] or a "'" (Qt cannot read such names back). `[text]` and `\c` become
/// quoted Qt literals.
std::optional<QString> qtDateFormatFromMoment(const QString &moment);

/// The vault paths of Obsidian's settings files read by the Vault.
QString obsidianDailyNotesPath();  ///< ".obsidian/daily-notes.json"
QString obsidianAppPath();         ///< ".obsidian/app.json"
QString obsidianTemplatesPath();   ///< ".obsidian/templates.json"
QString obsidianCorePluginsPath(); ///< ".obsidian/core-plugins.json"

/// The raw bytes of Obsidian's settings files; nullopt = the file is absent.
struct ObsidianVaultFiles {
    std::optional<QByteArray> dailyNotes;  ///< .obsidian/daily-notes.json
    std::optional<QByteArray> app;         ///< .obsidian/app.json
    std::optional<QByteArray> templates;   ///< .obsidian/templates.json
    std::optional<QByteArray> corePlugins; ///< .obsidian/core-plugins.json
};

struct ObsidianAdoption {
    VaultConfig config;      ///< the base with the adopted fields replaced
    /// config.json keys whose value was taken from Obsidian and differs from the base, in the
    /// order of the settings page: "journalsFolder", "journalFileFormat", "journalTemplate",
    /// "templatesFolder", "attachmentsFolder".
    QStringList adoptedKeys;
    QStringList warnings;    ///< English
};

/// `trackedFiles`: the vault's tracked file paths (for the guards).
ObsidianAdoption adoptObsidianSettings(const ObsidianVaultFiles &files, const QStringList &trackedFiles,
                                       const VaultConfig &base = {});

/// True when a tracked file at `path` coming or going can change what adoptObsidianSettings()
/// takes with that `base` (a file at the top of the notebook or in one of the guarded folders).
bool obsidianAdoptionDependsOn(const QString &path, const VaultConfig &base = {});

} // namespace brulion

// settingsnote — the notebook's settings in a note (docs/NOTEBOOK-SETTINGS.md "The settings note"):
// the page `Notebook settings.md` / `Ustawienia notatnika.md` whose frontmatter says
// `brulion: settings` and holds config.json's keys as YAML. Pure: texts in, texts and settings out.
//
//   isSettingsNote(content)                 its frontmatter says `brulion: settings`
//   noteSettings(content)                   its keys as a JSON object (`brulion` left out)
//   mergedConfig(configJson, noteContent)   config.json's settings with the note's over them, key by
//                                           key (a note's invalid value: config.json's kept, warned)
//   migratedNote(configJson, note, lang)    the note made from config.json, or completed: idempotent
//   withSetting(note, key, value)           one key written into the note's frontmatter in place
#pragma once

#include "vault/vaultconfig.h"

#include <QByteArrayView>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::settingsnote {

/// The note's names at the notebook's root, in the order they are looked for.
QStringList noteNames();
/// The name a new note gets in a notebook of `language` ("pl": Polish, else English).
QString noteName(QStringView language);

/// The frontmatter of `content` says `brulion: settings`.
bool isSettingsNote(QStringView content);

/// The settings `content`'s frontmatter holds (every key but `brulion`), as JSON.
QJsonObject noteSettings(QStringView content);

/// config.json's settings (`configJson`, "" none) with the note's (`noteContent`, "" none) over them,
/// key by key. A key of the note whose value is not valid keeps config.json's value (or the default)
/// and is warned about ("Notebook settings: …"). `raw` is the merged object (unknown keys kept).
ParsedVaultConfig mergedConfig(QByteArrayView configJson, QStringView noteContent);
/// mergedConfig() over settings read already (config.json's, or those taken from an Obsidian vault).
ParsedVaultConfig mergedOver(const ParsedVaultConfig &base, QStringView noteContent);
/// The keys whose values differ between `before` and `after` (vaultConfigToJson()'s), with `after`'s
/// values: what a save changes in the note.
QJsonObject changedSettings(const VaultConfig &before, const VaultConfig &after);

/// The note after a migration from config.json (`configJson`): `note` ("" none yet) with every key
/// config.json has that differs from the defaults and the note lacks — a key both have keeps the
/// note's; a new note gets `brulion: settings`, a heading and a paragraph in `language`. Running it on
/// its own result changes nothing.
QString migratedNote(QByteArrayView configJson, const QString &note, QStringView language);

/// `note` with `key` set to `value` in its frontmatter (other lines kept byte for byte; an object or
/// a list as YAML); an undefined `value` takes the key out.
QString withSetting(const QString &note, const QString &key, const QJsonValue &value);

} // namespace brulion::settingsnote

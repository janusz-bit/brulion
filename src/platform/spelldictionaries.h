// Where Hunspell dictionaries are (spell_hunspell.cpp; docs/UI.md "Spell checking"). Pure, so
// tested on every platform (tests/platform/tst_spelldictionaries.cpp).
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace brulion::platform {

/// One dictionary: `name` as its files are named ("pl_PL"), `language` its primary subtag ("pl").
struct SpellDictionary {
    QString language;
    QString name;
    QString affPath;
    QString dicPath;

    friend bool operator==(const SpellDictionary &, const SpellDictionary &) = default;
};

/// The folders to look in, in order, without duplicates and empty entries:
///  1. `dicPathVariable` (Hunspell's DICPATH, entries separated by `listSeparator`: ':' or ';');
///  2. the app's own: `<appDir>/dictionaries` (Windows), `<appDir>/../share/hunspell` (the AppImage,
///     a `cmake --install` prefix);
///  3. `builtIn` (BRULION_SPELL_DICTIONARY_DIRS of the build: the Nix package's store paths);
///  4. `<dir>/hunspell` and `<dir>/myspell` of each of `dataDirs` (XDG_DATA_DIRS, NixOS profiles),
///     then /usr/share/hunspell, /usr/share/myspell, /usr/share/myspell/dicts,
///     /usr/local/share/hunspell when `systemDirs`.
QStringList spellDictionaryDirs(const QString &dicPathVariable, QChar listSeparator, const QString &appDir,
                                const QStringList &builtIn, const QStringList &dataDirs, bool systemDirs);

/// The dictionaries in `dirs` with both a .aff and a .dic file, the best one per language: Polish
/// pl_PL; English en_US, then en_GB, en_CA, en_AU, then any other en_* ("en_GB-ise"); any other
/// language its first name in alphabetical order. A folder earlier in `dirs` wins for the same
/// name; a better name in a later folder still wins over a worse one. Ordered by language.
QList<SpellDictionary> findSpellDictionaries(const QStringList &dirs);

} // namespace brulion::platform

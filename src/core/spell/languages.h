// The languages spell checking uses (docs/UI.md "Spell checking"). Part of the core module
// "spell" — see spell/spell.h.
#pragma once

#include <QString>
#include <QStringList>

namespace brulion::spell {

/// The primary language subtag of a locale or dictionary name, lower case: "pl_PL" → "pl",
/// "en-US" → "en", "en_GB-ise" → "en", "" → "".
QString primaryLanguage(const QString &name);

/// The languages to check, in order (the first one's suggestions come first), of those
/// `available` (primary subtags, as backends report them):
///  * "auto": the UI language, then the system's, then English — "pl" UI on an English system
///    checks Polish and English, an English UI on a Polish system English and Polish;
///  * a list ("pl", "en", "pl,en", "en, pl"): those languages in that order.
/// Unknown and unavailable languages are left out, duplicates too; an empty or unreadable
/// setting counts as "auto".
QStringList resolveLanguages(const QString &setting, const QString &uiLanguage, const QString &systemLanguage,
                             const QStringList &available);

} // namespace brulion::spell

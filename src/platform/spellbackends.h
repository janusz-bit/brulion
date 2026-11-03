// Spell checking backends of this build (docs/UI.md "Spell checking"): one per language, behind
// the core's spell::SpellBackend (src/core/spell/spellchecker.h). One implementation per build,
// chosen by src/platform/CMakeLists.txt (BRULION_SPELL_BACKEND):
//   hunspell  the desktop (Linux, Windows): Hunspell with the Polish and English dictionaries it
//             finds (spelldictionaries.h: DICPATH, next to the app, the build's own folders, the
//             system's) — spell_hunspell.cpp;
//   android   the system's spell checker (TextServicesManager / SpellCheckerSession: the
//             keyboard's dictionaries) through JNI to android/…/SpellCheck.kt — spell_android.cpp;
//   none      the browser and builds without Hunspell: no languages, the app hides spell checking
//             — spell_none.cpp.
#pragma once

#include "spell/spellchecker.h"

#include <QString>
#include <QStringList>

#include <memory>

namespace brulion::platform {

/// "hunspell", "android" or "" (no spell checking in this build).
QString spellEngine();

/// The languages this device can check (primary subtags, "pl", "en"), quickly (Hunspell: the
/// dictionary files found; Android: the system checker's languages). Empty: none — no
/// dictionaries, the system's checker switched off, or no backend in this build.
QStringList spellLanguages();

/// A backend checking `language` (one of spellLanguages()), or nullptr. Slow — Hunspell reads its
/// dictionary (Polish: a few hundred ms), Android opens a session — so call it on the thread that
/// will use the backend (a worker), never the UI thread. A backend is used by one thread at a
/// time.
std::unique_ptr<spell::SpellBackend> createSpellBackend(const QString &language);

} // namespace brulion::platform

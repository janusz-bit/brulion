#pragma once
// The extensions that come with Brulion (resources :/extensions/…, src/core/extensions/builtin/):
// trusted code, written only with the public API (docs/EXTENSIONS.md).
//
//   for (const BuiltinExtension &b : builtinExtensions()) engine.load(b.extension);
//
// silownia — the gym (docs/SYNTAX.md §5 "Gym"): silownia.luau + exercises.json (b.data("exercises")).
// asystent — the AI assistant (docs/EXTENSIONS.md "AI"): asystent.luau, b.ai about the page on screen.
// nawyki   — the habits (docs/SYNTAX.md §5 "Habits"): nawyki.luau — the month view, checks written
//            into daily notes, and the commands `brulion-cli habit …` runs (list, set, schedule).
// fiszki   — the flashcards (docs/SYNTAX.md "Flashcards"): fiszki.luau — cards of the Obsidian
//            Spaced Repetition plugin and of Logseq, FSRS-6, their state next to each card, the
//            decks, review and statistics views, and the commands `brulion-cli card …` runs.

#include "extensions/engine.h"

#include <QList>
#include <QString>

namespace brulion::extensions {

struct BuiltinExtension
{
    Extension extension;
    QString titleEn; ///< "Gym"
    QString titlePl; ///< "Siłownia"
};

QList<BuiltinExtension> builtinExtensions();
/// One of them by id, or an empty Extension (id "") when there is none.
Extension builtinExtension(const QString &id);
/// The built-in extension an extension page with id `pageId` overrides (pageextension.h): the one
/// whose id or folded title it is ("silownia", "gym" → "silownia"), else "".
QString builtinFor(const QString &pageId);

} // namespace brulion::extensions

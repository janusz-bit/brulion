// Spell checking — overview of the core module "spell" (docs/UI.md "Spell checking").
//
//   spellwords.h    findWords(line) / wordAt(line, pos): the words of one line of Markdown that
//                   are worth checking — inline code, comments, formulas, links (a Markdown link's
//                   text is checked), URLs, e-mail addresses, file names, tags, task and date
//                   tokens, block ids, property names, task keywords, HTML skipped; words with
//                   digits, single letters, acronyms and mixed case skipped. hyphenParts(),
//                   normalizedWord().
//   spellchecker.h  SpellBackend (one language: check, suggest; the platform's Hunspell or
//                   Android's spell checker, WordListBackend in tests), SpellChecker (several
//                   backends at once — a word is right when one language knows it —, the personal
//                   word list and the ignored words with their letter-case rules, hyphenated
//                   words by their parts, "np." with its dot, merged suggestions).
//   languages.h     resolveLanguages(setting, ui, system, available): "auto" → the UI language,
//                   the system's and English; primaryLanguage("pl_PL") → "pl".
//   dictencoding.h  encodeDictionaryText / decodeDictionaryText: UTF-8, ISO8859-1, -2, -15 (the
//                   Polish Hunspell dictionary is ISO8859-2).
#pragma once

#include "spell/dictencoding.h"
#include "spell/languages.h"
#include "spell/spellchecker.h"
#include "spell/spellwords.h"

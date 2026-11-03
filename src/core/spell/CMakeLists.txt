# spell — spell checking of notes, GUI-free (docs/UI.md "Spell checking"): the words of a line of
# Markdown worth checking (spellwords.h: syntax, code, links, tags, dates and formulas skipped),
# SpellChecker over one backend per language with the personal word list, ignored words,
# hyphenated words and several languages at once (spellchecker.h; WordListBackend for tests and the
# playground), the languages a setting means (languages.h) and the 8-bit encodings of Hunspell
# dictionaries (dictencoding.h). The backends themselves (Hunspell, Android's spell checker) live
# in src/platform (spellbackends.h).
brulion_add_core_module(spell
    SOURCES
        spell.h
        spellwords.h spellwords.cpp
        spellchecker.h spellchecker.cpp
        languages.h languages.cpp
        dictencoding.h dictencoding.cpp
    DEPS text markdown)

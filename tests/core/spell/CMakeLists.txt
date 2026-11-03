# Spell checking (src/core/spell): the words of a line of Markdown (syntax, code, links, tags,
# dates and formulas skipped; Polish letters, hyphens, apostrophes, numbers), the checker over
# word-list backends (several languages, the personal list, ignored words, hyphenated words,
# suggestions), the languages a setting means and the dictionaries' 8-bit encodings.
brulion_add_test(tst_spell_words SOURCES tst_spell_words.cpp LIBS brulion_core_spell)
brulion_add_test(tst_spell_checker SOURCES tst_spell_checker.cpp LIBS brulion_core_spell)

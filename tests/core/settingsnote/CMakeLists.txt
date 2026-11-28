# The notebook's settings in a note (brulion_core_settingsnote): found by its frontmatter, merged
# over config.json key by key (invalid values warned and left out), the migration idempotent, one key
# written in place; Polish text, CRLF, malformed YAML.
brulion_add_test(tst_settingsnote SOURCES tst_settingsnote.cpp LIBS brulion_core_settingsnote)

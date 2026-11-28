# settingsnote — the notebook's settings in a note (docs/NOTEBOOK-SETTINGS.md): the page found by its
# frontmatter, its keys merged over .brulion/config.json's, the migration, one key written. Pure.
brulion_add_core_module(settingsnote
    SOURCES
        settingsnote.h settingsnote.cpp
    DEPS vault markdown)

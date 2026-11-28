# Tests of the vault module (brulion_core_vault). brulion_platform: its file sync, which the app
# installs for the vault's own writes (vault/filesync.h; vaulttest.h installs it too).
foreach(_t vault vault_paths vault_localstore vault_history vault_watch vault_edge vault_safety vault_obsidian vault_async
        vault_leftovers vault_coarse vault_contentcache)
    brulion_add_test(tst_${_t} SOURCES tst_${_t}.cpp vaulttest.h LIBS brulion_core_vault brulion_platform)
endforeach()
# The notebook's settings in a note (vault/settingsnote.h, docs/NOTEBOOK-SETTINGS.md): found by its
# frontmatter, merged over config.json key by key (invalid values warned and left out), the migration
# idempotent, one key written in place; Polish text, CRLF, malformed YAML.
brulion_add_test(tst_settingsnote SOURCES tst_settingsnote.cpp LIBS brulion_core_vault)

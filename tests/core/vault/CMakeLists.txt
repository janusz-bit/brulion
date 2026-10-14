# Tests of the vault module (brulion_core_vault).
foreach(_t vault vault_paths vault_localstore vault_history vault_watch vault_edge)
    brulion_add_test(tst_${_t} SOURCES tst_${_t}.cpp vaulttest.h LIBS brulion_core_vault)
endforeach()

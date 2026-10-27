# vault — the app's view of a vault folder: listing + cache, atomic writes, change signals,
# trash, external-change detection (file system watcher + rescan), .brulion/config.json (or, without
# it, the settings of an Obsidian vault's .obsidian/ folder); and the device-local History
# (version snapshots) and LocalStore (JSON key/value store).
# Public headers: vault.h (overview), history.h, localstore.h, vaultconfig.h, obsidiansettings.h.
# Internal: vault_p.h (d-pointer), vaultutil_p.h (path helpers, logging category).
brulion_add_core_module(vault
    SOURCES
        vault.h vault_p.h vault.cpp vaultmove.cpp vaultscan.cpp
        vaultconfig.h vaultconfig.cpp
        obsidiansettings.h obsidiansettings.cpp
        history.h history.cpp
        localstore.h localstore.cpp
        vaultutil_p.h vaultutil.cpp
    DEPS util text log)

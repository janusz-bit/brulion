# encryption — encrypted notes (encryption/encryption.h; docs/SYNTAX.md §11 "Encrypted text"): Meld
# Encrypt's formats — inline fragments of every version read, v3 written; whole `.mdenc` notes 1.0 /
# 2.0 read, 3.0 read and written as the plugin writes them —, the pure edits of fragments, and the
# session's keyring. Over core `crypto`; the syntax of fragments is markdown/encrypted.h.
brulion_add_core_module(encryption
    SOURCES
        encryption.h
        fragments.h fragments.cpp
        keyring.h keyring.cpp
        meld.h meld.cpp
    DEPS util markdown crypto)

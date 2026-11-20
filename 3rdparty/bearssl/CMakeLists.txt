# BearSSL (https://www.bearssl.org/, MIT, see LICENSE; commit 7bea48e5e850ab4cafbe68d3765cdaba13a86d6f
# of https://www.bearssl.org/git/BearSSL, after v0.6) — the primitives of src/core/crypto: AES in its
# constant-time bitsliced 64-bit implementation (aes_ct64: no tables indexed by secret data), GHASH
# by constant-time carryless multiplication (ghash_ctmul64), GCM over them, SHA-224/256/384/512 and
# HMAC. Vendored unmodified: inc/ (every public header — src/inner.h includes bearssl.h, which
# includes them all), src/inner.h, src/config.h and only the sources below, at their places in
# BearSSL's tree. Nothing of BearSSL's SSL/TLS, X.509, RSA or EC code is compiled.
#
# C, not C++ (BearSSL converts void pointers implicitly): the project enables both languages.
# Warnings are disabled for this third-party code; Brulion's -Werror flags never apply to it.
add_library(brulion_bearssl STATIC
    src/aead/gcm.c
    src/codec/ccopy.c
    src/codec/dec32be.c
    src/codec/dec32le.c
    src/codec/dec64be.c
    src/codec/enc32be.c
    src/codec/enc32le.c
    src/codec/enc64be.c
    src/hash/ghash_ctmul64.c
    src/hash/sha2big.c
    src/hash/sha2small.c
    src/mac/hmac.c
    src/symcipher/aes_ct64.c
    src/symcipher/aes_ct64_ctr.c
    src/symcipher/aes_ct64_enc.c)
set_target_properties(brulion_bearssl PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
target_include_directories(brulion_bearssl SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(brulion_bearssl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(brulion_bearssl PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w> $<$<C_COMPILER_ID:MSVC>:/w>)
# Optimised in every build type: a debug build's unoptimised SHA-512 makes each key derivation of
# an encrypted note (220 000 rounds) take seconds — tests and the app's debug builds alike. (MSVC's
# debug runtime checks cannot be combined with /O2; its debug builds stay as they are.)
target_compile_options(brulion_bearssl PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-O2>)

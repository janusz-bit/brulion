# crypto — the cryptographic primitives (crypto/crypto.h; docs/ARCHITECTURE.md "Security"): SHA-2,
# HMAC, PBKDF2, AES-GCM, Base64 for formats with secrets, wiped memory, constant-time comparison,
# salted slow hashes of a PIN. BearSSL's constant-time code underneath (3rdparty/bearssl, MIT).
# QtCore only; any module may use it (encrypted notes, the app lock, signing later).
if(NOT TARGET brulion_bearssl)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/bearssl ${CMAKE_BINARY_DIR}/3rdparty/bearssl)
endif()
brulion_add_core_module(crypto
    SOURCES
        crypto.h
        aead.h aead.cpp
        base64.h base64.cpp
        hash.h hash.cpp
        random.h random.cpp
        secrethash.h secrethash.cpp
        secure.h secure.cpp)
target_link_libraries(brulion_core_crypto PRIVATE brulion_bearssl)
